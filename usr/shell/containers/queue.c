#include "queue.h"

#include <stdlib.h>
#include <assert.h>

struct qitem {
    struct qitem *next;
    void         *data;
};

void queue_init(struct queue *q)
{
    q->head = NULL;
    q->tail = NULL;
}

void queue_free(struct queue *q)
{
    struct qitem *curr = q->head;
    while (curr != NULL) {
        struct qitem *next = curr->next;
        free(curr->data);
        free(curr);
        curr = next;
    }
    q->head = NULL;
    q->tail = NULL;
}

size_t queue_size(struct queue *q)
{
    struct qitem *curr  = q->head;
    size_t        count = 0;
    while (curr != NULL) {
        ++count;
        curr = curr->next;
    }
    return count;
}

bool queue_empty(struct queue *q)
{
    return q->head == NULL;
}

static struct qitem *_queue_create_item(size_t size)
{
    struct qitem *item = malloc(sizeof(struct qitem));
    item->next         = NULL;
    item->data         = malloc(size);
    return item;
}

void *queue_push(struct queue *q, size_t size)
{
    if (q->tail == NULL) {
        assert(q->head == NULL);
        q->head = q->tail = _queue_create_item(size);
    } else {
        q->tail->next = _queue_create_item(size);
        q->tail       = q->tail->next;
    }
    return q->tail->data;
}

void *queue_pop(struct queue *q)
{
    if (queue_empty(q)) {
        return NULL;
    }
    struct qitem *next = q->head->next;
    void         *data = q->head->data;
    free(q->head);
    q->head = next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    return data;
}

void *queue_head(struct queue *q)
{
    if (queue_empty(q)) {
        return NULL;
    }
    return q->head->data;
}