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

struct EndMsg end_msgs[MAX_N_TASKS];
int end_msgs_cnt = 0;
sem_t emc_m;

int finished_proc_cnt = 0;
sem_t fpc_mutex;

sem_t end_info_m;

sem_t print_mutex; // to mutuallly exclude printing info

sem_t pc_mutex;
bool processing_command = false;

pthread_barrier_t task_start_info_br;

void kill_tasks();

void *read_err(void *arg)
{
    struct Task *task = &tasks[*(int *) arg];
    char local_buf[MAX_LINE_LEN];
    FILE *in_stream = fdopen(task->fderr[0], "r");

// TODO rozroznienie linii od jej czesci
    while (read_line(local_buf, MAX_LINE_LEN, in_stream, true)) {
//        printf("read %s\n", local_buf);
        assert(!sem_wait(&task->stderr_mutex));
//        printf("wrote\n");
        memcpy(task->stderr_buff, local_buf, MAX_LINE_LEN);
        assert(!sem_post(&task->stderr_mutex));
    }
    assert(!fclose(in_stream));
    return 0;
}

void *read_out(void *arg)
{
    struct Task *task = &tasks[*(int *) arg];
    char local_buf[MAX_LINE_LEN];
    FILE *in_stream = fdopen(task->fdout[0], "r");

// TODO rozroznienie linii od jej czesci
    while (read_line(local_buf, MAX_LINE_LEN, in_stream, true)) {
//        printf("read %s\n", local_buf);
        assert(!sem_wait(&task->stdout_mutex));
//        printf("wrote\n");
        memcpy(task->stdout_buff, local_buf, MAX_LINE_LEN);
        assert(!sem_post(&task->stdout_mutex));
    }
    assert(!fclose(in_stream));
    return 0;
}

void run(task_id_t task_id)
{
    struct Task *task = &tasks[task_id];
    char **task_args = task->run_args + 1;
    task->is_running = true;

    assert(!pipe(task->fderr));
    assert(!pipe(task->fdout));
    set_close_on_exec(task->fderr[0], true);
    set_close_on_exec(task->fderr[1], true);
    set_close_on_exec(task->fdout[0], true);
    set_close_on_exec(task->fdout[1], true);

    assert(!pthread_create(&task->out_reader, NULL, read_out, &task_id));
    assert(!pthread_create(&task->err_reader, NULL, read_err, &task_id));

    task->pid = fork();
    assert(task->pid != -1);
    if (!task->pid) {
//        fprintf(stderr, "executing exec\n");
        if (dup2(task->fdout[1], STDOUT_FILENO) == -1) {
            fatal("dup2 failed: %s\n", strerror(errno));
        }
//        assert(!close(fdout[0]));
//        assert(!close(fdout[1]));

        if (dup2(task->fderr[1], STDERR_FILENO) == -1) {
            fatal("dup2 failed: %s\n", strerror(errno));
        }
        // close stdin too
//        assert(!close(fderr[1]));
//        assert(!close(fderr[0]));
        if (task_args[0][0] == '.') {
            execv(task_args[0], task_args);
        } else {
            execvp(task_args[0], task_args);
        }
        exit(2);
    }
    assert(!close(task->fdout[1]));
    assert(!close(task->fderr[1]));
    printf("Task %d started: pid %d.\n", task_id, task->pid); // TODO podczas sleep?
/**
//    int status;
//    if (waitpid(task->pid, &status, 0) == -1) {
//        fatal("waitpid failed\n");
//        exit(EXIT_FAILURE);
//    }
//    assert(!sem_wait(&fpc_mutex));
//    finished_proc_cnt++;
//    if (finished_proc_cnt == 1) {
//        assert(!sem_wait(&pc_mutex)); // first finished process blocks main thread from
//    }                                 // starting or finishing processsing command
//    assert(!sem_post(&fpc_mutex));
//
////    task->is_running = false; // delete
////    void *out_status, *err_status;
//    assert(!pthread_join(task->out_reader, NULL));
//    assert(!pthread_join(task->err_reader, NULL));
////    close(fdout[0]); - threads close stream which closes fd as well
////    close(fderr[0]);
////    assert(!(*(int *)out_status));
////    assert(!(*(int *)err_status));
//    bool signalled = false;
//    int exit_code = -1;
//
//    if (WIFEXITED(status)) {
//        exit_code = WEXITSTATUS(status);
//    } else {
//        signalled = true;
//    }
//
//    if (processing_command) {
//        assert(end_msgs_cnt < MAX_N_TASKS);
//        assert(!sem_wait(&emc_m));
//        struct EndMsg *end_msg = &end_msgs[end_msgs_cnt++];
//        assert(!sem_post(&emc_m));
//        end_msg->task_id = task_id;
//        end_msg->signalled = signalled;
//        end_msg->exit_code = exit_code;
//        end_msg->controller = task->controller;
//    } else {
////        int sval;
////        assert(!sem_getvalue(&print_mutex, &sval));
////        fprintf(stderr, "printing end info, print_mtx val: %d\n", sval);
//        assert(!sem_wait(&print_mutex));
//        if (signalled) {
//            printf("Task %d ended: signalled.\n", task_id);
//        } else {
//            printf("Task %d ended: status %d.\n", task_id, exit_code);
//        }
//        assert(!sem_post(&print_mutex));
//    }
//    assert(!sem_wait(&fpc_mutex));
//    finished_proc_cnt--;
//    if (finished_proc_cnt == 0) {
//        assert(!sem_post(&pc_mutex)); // unables main thread to change state
//    }
//    assert(!sem_post(&fpc_mutex));
 */
}

void init_tasks()
{
    for (int i = 0; i < MAX_N_TASKS; ++i) {
        struct Task *task = &tasks[i];
        task->task_id = i;
        task->is_running = false;
        task->was_joined = false;
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

struct Task *new_task(char **args)
{
    struct Task* new = &tasks[new_task_id++];
    new->is_running = true;
    new->run_args = args;
    return new;
}

void print_line(sem_t *mutex, task_id_t task_id, const char *line, const char *line_src_strm)
{
    assert(!sem_wait(mutex));
    printf("Task %d %s: '%s'.\n", task_id, line_src_strm, line);
    assert(!sem_post(mutex));
}

void print_overdue_end_infos() {
    // in the possesion of pc_mutex
    assert(!sem_wait(&print_mutex));
    for (int i = 0; i < end_msgs_cnt; ++i) {
        struct EndMsg end_msg = end_msgs[i];
        // ensure that after printing the end message the controller thread is gone
        // which means that all the threads/processes associated with this task are gone
        tasks[end_msg.task_id].was_joined = true;
        if (end_msg.signalled) {
            printf("Task %d ended: signalled.\n", end_msg.task_id);
        } else {
            printf("Task %d ended: status %d.\n", end_msg.task_id, end_msg.exit_code);
        }
    }
    end_msgs_cnt = 0;
    assert(!sem_post(&print_mutex));
}

void init_global_synch_mechs() {
    assert(!pthread_barrier_init(&task_start_info_br, NULL, 2));
    assert(!sem_init(&print_mutex, 0, 1));
    assert(!sem_init(&pc_mutex, 0, 1));
    assert(!sem_init(&fpc_mutex, 0, 1));
    assert(!sem_init(&end_info_m, 0, 1));
    assert(!sem_init(&emc_m, 0, 1));
}

void *handle_ends(void *arg)
{
    siginfo_t info;
    sigset_t signals_to_wait_for;
    sigemptyset(&signals_to_wait_for);
    sigaddset(&signals_to_wait_for, SIGINT);
    sigaddset(&signals_to_wait_for, SIGCHLD);
    for (;;) {
        if (sigwaitinfo(&signals_to_wait_for, &info) == -1) fatal("sigwait");
        fprintf(stderr, "received signal\n");
        assert(!sem_wait(&pc_mutex));
        while (1) {
            printf("Parent: got signal >>%s<< from %d\n", strsignal(info.si_signo), info.si_pid);
            int status, es;
            waitpid(info.si_pid, &status, WNOHANG);
            if (WIFEXITED(status)) {
                es = WEXITSTATUS(status);
                printf("Task ended: status %d.\n", es);
            } else {
                printf("Task ended: signalled.\n");
            }
            sigset_t pending_mask;
            sigpending(&pending_mask);
            if (!sigismember(&pending_mask, SIGCHLD))
                break;
            if (sigwaitinfo(&signals_to_wait_for, &info) == -1) fatal("sigwait");
        }
        assert(!sem_post(&pc_mutex));
    }
}

int main(void)
{
//    exit(1);
//fatal("");
//    printf("xxx");
    init_tasks();
    init_global_synch_mechs();
    pthread_t handler;
    sigset_t set;
    int s;
    /* Block SIGQUIT and SIGUSR1; other threads created by main()
        will inherit a copy of the signal mask. */
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    s = pthread_sigmask(SIG_BLOCK, &set, NULL);
    if (s != 0)
        fatal("pthread_sigmask");
    s = pthread_create(&handler, NULL, handle_ends, (void *) &set);
    if (s != 0)
        fatal("pthread_create");
    char input[511];
    bool broke = false;
    while (read_line(input, 511, stdin, true))
    {
        assert(!sem_wait(&pc_mutex)); // begin processing command
        fprintf(stderr, "begin proc comm\n");
        char **args = split_string(input);
        assert(*args);
        
        if (!strcmp(*args, "quit")) {
            broke = true;
            break;
        }
        if (!strcmp(*args, "run")) {
            assert(args[1]);
            struct Task *task = new_task(args);
            run(task->task_id);
        } else {
            if (!strcmp(*args, "out") || !strcmp(*args, "err")) {
                assert(args[1]);
                task_id_t task_id = (task_id_t) strtol(args[1], NULL, 10);

                if (task_id >= new_task_id) {
                    fprintf(stderr, "task %d has not been run!\n", task_id);
                } else {
                    struct Task *task = &tasks[task_id];
//                assert(!sem_wait(&print_mutex));
                    if (!strcmp(*args, "out"))
                        print_line(&task->stdout_mutex, task->task_id, task->stdout_buff, "stdout");
                    else
                        print_line(&task->stderr_mutex, task->task_id, task->stderr_buff, "stderr");
//                assert(!sem_post(&print_mutex));
                }
            } else if (!strcmp(*args, "sleep")) {
                assert(args[1]);
                long long nap_time = strtoll(args[1], NULL, 10);
//            fprintf(stderr, "taking a nap... zZzz\n");
                usleep(nap_time * 1000);
//            fprintf(stderr, "woke up :D armed and ready!\n");
            } else if (!strcmp(*args, "kill")) {
                assert(args[1]);
                task_id_t task_id = (task_id_t) strtol(args[1], NULL, 10);

                if (task_id >= new_task_id) {
                    fprintf(stderr, "task %hu has not been run!\n", task_id);
                } else {
                    kill(tasks[task_id].pid, SIGINT);
                }
//            continue; - we don't know if the process will exit after sending SIGINT
            } else if (strcmp(*args, "\n") != 0) {
                fprintf(stderr, "Wrong command\n"); // TODO break
            }
            free_split_string(args);
        }
//        print_overdue_end_infos();    // print infos about processes that finished during command processing
        fprintf(stderr, "finish proc comm\n");
        assert(!sem_post(&pc_mutex));
    }
    if (!broke) { // read EOF
        assert(!sem_wait(&pc_mutex));
        processing_command = true;
        assert(!sem_post(&pc_mutex));
    } // else - quit
    // assert(!sem_wait(&print_mutex)); - deadlock with run
    kill_tasks();
    // all tasks were killed and all threads finished normally
//    print_overdue_end_infos(); // - print or not to print after quit - to choose
    assert(!sem_destroy(&print_mutex));
    assert(!sem_destroy(&pc_mutex));
    assert(!sem_destroy(&fpc_mutex));
    assert(!sem_destroy(&end_info_m));
    assert(!sem_destroy(&emc_m));
    assert(!pthread_barrier_destroy(&task_start_info_br));
    free_tasks();
    return 0;
}

void kill_tasks()
{
    for (int i = 0; i < new_task_id; ++i) {
        struct Task *task = &tasks[i];
        kill(task->pid, SIGKILL);
        // "All the threads in your process will be terminated when you return from main()."
//        if (!task->was_joined);
    }
}