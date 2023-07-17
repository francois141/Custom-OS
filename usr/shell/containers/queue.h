#ifndef LIBBARRELFISH_QUEUE_H
#define LIBBARRELFISH_QUEUE_H

#include <stddef.h>
#include <stdbool.h>

struct qitem;  ///< forward declaration
struct queue {
    struct qitem *head, *tail;
};

void queue_init(struct queue *q);
void queue_free(struct queue *q);

size_t queue_size(struct queue *q);
bool   queue_empty(struct queue *q);

// append to the end of a queue
void *queue_push(struct queue *q, size_t size);

// pop from the beginning of the queue
void *queue_pop(struct queue *q);
void *queue_head(struct queue *q);

#endif  // LIBBARRELFISH_QUEUE_H