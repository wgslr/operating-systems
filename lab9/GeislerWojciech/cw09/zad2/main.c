#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <semaphore.h>
#include <bits/time.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#define OK(_EXPR, _ERR_MSG) if((_EXPR) < 0) { fprintf(stderr, "%s: %d %s\n", _ERR_MSG, errno, strerror(errno)); exit(1); }

// Print timestamped message with pid
#define LOG(_VERBOSE, _PRODUCER, args...) { \
    if(!_VERBOSE || verbose) { \
        struct timespec time; \
        clock_gettime(CLOCK_MONOTONIC, &time); \
        char msg[1024]; \
        sprintf(msg, args); \
        printf("%ld.%06ld %lu(%c): %s\n", time.tv_sec, time.tv_nsec / 1000, pthread_self()%10000, _PRODUCER ? 'P' : 'C', msg); \
        fflush(stdout); \
    }\
}

typedef struct {
    char **buffer;
    size_t next_read;
    size_t next_write; // 1 past last written
} buffer;

typedef enum {
    SMALLER = -1,
    EQUAL = 0,
    BIGGER = 1
} SEARCH_MODE;

typedef struct {
    char input_path[128];
    int producers_cnt;
    int consumers_cnt;
    size_t buffor_size;
    int thread_ttl;
    int threshold;
    SEARCH_MODE search_mode;
    bool verbose;
} config;

bool matching(const char* line, const config *c);

void print_buf(size_t s) ;

bool verbose;

buffer buff;

sem_t **slot_locks;
sem_t *queue_lock;
sem_t *free_slots;
sem_t *filled_slots;

int skipped = 0;

void semsignal(sem_t* sem) {
    OK(sem_post(sem), "Semaphore post failed");
}


void semwait(sem_t* sem) {
    OK(sem_wait(sem), "Waiting for semaphore failed");
}

void *produce(const config *c) {
    LOG(1, 1, "Read file %s", c->input_path);

    FILE *fd = fopen(c->input_path, "r");

    if(fd == NULL) {
        fprintf(stderr, "Error opening input file for reading: %s\n", strerror(errno));
    }

    const time_t start_time = time(NULL);
    char *line = NULL;
    size_t len = 0;
    int line_number = 0;

    while(c->thread_ttl == 0 || (time(NULL) - start_time) < c->thread_ttl) {
        // TODO end on ctrl+c
        if(getline(&line, &len, fd) == -1) {
            LOG(1, 1, "End of file");
            break;
        }
        ++line_number;

        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec = start_time + c->thread_ttl;

        LOG(1,1,"Waiting for a place in buffer");
        OK(sem_timedwait(free_slots, &timeout), "Timeout");
//
//        LOG(1,1,"Waiting for queue lock");
//        semwait(queue_lock);

        size_t pos = buff.next_write;

        do {
            LOG(1,1, "Locking slot %zu", pos);
            semwait(slot_locks[pos]);
            LOG(1,1, "Locked slot %zu", pos);

            if(buff.buffer[pos] != NULL) {
                LOG(1,1, "Slot %zu not empty, looking for another", pos);
                semsignal(slot_locks[pos]);
                pos = (pos + 1) % c->buffor_size;
                ++skipped;
                fprintf(stderr, "Skipped %d\n", skipped);
            } else {
                buff.next_write = (pos + 1) % c->buffor_size;
                LOG(1,1, "Set next write to %zu", (pos + 1) % c->buffor_size);

//                printf("%d Line\n", line_number);
                buff.buffer[pos] = calloc(strlen(line) + 1 + 0, sizeof(char));
                strcpy(buff.buffer[pos], line);
//                *((int*) (buff.buffer[pos] + (strlen(line) + 2))) = line_number;

                LOG(1,1, "Written line to slot %zu. Freeing lock", pos);
                semsignal(slot_locks[pos]);
                semsignal(filled_slots);
                LOG(1,1, "Increased filled_slots count", pos);
                break;
            }
        } while(true);
    }
    fclose(fd);
    free(line);
    return NULL;
}


void *consume(const config *c) {
    const time_t start_time = time(NULL);
    LOG(1,0, "Consumer is born");

    while(c->thread_ttl == 0 || (time(NULL) - start_time) < c->thread_ttl) {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec = start_time + c->thread_ttl;

        LOG(1,0,"Waiting for information place in buffer");
        OK(sem_timedwait(filled_slots, &timeout), "Timeout");

        size_t pos = buff.next_read;

        do {
            LOG(1,0, "Locking slot %zu", pos);
            semwait(slot_locks[pos]);

            LOG(1,0, "Locked slot %zu", pos);

            if(buff.buffer[pos] == NULL) {
                LOG(1,0, "Slot %zu not filled, looking for another", pos);
                semsignal(slot_locks[pos]);
                pos = (pos + 1) % c->buffor_size;
                ++skipped;
                fprintf(stderr, "Skipped %d\n", skipped);
            } else {
                buff.next_read = (pos + 1) % c->buffor_size;
                LOG(1,0, "Set next read to %zu", (pos + 1) % c->buffor_size);

//                size_t len = strlen(buff.buffer[pos]);
//                int line_number = *((int*) (buff.buffer[pos] + (strlen(buff.buffer[pos]) + 2)));

                if(matching(buff.buffer[pos], c)) {
                    LOG(0, 0, "buff[%zu]: %.*s", pos, (int)strlen(buff.buffer[pos]) - 1, buff.buffer[pos]);
                } else {
                    LOG(1, 0, "skipping buff[%zu]", pos);
                }

                LOG(1, 0, "Emptying slot %zu", pos);
                free(buff.buffer[pos]);
                buff.buffer[pos] = NULL;

                LOG(1,0, "Unlocking slot %zu", pos);
                semsignal(slot_locks[pos]);
                semsignal(free_slots);
                break;
            }
        } while(true);
    }

    return NULL;
}

void print_buf(size_t s) {
    for(int i = 0; i < s; ++i) {
        printf("%3d", i);
    }
    printf("\t%d..%d", buff.next_read, buff.next_write);
    printf("\n");
    for(int i = 0; i < s; ++i) {
        printf("%3d", buff.buffer[i] != NULL);
    }
    printf("\n");

}

bool matching(const char* line, const config *c) {
    return true;
    const size_t len = strlen(line);
    switch(c->search_mode) {
        case BIGGER:
            return len > c->threshold;
        case EQUAL:
            return len == c->threshold;
        case SMALLER:
            return len < c->threshold;
        default:
            fprintf(stderr, "Unexpected enum value\n");
            exit(1);
    }
}

config load_config(const char *path) {
    config c;
    int verbose_int;
    FILE *const f = fopen(path, "r");
    if(f == NULL) {
        fprintf(stderr, "Could not open config file\n");
        exit(1);
    }
    if(fscanf(f, "%s %d %d %zu %d %d %d %d", c.input_path, &c.producers_cnt, &c.consumers_cnt,
           &c.buffor_size, &c.thread_ttl, &c.threshold, &c.search_mode, &verbose_int) < 7) {
        fprintf(stderr, "Too few values in config file\n");
        exit(1);
    }

    c.verbose = (bool) verbose_int;
    fclose(f);
    return c;
}

void spawn(config *c) {
    pthread_attr_t *attr = calloc(1, sizeof(pthread_attr_t));

    pthread_t p_tids[c->producers_cnt];
    pthread_t c_tids[c->consumers_cnt];
    pthread_attr_init(attr);

    for(int i = 0; i < c->producers_cnt; ++i) {
        pthread_create(p_tids + i, attr, (void *(*)(void *)) &produce, (void *) c);
    }
    for(int i = 0; i < c->consumers_cnt; ++i) {
        pthread_create(c_tids + i, attr, (void *(*)(void *)) &consume, (void *) c);
    }

    for(int i = 0; i < c->producers_cnt; ++i) {
        pthread_join(p_tids[i], NULL);
    }
    for(int i = 0; i < c->consumers_cnt; ++i) {
        pthread_join(c_tids[i], NULL);
    }

    pthread_attr_destroy(attr);
    free(attr);
}


int main(int argc, char *argv[]) {
    if(argc < 2) {
        fprintf(stderr, "Please provide path to config file\n");
        exit(1);
    }
    config c = load_config(argv[1]);
    verbose = c.verbose;

    buff.buffer = calloc(c.buffor_size, sizeof(char *));
    buff.next_write = 0;
    buff.next_read = 0;

    slot_locks = calloc(c.buffor_size, sizeof(sem_t *));
    queue_lock = calloc(1, sizeof(sem_t));
    free_slots = calloc(1, sizeof(sem_t));
    filled_slots = calloc(1, sizeof(sem_t));

    for(size_t i = 0; i < c.buffor_size; ++i) {
        slot_locks[i] = calloc(1, sizeof(sem_t));
        sem_init(slot_locks[i], 0, 1);
    }
    sem_init(queue_lock, 0, 1);
    sem_init(free_slots, 0, (unsigned int) c.buffor_size);
    sem_init(filled_slots, 0, 0);

    LOG(1, 0, "Main");

    spawn(&c);


    LOG(1, 0, "Freeing");

    for(size_t i = 0; i < c.buffor_size; ++i) {
        sem_destroy(slot_locks[i]);
        free(slot_locks[i]);
    }
    free(slot_locks);

    sem_destroy(queue_lock);
    sem_destroy(free_slots);
    sem_destroy(filled_slots);
    free(queue_lock);
    free(free_slots);
    free(filled_slots);

    return 0;
}
