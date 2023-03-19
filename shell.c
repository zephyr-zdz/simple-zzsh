#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_BUFFER_SIZE 1024
#define MAX_ARGS 128

void handle_redirection(char *args[], int *p_in, int *p_out, int *p_append, int *p_err);
void execute_command(char *args[], const int *p_in, const int *p_out, const int *p_append, const int *p_err);
void print_help();
void parse_command(char *line, char **args);

int main(int argc, char *argv[]) {
    // Give help information
    int opt;
    if ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
            case 'h':
                print_help();
                exit(0);
            default:
                fprintf(stderr, "Usage: %s [-h]\n", argv[0]);
                exit(1);
        }
    }
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
        if (strcmp(buffer, "exit") == 0) { // exit
            printf("Bye!:)\n");
            break;
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
        execute_command(args, &in, &out, &append, &err); // execute command
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
        } else if (strcmp(args[argc], "|") == 0) { // |
            int pipe_fd[2]; // pipe_fd[0]: out, pipe_fd[1]: in
            if (pipe(pipe_fd) < 0) { // create pipe
                perror("pipe");
                exit(1);
            }
            pid_t pid = fork(); // create child process(left command)
            // pipe: child_out -> parent_in
            if (pid < 0) {
                perror("fork");
                exit(1);
            } else if (pid == 0) { // child
                close(pipe_fd[0]); // close pipe_out
                handle_redirection(&args[argc], p_in, &pipe_fd[1], p_append, &err); // handle redirection
                execute_command(&args[argc], p_in, &pipe_fd[1], p_append, &err); // execute command: p_in -> pipe_out --pipe--> parent_in
                exit(0);
            } else { // parent
                close(pipe_fd[1]);// close pipe_in
                in = pipe_fd[0]; // child_out --pipe--> parent_in
                waitpid(pid, NULL, 0); // wait for child process
            }
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
    int background = 0; // is background process
    int argc = 0; // argument count
    while (args[argc] != NULL) { // check all arguments
        if (strcmp(args[argc], "&") == 0) { // & (background process)
            args[argc] = NULL;
            background = 1;
            break;
        } else {
            argc++;
        }
    }
    pid_t pid = fork(); // create child process
    if (pid < 0) {
        perror("fork");
        exit(1);
    } else if (pid == 0) { // child
        if (in != STDIN_FILENO) { // redirect in
            dup2(in, STDIN_FILENO);
            close(in);
        }
        if (out != STDOUT_FILENO) { // redirect out
            dup2(out, STDOUT_FILENO);
            close(out);
        }
        if (err != STDERR_FILENO) { // redirect err
            dup2(err, STDERR_FILENO);
            close(err);
        }
        if (append) { // append
            dup2(out, STDERR_FILENO);
        }
        if (execvp(args[0], args) < 0) { // execute command
            perror("execvp");
            exit(1);
        }
    } else {
        if (in != STDIN_FILENO) { // close in
            close(in);
        }
        if (out != STDOUT_FILENO) {
            close(out);
        }
        if (err != STDERR_FILENO) {
            close(err);
        }
        if (!background) {
            waitpid(pid, NULL, 0);
        }
    }
}

