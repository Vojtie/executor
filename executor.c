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
struct Task tasks[MAX_N_TASKS];
sem_t main_mutex;

void kill_tasks();
void out();

void *manage_output(void *arg)
{
    struct ReaderArg* rarg = arg;
    char local_buf[MAX_LINE_LEN];
    FILE *in_stream = fdopen(rarg->fd[0], "r");
    assert(!close(rarg->fd[1]));

    fprintf(stderr, "waiting for input\n");
// TODO rozroznienie linii od jej czesci
    while (read_line(local_buf, MAX_LINE_LEN, in_stream, true)) {
        printf("read %s\n", local_buf);
        assert(!sem_wait(rarg->mutex));
        printf("wrote\n");
        memcpy(rarg->buf, local_buf, MAX_LINE_LEN);
        assert(!sem_post(rarg->mutex));
    }
    return 0;
}

void *run(void *arg)
{
    task_id_t task_id = *(task_id_t *)arg;
    struct Task *task = &tasks[task_id];
    char **run_args = task->run_args;
    sem_t *stdout_mutex = &task->stdout_mutex;
    sem_t *stderr_mutex = &task->stderr_mutex;
    char *stdout_buf = task->stdout_buff;
    char *stderr_buf = task->stderr_buff;
    char **task_args = run_args + 1;
    task->is_running = true;
    printf("running task %d :> %s\n", task_id, task->run_args[1]);
    int fderr[2];
    int fdout[2];
    pipe(fderr);
    pipe(fdout);

    struct ReaderArg out_reader_arg = {.mutex = stdout_mutex, .buf = stdout_buf, .fd = {fdout[0], fdout[1]}};
    assert(!pthread_create(&task->out_reader, NULL, manage_output, &out_reader_arg));

    struct ReaderArg err_reader_arg = {.mutex = stderr_mutex, .buf = stderr_buf, .fd = {fderr[0], fderr[1]}};
    assert(!pthread_create(&task->err_reader, NULL, manage_output, &err_reader_arg));

    task->pid = fork();
    assert(task->pid != -1);
    if (!task->pid) {
        fprintf(stderr, "executing exec\n");
        dup2(fdout[1], STDOUT_FILENO);
        close(fdout[0]);
        close(fdout[1]);

        dup2(fderr[1], STDERR_FILENO);
        close(fderr[1]);
        close(fderr[0]);
        if (task_args[0][0] == '.') {
            execv(task_args[0], task_args);
        } else {
            execvp(task_args[0], task_args);
        }
        exit(1);
    }
    printf("Task %d started: pid %d.\n", task_id, task->pid); // TODO podczas sleep?
//    assert(!close(fdout[1]));
//    assert(!close(fderr[1]));

    assert(!sem_post(&main_mutex));

    int status;
    if (waitpid(task->pid, &status, 0) == -1) {
        fatal("waitpid failed\n");
        exit(EXIT_FAILURE);
    }
    printf("Task %d ended\n", task_id);
    assert(!sem_wait(&main_mutex));

    task->is_running = false;
//    void *out_status, *err_status;
    assert(!pthread_join(task->out_reader, NULL));
    assert(!pthread_join(task->err_reader, NULL));
//    assert(!(*(int *)out_status));
//    assert(!(*(int *)err_status));

    if (WIFEXITED(status)) {
        const int es = WEXITSTATUS(status);
        printf("Task %d ended: status %d.\n", task_id, es); // TODO podczas sleep?
    } else {
        printf("Task %d ended: signalled.\n", task_id);
    }
    close(fdout[0]);
    close(fderr[0]);
//    free_split_string(run_args);
    assert(!sem_post(&main_mutex));
    return 0;
}

void init_tasks()
{
    for (int i = 0; i < MAX_N_TASKS; ++i) {
        struct Task *task = &tasks[i];
        task->task_id = i;
        task->is_running = false;
        memset(task->stdout_buff, '\0', MAX_LINE_LEN);
        memset(task->stderr_buff, '\0', MAX_LINE_LEN);
        assert(!sem_init(&task->stderr_mutex, 0, 1));
        assert(!sem_init(&task->stdout_mutex, 0, 1));
    }
}

void free_tasks()
{
    for (int i = 0; i < MAX_N_TASKS; ++i) {
        struct Task *task = &tasks[i];
        assert(!sem_destroy(&task->stdout_mutex));
        assert(!sem_destroy(&task->stderr_mutex));
        if (i < new_task_id) {
            free_split_string(task->run_args);
        }
    }
}

struct Task *new_task()
{
    return &tasks[new_task_id++];
}

void print_line(sem_t *mutex, task_id_t task_id, const char *line, const char *line_src_strm)
{
    assert(!sem_wait(mutex));
    printf("Task %d %s: '%s'.\n", task_id, line_src_strm, line);
    assert(!sem_post(mutex));
}

int main(void)
{
//    fatal("");
    init_tasks();
    char input[511];
    assert(!sem_init(&main_mutex, 0, 1));
    while (read_line(input, 511, stdin, true)) {
        char **args = split_string(input);
        assert(*args);
        assert(!sem_wait(&main_mutex));
        
        if (!strcmp(*args, "quit")) {
            break;
        }
        else if (!strcmp(*args, "run")) {
            assert(args[1]);
            struct Task *task = new_task();
            task->run_args = args;
            assert(!pthread_create(&task->controller, NULL, run, &task->task_id));
            continue;
        }
        else if (!strcmp(*args, "out") || !strcmp(*args, "err")) {
            assert(args[1]);
            task_id_t task_id = (task_id_t)strtol(args[1], NULL, 10);

            if (task_id >= new_task_id) {
                fprintf(stderr, "task %hu has not been run!\n", task_id);
                assert(!sem_post(&main_mutex));
                continue;
            }
            struct Task *task = &tasks[task_id];

            if (!strcmp(*args, "out"))
                print_line(&task->stdout_mutex, task->task_id, task->stdout_buff, "stdout");
            else
                print_line(&task->stderr_mutex, task->task_id, task->stderr_buff, "stderr");
        }
        else if (!strcmp(*args, "sleep")) {
            assert(args[1]);
            long long nap_time = strtoll(args[1], NULL, 10);
//            fprintf(stderr, "taking a nap... zZzz\n");
            usleep(nap_time * 1000);
//            fprintf(stderr, "woke up :D armed and ready!\n");
        }
        else if (!strcmp(*args, "kill")) {
            assert(args[1]);
            task_id_t task_id = (task_id_t)strtol(args[1], NULL, 10);

            if (task_id >= new_task_id) {
                fprintf(stderr, "task %hu has not been run!\n", task_id);
                assert(!sem_post(&main_mutex));
                continue;
            }
//            if (tasks[task_id]->is_running) { // to avoid sending task when process just finished and posted mutex but didnt exit
            kill(tasks[task_id].pid, SIGINT);
//            continue; - we don't know if the process will exit after sending SIGINT
        }
        else if (strcmp(*args, "\n") != 0) {
            fprintf(stderr, "wrong command\n"); // TODO break
        }
        assert(!sem_post(&main_mutex));
    }
    assert(!sem_destroy(&main_mutex));
    kill_tasks();
    free_tasks();
    return 0;
}

void kill_tasks()
{
    for (int i = 0; i < new_task_id; ++i) {
        struct Task *task = &tasks[i];
        kill(task->pid, SIGKILL);
        //All the threads in your process will be terminated when you return from main() .
//        assert(!pthread_join(task->controller, NULL));
//        assert(!pthread_join(task->out_reader, NULL));
//        assert(!pthread_join(task->err_reader, NULL));
    }
}