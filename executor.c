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
sem_t *main_mutex;



void kill_tasks();
void out();

void manage_output(int fd, char *buf, sem_t *mutex)
{
    char local_buf[MAX_LINE_LEN];
    FILE *in_stream = fdopen(fd, "r");

//    fprintf(stderr, "waiting for input\n");
// TODO rozroznienie linii od jej czesci
    while (read_line(local_buf, MAX_LINE_LEN, in_stream, true)) {
        assert(!sem_wait(mutex));
        memcpy(buf, local_buf, MAX_LINE_LEN);
        assert(!sem_post(mutex));
    }

//    fprintf(stderr, "finished reading\n");
}

void handler(int sig)
{   // inherited main_mutex
//    fprintf(stderr, "handler\n");
    task_id_t task_id = new_task_id - 1;
    struct Task *task = tasks[task_id];
    pid_t rnr_pid = tasks[task_id]->pid;

    kill(rnr_pid, SIGINT);
    int status;
    if (waitpid(rnr_pid, &status, 0) == -1) { // TODO czy SIGINT na pewno kończy zadanie?
        perror("waitpid failed");
        exit(EXIT_FAILURE);
    }
    task->is_running = false;
    kill(task->stdout_rdr_pid, SIGINT);
    kill(task->stderr_rdr_pid, SIGINT);
    assert(waitpid(task->stdout_rdr_pid, NULL, 0) != -1);
    assert(waitpid(task->stderr_rdr_pid, NULL, 0) != -1);

    if (WIFEXITED(status)) {
        const int es = WEXITSTATUS(status);
        printf("Task %d ended: status %d.\n", task_id, es); // TODO podczas sleep?
    } else {
        printf("Task %d ended: signalled.\n", task_id);
    }
    assert(!sem_post(main_mutex));
    exit(0);
}

void run(char **run_args, pid_t *runner_pid, task_id_t task_id, sem_t *stdout_mutex, sem_t *stderr_mutex, char *stdout_buf, char *stderr_buf)
{
//    printf("running task %d :>\n", task_id);

    char **task_args = run_args + 1;
    int fderr[2];
    int fdout[2];
    pipe(fderr);
    pipe(fdout);

    pid_t stdout_rdr_pid = fork();
    if (!stdout_rdr_pid) {
        close(fdout[1]);
        close(fderr[1]);
        close(fderr[0]);
        manage_output(fdout[0], stdout_buf, stdout_mutex);
        close(fdout[0]);
        exit(0);
    }
    pid_t stderr_rdr_pid = fork();
    if (!stderr_rdr_pid) {
        close(fdout[1]);
        close(fderr[1]);
        close(fdout[0]);
        manage_output(fderr[0], stderr_buf, stderr_mutex);
        close(fderr[0]);
        exit(0);
    }
    close(fdout[0]);
    close(fderr[0]);
    tasks[task_id]->stdout_rdr_pid = stdout_rdr_pid;
    tasks[task_id]->stderr_rdr_pid = stderr_rdr_pid;
    // change SIGINT handler to kill child processes
    struct sigaction new_action;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_handler = handler;
    new_action.sa_flags = 0;
    assert(!sigaction(SIGINT, &new_action, NULL));

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
    *runner_pid = rnr_pid;
    printf("Task %d started: pid %d.\n", task_id, rnr_pid); // TODO podczas sleep?
    close(fdout[1]);
    close(fderr[1]);

    assert(!sem_post(main_mutex));
    // now we can receive a SIGINT from main process
    int status;
    if ( // TODO system used to print 'waitpid failed: interrupted system call' and doesn't anymore
        waitpid(rnr_pid, &status, 0)
                == -1) {
//            fprintf(stderr, "waitpid interrupted");
            exit(EXIT_FAILURE);
    }
//    fprintf(stderr, "runner finished\n");
    assert(!sem_wait(main_mutex));
    // now we cannot receive SIGINT from main process
    tasks[task_id]->is_running = false;
//    fprintf(stderr, "runner finished mutexed\n");

//    kill(stdout_rdr_pid, SIGINT);
//    kill(stderr_rdr_pid, SIGINT);
    assert(waitpid(stdout_rdr_pid, NULL, 0) != -1);
    assert(waitpid(stderr_rdr_pid, NULL, 0) != -1);

//    assert(!sem_wait(main_mutex));
    if (WIFEXITED(status)) {
        const int es = WEXITSTATUS(status);
        printf("Task %d ended: status %d.\n", task_id, es); // TODO podczas sleep?
    } else {
        printf("Task %d ended: signalled.\n", task_id);
    }
    assert(!sem_post(main_mutex));
//    fprintf(stderr, "goodbye\n");
//    assert(waitpid(stdout_rdr_pid, NULL, 0) == stdout_rdr_pid);
//    assert(waitpid(stderr_rdr_pid, NULL, 0) == stderr_rdr_pid);
//
//    printf("ending task %d :<\n", task_id);
//
//    destroy(run_args, stdout_mutex, stderr_mutex);
//    exit(0); // closes opened streams
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

int main(void)
{
//    fatal("");
    char input[511];
    main_mutex = mmap(
        NULL,
        sizeof(sem_t),
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1,
        0);
    assert(!sem_init(main_mutex, 1, 1));
    while (read_line(input, 511, stdin, true))
    {
        char **args = split_string(input);
        assert(*args);
        assert(!sem_wait(main_mutex));

        if (!strcmp(*args, "quit")) {
            kill_tasks();
            break;
        }
        else if (!strcmp(*args, "run")) {
            assert(args[1]);
            struct Task *task = init_task();

            pid_t pid = fork();
            if (!pid) {
                run(args, &task->pid, task->task_id, &task->stdout_mutex, &task->stderr_mutex, task->stdout_buff, task->stderr_buff);
                return 0;
            }
            task->ppid = pid;
            free_split_string(args);
            continue;
        }
        else if (!strcmp(*args, "out") || !strcmp(*args, "err")) {
            assert(args[1]);
            task_id_t task_id = (task_id_t)strtol(args[1], NULL, 10);

            if (task_id >= new_task_id) {
//                fprintf(stderr, "task %hu has not been run!\n", task_id);
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
//            fprintf(stderr, "taking a nap... zZzz\n");
            usleep(nap_time);
//            fprintf(stderr, "woke up :D armed and ready!\n");
        }
        else if (!strcmp(*args, "kill")) {
            assert(args[1]);
            task_id_t task_id = (task_id_t)strtol(args[1], NULL, 10);

            if (task_id >= new_task_id) {
//                fprintf(stderr, "task %hu has not been run!\n", task_id);
                assert(!sem_post(main_mutex));
                continue;
            }
            if (tasks[task_id]->is_running) { // to avoid sending task when process just finished and posted mutex but didnt exit
                kill(tasks[task_id]->ppid, SIGINT);
                continue;
            }
        }
        else if (strcmp(*args, "\n") != 0)
//            fprintf(stderr, "wrong command\n"); //TODO break

        assert(!sem_post(main_mutex));
    }
    assert(!sem_destroy(main_mutex));
    kill_tasks();
    return 0;
}

void kill_tasks()
{
    for (int i = 0; i < new_task_id; ++i) {
        kill(tasks[i]->pid, SIGINT);
    }
}