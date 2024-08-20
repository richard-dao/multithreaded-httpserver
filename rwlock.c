#include "rwlock.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>

struct rwlock {
    PRIORITY p;
    int n;

    pthread_mutex_t mutex;
    pthread_cond_t reader_cv;
    pthread_cond_t writer_cv;

    int active_readers;
    int active_writers;

    int waiting_readers;
    int waiting_writers;

    int nwaycount;
    int flag; // -1 = unset, 0 = readers, 1 = writers
};

rwlock_t *rwlock_new(PRIORITY p, uint32_t n) {
    rwlock_t *rw = (rwlock_t *) malloc(sizeof(rwlock_t));

    rw->p = p;
    if (p == N_WAY) {
        rw->n = n;
    }

    pthread_mutex_init(&(rw->mutex), NULL);
    pthread_cond_init(&(rw->reader_cv), NULL);
    pthread_cond_init(&(rw->writer_cv), NULL);

    rw->active_readers = 0;
    rw->active_writers = 0;

    rw->waiting_readers = 0;
    rw->waiting_writers = 0;

    rw->nwaycount = 0;
    rw->flag = -1;
    return rw;
}

void rwlock_delete(rwlock_t **rw) {
    if ((*rw) != NULL) {
        pthread_mutex_destroy(&((*rw)->mutex));
        pthread_cond_destroy(&((*rw)->reader_cv));
        pthread_cond_destroy(&((*rw)->writer_cv));
        free(*rw);
        *rw = NULL;
    }
}

int reader_wait(rwlock_t *rw) {
    if (rw->p == READERS) {
        return rw->active_writers;
    } else if (rw->p == WRITERS) {
        if (rw->active_writers != 0) {
            return rw->active_writers;
        } else {
            return rw->waiting_writers;
        }
    } else if (rw->p == N_WAY) {
        if (rw->active_readers != 0) {
            if (rw->waiting_writers != 0) {
                if (rw->nwaycount < rw->n) {
                    return 0;
                } else {
                    if (rw->flag != -1) {
                        return rw->waiting_writers;
                    } else {
                        return 0;
                    }
                }
            } else {
                return 0;
            }
        } else if (rw->active_writers != 0) {
            return rw->active_writers;
        } else {
            if (rw->flag != -1) {
                if (rw->active_writers != 0) {
                    return rw->active_writers;
                } else {
                    return rw->waiting_writers;
                }
            } else {
                return 0;
            }
        }
    } else {
        fprintf(stderr, "Huh?\n");
        return -9;
    }
}

int writer_wait(rwlock_t *rw) {
    if (rw->p == READERS) {
        if (rw->active_writers != 0) {
            return rw->active_writers;
        } else {
            return rw->active_readers;
        }
    } else if (rw->p == WRITERS) {
        if (rw->active_writers != 0) {
            return rw->active_writers;
        } else {
            return rw->active_readers;
        }
    } else if (rw->p == N_WAY) {
        if (rw->active_readers != 0) {
            return rw->active_readers;
        } else if (rw->active_writers != 0) {
            return rw->active_writers;
        } else {
            if (rw->flag != -1) {
                return rw->waiting_readers;
            } else {
                return 0;
            }
        }
    } else {
        fprintf(stderr, "Huh?\n");
        return -9;
    }
}

void reader_lock(rwlock_t *rw) {

    pthread_mutex_lock(&(rw->mutex));
    rw->waiting_readers += 1;

    while (reader_wait(rw) != 0) {
        pthread_cond_wait(&(rw->reader_cv), &(rw->mutex));
    }

    rw->waiting_readers -= 1;
    rw->active_readers += 1;

    if (rw->p == N_WAY) {
        if (rw->flag != -1 && rw->nwaycount == 0) {
            rw->flag = -1;
        }
        if (rw->flag == -1 && rw->nwaycount < rw->n) {
            rw->nwaycount += 1;
        }
    }

    pthread_mutex_unlock(&(rw->mutex));
}

void reader_unlock(rwlock_t *rw) {
    pthread_mutex_lock(&(rw->mutex));
    rw->active_readers -= 1;

    if (rw->p == N_WAY) {
        if (rw->nwaycount == rw->n) {
            rw->flag = 1;
        }
        if (rw->flag != -1 && rw->nwaycount > 0) {
            rw->nwaycount = 0;
        }
    }

    pthread_cond_broadcast(&(rw->reader_cv));
    pthread_cond_broadcast(&(rw->writer_cv));

    pthread_mutex_unlock(&(rw->mutex));
}

void writer_lock(rwlock_t *rw) {
    pthread_mutex_lock(&(rw->mutex));
    rw->waiting_writers += 1;

    while (writer_wait(rw) != 0) {
        pthread_cond_wait(&(rw->writer_cv), &(rw->mutex));
    }

    rw->waiting_writers -= 1;
    rw->active_writers += 1;

    pthread_mutex_unlock(&(rw->mutex));
}

void writer_unlock(rwlock_t *rw) {
    pthread_mutex_lock(&(rw->mutex));
    rw->active_writers -= 1;

    if (rw->p == N_WAY) {
        if (rw->flag != -1) {
            rw->flag = -1;
        }
    }

    pthread_cond_broadcast(&(rw->reader_cv));
    pthread_cond_broadcast(&(rw->writer_cv));
    pthread_mutex_unlock(&(rw->mutex));
}
