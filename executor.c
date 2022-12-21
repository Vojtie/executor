#include "commands.h"
#include "utils.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#define X 10
int new_task_id = 0;
struct Task *tasks[MAX_N_TASKS];

void quit();
void out();

void run(struct Command *run_info, sem_t *mutex, char *output)
{
    printf("hello from %d :>\n", run_info->task_id);
    int fd[2];
    pipe(fd);
    pid_t pid = fork();
    if (!pid) {
        dup2(fd[1], STDOUT_FILENO);
        close(fd[0]);
        if (run_info->task_args[0][0] == '.')
            execv(run_info->task_args[0], run_info->task_args);
        else
            execvp(run_info->task_args[0], run_info->task_args);
        fprintf(stderr, "error in exec\n");
        exit(1);
    }
    close(fd[1]);
    dup2(fd[0], STDIN_FILENO);
    char buffer[MAX_LINE_LEN];
    while (true) {
        // put read line after acquiring mutex
        bool read_l = read_line(buffer, MAX_LINE_LEN, stdin, true);
        if (!read_l)
            break;

        assert(!sem_wait(mutex));
        memcpy(output, buffer, MAX_LINE_LEN);
        assert(!sem_post(mutex));

        printf("proc %d: task %d: %s\n", getpid(), run_info->task_id, buffer);
    }
    close(fd[0]);
    printf("goodbye from %d :<\n", run_info->task_id);
    free_split_string(run_info->to_be_freed);
    exit(0);
}

struct Task *init_task()
{
    assert(new_task_id < MAX_N_TASKS);

    tasks[new_task_id] = mmap(
        NULL,
        sizeof(struct Task),
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1,
        0);

    if (tasks[new_task_id] == MAP_FAILED)
        exit(1);

    tasks[new_task_id]->task_id = new_task_id;
    memset(tasks[new_task_id]->output, '\0', MAX_LINE_LEN);
    if (!sem_init(&tasks[new_task_id]->mutex, 1, 1)) {
        fprintf(stderr, " (%d; %s)\n", errno, strerror(errno));
        exit(1);
    }

    return tasks[new_task_id++];
}

int main(int argc, char *argv[])
{
    char input[511];
    while (true) {
        assert(read_line(input, 511, stdin, true));
        char **args = split_string(input);
        if (*args == NULL) exit(1);

//        int i = 0;
//        while (args[i] != NULL) printf("%s", args[i++]);

        if (!strncmp(*args, "quit", 4)) {
            quit();
            return 0;
        } else if (!strncmp(*args, "run", 3)) {
            assert(args[1]);
            struct Task *task = init_task();

            struct Command *command = malloc(sizeof(struct Command));
            command->task_id = new_task_id;
            command->task_args = args + 1;
            command->to_be_freed = args;

            pid_t pid = fork();
            if (!pid) {
                run(command, &task->mutex, task->output);
                return 0;
            }
            task->pid = pid;
            free(command);
            free_split_string(args);
        } else if (!strncmp(*args, "out", 3)) {
            assert(args[1]);
            task_id_t task_id = (task_id_t)strtol(args[1], NULL, 10);
            assert(!sem_wait(&tasks[task_id]->mutex));
            printf("Task %d stdout: %s.\n", task_id, tasks[task_id]->output);
            assert(!sem_post(&tasks[task_id]->mutex));
        }
    }
    return 0;
}

void quit()
{
    exit(0);
}