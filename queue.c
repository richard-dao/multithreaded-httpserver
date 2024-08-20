#include "queue.h"
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>

struct queue {
    int size;
    int head;
    int tail;
    void **buf;

    sem_t full;
    sem_t empty;
    sem_t mutex;
};

queue_t *queue_new(int size) {
    queue_t *queue = (queue_t *) malloc(sizeof(queue_t));
    queue->size = size;
    queue->head = 0;
    queue->tail = 0;

    queue->buf = (void **) malloc(size * sizeof(void *));
    sem_init(&(queue->full), 0, size);
    sem_init(&(queue->empty), 0, 0);
    sem_init(&(queue->mutex), 0, 1);
    return queue;
}

void queue_delete(queue_t **q) {
    if ((*q) != NULL) {
        sem_destroy(&((*q)->full));
        sem_destroy(&((*q)->empty));
        sem_destroy(&((*q)->mutex));
        free((*q)->buf);
        free(*q);
        *q = NULL;
    }
}

bool queue_push(queue_t *q, void *elem) {
    if (q != NULL) {
        if (elem != NULL) {
            while (sem_wait(&(q->full)) != 0) {
            }
            while (sem_wait(&(q->mutex)) != 0) {
            }

            q->buf[q->tail] = elem;
            q->tail = (q->tail + 1) % q->size;

            sem_post(&(q->mutex));
            sem_post(&(q->empty));
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

bool queue_pop(queue_t *q, void **elem) {

    if (q != NULL) {
        while (sem_wait(&(q->empty)) != 0) {
        }
        while (sem_wait(&(q->mutex)) != 0) {
        }

        *elem = q->buf[q->head];
        q->head = (q->head + 1) % q->size;

        sem_post(&(q->mutex));
        sem_post(&(q->full));
        return true;
    }
    return false;
}
