#ifndef QUEUE_H
#define QUEUE_H
/* Circular buffer queue */

// Queue elements type.
typedef uint8_t q_elem_t;

typedef struct queue_control {
    // Ptr to current available head/producer index in 'buffer'.
    unsigned head;
    // Ptr to last index in 'buffer' used by consumer.
    unsigned tail;
} queue_control_t;

typedef struct simqueue {
    // MMIO queue control.
    volatile queue_control_t *q_ctrl;
    // Size of the queue buffer/data.
    unsigned maxlen;
    // Queue data buffer.
    q_elem_t *buffer;
} simqueue_t;

int circ_bbuf_push(simqueue_t *q, q_elem_t data)
{
    unsigned next, head;

    head = q->q_ctrl->head; // do a single mmio read and cache the value.

    next = head + 1;  // next is where head will point to after this write.
    if (next >= q->maxlen)
        next = 0;

    if (next == q->q_ctrl->tail)  // if the head + 1 == tail, circular buffer is full
        return -1;

    q->buffer[head] = data;  // Load data and then move
    q->q_ctrl->head = next;  // head to next data offset.
    return 0;  // return success to indicate successful push.
}

int circ_bbuf_pop(simqueue_t *q, q_elem_t *data)
{
    unsigned next, tail;

    tail = q->q_ctrl->tail;         // do a single mmio read and cache the value.
    if (q->q_ctrl->head == tail)    // if the head == tail, we don't have any data
        return -1;

    next = tail + 1;    // next is where tail will point to after this read.
    if(next >= q->maxlen)
        next = 0;

    *data = q->buffer[tail];    // Read data and then move
    q->q_ctrl->tail = next;     // tail to next offset.
    return 0;   // return success to indicate successful push.
}

#endif //QUEUE_H