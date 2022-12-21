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
    char output[MAX_LINE_LEN];
    sem_t mutex;
};

#endif // MIMUW_FORK_COMMANDS_H
