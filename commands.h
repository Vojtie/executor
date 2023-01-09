//
// Created by Wojciech Kuzebski on 14/12/2022.
//

#ifndef MIMUW_FORK_COMMANDS_H
#define MIMUW_FORK_COMMANDS_H

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <limits.h>

#include "utils.h"

#define MAX_N_TASKS 4096
#define MAX_LINE_LEN 1022 + 1

typedef uint16_t task_id_t;

struct Task {
    task_id_t task_id;
    pid_t pid;
    pthread_t out_reader;
    pthread_t err_reader;
    char stdout_buff[MAX_LINE_LEN];
    char stderr_buff[MAX_LINE_LEN];
    sem_t stdout_mutex;
    sem_t stderr_mutex;
    char **run_args;
    bool is_running;
    bool was_joined;
    int fderr[2];
    int fdout[2];
};

void destroy(char **split_string, sem_t *m1, sem_t *m2) {
    assert(!sem_destroy(m1));
    assert(!sem_destroy(m2));
    free_split_string(split_string);
}

#endif // MIMUW_FORK_COMMANDS_H
