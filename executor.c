#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "commands.h"
#include "err.h"
#include "utils.h"

int new_task_id = 0;
struct Task *tasks[MAX_N_TASKS];

void quit();
void out();

void manage_output(int fd, char *buf, sem_t *mutex)
{
    char local_buf[MAX_LINE_LEN];
    FILE *in_stream = fdopen(fd, "r");

    fprintf(stderr, "waiting for input\n");

    while (read_line(local_buf, MAX_LINE_LEN, in_stream, true)) {
        assert(!sem_wait(mutex));
        memcpy(buf, local_buf, MAX_LINE_LEN);
        assert(!sem_post(mutex));
    }

    fprintf(stderr, "finished reading\n");
}

void run(char **run_args, task_id_t task_id, sem_t *stdout_mutex, sem_t *stderr_mutex, char *stdout_buf, char *stderr_buf)
{
    printf("running task %d :>\n", task_id);

    char **task_args = run_args + 1;
    int fderr[2];
    int fdout[2];
    pipe(fderr);
    pipe(fdout);
    pid_t rnr_pid = fork();

    if (!rnr_pid) {
        dup2(fdout[1], STDOUT_FILENO);
        close(fdout[0]);
        close(fdout[1]);

        dup2(fderr[1], STDERR_FILENO);
        close(fderr[1]);
        close(fderr[0]);

        if (task_args[0][0] == '.') {
            execv(task_args[0], task_args);
            fatal("error in execv(./)");
        } else {
            execvp(task_args[0], task_args);
            fatal("error in execvp()");
        }
    }
    close(fdout[1]);
    close(fderr[1]);

    pid_t stdout_rdr_pid = fork();
    if (!stdout_rdr_pid) {
        close(fderr[0]);
        manage_output(fdout[0], stdout_buf, stdout_mutex);
        close(fdout[0]);
        exit(0);
    }
    pid_t stderr_rdr_pid = fork();
    if (!stderr_rdr_pid) {
        close(fdout[0]);
        manage_output(fderr[0], stderr_buf, stderr_mutex);
        close(fderr[0]);
        exit(0);
    }
    close(fdout[0]);
    close(fderr[0]);

//    assert(waitpid(stdout_rdr_pid, NULL, 0) == stdout_rdr_pid);
//    assert(waitpid(stderr_rdr_pid, NULL, 0) == stderr_rdr_pid);
//
//    printf("ending task %d :<\n", task_id);
//
//    destroy(run_args, stdout_mutex, stderr_mutex);
    exit(0); // closes opened streams
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

    tasks[new_task_id]->is_running = true;
    tasks[new_task_id]->task_id = new_task_id;
    memset(tasks[new_task_id]->stdout_buff, '\0', MAX_LINE_LEN);
    memset(tasks[new_task_id]->stderr_buff, '\0', MAX_LINE_LEN);

    if (sem_init(&tasks[new_task_id]->stdout_mutex, 1, 1)) {
        fatal("sem init failed\n");
    }
    if (sem_init(&tasks[new_task_id]->stderr_mutex, 1, 1)) {
        fatal("sem init failed\n");
    }

    return tasks[new_task_id++];
}

void print_line(sem_t *mutex, task_id_t task_id, const char *line, const char *line_src_strm)
{
    assert(!sem_wait(mutex));
    printf("Task %d %s: '%s'.\n", task_id, line_src_strm, line);
    assert(!sem_post(mutex));
}

int main(int argc, char *argv[])
{
//    fatal("");
    char input[511];
    while (true)
    {
        assert(read_line(input, 511, stdin, true));
        char **args = split_string(input);
        assert(*args);

        if (!strcmp(*args, "quit")) {
            quit();
            return 0;
        }
        else if (!strcmp(*args, "run")) {
            assert(args[1]);
            struct Task *task = init_task();

            pid_t pid = fork();
            if (!pid) {
                run(args, task->task_id, &task->stdout_mutex, &task->stderr_mutex, task->stdout_buff, task->stderr_buff);
                return 0;
            }
            task->pid = pid;
            free_split_string(args);
        }
        else if (!strcmp(*args, "out") || !strcmp(*args, "err")) {
            assert(args[1]);
            task_id_t task_id = (task_id_t)strtol(args[1], NULL, 10);

            if (task_id >= new_task_id) {
                fprintf(stderr, "task %hu has not been run!\n", task_id);
                continue;
            }
            struct Task *task = tasks[task_id];

            if (!strcmp(*args, "out"))
                print_line(&task->stdout_mutex, task->task_id, task->stdout_buff, "stdout");
            else
                print_line(&task->stderr_mutex, task->task_id, task->stderr_buff, "stderr");
        }
        else if (!strcmp(*args, "sleep")) {
            assert(args[1]);
            long long nap_time = strtoll(args[1], NULL, 10);
            fprintf(stderr, "taking a nap... zZzz\n");
            usleep(nap_time);
            fprintf(stderr, "woke up :D armed and ready!\n");
        }
        else if (strcmp(*args, "\n") != 0)
            fprintf(stderr, "wrong command\n");
    }
    return 0;
}

void quit()
{
    exit(0);
}