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

#include "utils.h"

#define MAX_N_TASKS 4096
#define MAX_LINE_LEN 1022 + 1

typedef uint16_t task_id_t;

enum CommandT {
    RUN,
    OUT,
    ERR,
    KILL,
    SLEEP,
    QUIT
};

struct Command {
    enum CommandT type;
    task_id_t task_id;
    task_id_t task_args_len;
    char **task_args;
    char **to_be_freed;
};

//struct Command parse_command(int argc, char **prog_args) {
//
//}

struct Task {
    task_id_t task_id;
    pid_t pid;
    pid_t ppid;
    pid_t stdout_rdr_pid;
    pid_t stderr_rdr_pid;
    char stdout_buff[MAX_LINE_LEN];
    char stderr_buff[MAX_LINE_LEN];
    sem_t stdout_mutex;
    sem_t stderr_mutex;
    bool is_running;
};

void destroy(char **split_string, sem_t *m1, sem_t *m2) {
    assert(!sem_destroy(m1));
    assert(!sem_destroy(m2));
    free_split_string(split_string);
}

#endif // MIMUW_FORK_COMMANDS_H
