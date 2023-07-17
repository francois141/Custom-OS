#ifndef LIBBARRELFISH_RING_BUFFER_H
#define LIBBARRELFISH_RING_BUFFER_H

#include <stddef.h>
#include <stdbool.h>

struct ring_buffer {
    char  *buf;  ///< buffer containing data (one slot is used to differentiate empty/full)
    size_t capacity, head, tail;  ///< empty iff. head == tail, full iff. head + 1 == tail
    ///  reading: advance tail, writing: advance tail
};

void rb_init(struct ring_buffer *rb, size_t size);
void rb_free(struct ring_buffer *rb);

size_t rb_size(struct ring_buffer *rb);
bool   rb_empty(struct ring_buffer *rb);
bool   rb_full(struct ring_buffer *rb);

// push will "always" succeed, overrides if necessary.
void rb_push(struct ring_buffer *rb, char c);

// pop will return whether or not it was successful.
bool rb_pop(struct ring_buffer *rb, char *c);

#endif  // LIBBARRELFISH_RING_BUFFER_H
