#ifndef MIMUW_FORK_EXECUTOR_H
#define MIMUW_FORK_EXECUTOR_H

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

struct EndedMessage {
    bool signalled;
    int exit_code;
    task_id_t task_id;
    pthread_t controller;
};

struct ControllerTaskId {
    task_id_t task_id;
    pthread_t controller;
};

struct Task {
    task_id_t task_id;
    pid_t pid;
    pthread_t controller;
    pthread_t out_reader;
    pthread_t err_reader;
    char stdout_buff[MAX_LINE_LEN];
    char stderr_buff[MAX_LINE_LEN];
    sem_t stdout_mutex;
    sem_t stderr_mutex;
    char **run_args;
    bool was_joined;
};

struct ReaderArg {
    int fd[2];
    char *buf;
    sem_t *mutex;
};

void assert_zero(int expr)
{
    if (expr != 0) {
        exit(1);
    }
}

void assert_sys_ok(int expr)
{
    if (expr == -1) {
        exit(1);
    }
}

#endif
