#include <aos/aos.h>
#include <aos/ump_chan.h>
#include "waitset_chan_priv.h"


errval_t ump_chan_init(struct ump_chan* chan, struct capref frame, bool primary) {
    errval_t err = SYS_ERR_OK;

    chan->frame = frame;

    struct frame_identity id;
    err = frame_identify(frame, &id);
    if (err_is_fail(err)) {
        return err;
    }

    void* buf;
    err = paging_map_frame(get_current_paging_state(), &buf, id.bytes, frame);
    if (err_is_fail(err)) {
        return err;
    }

    size_t lines = (id.bytes / sizeof(struct ump_line)) / 2;

    if (primary) {
        chan->send.buf = (struct ump_line*)buf;
        chan->recv.buf = chan->send.buf + lines;
    } else {
        chan->recv.buf = (struct ump_line*)buf;
        chan->send.buf = chan->recv.buf + lines;
    }

    chan->send.size = lines;
    chan->send.offset = 0;

    chan->recv.size = lines;
    chan->recv.offset = 0;

    waitset_chanstate_init(&chan->send.waitset_state, CHANTYPE_UMP_OUT);
    waitset_chanstate_init(&chan->recv.waitset_state, CHANTYPE_UMP_IN);
    chan->send.waitset_state.chan_data = chan;
    chan->recv.waitset_state.chan_data = chan;

    return err;
}

errval_t ump_chan_register_recv(struct ump_chan *chan, struct waitset *ws, struct event_closure closure) {
    errval_t err;

    dispatcher_handle_t handle = disp_disable();

    if (ump_chan_can_recv(chan)) { // trigger immediately if possible
        err = waitset_chan_trigger_closure_disabled(ws, &chan->recv.waitset_state, closure, handle);
    } else {
        err = waitset_chan_register_polled_disabled(ws, &chan->recv.waitset_state, closure, handle);
    }

    disp_enable(handle);

    return err;
}

errval_t ump_chan_register_send(struct ump_chan *chan, struct waitset *ws, struct event_closure closure) {
    errval_t err;

    dispatcher_handle_t handle = disp_disable();

    if (ump_chan_can_send(chan)) { // trigger immediately if possible
        err = waitset_chan_trigger_closure_disabled(ws, &chan->send.waitset_state, closure, handle);
    } else {
        err = waitset_chan_register_polled_disabled(ws, &chan->send.waitset_state, closure, handle);
    }

    disp_enable(handle);

    return err;
}
