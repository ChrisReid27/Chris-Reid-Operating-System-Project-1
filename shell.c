// Christopher Reid
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define MAX_COMMAND_LINE_LEN 1024
#define MAX_COMMAND_LINE_ARGS 128

char prompt[] = "> ";
char delimiters[] = " \t\r\n";
extern char **environ;

// Global to track the foreground child PID for Task 5
volatile pid_t fg_pid = -1;

// Task 4: Preventing shell ctrl-c (SIGINT) termination.
void sigint_handler(int sig) {
    printf("\n");
    fflush(stdout);
}

// Task 5: 10s timer to terminate foreground process when < 10s
void sigalrm_handler(int sig) {
    if (fg_pid > 0) {
        // Alarm went off, terminate the foreground process
        if (kill(fg_pid, SIGKILL) == 0) {
            fprintf(stderr, "\nProcess %d exceeded 10 second limit and was terminated.\n", fg_pid);
        } else {
            // Check if the process already finished (ESRCH means no such process)
            if (errno != ESRCH) {
                perror("kill failed");
            }
        }
    }
    fg_pid = -1; // Reset tracker
    // waitpid() in main will be interrupted and return -1 with errno=EINTR
}

int tokenize_and_substitute(char *command_line, char *arguments[]) {
    char *token = strtok(command_line, delimiters);
    int arg_count = 0;
    
    while (token != NULL && arg_count < MAX_COMMAND_LINE_ARGS - 1) {
        if (token[0] == '$') {
            // Variable substitution: token is like "$HOME"
            char *var_value = getenv(token + 1); // token + 1 skips the '$'
            if (var_value != NULL) {
                arguments[arg_count] = var_value;
            } else {
                // If variable not found, treat it as an empty string
                arguments[arg_count] = "";
            }
        } else {
            arguments[arg_count] = token;
        }
        arg_count++;
        token = strtok(NULL, delimiters);
    }
    arguments[arg_count] = NULL;
    return arg_count;
}

int main() {
    // Stores the string typed into the command line.
    char command_line[MAX_COMMAND_LINE_LEN];
    char cmd_bak[MAX_COMMAND_LINE_LEN];
  
    // Stores the tokenized command line input.
    char *arguments[MAX_COMMAND_LINE_ARGS];
    
    // Task 4: Register the SIGINT handler once
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        perror("signal SIGINT failed");
    }

    while (true) {
        // Task 1: Input loop and prompt 
        do{ 
            // Print the shell prompt.
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                char *last_slash = strrchr(cwd, '/');
                if (last_slash != NULL && *(last_slash + 1) != '\0') {
                    printf("%s%s", last_slash + 1, prompt); 
                } else if (strcmp(cwd, "/") == 0) {
                     printf("/%s", prompt); // Root directory special case
                } else {
                    printf("%s%s", cwd, prompt); // Fallback to full path
                }
            } else {
                perror("getcwd() error");
                printf("%s", prompt); // Fallback prompt
            }
            fflush(stdout);
        
            if ((fgets(command_line, MAX_COMMAND_LINE_LEN, stdin) == NULL) && ferror(stdin)) {
                fprintf(stderr, "fgets error");
                exit(0);
            }
 
        }while(command_line[0] == 0x0A);  // while just ENTER pressed
        command_line[strlen(command_line) - 1] = '\0';
      
        // If the user input was EOF (ctrl+d), exit the shell.
        if (feof(stdin)) {
            printf("\n");
            return 0;
        }

        // Remove newline character
        command_line[strcspn(command_line, "\n")] = 0;
        
        // A copy made for backup
        strcpy(cmd_bak, command_line); 

        // Task 1 Cont.: Tokenization and variable substitution
        int arg_count = tokenize_and_substitute(command_line, arguments);

        if (arg_count == 0 || arguments[0][0] == '\0') {
            continue; // Handle empty command or command that was only non-existent variable
        }
        
        // Task 6: I/O redirection parsing
        char *input_file = NULL;
        char *output_file = NULL;
        int exec_arg_count = arg_count;

        for (int i = 0; arguments[i] != NULL; i++) {
            if (strcmp(arguments[i], "<") == 0) {
                if (arguments[i+1] != NULL) {
                    input_file = arguments[i+1];
                    arguments[i] = NULL; // Terminate arguments before '<'
                    exec_arg_count = i;
                    i = MAX_COMMAND_LINE_ARGS; // Break outer loop
                }
            } else if (strcmp(arguments[i], ">") == 0) {
                if (arguments[i+1] != NULL) {
                    output_file = arguments[i+1];
                    arguments[i] = NULL; // Terminate arguments before '>'
                    exec_arg_count = i;
                    i = MAX_COMMAND_LINE_ARGS; // Break outer loop
                }
            }
        }
        
        // Task 6: Pipe Parsing
        // Initialize pipe-related arrays
        char *cmd1_args[MAX_COMMAND_LINE_ARGS] = {NULL};
        char *cmd2_args[MAX_COMMAND_LINE_ARGS] = {NULL};
        int pipe_pos = -1;

        // Find the position of the pipe symbol
        for (int i = 0; arguments[i] != NULL; i++) {
            if (strcmp(arguments[i], "|") == 0) {
                pipe_pos = i;
                break;
            }
        }

        if (pipe_pos != -1) {
            // Pipe found. Command is NOT a built-in.
            
            // Copy arguments for command 1
            for (int i = 0; i < pipe_pos; i++) {
                cmd1_args[i] = arguments[i];
            }
            cmd1_args[pipe_pos] = NULL;
            
            // Copy arguments for command 2
            int cmd2_count = 0;
            for (int i = pipe_pos + 1; arguments[i] != NULL; i++) {
                cmd2_args[cmd2_count++] = arguments[i];
            }
            cmd2_args[cmd2_count] = NULL;

            // Check if both commands are valid
            if (cmd1_args[0] == NULL || cmd2_args[0] == NULL) {
                fprintf(stderr, "quash: Pipe requires two valid commands.\n");
                continue;
            }
            
            // Pipe execution logic (replacing the built-in checks for piping)
            int fd[2]; // fd[0] is read end, fd[1] is write end
            pid_t pid1, pid2;

            if (pipe(fd) == -1) {
                perror("pipe failed");
                continue;
            }

            // Fork the first command (writer)
            pid1 = fork();

            if (pid1 < 0) {
                perror("fork cmd1 failed");
            } else if (pid1 == 0) {
                // CHILD 1 (Writer: cmd1 | ...)
                signal(SIGINT, SIG_DFL);
                signal(SIGALRM, SIG_DFL);

                // Close the read end, redirect stdout to the write end
                close(fd[0]);
                if (dup2(fd[1], STDOUT_FILENO) < 0) {
                    perror("dup2 cmd1 failed");
                    exit(1);
                }
                close(fd[1]); // Close original write descriptor

                execvp(cmd1_args[0], cmd1_args);
                perror("execvp cmd1 failed");
                exit(1);
            }

            // Fork the second command (reader)
            pid2 = fork();

            if (pid2 < 0) {
                perror("fork cmd2 failed");
            } else if (pid2 == 0) {
                // CHILD 2 (... | cmd2)
                signal(SIGINT, SIG_DFL);
                signal(SIGALRM, SIG_DFL);
                
                // Close the write end, redirect stdin to the read end
                close(fd[1]);
                if (dup2(fd[0], STDIN_FILENO) < 0) {
                    perror("dup2 cmd2 failed");
                    exit(1);
                }
                close(fd[0]); // Close original read descriptor

                execvp(cmd2_args[0], cmd2_args);
                perror("execvp cmd2 failed");
                exit(1);
            }

            // PARENT (Shell)
            
            // Close both ends of the pipe in the parent
            close(fd[0]);
            close(fd[1]);
            
            // Parent must wait for BOTH children to finish
            int status;
            if (waitpid(pid1, &status, 0) < 0) perror("waitpid cmd1 failed");
            if (waitpid(pid2, &status, 0) < 0) perror("waitpid cmd2 failed");

            // Skip the rest of the main loop (built-in and single command execution)
            continue;
        }

        // Task 1: Built-In Commands
        // cd
        if (strcmp(arguments[0], "cd") == 0) {
            char *dir_to_change = arguments[1] != NULL ? arguments[1] : getenv("HOME");
            if (dir_to_change == NULL) dir_to_change = "/";
            if (chdir(dir_to_change) != 0) {
                perror("quash: cd");
            }
        }
        // pwd
        else if (strcmp(arguments[0], "pwd") == 0) {
            char cwd_print[1024];
            if (getcwd(cwd_print, sizeof(cwd_print)) != NULL) {
                printf("%s\n", cwd_print);
            } else {
                perror("quash: pwd");
            }
        }
        // echo
        else if (strcmp(arguments[0], "echo") == 0) {
            for (int i = 1; arguments[i] != NULL && arguments[i][0] != 0; i++) {
                printf("%s%s", arguments[i], (arguments[i+1] != NULL ? " " : ""));
            }
            printf("\n");
        }
        // exit
        else if (strcmp(arguments[0], "exit") == 0 || strcmp(arguments[0], "quit") == 0) {
            return 0;
        }
        // env
        else if (strcmp(arguments[0], "env") == 0) {
            if (arguments[1] != NULL) {
                // If argument provided, print its value
                char *val = getenv(arguments[1]);
                if (val != NULL) {
                    printf("%s\n", val);
                }
            } else {
                // Print all environment variables
                for (char **env_ptr = environ; *env_ptr != NULL; env_ptr++) {
                    printf("%s\n", *env_ptr);
                }
            }
        }
        // setenv
        else if (strcmp(arguments[0], "setenv") == 0) {
            if (arguments[1] != NULL) {
                char *equal_sign = strchr(arguments[1], '=');
                if (equal_sign != NULL) {
                    *equal_sign = '\0'; // Split the string into name and value
                    char *var_name = arguments[1];
                    char *var_value = equal_sign + 1;
                    if (setenv(var_name, var_value, 1) != 0) { // overwrite=1
                        perror("quash: setenv");
                    }
                } else {
                    fprintf(stderr, "quash: setenv usage: setenv VAR=value\n");
                }
            } else {
                // Print current environment if no arguments (like env command)
                 for (char **env_ptr = environ; *env_ptr != NULL; env_ptr++) {
                    printf("%s\n", *env_ptr);
                }
            }
        }

        // Execution of external commands (Tasks 2, 3, 5)
        else {
            // Task 3: Check for background process
            bool is_background = false;
            if (exec_arg_count > 0 && arguments[exec_arg_count - 1] != NULL && strcmp(arguments[exec_arg_count - 1], "&") == 0) {
                is_background = true;
                arguments[exec_arg_count - 1] = NULL; // Remove '&' from arguments list
            }

            // Task 2: Create a child process
            pid_t pid = fork();

            if (pid < 0) {
                perror("fork failed");
            } 
            // CHILD PROCESS
            else if (pid == 0) {
                // Task 4: Reset signal handler for child processes
                signal(SIGINT, SIG_DFL); 
                signal(SIGALRM, SIG_DFL); // Reset timer signal handler

                // Task 6: I/O Redirection Setup
                if (output_file != NULL) {
                    int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd < 0) {
                        perror("quash: open output failed");
                        exit(1);
                    }
                    if (dup2(fd, STDOUT_FILENO) < 0) {
                        perror("quash: dup2 output failed");
                        exit(1);
                    }
                    close(fd);
                }
                if (input_file != NULL) {
                    int fd = open(input_file, O_RDONLY);
                    if (fd < 0) {
                        perror("quash: open input failed");
                        exit(1);
                    }
                    if (dup2(fd, STDIN_FILENO) < 0) {
                        perror("quash: dup2 input failed");
                        exit(1);
                    }
                    close(fd);
                }
                
                // Execute the command
                execvp(arguments[0], arguments);

                // If execvp returns, it failed
                perror("execvp() failed: ");
                // The prompt requires specific error message for task 2
                fprintf(stderr, "An error occurred.\n"); 
                exit(1); 
            } 
            // PARENT PROCESS
            else {
                if (!is_background) {
                    // Task 5: Foreground process timer logic
                    fg_pid = pid; // Track foreground child PID
                    
                    // Register SIGALRM handler
                    if (signal(SIGALRM, sigalrm_handler) == SIG_ERR) {
                        perror("signal SIGALRM failed");
                    }
                    
                    alarm(10); // Set 10s timer
                    
                    int status;
                    
                    // Task 2: Wait for child to complete. Loop if interrupted by signal.
                    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { 
                        // Loop if interrupted by signal
                    }

                    alarm(0); // Cancel the alarm if the child finished before 10s
                    fg_pid = -1; // Reset tracker
                    
                } else {
                    // Task 3: Background process
                    printf("[PID %d] Running in the background.\n", pid);
                }
            }
        }
    }
    // This should never be reached.
    return -1;
}
