#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <regex.h>

#define MAX_BUFFER_SIZE 1024
#define MAX_ARGS 128

int find_pipe(char **args);
void execute_pipeline(char **args, int in, int out, int err);
void handle_redirection(char *args[], int *p_in, int *p_out, int *p_append, int *p_err);
void execute_command(char *args[], const int *p_in, const int *p_out, const int *p_append, const int *p_err);
void print_help();
void parse_command(char *line, char **args);

int orig_stdin;
int orig_stdout;
int orig_stderr;

int main() {
    char buffer[MAX_BUFFER_SIZE];
    int in = STDIN_FILENO, out = STDOUT_FILENO, err = STDERR_FILENO, append = 0;
    orig_stdin = dup(STDIN_FILENO);
    orig_stdout = dup(STDOUT_FILENO);
    orig_stderr = dup(STDERR_FILENO);

    while (1) {
        printf("zzsh>> ");
        if (fgets(buffer, MAX_BUFFER_SIZE, stdin) == NULL) {
            break;
        }
        if (strcmp(buffer, "exit\n") == 0) {
            printf("Bye!:)\n");
            break;
        }
        if (strcmp(buffer, "help\n") == 0) {
            print_help();
            continue;
        }
        int buffer_len = strlen(buffer);
        if (buffer_len > 0 && buffer[buffer_len - 1] == '\n') {
            buffer[buffer_len - 1] = '\0';
        }
        char *args[MAX_ARGS];
        parse_command(buffer, args); // e.g.: ls -l | wc -l -> args = {"ls", "-l", "|", "wc", "-l", NULL}
        handle_redirection(args, &in, &out, &append, &err);
        if (args[0] == NULL) {
            continue;
        }
        execute_pipeline(args, in, out, err);
    }
    return 0;
}

void parse_command(char *line, char **args) { // parse command line into arguments
    int argc = 0;
    char *token, *save_ptr;
    token = strtok_r(line, " ", &save_ptr);
    while (token != NULL) {
        args[argc++] = token;
        token = strtok_r(NULL, " ", &save_ptr);
    }
    args[argc] = NULL;
}

int find_pipe(char **args) { // find pipe symbol in args
    int i = 0;
    while (args[i] != NULL) {
        if (strcmp(args[i], "|") == 0) {
            return i;
        }
        i++;
    }
    return -1;
}

void execute_pipeline(char **args, int in, int out, int err) { // from left to right execute commands
    int pipe_loc = find_pipe(args);

    if (pipe_loc == -1) {
        int append = 0;
        execute_command(args, &in, &out, &append, &err);
    } else {
        args[pipe_loc] = NULL;
        int pipe_fd[2];
        if (pipe(pipe_fd) < 0) {
            perror("pipe");
            exit(1);
        }
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        } else if (pid == 0) {
            close(pipe_fd[0]);
            execute_pipeline(args, in, pipe_fd[1], err);
            close(pipe_fd[1]);
            exit(0);
        } else {
            close(pipe_fd[1]);
            execute_pipeline(args + pipe_loc + 1, pipe_fd[0], out, err);
            close(pipe_fd[0]);
            waitpid(pid, NULL, 0);
        }
    }
    if (in != STDIN_FILENO) dup2(orig_stdin, STDIN_FILENO);
    if (out != STDOUT_FILENO) dup2(orig_stdout, STDOUT_FILENO);
    if (err != STDERR_FILENO) dup2(orig_stderr, STDERR_FILENO);
}

void handle_redirection(char *args[], int *p_in, int *p_out, int *p_append, int *p_err) {
    int in = *p_in, out = *p_out, append = *p_append, err = *p_err;
    int argc = 0, n, n_append;
    while (args[argc] != NULL) {
        if (strcmp(args[argc], "<") == 0) { // input redirection
            in = open(args[argc + 1], O_RDONLY);
            if (in < 0) {
                perror("open");
                exit(1);
            }
            args[argc] = NULL;
            argc++;
        } else if (strcmp(args[argc], ">") == 0) { // output redirection
            out = open(args[argc + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out < 0) {
                perror("open");
                exit(1);
            }
            args[argc] = NULL;
            argc++;
        } else if (strcmp(args[argc], ">>") == 0) { // output redirection with append
            out = open(args[argc + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (out < 0) {
                perror("open");
                exit(1);
            }
            args[argc] = NULL;
            argc++;
            append = 1;
        } else {
            regex_t regex1, regex2;
            regcomp(&regex1, "^[0-9]+>$", REG_EXTENDED);
            regcomp(&regex2, "^[0-9]+>>$", REG_EXTENDED);
            if (regexec(&regex1, args[argc], 0, NULL, 0) == 0) { // n> file
                sscanf(args[argc], "%d>", &n);
                int fd = open(args[argc + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (dup2(fd, n) < 0) {
                    perror("dup2");
                    exit(1);
                }
                close(fd); // Close the file descriptor after dup2
                args[argc] = NULL;
                args[argc + 1] = NULL;
                argc += 2;
            } else if (regexec(&regex2, args[argc], 0, NULL, 0) == 0) { // n>> file
                sscanf(args[argc], "%d>>", &n);
                int fd = open(args[argc + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (dup2(fd, n) < 0) {
                    perror("dup2");
                    exit(1);
                }
                close(fd); // Close the file descriptor after dup2
                args[argc] = NULL;
                args[argc + 1] = NULL;
                argc += 2;
            }
            else {
                argc++; // next argument
            }
            regfree(&regex1);
            regfree(&regex2);
        }
    }
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
    } else if (pid == 0) { // Child
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

        // Restore stdin, stdout, stderr
        dup2(orig_stdin, STDIN_FILENO);
        dup2(orig_stdout, STDOUT_FILENO);
        dup2(orig_stderr, STDERR_FILENO);
        waitpid(pid, NULL, 0); // Wait for child process
    }
}



void print_help() {
    printf("zzsh - A simple shell implementation\n");
    printf("Usage: command [arguments]\n");
    printf("Built-in commands:\n");
    printf("  help          - Display this help message\n");
    printf("  exit          - Exit the shell\n");
    printf("Redirections and pipes:\n");
    printf("  command > file   - Redirect command output to file\n");
    printf("  command >> file  - Append command output to file\n");
    printf("  command < file   - Redirect input from file to command\n");
    printf("  command n> file  - Redirect command output to file descriptor n\n");
    printf("  command n>> file - Append command output to file descriptor n\n");
    printf("  command1 | command2 - Pipe output of command1 to input of command2\n");
}
