#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_BUFFER_SIZE 1024
#define MAX_ARGS 128

int find_pipe(char **args);
void execute_pipeline(char **args, int in, int out, int err);
void handle_redirection(char *args[], int *p_in, int *p_out, int *p_append, int *p_err);
void execute_command(char *args[], const int *p_in, const int *p_out, const int *p_append, const int *p_err);
void print_help();
void parse_command(char *line, char **args);

int main() {
    // Read command buffer
    char buffer[MAX_BUFFER_SIZE];
    int in = STDIN_FILENO; // input
    int out = STDOUT_FILENO; // output
    int append = 0; // 0: not append, 1: append
    int err = STDERR_FILENO; // error
    while (1) {
        printf("zzsh>> ");
        if (fgets(buffer, MAX_BUFFER_SIZE, stdin) == NULL) { // EOF
            break;
        }
        if (strcmp(buffer, "exit\n") == 0) { // exit
            printf("Bye!:)\n");
            break;
        }
        if (strcmp(buffer, "help\n") == 0) { // help
            print_help();
            continue;
        }
        int buffer_len = strlen(buffer);
        if (buffer_len > 0 && buffer[buffer_len - 1] == '\n') { // remove '\n'
            buffer[buffer_len - 1] = '\0';
        }
        char *args[MAX_ARGS];
        parse_command(buffer, args); // e.g: ls -l | less -> args = ["ls", "-l", "|", "less", NULL]
        handle_redirection(args, &in, &out, &append, &err); // handle redirection
        if (args[0] == NULL) { // empty command
            continue;
        }
        execute_pipeline(args, in, out, err);
    }
    return 0;
}

void parse_command(char *line, char **args) {
    int argc = 0;
    char *token;
    char *save_ptr;
    token = strtok_r(line, " ", &save_ptr);
    while (token != NULL) {
        args[argc++] = token;
        token = strtok_r(NULL, " ", &save_ptr);
    }
    args[argc] = NULL;
}

int find_pipe(char **args) {
    int i = 0;
    while (args[i] != NULL) {
        if (strcmp(args[i], "|") == 0) {
            return i;
        }
        i++;
    }
    return -1;
}

void execute_pipeline(char **args, int in, int out, int err) {
    int pipe_loc = find_pipe(args);

    if (pipe_loc == -1) {
        // 没有管道，直接执行命令
        int append = 0;
        execute_command(args, &in, &out, &append, &err);
    } else {
        args[pipe_loc] = NULL; // split args at pipe_loc
        // Create a pipe
        int pipe_fd[2];
        if (pipe(pipe_fd) < 0) {
            perror("pipe");
            exit(1);
        }

        pid_t pid = fork(); // Create child process for the left-side command
        if (pid < 0) {
            perror("fork");
            exit(1);
        } else if (pid == 0) { // Child
            close(pipe_fd[0]); // Close pipe_out
            execute_pipeline(args, in, pipe_fd[1], err);
            close(pipe_fd[1]); // Close pipe_in
            exit(0);
        } else { // Parent
            close(pipe_fd[1]); // Close pipe_in
            execute_pipeline(args + pipe_loc + 1, pipe_fd[0], out, err);
            close(pipe_fd[0]); // Close pipe_out
            waitpid(pid, NULL, 0); // Wait for child process
        }
    }
}
void handle_redirection(char *args[], int *p_in, int *p_out, int *p_append, int *p_err) {
    int in = *p_in;
    int out = *p_out;
    int append = *p_append;
    int err = *p_err;
    int argc = 0; // argument count
    while (args[argc] != NULL) { // check all arguments ?(<>|)
        if (strcmp(args[argc], "<") == 0) { // <
            in = open(args[argc + 1], O_RDONLY);
            if (in < 0) {
                perror("open");
                exit(1);
            }
            args[argc] = NULL;
            argc++;
        } else if (strcmp(args[argc], ">") == 0) { // >
            out = open(args[argc + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644); // rw-r--r--
            if (out < 0) {
                perror("open");
                exit(1);
            }
            args[argc] = NULL;
            argc++;
        } else if (strcmp(args[argc], ">>") == 0) { // >>
            out = open(args[argc + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (out < 0) {
                perror("open");
                exit(1);
            }
            args[argc] = NULL;
            argc++;
            append = 1;
        } else if (strcmp(args[argc], "2>") == 0) { // 2>
            err = open(args[argc + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (err < 0) {
                perror("open");
                exit(1);
            }
            args[argc] = NULL;
            argc++;
        } else {
            argc++; // next argument
        }
    }
// update in, out, append, err
    *p_in = in;
    *p_out = out;
    *p_append = append;
    *p_err = err;
}

void execute_command(char *args[], const int *p_in, const int *p_out, const int *p_append, const int *p_err) {
    int in = *p_in;
    int out = *p_out;
    int append = *p_append;
    int err = *p_err;
    pid_t pid = fork(); // create child process
    if (pid < 0) {
        perror("fork");
        exit(1);
    } else if (pid== 0) { // Child
        if (in != STDIN_FILENO) { // Redirect input
            dup2(in, STDIN_FILENO);
            close(in);
        }
        if (out != STDOUT_FILENO) { // Redirect output
            dup2(out, STDOUT_FILENO);
            close(out);
        }
        if (err != STDERR_FILENO) { // Redirect error
            dup2(err, STDERR_FILENO);
            close(err);
        }
        if (append) { // Append
            dup2(out, STDERR_FILENO);
        }
        if (execvp(args[0], args) < 0) { // Execute command
            perror("execvp");
            exit(1);
        }
    } else { // Parent
        if (in != STDIN_FILENO) { // Close input
            close(in);
        }
        if (out != STDOUT_FILENO) { // Close output
            close(out);
        }
        if (err != STDERR_FILENO) { // Close error
            close(err);
        }
        waitpid(pid, NULL, 0); // Wait for child process
    }
}

void print_help() {
    printf("Usage: zzsh [OPTION] [FILE] [ARG] [<>|] ...\n");
    printf("A simple shell that supports basic file redirection and piping.\n");
    printf("\nRedirection:\n");
    printf(" <\tRead input from a file.\n");
    printf(" >\tWrite output to a file (overwrite).\n");
    printf(" >>\tWrite output to a file (append).\n");
    printf(" 2>\tWrite error output to a file.\n");
    printf("\nPiping:\n");
    printf(" |\tPipe the output of one command as input to another command.\n");
}


