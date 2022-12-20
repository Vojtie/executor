#include "commands.h"
#include "utils.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

char buffer[10][1022];
int new_task_id = 0;

void quit();
void out();

void run(struct Command *run_info) {
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
    }
    close(fd[1]);
    dup2(fd[0], STDIN_FILENO);
    while (true) {
        // put read line after acquiring mutex
        bool read_l = read_line(buffer[run_info->task_id], 1022, stdin, false);
        if (!read_l) break;
        printf("task %d: %s\n", run_info->task_id, buffer[run_info->task_id]);
    }
    close(fd[0]);
    printf("goodbye from %d :<\n", run_info->task_id);
}

int main(int argc, char *argv[]) {
    char input[511];
    while (true) {
        assert(read_line(input, 511, stdin, true));
        char **args = split_string(input);
        if (*args == NULL) exit(1);

        int i = 0;
        while (args[i] != NULL) printf("%s", args[i++]);

        if (!strncmp(*args, "quit", 4)) {
            quit();
            return 0;
        } else if (!strncmp(*args, "run", 3)) {
            struct Command *command = malloc(sizeof(struct Command));
            command->task_id = new_task_id++;
            command->task_args = args + 1;
//            free(*args);
            if (!fork()) {
                run(command);
                return 0;
            }
        } else if (!strncmp(*args, "out", 3)) {

        }
    }
}

void quit() {
    exit(0);
}