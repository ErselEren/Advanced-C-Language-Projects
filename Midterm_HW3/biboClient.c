#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <semaphore.h>  // Include the semaphore.h header file
#include <signal.h>
#include <errno.h>


#define SERVER_FIFO "/tmp/server_fifo"
#define NOT_FIFO "notification_fifo"
#define MAX_MESSAGE_LEN 1024
#define SEM_NAME "/my_semaphore"

pid_t server_pid;
int client_to_server_fifo_fd;
char client_to_server_fifo_name[35];


void signal_handler(int signum) {
    printf("User pressed Ctrl+C. Sending signal to server...\n");

    union sigval value; // The value to send with the signal
    value.sival_int = getpid(); // Send the client's PID to the server

    if (sigqueue(server_pid, SIGUSR1, value) == -1) { // Send the signal
        perror("Error sending signal");
        exit(1);
    }

    printf("Sent SIGUSR1 to server process %d.\n", server_pid);
    exit(0);
}

void sigterm_handler(int signum) {
    printf("Received SIGTERM. Exiting...\n");
    //close client_to_server_fifo_fd
    close(client_to_server_fifo_fd);
    unlink(client_to_server_fifo_name);
    exit(0);
}


int get_connection_status(pid_t client_pid, int server_to_client_fifo_fd, int connection_mode){
    while(1){ //loop until we get a response from the server
        int message_len; 
        ssize_t bytes_read = read(server_to_client_fifo_fd, &message_len, sizeof(message_len)); //read the length of the message
        
        if(bytes_read <= 0){
            continue;
        } 
            
        printf("Response from server : %d \n",message_len);
        return message_len;
    }
}

void handle_response(char* message, int server_to_client_fifo_fd, char* FIFO_NAME ){

    int len;
    // Read the length of the response
    if (read(server_to_client_fifo_fd, &len, sizeof(int)) == -1) {
        perror("read length");
        close(server_to_client_fifo_fd);
        exit(1);
    }

    char* response = malloc(len + 1);
    // Read the response
    if (read(server_to_client_fifo_fd, response, len) == -1) {
        perror("read result");
        close(server_to_client_fifo_fd);
        free(response);
        exit(1);
    }

    response[len] = '\0';
    printf("%s\n", response);
    free(response); // Free the response buffer
}

int main(int argc, char *argv[]) {
    
    // Check the number of command line arguments
    if (argc != 3) {
        printf("Usage: ./client <connect|tryConnect> <server_pid>\n");
        return 1;
    }
    //print pid of client
    printf("Client PID: %d\n", getpid());

    //Get the connection mode and server PID from the command line arguments
    int connection_mode = strcmp(argv[1], "tryConnect") == 0 ? 1 : 0; // 0 for connect, 1 for tryConnect
    server_pid = atoi(argv[2]);

    struct sigaction sa_term, sa_int;

    // Set up the SIGTERM signal handler
    sa_term.sa_handler = sigterm_handler;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = 0;

    if (sigaction(SIGTERM, &sa_term, NULL) == -1) {
        perror("Failed to register SIGTERM handler");
        return 1;
    }

    // Set up the SIGINT signal handler
    sa_int.sa_handler = signal_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;

    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("Failed to register SIGINT handler");
        return 1;
    }

    // Open the server's named pipe for writing
    int server_fifo_fd = open(SERVER_FIFO, O_WRONLY);

    sem_t *semaphore = sem_open(SEM_NAME, O_CREAT, 0644, 1);
    if (semaphore == SEM_FAILED) {
        perror("sem_open");
        return 1;
    }

    // Acquire the semaphore
    if (sem_wait(semaphore) == -1) {
        perror("sem_wait");
        return 1;
    }

    // Send the client's PID and connection mode to the server
    pid_t client_pid = getpid();
    write(server_fifo_fd, &client_pid, sizeof(client_pid));
    write(server_fifo_fd, &connection_mode, sizeof(connection_mode));

    // Release the semaphore
    if (sem_post(semaphore) == -1) {
        perror("sem_post");
        return 1;
    }

    // OPEN THE SERVER TO CLIENT FIFO
    char server_to_client_fifo_name[35];
    sprintf(server_to_client_fifo_name, "/tmp/server_to_client_%d_fifo", client_pid);
    mkfifo(server_to_client_fifo_name, 0666);
    int server_to_client_fifo_fd = open(server_to_client_fifo_name, O_RDONLY);
    if(server_to_client_fifo_fd == -1){
        printf("Error opening server to client fifo\n");
        exit(1);
    }

    int status = get_connection_status(client_pid, server_to_client_fifo_fd, connection_mode);

    if(connection_mode == 1 && status != 2){
        while(1){
            if(get_connection_status(client_pid, server_to_client_fifo_fd, connection_mode)==2){
                break;
            }
        }
    }

    // OPEN THE CLIENT TO SERVER FIFO
    
    sprintf(client_to_server_fifo_name, "/tmp/client_to_server_%d_fifo", client_pid);
    mkfifo(client_to_server_fifo_name, 0666);
    client_to_server_fifo_fd = open(client_to_server_fifo_name, O_WRONLY);

    


    while (1) {
        char message[MAX_MESSAGE_LEN];
        printf("Enter your message to the server (type 'quit' to exit): ");
        fgets(message, MAX_MESSAGE_LEN, stdin);

        // Remove the newline character at the end of the message
        message[strcspn(message, "\n")] = '\0';

        // Send the message length and then the message to the server
        int message_len = strlen(message);
        write(client_to_server_fifo_fd, &message_len, sizeof(message_len));
        write(client_to_server_fifo_fd, message, message_len);

        if(strcmp(message, "quit") == 0){
            break;
        }
       
        handle_response(message, server_to_client_fifo_fd,client_to_server_fifo_name);
        
    }
    


    while(1){
        int message_len;
        ssize_t bytes_read = read(server_to_client_fifo_fd, &message_len, sizeof(message_len));
        
        if(bytes_read <= 0){
            //usleep(500000);
            continue;
        }  
            char message2[message_len + 1];
            read(server_to_client_fifo_fd, message2, message_len);
            message2[message_len] = '\0';
            printf("Notification from server : %s \n",message2);
            break;

    }

    // Close the named pipes
    close(server_fifo_fd);
    close(client_to_server_fifo_fd);
    close(server_to_client_fifo_fd);
    // Remove the client-specific named pipe
    unlink(client_to_server_fifo_name);
    unlink(server_to_client_fifo_name);


    // Close the semaphore
    if (sem_close(semaphore) == -1) {
        perror("sem_close");
        return 1;
    }

    return 0;
}
