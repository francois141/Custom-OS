#include "ring_buffer.h"

#include <stdlib.h>
#include <stdio.h>

void rb_init(struct ring_buffer *rb, size_t size)
{
    rb->capacity = size;
    rb->buf      = malloc(sizeof(char) * size);
    rb->head = rb->tail = 0;
}

void rb_free(struct ring_buffer *rb)
{
    rb->capacity = 0;
    free(rb->buf);
    rb->head = rb->tail = 0;
}

size_t rb_size(struct ring_buffer *rb)
{
    if (rb->head >= rb->tail) {
        return rb->head - rb->tail;
    }
    return (rb->head + rb->capacity) - rb->tail;
}

bool rb_empty(struct ring_buffer *rb)
{
    return rb->head == rb->tail;
}

bool rb_full(struct ring_buffer *rb)
{
    return (rb->head + 1 == rb->capacity ? 0 : (rb->head + 1)) == rb->tail;
}

static void _rb_advance(struct ring_buffer *rb, size_t *p)
{
    if (++(*p) == rb->capacity) {
        *p = 0;
    }
}

void rb_push(struct ring_buffer *rb, char c)
{
    if (rb_full(rb)) {
        rb->buf[rb->head] = c;
        _rb_advance(rb, &rb->head);
        // we also need to advanced tail to preserve the invariant.
        printf("gb: dropping '%c' due to full buffer\n", rb->buf[rb->tail]);
        _rb_advance(rb, &rb->tail);
        return;
    }
    rb->buf[rb->head] = c;
    _rb_advance(rb, &rb->head);
}

bool rb_pop(struct ring_buffer *rb, char *c)
{
    if (rb_empty(rb)) {
        *c = '\0';
        return false;
    }
    *c = rb->buf[rb->tail];
    _rb_advance(rb, &rb->tail);
    return true;
}