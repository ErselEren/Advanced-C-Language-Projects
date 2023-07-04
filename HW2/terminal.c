#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>

#define MAX_COMMAND_LEN 256
#define MAX_ARGS 20

void handle_sigchld(int signum, siginfo_t *siginfo, void *context) { // handle SIGCHLD
    pid_t pid;
    int status;
    pid = waitpid(siginfo->si_pid, &status, WNOHANG);
    if (pid > 0) {
        if (WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL) {
            printf("Child process %d was killed with SIGKILL.\n", pid);
            kill(getpid(), SIGTERM);
        }
    }
}



void trim(char* str) { // trim whitespace from the beginning and end of a string
    int start = 0, end = strlen(str) - 1;
    
    while (isspace(str[start])) {
        start++;
    }
    
    while (isspace(str[end])) {
        end--;
    }
    
    for (int i = start; i <= end; i++) {
        str[i - start] = str[i];
    }
    
    str[end - start + 1] = '\0';
}


int main() {
    
    //ignore signals
    struct sigaction act;
    act.sa_handler = SIG_IGN;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    //handle sigchld
    struct sigaction sa;
    sa.sa_sigaction = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;

    //register sigchld
    if(sigaction(SIGCHLD, &sa, NULL) == -1){
        perror("sigaction");
        return 1;
    }

    //register sigint
    if (sigaction(SIGINT, &act, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    //
    if (sigaction(SIGTERM, &act, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

   
    char command[MAX_COMMAND_LEN]; // stores the command entered by the user
    char* commands[MAX_ARGS]; // stores the commands after parsing
    int number_of_commands = 0; // stores the number of commands entered by the user
    

    while(1){
        
        //--------------- GET INPUT FROM USER AND PARSE IT ------------------
        printf("\nEnter a command: "); 
        fgets(command, MAX_COMMAND_LEN, stdin); // get input from user
        
        //if input is :q then exit
        if (strcmp(command, ":q\n") == 0) { // if user enters :q, exit
            exit(EXIT_SUCCESS);
            return 0;
        }

        // parse the command
        commands[number_of_commands] = strtok(command, "|"); 
        while (commands[number_of_commands] != NULL && number_of_commands < MAX_ARGS) { 
            number_of_commands++;
            commands[number_of_commands] = strtok(NULL, "|");
        }
        for (int j = 0; j < number_of_commands; j++) {
            trim(commands[j]); // trim whitespace from the beginning and end of each command
        }
        
        //print number of commands
        printf("Number of commands: %d \n", number_of_commands);
        //---------------- Create Pipes ----------------
        int pipefds[number_of_commands - 1][2]; // stores the file descriptors for the pipes

        for (int i = 0; i < number_of_commands - 1; i++) {
            pipe(pipefds[i]);
        }
        //----------------------------------------------

        pid_t children[number_of_commands];
        int status;
            // Loop through each command and execute it in a child process
            for (int i = 0; i < number_of_commands; i++) {
                
                children[i] = fork(); //
                
                int number_of_args = 0;
                char* args[MAX_ARGS]; // stores the arguments of the command
                args[number_of_args] = strtok(commands[i], " "); // parse the command
                
                while (args[number_of_args] != NULL && number_of_args < MAX_ARGS) { // store the arguments in the args array
                    number_of_args++;
                    args[number_of_args] = strtok(NULL, " ");
                }
                args[number_of_args] = NULL;
                
                
                if (children[i] == -1) { // fork failed
                    perror("fork");
                    exit(EXIT_FAILURE);
                }

                else if (children[i] == 0) { // child process                 
                    for (int j = 0; j < number_of_commands - 1; j++) { // close unnecessary pipes
                            if(j==i-1){
                                close(pipefds[j][1]);
                            }
                            else if(j==i){
                                close(pipefds[j][0]);
                            }
                            else{
                                close(pipefds[j][0]);
                                close(pipefds[j][1]);
                            }
                        }
                    if(number_of_commands>1){
                        if(i==0){ // first command
                            dup2(pipefds[0][1], STDOUT_FILENO);
                            close(pipefds[0][1]);   
                        }
                        else if(i==number_of_commands-1){ // last command
                            dup2(pipefds[i-1][0], STDIN_FILENO);
                            close(pipefds[i-1][0]);
                        }
                        else{  // middle command            
                            dup2(pipefds[i-1][0], STDIN_FILENO);
                            dup2(pipefds[i][1], STDOUT_FILENO);
                            close(pipefds[i-1][0]);
                            close(pipefds[i][1]);
                        }
                    }

                    // check if there is a '<' or '>' symbol in the command
                    char* input_file = NULL;
                    char* output_file = NULL;
                    for (int i = 1; args[i] != NULL; i++) {
                        if (strcmp(args[i], "<") == 0) 
                            input_file = args[i+1];
                        else if (strcmp(args[i], ">") == 0) 
                            output_file = args[i+1];                              
                    }
                            
                    for (int i = 1; args[i] != NULL; i++) {
                        // find first '<' or '>' symbol
                        if (strcmp(args[i], "<") == 0 || strcmp(args[i], ">") == 0) {
                            // remove '<' or '>' and everything after it from args array
                            args[i] = NULL;
                            break;
                        }
                    }
                                

                        // redirect input to the input file
                        if (input_file != NULL) {
                            int input_fd = open(input_file, O_RDONLY);
                            if (input_fd == -1) {
                                perror("open");
                                exit(EXIT_FAILURE);
                            }
                            dup2(input_fd, STDIN_FILENO);
                            close(input_fd);
                        }

                        // redirect output to the output file
                        if (output_file != NULL) {
                            int output_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                            if (output_fd == -1) {
                                perror("open");
                                exit(EXIT_FAILURE);
                            }
                            dup2(output_fd, STDOUT_FILENO);
                            close(output_fd);
                        }

                        
                        
                        char filename[100];
                        time_t now = time(NULL);
                        struct tm *tm = localtime(&now);
                        sprintf(filename, "%02d-%02d-%04d-%02d-%02d-%02d_child_%d.log",
                            tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900,
                            tm->tm_hour, tm->tm_min, tm->tm_sec, i);

                        FILE* log_file = fopen(filename, "w");
                        if(log_file == NULL) {
                            fprintf(stderr, "Failed to create log file %s\n", filename);
                            exit(1);
                        }

                        fprintf(log_file, "Child process %d pid: %d\n", i, getpid());

                        // Write the executed command to the log file
                        fprintf(log_file, "Executed command: ");
                        for (int j = 0; args[j] != NULL; j++) {
                            fprintf(log_file, "%s ", args[j]);
                        }

                        // Log input and output files if they exist
                        if (input_file != NULL) {
                            fprintf(log_file, "< %s ", input_file);
                        }
                        if (output_file != NULL) {
                            fprintf(log_file, "> %s ", output_file);
                        }
                        fprintf(log_file, "\n");

                        fclose(log_file);
                        
                        if (execvp(args[0], args) == -1) {
                            perror("execvp failed : ");
                        }
                        exit(0);
                                
                }   
            }

            // Close pipes in the parent process
            for (int i = 0; i < number_of_commands - 1; i++) {
                close(pipefds[i][0]);
                close(pipefds[i][1]);
            }

            // Wait for all child processes to complete
            for (int i = 0; i < number_of_commands; i++) {
                
                waitpid(children[i], &status, 0);

                // check if child process exited normally
                if (WIFEXITED(status)) {
                    printf("Child process exited with status %d\n", WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    printf("Child process terminated due to unhandled signal %d\n", WTERMSIG(status));
                }
            }

            // Reset the number_of_commands for the next iteration
            number_of_commands = 0;

    }

    return 0;
}
