#include "helper_funcs_socket.h"
#include "connection.h"
#include "response.h"
#include "request.h"
#include "queue.h"
#include "rwlock.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <err.h>
#include <errno.h>

int num_threads = 4;

void handle_connection(int);
void handle_get(conn_t *);
void handle_put(conn_t *);
void handle_unsupported(conn_t *);

typedef struct Node {
    char *key;
    rwlock_t *rwlock;
    struct Node *next;
} node_t;

queue_t *q;
node_t *list_head;
pthread_mutex_t m;

node_t *new_Node(char *k, node_t *nxt) {
    node_t *node = (node_t *) malloc(sizeof(node_t));
    node->key = strdup(k);
    node->rwlock = rwlock_new(N_WAY, 1);
    node->next = nxt;
    return node;
}

int listSearch(char *key) {
    node_t *curr = list_head;
    while (curr != NULL && curr->key != NULL) {
        if (strcmp(curr->key, key) == 0) {
            return 1;
        }
        curr = curr->next;
    }
    return 0;
}

void lockNode(char *key, bool reader) {
    node_t *curr = list_head;

    // To lock a rwlock that already exists
    while (curr->next != NULL) {
        if (strcmp(curr->key, key) == 0) {
            if (reader) {
                reader_lock(curr->rwlock);
                return;
            } else {
                writer_lock(curr->rwlock);
                return;
            }
        }
        curr = curr->next;
    }
}

void unlockNode(char *key, bool reader) {
    node_t *curr = list_head;
    while (curr->next != NULL) {
        if (strcmp(curr->key, key) == 0) {
            if (reader) {
                reader_unlock(curr->rwlock);
                return;
            } else {
                writer_unlock(curr->rwlock);
                return;
            }
        }
        curr = curr->next;
    }
}

void insertNode(char *key) {
    if (list_head == NULL) {
        list_head = new_Node(key, NULL);
        // fprintf(stderr, "Makes new head correctly?\n");

        return;
    }
    node_t *curr = list_head;
    while (curr->next != NULL) {
        curr = curr->next;
    }
    curr->next = new_Node(key, NULL);
}

void deleteList(node_t **head) {
    while ((*head) != NULL) {
        node_t *tempNext = (*head)->next;
        rwlock_delete(&((*head)->rwlock));
        free((*head));
        (*head) = tempNext;
    }
}

void fake_flock(char *URI, int mode, int oper) {
    // Mode 0 == LOCK_SH
    // Mode 1 == LOCK_EX
    // Mode 2 == LOCK_UN

    // First check if URI exists in list
    int exists = listSearch(URI);

    // fprintf(stderr, "Does the URI: %s exist? %d\n", URI, exists);

    if (exists == 0) {
        // fprintf(stderr, "Attempts to add node.\n");
        insertNode(URI);
        // fprintf(stderr, "Inserts Node successfully?\n");
    }

    if (mode == 0) {
        lockNode(URI, true);
        // fprintf(stderr, "Locks Node for reading successfully?\n");
    } else if (mode == 1) {
        lockNode(URI, false);
        // fprintf(stderr, "Locks Node for writing successfully?\n");
    } else {
        if (oper == 0) {
            unlockNode(URI, true);
            // fprintf(stderr, "Unlocks Node from reading successfully?\n");
        } else {
            unlockNode(URI, false);
            // fprintf(stderr, "Unlocks Node from writing successfully?\n");
        }
    }
}

void *worker_thread() {
    while (1) {
        uintptr_t connfd = 0;
        queue_pop(q, (void **) &connfd);
        handle_connection(connfd);
        close(connfd);
    }
    return (void *) 1;
}

void handle_memory(int signal) {
    queue_delete(&q);
    pthread_mutex_destroy(&m);
    deleteList(&list_head);
    exit(signal);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        warnx("wrong arguments: %s port_num", argv[0]);
        fprintf(stderr, "invalid arguments");
        return EXIT_FAILURE;
    }

    int c;
    while ((c = getopt(argc, argv, "t:")) != -1) {
        switch (c) {
        case 't': num_threads = atoi(optarg); break;
        default: fprintf(stderr, "usage: -t <threadcount> %s <port\n", argv[0]);
        }
    }

    char *endptr = NULL;
    size_t port = (size_t) strtoull(argv[optind], &endptr, 10);

    if (endptr && *endptr != '\0') {
        warnx("invalid port number: %s", argv[optind]);
        return EXIT_FAILURE;
    }

    // Thread crap that was done in section
    // Dispatcher

    signal(SIGPIPE, SIG_IGN);

    // Thread threads[num_threads];
    pthread_t threads[num_threads];
    q = queue_new(num_threads);

    for (int i = 0; i < num_threads; i++) {

        pthread_create(&threads[i], NULL, worker_thread, (void *) ((uintptr_t) i));
    }

    pthread_mutex_init(&m, NULL);
    signal(SIGINT, handle_memory);
    signal(SIGTERM, handle_memory);

    Listener_Socket sock;
    listener_init(&sock, port);

    while (1) {
        uintptr_t connfd = listener_accept(&sock);
        queue_push(q, (void *) connfd);
    }
    pthread_mutex_destroy(&m);
    queue_delete(&q);
    deleteList(&list_head);
    return EXIT_SUCCESS;
}

void handle_connection(int connfd) {
    conn_t *conn = conn_new(connfd);

    const Response_t *res = conn_parse(conn);

    if (res != NULL) {
        conn_send_response(conn, res);
    } else {
        const Request_t *req = conn_get_request(conn);
        if (req == &REQUEST_GET) {
            handle_get(conn);
        } else if (req == &REQUEST_PUT) {
            handle_put(conn);
        } else {
            handle_unsupported(conn);
        }
    }
    conn_delete(&conn);
}

void handle_get(conn_t *conn) {
    char *URI = conn_get_uri(conn);
    const Response_t *res = NULL;

    int fd = open(URI, O_RDONLY);

    if (fd == -1) {
        if (errno == EACCES) {
            fprintf(stderr, "%s,%s,%d,%s\n", "GET", URI, 403, conn_get_header(conn, "Request-Id"));
            res = &RESPONSE_FORBIDDEN;
            conn_send_response(conn, res);
            return;
        } else if (errno == ENOENT) {
            fprintf(stderr, "%s,%s,%d,%s\n", "GET", URI, 404, conn_get_header(conn, "Request-Id"));
            res = &RESPONSE_NOT_FOUND;
            conn_send_response(conn, res);
            return;
        } else {
            fprintf(stderr, "%s,%s,%d,%s\n", "GET", URI, 500, conn_get_header(conn, "Request-Id"));
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
            conn_send_response(conn, res);
            return;
        }
    }

    fake_flock(URI, 0, 0);
    // flock(fd, LOCK_SH);
    struct stat buf1;
    fstat(fd, &buf1);

    if (S_ISDIR(buf1.st_mode)) {
        res = &RESPONSE_FORBIDDEN;
        fake_flock(URI, 2, 0);
        // flock(fd, LOCK_UN);
        close(fd);
        conn_send_response(conn, res);
        return;
    }

    conn_send_file(conn, fd, buf1.st_size);
    fprintf(stderr, "%s,%s,%d,%s\n", "GET", URI, 200, conn_get_header(conn, "Request-Id"));
    fake_flock(URI, 2, 0);
    // flock(fd, LOCK_UN);
    close(fd);

    return;
}

void handle_put(conn_t *conn) {
    char *URI = conn_get_uri(conn);
    const Response_t *res = NULL;

    pthread_mutex_lock(&m);

    bool exists = access(URI, F_OK) == 0;

    int fd = open(URI, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) {
        if (errno == EACCES || errno == EISDIR || errno == ENOENT) {
            fprintf(stderr, "%s,%s,%d,%s\n", "PUT", URI, 403, conn_get_header(conn, "Request-Id"));
            res = &RESPONSE_FORBIDDEN;
            conn_send_response(conn, res);
            return;
        } else {
            fprintf(stderr, "%s,%s,%d,%s\n", "PUT", URI, 500, conn_get_header(conn, "Request-Id"));
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
            conn_send_response(conn, res);
            return;
        }
    }

    fake_flock(URI, 1, 1);
    // flock(fd, LOCK_EX);
    pthread_mutex_unlock(&m);

    res = conn_recv_file(conn, fd);

    if (exists && res == NULL) {
        res = &RESPONSE_OK;
        fprintf(stderr, "%s,%s,%d,%s\n", "PUT", URI, 200, conn_get_header(conn, "Request-Id"));
    } else if (!exists && res == NULL) {
        res = &RESPONSE_CREATED;
        fprintf(stderr, "%s,%s,%d,%s\n", "PUT", URI, 201, conn_get_header(conn, "Request-Id"));
    }

    conn_send_response(conn, res);
    fake_flock(URI, 2, 1);
    // flock(fd, LOCK_UN);
    close(fd);
    return;
}

void handle_unsupported(conn_t *conn) {
    const Response_t *res = NULL;
    res = &RESPONSE_NOT_IMPLEMENTED;
    conn_send_response(conn, res);
    fprintf(stderr, "%s,%s,%d,%s\n", "", "", 501, "0");
}
