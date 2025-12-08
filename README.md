# Computer-Operating-Systems-Project-1
Files found in master branch


# Report for my Shell Desgin

  My shell is designed with the specified tasks in mind like signal handling, process management, termination handling, and I/O manipulation. Within the shell's main function, the logic is based on Read-Evaluate-Print loop or REPL method. Input is read using fgets() after displaying a dynamic prompt showing the current working directory using getcwd(). For tokenization and subsittution, the function handles parsing the input string into an array of arguments and performs environment variable substitution before execution of commands. For classification of commands there's piping, buil-in's, and external execution that my shell checks for in sequence. If a pipe is detected, the command is handled by dedicated multi-process logic. Next, if it's not a pipe, it checks for internal commands. And then if it's neither, a new process is forked to execute an external program. External commands are executed using fork() and execvp(). The parent process uses waitpid() to manage child execution, either in the foreground or the background. For error handling, built-in errors use perror() or they use fprintf(stderr, ...etc.) message. External command failures like command not found will lead to the child process calling perror("execvp failed") and exiting with a non-zero status, so the shell doesn't crash.
    F
