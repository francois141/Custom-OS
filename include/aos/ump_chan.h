#ifndef BARRELFISH_UMP_CHAN_H
#define BARRELFISH_UMP_CHAN_H

#include <sys/cdefs.h>
#include <aos/waitset.h>
#include <aos/waitset_chan.h>
#include <aos/debug.h>

#define UMP_LINE_SIZE (8ull)
#define UMP_CONTROL_WORD_IDX (UMP_LINE_SIZE - 1)
#define UMP_MSG_MAX_SIZE (sizeof(uintptr_t) * (UMP_LINE_SIZE - 1))

#define UMP_MSG_MORE      (1ull << 63)
#define UMP_MSG_SIZE_MASK (~UMP_MSG_MORE)

struct ump_line {
    uintptr_t words[UMP_LINE_SIZE];
};

struct ump_msg {
    char data[UMP_MSG_MAX_SIZE];
    size_t size;
    bool more;
};

struct ump_chan {
    struct capref frame;

    struct {
        struct waitset_chanstate waitset_state; 
        struct ump_line* buf;
        size_t size;
        size_t offset;
    } send;

    struct {
        struct waitset_chanstate waitset_state;
        struct ump_line* buf;
        size_t size;
        size_t offset;
    } recv;
};

errval_t ump_chan_init(struct ump_chan* chan, struct capref frame, bool primary);


static inline bool ump_chan_can_send(struct ump_chan* chan) {
    struct ump_line* line = chan->send.buf + chan->send.offset;
    return line->words[UMP_CONTROL_WORD_IDX] == 0;
}

static inline bool ump_chan_can_recv(struct ump_chan* chan) {
    struct ump_line* line = chan->recv.buf + chan->recv.offset;
    return line->words[UMP_CONTROL_WORD_IDX] != 0;
}

static inline errval_t ump_chan_send(struct ump_chan* chan, const struct ump_msg* msg) {
    if (!ump_chan_can_send(chan)) {
        return LIB_ERR_UMP_CHAN_FULL;
    }

    // writes are never speculated, hence no barrier is necessary

    struct ump_line* line = chan->send.buf + chan->send.offset;
    memcpy(line->words, msg->data, msg->size);

    uintptr_t control = msg->size | (msg->more ? UMP_MSG_MORE : 0);

    // written data must be visible before control word
    dmb();

    line->words[UMP_CONTROL_WORD_IDX] = control;

    chan->send.offset = (chan->send.offset + 1) % chan->send.size;

    return SYS_ERR_OK;
}

static inline errval_t ump_chan_recv(struct ump_chan* chan, struct ump_msg* msg) {
    if (!ump_chan_can_recv(chan)) {
        return LIB_ERR_UMP_CHAN_EMPTY;
    }

    struct ump_line* line = chan->recv.buf + chan->recv.offset;
    uintptr_t control = line->words[UMP_CONTROL_WORD_IDX];
    msg->size = control & UMP_MSG_SIZE_MASK;
    msg->more = control & UMP_MSG_MORE;

    // avoid data being prefetched speculatively
    dmb();

    memcpy(msg->data, line->words, msg->size);

    // data must be read before clearing control word visible to writer
    dmb();

    // clear control word after reading data
    line->words[UMP_CONTROL_WORD_IDX] = 0;

    chan->recv.offset = (chan->recv.offset + 1) % chan->recv.size;

    return SYS_ERR_OK;
}

errval_t ump_chan_register_recv(struct ump_chan *chan, struct waitset *ws, struct event_closure closure);
errval_t ump_chan_register_send(struct ump_chan *chan, struct waitset *ws, struct event_closure closure);

static inline errval_t ump_chan_deregister_send(struct ump_chan *chan) {
    return waitset_chan_deregister(&chan->send.waitset_state);
}

static inline errval_t ump_chan_deregister_recv(struct ump_chan *chan) {
    return waitset_chan_deregister(&chan->recv.waitset_state);
}

static inline bool ump_err_is_transient(errval_t err) {
    return err_no(err) == LIB_ERR_UMP_CHAN_FULL ||
           err_no(err) == LIB_ERR_UMP_CHAN_EMPTY;
}

#endif