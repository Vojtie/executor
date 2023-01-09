#include "err.h"
#include "utils.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


void set_close_on_exec(int file_descriptor, bool value)
{
    int flags = fcntl(file_descriptor, F_GETFD);
    if (flags == -1)
        exit(1);
    if (value)
        flags |= FD_CLOEXEC;
    else
        flags &= ~FD_CLOEXEC;
    if (fcntl(file_descriptor, F_SETFD, flags) == -1)
        exit(1);
}

char** split_string(const char* s)
{
    size_t len = strlen(s);
    int spaces = 0;
    for (int i = 0; i < len; ++i)
        if (s[i] == ' ')
            spaces++;
    char** parts = calloc(spaces + 2, sizeof(char*));
    parts[spaces + 1] = NULL;
    int p = 0;
    int b = 0;
    for (int i = 0; i < len; ++i) {
        if (s[i] == ' ') {
            parts[p++] = strndup(s + b, i - b);
            b = i + 1;
        }
    }
    parts[p++] = strndup(s + b, len - b);
    assert(p == spaces + 1);
    return parts;
}

void free_split_string(char** parts)
{
    for (int i = 0; parts[i] != NULL; ++i)
        free(parts[i]);
    free(parts);
}

bool read_line(char* buffer, size_t size_of_buffer, FILE* file, bool trim_newline)
{
    if (size_of_buffer < 2)
        fatal("Buffer too small: %d\n", size_of_buffer);

    char* line = NULL;
    size_t n_bytes;
    ssize_t n_chars = getline(&line, &n_bytes, file);

    if (n_chars == -1) {
        free(line);
        if (ferror(file))
            exit(1);
        assert(feof(file));
        buffer[0] = '\0';
        return false;
    }

    if (n_chars == 0) {
        free(line);
        assert(feof(file));
        buffer[0] = '\0';
        return false;
    }

    size_t len = strlen(line);
    if (len < n_chars)
        fatal("Null character in input.");
    assert(n_chars == len);

    if (len + 1 > size_of_buffer)
        fatal("Line too long: %d > %d.", len, size_of_buffer - 1);

    if (trim_newline && len > 1 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
        len--;
    }
    memcpy(buffer, line, len + 1);

    free(line);

    return true;
}
