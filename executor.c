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

struct EndedMessage end_msgs[MAX_N_TASKS];
int end_msgs_cnt = 0;
sem_t end_msgs_mtx;

int finished_proc_cnt = 0;
sem_t finished_proc_mtx;

sem_t print_mtx;

sem_t main_thread_mtx;
bool processing_command = false;

pthread_barrier_t run_barrier;

struct ControllerTaskId prev_controller_task_id = {.controller = -1, .task_id = -1};

void kill_tasks();
void destroy();

void wait_run_task()
{
    int s = pthread_barrier_wait(&run_barrier);
    if (s != 0 && s != PTHREAD_BARRIER_SERIAL_THREAD) {
        fprintf(stderr, "pthread barrier %d\n", s);
        exit(1);
    }
}

void *manage_output(void *arg)
{
    struct ReaderArg* rarg = arg;
    char local_buf[MAX_LINE_LEN];
    FILE *in_stream = fdopen(rarg->fd[0], "r");

    while (read_line(local_buf, MAX_LINE_LEN, in_stream, true)) {
        ASSERT_ZERO(sem_wait(rarg->mutex));
        memcpy(rarg->buf, local_buf, MAX_LINE_LEN);
        ASSERT_ZERO(sem_post(rarg->mutex));
    }
    ASSERT_ZERO(fclose(in_stream));
    return 0;
}

void *run(void *arg)
{
    task_id_t task_id = *(task_id_t *)arg;
    struct Task *task = &tasks[task_id];
    char **task_args = task->run_args + 1;

    int fderr[2];
    int fdout[2];
    ASSERT_ZERO(pipe(fderr));
    ASSERT_ZERO(pipe(fdout));
    set_close_on_exec(fderr[0], true);
    set_close_on_exec(fderr[1], true);
    set_close_on_exec(fdout[0], true);
    set_close_on_exec(fdout[1], true);

    struct ReaderArg out_reader_arg = {.mutex = &task->stdout_mutex, .buf = task->stdout_buff, .fd = {fdout[0], fdout[1]}};
    ASSERT_ZERO(pthread_create(&task->out_reader, NULL, manage_output, &out_reader_arg));

    struct ReaderArg err_reader_arg = {.mutex = &task->stderr_mutex, .buf = task->stderr_buff, .fd = {fderr[0], fderr[1]}};
    ASSERT_ZERO(pthread_create(&task->err_reader, NULL, manage_output, &err_reader_arg));

    task->pid = fork();
    ASSERT_SYS_OK(task->pid);

    if (!task->pid) {
        ASSERT_SYS_OK(dup2(fdout[1], STDOUT_FILENO));
        ASSERT_SYS_OK(dup2(fderr[1], STDERR_FILENO));
        ASSERT_SYS_OK(execvp(task_args[0], task_args));
        exit(1);
    }
    free_split_string(task->run_args);
    ASSERT_ZERO(close(fdout[1]));
    ASSERT_ZERO(close(fderr[1]));
    
    printf("Task %d started: pid %d.\n", task_id, task->pid);
    
    wait_run_task();

    int status;
    ASSERT_SYS_OK(waitpid(task->pid, &status, 0));
    
    ASSERT_ZERO(sem_wait(&finished_proc_mtx));
    finished_proc_cnt++;
    if (finished_proc_cnt == 1) {
        ASSERT_ZERO(sem_wait(&main_thread_mtx)); // first finished process blocks main thread
    }
    ASSERT_ZERO(sem_post(&finished_proc_mtx));

    ASSERT_ZERO(pthread_join(task->out_reader, NULL));
    ASSERT_ZERO(pthread_join(task->err_reader, NULL));

    bool signalled = false;
    int exit_code = -1;

    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else {
        signalled = true;
    }

    if (processing_command) {
        ASSERT_ZERO(sem_wait(&end_msgs_mtx));
        assert(end_msgs_cnt < MAX_N_TASKS);
        struct EndedMessage *end_msg = &end_msgs[end_msgs_cnt++];
        end_msg->task_id = task_id;
        end_msg->signalled = signalled;
        end_msg->exit_code = exit_code;
        end_msg->controller = task->controller;
        ASSERT_ZERO(sem_post(&end_msgs_mtx));
    } else {
        ASSERT_ZERO(sem_wait(&print_mtx));
        if (prev_controller_task_id.controller != -1 && !tasks[prev_controller_task_id.task_id].was_joined) {
            ASSERT_ZERO(pthread_join(prev_controller_task_id.controller, NULL));
            tasks[prev_controller_task_id.task_id].was_joined = true;
        }
        if (signalled) {
            printf("Task %d ended: signalled.\n", task_id);
        } else {
            printf("Task %d ended: status %d.\n", task_id, exit_code);
        }
        prev_controller_task_id.controller = task->controller;
        prev_controller_task_id.task_id = task_id;
        ASSERT_ZERO(sem_post(&print_mtx));
    }
    ASSERT_ZERO(sem_wait(&finished_proc_mtx));
    finished_proc_cnt--;
    if (finished_proc_cnt == 0) {
        if (processing_command) {
            prev_controller_task_id.controller = -1;
            prev_controller_task_id.task_id = -1;
        }
        ASSERT_ZERO(sem_post(&main_thread_mtx)); // unables main thread to change state
    }
    ASSERT_ZERO(sem_post(&finished_proc_mtx));
    
    return NULL;
}

void init_tasks()
{
    for (int i = 0; i < MAX_N_TASKS; ++i) {
        struct Task *task = &tasks[i];
        task->task_id = i;
        task->was_joined = false;
        memset(task->stdout_buff, '\0', MAX_LINE_LEN);
        memset(task->stderr_buff, '\0', MAX_LINE_LEN);
        ASSERT_ZERO(sem_init(&task->stderr_mutex, 0, 1));
        ASSERT_ZERO(sem_init(&task->stdout_mutex, 0, 1));
    }
}

struct Task *new_task(char **args)
{
    struct Task* new = &tasks[new_task_id++];
    new->run_args = args;
    return new;
}

void print_line(sem_t *mutex, task_id_t task_id, const char *line, const char *line_src_strm)
{
    ASSERT_ZERO(sem_wait(mutex));
    printf("Task %d %s: '%s'.\n", task_id, line_src_strm, line);
    ASSERT_ZERO(sem_post(mutex));
}

// print infos about processes that finished during command processing
void print_overdue_end_infos()
{
    // in the possesion of main_thread_mtx
    ASSERT_ZERO(sem_wait(&print_mtx));
    for (int i = 0; i < end_msgs_cnt; ++i) {
        struct EndedMessage end_msg = end_msgs[i];
        // ensure that after printing the end message the controller thread is gone
        // which means that all the threads/processes associated with this task are gone
        if (!tasks[end_msg.task_id].was_joined) {
            ASSERT_ZERO(pthread_join(end_msg.controller, NULL));
            tasks[end_msg.task_id].was_joined = true;
        }
        if (end_msg.signalled) {
            printf("Task %d ended: signalled.\n", end_msg.task_id);
        } else {
            printf("Task %d ended: status %d.\n", end_msg.task_id, end_msg.exit_code);
        }
    }
    end_msgs_cnt = 0;
    ASSERT_ZERO(sem_post(&print_mtx));
}

void join_last_controller()
{
    if (prev_controller_task_id.controller != -1) {
        ASSERT_ZERO(pthread_join(prev_controller_task_id.controller, NULL));
        tasks[prev_controller_task_id.task_id].was_joined = true;
        prev_controller_task_id.controller = -1;
        prev_controller_task_id.task_id = -1;
    }
}

void init()
{
    ASSERT_ZERO(pthread_barrier_init(&run_barrier, NULL, 2));
    ASSERT_ZERO(sem_init(&print_mtx, 0, 1));
    ASSERT_ZERO(sem_init(&main_thread_mtx, 0, 1));
    ASSERT_ZERO(sem_init(&finished_proc_mtx, 0, 1));
    ASSERT_ZERO(sem_init(&end_msgs_mtx, 0, 1));
    init_tasks();
}

void set_processing_command(bool start_processing)
{
    ASSERT_ZERO(sem_wait(&main_thread_mtx));
    if (start_processing) {
        join_last_controller();
    }
    else {
        print_overdue_end_infos();
    }
    processing_command = start_processing;
    ASSERT_ZERO(sem_post(&main_thread_mtx));
}

bool manage_command(char **args)
{
    if (!strcmp(*args, "quit")) {
        free_split_string(args);
        return true;
    }
    if (!strcmp(*args, "run")) {
        struct Task *task = new_task(args);
        ASSERT_ZERO(pthread_create(&task->controller, NULL, run, &task->task_id));
        wait_run_task();
    }
    else if (!strcmp(*args, "out") || !strcmp(*args, "err")) {
        task_id_t task_id = (task_id_t) strtol(args[1], NULL, 10);
        struct Task *task = &tasks[task_id];
        if (!strcmp(*args, "out"))
            print_line(&task->stdout_mutex, task->task_id, task->stdout_buff, "stdout");
        else
            print_line(&task->stderr_mutex, task->task_id, task->stderr_buff, "stderr");
    }
    else if (!strcmp(*args, "sleep")) {
        assert(args[1]);
        long long nap_time = strtoll(args[1], NULL, 10);
        usleep(nap_time * 1000);
    }
    else if (!strcmp(*args, "kill")) {
        assert(args[1]);
        task_id_t task_id = (task_id_t) strtol(args[1], NULL, 10);
        kill(tasks[task_id].pid, SIGINT);
    }
    if (strcmp(*args, "run") != 0) {
        free_split_string(args);
    }

    return false;
}

int main(void)
{
    init();
    char input[511];
    bool quit = false;
    
    while (read_line(input, 511, stdin, true))
    {
        set_processing_command(true);

        char **args = split_string(input);
        quit = manage_command(args);
        if (quit) break;

        set_processing_command(false);
    }
    if (!quit) {
        set_processing_command(true);
    }
    kill_tasks();
    // all tasks have been killed and all threads have been joined
    print_overdue_end_infos();
    destroy();
    
    return 0;
}

void kill_tasks()
{
    for (int i = 0; i < new_task_id; ++i) {
        struct Task *task = &tasks[i];
        kill(task->pid, SIGKILL);
        if (!task->was_joined) {
            ASSERT_ZERO(pthread_join(task->controller, NULL));
            task->was_joined = true;
        }
    }
}

void destroy()
{
    for (int i = 0; i < MAX_N_TASKS; ++i) {
        struct Task *task = &tasks[i];
        ASSERT_ZERO(sem_destroy(&task->stdout_mutex));
        ASSERT_ZERO(sem_destroy(&task->stderr_mutex));
    }
    ASSERT_ZERO(sem_destroy(&end_msgs_mtx));
    ASSERT_ZERO(sem_destroy(&finished_proc_mtx));
    ASSERT_ZERO(sem_destroy(&print_mtx));
    ASSERT_ZERO(sem_destroy(&main_thread_mtx));
    ASSERT_ZERO(pthread_barrier_destroy(&run_barrier));
}
