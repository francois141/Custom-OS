#include "serial.h"

#include <ctype.h>
#include <stdio.h>

#include <aos/inthandler.h>
#include <maps/qemu_map.h>
#include <maps/imx8x_map.h>
#include <drivers/pl011.h>
#include <drivers/lpuart.h>
#include <drivers/gic_dist.h>

#include <pthread.h>

#include <aos/aos_rpc.h>

#include "../containers/ring_buffer.h"
#include "../containers/queue.h"

struct serial_state {
    bool init;
    union {
        struct lpuart_s *uart;
        struct pl011_s  *pl011;
    } driver;

    struct ring_buffer rb;
    struct queue       requests;

    struct async_channel *async;  ///< async_channel to core0

    enum pi_platform platform;
    bool             use_usr_serial;  ///< as this does not work on qemu, we enable it selectively
    struct thread_mutex mutex;
};

struct serial_getchar_request {
    struct event_closure resume_fn;
    size_t               req, pos;
    size_t              *retlen;
    char                *buf;
};

// current serial state
struct serial_state current_serial_state
    = { .init = false, .driver.uart = NULL, .use_usr_serial = false };

bool is_usr_serial_enabled(void)
{
    assert(current_serial_state.init);
    return current_serial_state.use_usr_serial;
}

static genpaddr_t _platform_gic_dist_base(struct serial_state *state)
{
    switch (state->platform) {
    case PI_PLATFORM_QEMU:
        return QEMU_GIC_DIST_BASE;
    case PI_PLATFORM_IMX8X:
        return IMX8X_GIC_DIST_BASE;
    default:
        __builtin_unreachable();
    }
}

static gensize_t _platform_gic_dist_size(struct serial_state *state)
{
    switch (state->platform) {
    case PI_PLATFORM_QEMU:
        return QEMU_GIC_DIST_SIZE;
    case PI_PLATFORM_IMX8X:
        return IMX8X_GIC_DIST_SIZE;
    default:
        __builtin_unreachable();
    }
}

static genpaddr_t _platform_uart_base(struct serial_state *state)
{
    switch (state->platform) {
    case PI_PLATFORM_QEMU:
        return QEMU_UART_BASE;
    case PI_PLATFORM_IMX8X:
        return IMX8X_UART3_BASE;
    default:
        __builtin_unreachable();
    }
}

static gensize_t _platform_uart_size(struct serial_state *state)
{
    switch (state->platform) {
    case PI_PLATFORM_QEMU:
        return QEMU_UART_SIZE;
    case PI_PLATFORM_IMX8X:
        return IMX8X_UART_SIZE;
    default:
        __builtin_unreachable();
    }
}

static errval_t _gic_dist_init(struct serial_state *state, struct capref dev_cap,
                               struct capability dev_frame, struct gic_dist_s **gds)
{
    errval_t err = SYS_ERR_OK;
    void    *buf;
    err = dev_frame_map(dev_cap, dev_frame, _platform_gic_dist_base(state),
                        _platform_gic_dist_size(state), &buf);
    if (err_is_fail(err)) {
        return err;
    }
    err = gic_dist_init(gds, buf);
    if (err_is_fail(err)) {
        return err;
    }
    return SYS_ERR_OK;
}

static errval_t _uart_init(struct serial_state *state, struct capref dev_cap,
                           struct capability dev_frame)
{
    errval_t err = SYS_ERR_OK;
    void    *buf;
    err = dev_frame_map(dev_cap, dev_frame, _platform_uart_base(state), _platform_uart_size(state),
                        &buf);
    if (err_is_fail(err)) {
        return err;
    }
    switch (state->platform) {
    case PI_PLATFORM_QEMU:
        err = pl011_init(&state->driver.pl011, buf);
        break;
    case PI_PLATFORM_IMX8X:
        err = lpuart_init(&state->driver.uart, buf);
        break;
    default:
        __builtin_unreachable();
    }
    if (err_is_fail(err)) {
        return err;
    }
    return SYS_ERR_OK;
}

static void _finalize_request(struct serial_getchar_request *req)
{
    assert(req->req >= req->pos);
    if (req->retlen != NULL)
        *req->retlen = req->pos;
    req->resume_fn.handler(req->resume_fn.arg);
    free(req);
}

static inline bool _serial_requires_flush(char c) {
    return c == '\n' || c == '\r' || c == 4 /* EOT */;
}

static void _service_requests(void)
{
    /// XXX we don't need to lock the mutex (we should be holding it already!)
    //      but to be sure we lock here.
    thread_mutex_lock_nested(&current_serial_state.mutex);
    while (!rb_empty(&current_serial_state.rb) && !queue_empty(&current_serial_state.requests)) {
        struct serial_getchar_request *req  = queue_head(&current_serial_state.requests);
        char                           read = '\0';
        bool                           ok   = rb_pop(&current_serial_state.rb, &read);
        if (ok) {
            /// XXX for now we also send the newline character
            req->buf[req->pos++] = read;
        } else
            break;
        if (req->pos == req->req || _serial_requires_flush(read)) {
            // on new line / request limit, we "flush" the request
            _finalize_request(queue_pop(&current_serial_state.requests));
        }
    }
    thread_mutex_unlock(&current_serial_state.mutex);
}

static errval_t _platform_getchar(struct serial_state *state, char *c)
{
    switch (state->platform) {
    case PI_PLATFORM_QEMU:
        return pl011_getchar(state->driver.pl011, c);
    case PI_PLATFORM_IMX8X:
        return lpuart_getchar(state->driver.uart, c);
    default:
        __builtin_unreachable();
    }
}

static inline bool _serial_isprint(char c) {
    return (c >= 32 && c <= 126);
}

static bool _serial_has_pending_cooked(void) {
    if (queue_size(&current_serial_state.requests) == 0)
        return false;
    struct serial_getchar_request *req  = queue_head(&current_serial_state.requests);
    return req->req > 1; ///< for now anything that requests a single character is considered "raw"
}

static void _uart_interrupt(void *arg)
{
    struct serial_state *ses = arg;
    char                 c   = '\0';
    thread_mutex_lock_nested(&current_serial_state.mutex);
    errval_t err = _platform_getchar(ses, &c);
    // XXX eventually we could just pipeline everything into the requests directly..
    while (!err_is_fail(err)) {  // eventually we will hit LPUART_ERR_NO_DATA
#if 0
        if (c == '\r') {
            c = '\n'; ///< translate back to newline?
        }
#endif
        if ((_serial_isprint(c) || c == '\r' || c == '\n') && _serial_has_pending_cooked()) {
#if SERIAL_ECHO_ON_TYPE
            serial_putchar(c == '\r' ? '\n' : c);
#endif
        }
        rb_push(&current_serial_state.rb, c);
        err = _platform_getchar(ses, &c);
    }
    if (!rb_empty(&ses->rb) && !queue_empty(&ses->requests)) {
        _service_requests();
    }
    thread_mutex_unlock(&current_serial_state.mutex);
}

static int _platform_uart_int(struct serial_state *state)
{
    switch (state->platform) {
    case PI_PLATFORM_QEMU:
        return QEMU_UART_INT;
    case PI_PLATFORM_IMX8X:
        return IMX8X_UART3_INT;
    default:
        __builtin_unreachable();
    }
}

static errval_t _uart_hw_init(struct serial_state *state)
{
    errval_t          err     = SYS_ERR_OK;
    struct capref     dev_cap = { .cnode = cnode_task, .slot = TASKCN_SLOT_DEV };
    struct capability dev_frame;
    err = cap_direct_identify(dev_cap, &dev_frame);
    if (err_is_fail(err) || dev_frame.type != ObjType_DevFrame) {
        DEBUG_ERR(err, "cap_direct_identify");
        return err;
    }

    // XXX Initialize the GIC distributor driver
    struct gic_dist_s *gds;
    err = _gic_dist_init(state, dev_cap, dev_frame, &gds);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "_gic_driver_init");
        return err;
    }

    // XXX Initialize the LPUART driver
    err = _uart_init(state, dev_cap, dev_frame);
    if (err_is_fail(err)) {
        return err;
    }

    // XXX Obtain the IRQ destination cap and attach a handler to it
    struct capref irq_dst_cap;
    err = inthandler_alloc_dest_irq_cap(_platform_uart_int(state), &irq_dst_cap);
    if (err_is_fail(err)) {
        return err;
    }
    err = inthandler_setup(irq_dst_cap, get_default_waitset(),
                           MKCLOSURE(_uart_interrupt, /*arg*/ (void *)state));
    if (err_is_fail(err)) {
        return err;
    }

    // XXX Enable the interrupt in the GIC distributor
    err = gic_dist_enable_interrupt(gds, _platform_uart_int(state), /*cpu_targets*/ 0b1,
                                    /*priority*/ 0);
    if (err_is_fail(err)) {
        return err;
    }

    // XXX Enable the interrupt in the LPUART
    switch (state->platform) {
    case PI_PLATFORM_QEMU:
        err = pl011_enable_interrupt(state->driver.pl011);
        break;
    case PI_PLATFORM_IMX8X:
        err = lpuart_enable_interrupt(state->driver.uart);
        break;
    default:
        __builtin_unreachable();
    }
    if (err_is_fail(err)) {
        return err;
    }

    return SYS_ERR_OK;
}

errval_t serial_server_init(struct async_channel *async, enum pi_platform platform)
{
    (void)platform;
    bool use_usr_serial = (platform == PI_PLATFORM_IMX8X) || (platform == PI_PLATFORM_QEMU);
    thread_mutex_init(&current_serial_state.mutex);
    if (!use_usr_serial || disp_get_core_id() != 0) {
        current_serial_state.init           = true;
        current_serial_state.use_usr_serial = use_usr_serial;
        current_serial_state.async          = async;
        current_serial_state.platform       = platform;
        return SYS_ERR_OK;
    }
    errval_t err                  = SYS_ERR_OK;
    current_serial_state.async    = NULL;
    current_serial_state.platform = platform;

    queue_init(&current_serial_state.requests);
    rb_init(&current_serial_state.rb, 256);
    err = _uart_hw_init(&current_serial_state);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "_uart_hw_init");
        return err;
    }

    current_serial_state.use_usr_serial = true;
    current_serial_state.init           = true;
    return SYS_ERR_OK;
}

static errval_t _platform_putchar(struct serial_state *state, char c)
{
    switch (state->platform) {
    case PI_PLATFORM_QEMU:
        return pl011_putchar(state->driver.pl011, c);
    case PI_PLATFORM_IMX8X:
        return lpuart_putchar(state->driver.uart, c);
    default:
        __builtin_unreachable();
    }
}

errval_t serial_putchar(char c)
{
    assert(current_serial_state.init && current_serial_state.use_usr_serial
           && disp_get_core_id() == 0);
    thread_mutex_lock_nested(&current_serial_state.mutex);
    errval_t err = SYS_ERR_OK;
    err          = _platform_putchar(&current_serial_state, c);
    if (err_is_fail(err)) {
        thread_mutex_unlock(&current_serial_state.mutex);
        return err;
    }
    if (c == '\n') {
        // XXX we want identical behavior to "QEMU-mode"
        err = _platform_putchar(&current_serial_state, '\r');
        if (err_is_fail(err)) {
            thread_mutex_unlock(&current_serial_state.mutex);
            return err;
        }
    }
    thread_mutex_unlock(&current_serial_state.mutex);
    return SYS_ERR_OK;
}

errval_t serial_putstr(const char *buf, size_t len, size_t *retlen)
{
    assert(current_serial_state.init && current_serial_state.use_usr_serial
           && disp_get_core_id() == 0);
    errval_t err = SYS_ERR_OK;
    thread_mutex_lock_nested(&current_serial_state.mutex);
    if (len == 0) {
        *retlen = 0;
        thread_mutex_unlock(&current_serial_state.mutex);
        return SYS_ERR_OK;
    }

    for (size_t i = 0; i < len; ++i) {
        err = serial_putchar(buf[i]);
        if (err_is_fail(err)) {
            *retlen = i;
            thread_mutex_unlock(&current_serial_state.mutex);
            return err;
        }
    }
    *retlen = len;
    thread_mutex_unlock(&current_serial_state.mutex);
    return SYS_ERR_OK;
}

errval_t serial_getchar_register_wait(size_t len, struct event_closure resume_fn, size_t *retlen,
                                      char *buf)
{
    assert(current_serial_state.init && current_serial_state.use_usr_serial
           && disp_get_core_id() == 0);
    assert(len > 0);

    /// store the request.
    thread_mutex_lock_nested(&current_serial_state.mutex);
    struct serial_getchar_request *req = queue_push(&current_serial_state.requests,
                                                    sizeof(struct serial_getchar_request));
    // XXX here would be the point to do some validation on the request.
    req->resume_fn = resume_fn;
    req->req       = len;
    req->pos       = 0;
    req->retlen    = retlen;
    req->buf       = buf;
    if (!rb_empty(&current_serial_state.rb)) {
        /// XXX service as much of the request as possible
        _service_requests();
    }
    thread_mutex_unlock(&current_serial_state.mutex);
    return SYS_ERR_OK;
}
