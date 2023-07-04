#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h> 
#include <string.h> 
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>

#define SERVER_FIFO "/tmp/server_fifo"
#define SHARED_MEM_SIZE sizeof(int)
#define SHARED_MEM_NAME "empty_slot_count"
#define WAITING_CLIENTS_FIFO "waiting_clients_fifo"
#define MAX_SIZE 1024
#define SHM_KEY 146735


typedef struct {
    int max_clients;
    pid_t* connected_clients;
    int num_children;
    int empty_slot_count;
    pid_t server_pid;
    char directory_path[100];

} ServerStruct;

ServerStruct *server;

typedef struct {
    int items[MAX_SIZE];
    int front;
    int rear;
} Queue;


void initializeQueue(Queue* q) {
    q->front = -1;
    q->rear = -1;
}

int isFull(Queue* q) {
    return (q->rear == MAX_SIZE - 1);
}

int isEmpty(Queue* q) {
    return (q->front == -1 && q->rear == -1);
}

void enqueue(Queue* q, int data) {
    if (isFull(q)) {
        printf("Queue is full. Unable to enqueue element.\n");
        return;
    }
    if (isEmpty(q))
        q->front = q->rear = 0;
    else
        q->rear++;
    q->items[q->rear] = data;
}

int dequeue(Queue* q) {
    int data;
    if (isEmpty(q)) {
        printf("Queue is empty. Unable to dequeue element.\n");
        return -1;
    }
    data = q->items[q->front];
    if (q->front == q->rear)
        q->front = q->rear = -1;
    else
        q->front++;
    return data;
}

void help(char* message, int server_to_client_fifo_fd){
    char* command = strtok(message, " "); // command now points to "help"
    char* argument = strtok(NULL, " "); // argument now points to the command to get help on

    char help_message[1000];


    if (argument == NULL){
        sprintf(help_message, "Available commands: help, list, quit, killServer, readF, writeF, upload, download.\n");
        sprintf(help_message, "Type 'help [command]' to get help on a specific command.\n");
        // printf("Available commands: help, list, quit, killServer, readF, writeF, upload, download.\n");
        // printf("Type 'help [command]' to get help on a specific command.\n");
    } 
    else if (strcmp(argument, "help") == 0){
        sprintf(help_message, "help [command] : Display the list of possible client requests.\n");
        //printf("help [command] : Display the list of possible client requests.\n");
    }
    else if (strcmp(argument, "list") == 0){
        sprintf(help_message, "list: Sends a request to display the list of files in Servers directory.\n(also displays the list received from the Server)\n");
        //printf("list: Sends a request to display the list of files in Servers directory.\n(also displays the list received from the Server)\n");
    } 
    else if (strcmp(argument, "quit") == 0){
        sprintf(help_message, "quit : Send write request to Server side log file and quits\n");
        //printf("quit : Send write request to Server side log file and quits\n");
    } 
    else if (strcmp(argument, "killServer") == 0){
        sprintf(help_message, "killServer : Sends a kill request to the Server.\n");
        //printf("killServer : Sends a kill request to the Server.\n");
    } 
    else if (strcmp(argument, "readF") == 0){
        sprintf(help_message, "readF <file> <line #> :\n Requests to display the # line of the <file>, if no line number is given the whole contents of the file is requested (and displayed on the client side)\n");
        //printf("readF <file> <line #> :\n Requests to display the # line of the <file>, if no line number is given the whole contents of the file is requested (and displayed on the client side)\n");
    } 
    else if (strcmp(argument, "writeF") == 0){
        sprintf(help_message, "writeF <file> <line #> <string> : \n Request to write the content of “string” to the #th line the <file>, if the line # is not given writes to the end of file. If the file does not exists in Servers directory creates and edits the file at the same time\n");
        //printf("writeT <file> <line #> <string> : \n Request to write the content of “string” to the #th line the <file>, if the line # is not given writes to the end of file. If the file does not exists in Servers directory creates and edits the file at the same time\n");
    } 
    else if (strcmp(argument, "upload") == 0){
        sprintf(help_message, "upload <file> :\n Uploads the file from the current working directory of client to the Servers directory (beware of the cases no file in clients current working directory and file with the same name on Servers side)\n");
        //printf("upload <file> :\n Uploads the file from the current working directory of client to the Servers directory (beware of the cases no file in clients current working directory and file with the same name on Servers side)\n");
    } 
    else if (strcmp(argument, "download") == 0){
        sprintf(help_message, "download <file> :\n Request to receive <file> from Servers directory to client side \n");
        //printf("download <file> :\n Request to receive <file> from Servers directory to client side \n");
    } 
    else{
        sprintf(help_message, "Unknown command : '%s'. Type 'help' to get a list of available commands.\n", argument);
        //printf("Unknown command : '%s'. Type 'help' to get a list of available commands.\n", argument);
    }
    int len = strlen(help_message);
    write(server_to_client_fifo_fd, &len, sizeof(int));
    write(server_to_client_fifo_fd, help_message, len);


}

void quit(pid_t client_pid, int client_to_server_fifo_fd, int server_to_client_fifo_fd){
            for (int i = 0; i < server->max_clients; i++) {
                if (server->connected_clients[i] == client_pid) {
                    server->connected_clients[i] = 0;
                    break;
                }
            }
            
            char quit_message[100];
            sprintf(quit_message, "Client %d has disconnected.\n", client_pid);
            int quit_message_len = strlen(quit_message);
            write(server_to_client_fifo_fd, &quit_message_len, sizeof(quit_message_len));
            write(server_to_client_fifo_fd, quit_message, quit_message_len);
            close(client_to_server_fifo_fd);
            exit(0);
}

void list(int server_to_client_fifo_fd){
    char* result = NULL;
    char* buffer = malloc(4096);
    size_t resultSize = 0;

    FILE* pipe = popen("ls", "r");
    if (pipe == NULL) {
        perror("popen");
        exit(1);
    }

    size_t bytesRead;
    while ((bytesRead = fread(buffer, sizeof(char), 4096, pipe)) > 0) {
        result = realloc(result, resultSize + bytesRead + 1);
        memcpy(result + resultSize, buffer, bytesRead);
        resultSize += bytesRead;
    }

    if (result != NULL) {
        result[resultSize] = '\0';
    }

    free(buffer);
    pclose(pipe);

    //write result to server
    int len = strlen(result);
    write(server_to_client_fifo_fd, &len, sizeof(int));
    write(server_to_client_fifo_fd, result, len);
    free(result);
}

void readF(char *command, int server_to_client_fifo_fd){
    char filename[50];
    int line_number = -1;

    // Remove trailing newline character
    command[strcspn(command, "\n")] = '\0';

    // Try to parse filename and line number from command
    // If line number is not provided, sscanf will return 1, and line_number will remain -1
    if (sscanf(command, "readF %49s %d", filename, &line_number) < 1) {
        char response[50] = "Invalid command format.\n";
        int len = strlen(response);
        write(server_to_client_fifo_fd, &len, sizeof(int));
        write(server_to_client_fifo_fd, response, len);
        return;
    }
    else{
        FILE *file = fopen(filename, "r");
        if (file == NULL) {
            perror("Error opening file");
            return;
        }

        // Initial allocation of response string
        char *response = malloc(1024);
        char tempBuffer[2048];  // Temporary buffer for sprintf
        if (!response) {
            printf("Memory allocation failed!\n");
            return;
        }
        response[0] = '\0';  // Ensure empty string

        // If line number is provided and positive
        if (line_number > 0){
            char buffer[1024];
            int current_line = 1;

            // Read lines from file until the specified line is reached
            while (fgets(buffer, sizeof(buffer), file) != NULL) {
                if (current_line == line_number) {
                    sprintf(tempBuffer, "Line %d: %s", line_number, buffer);
                    response = realloc(response, strlen(response) + strlen(tempBuffer) + 1);
                    strcat(response, tempBuffer);
                    break;
                }
                current_line++;
            }

            // If specified line number is greater than total lines in file
            if (current_line < line_number) {
                sprintf(tempBuffer, "The file has only %d lines.\n", current_line - 1);
                response = realloc(response, strlen(response) + strlen(tempBuffer) + 1);
                strcat(response, tempBuffer);
            }
        } 
        // If no line number is provided, line_number remains -1, and this block is executed
        else{
            char ch;
            // Read entire file character by character
            while ((ch = fgetc(file)) != EOF) {
                char buffer[2] = {ch, '\0'};
                response = realloc(response, strlen(response) + 2);
                strcat(response, buffer);
            }
        }

        printf("%s", response);  // Print the result
        int len = strlen(response);
        write(server_to_client_fifo_fd, &len, sizeof(int));
        write(server_to_client_fifo_fd, response, len);

        free(response);
        fclose(file);
    }
}

void writeF(){

}

void upload(){

}

void download(){

}

void signal_handler(int signum, siginfo_t *info, void *context) {
    printf("Received SIGUSR1! Client PID: %d\n", info->si_value.sival_int);
    //exit(0);
}

void signal_handler2(int signum, siginfo_t *info, void *context) {

    for (int i = 0; i < server->max_clients - (server->empty_slot_count); i++) {
        printf("Killing child process %d\n", server->connected_clients[i]);
        if(server->connected_clients[i] != 0){
            kill(server->connected_clients[i], SIGTERM);
        }
        
    }
    
    // Wait for child processes
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0);
    
    exit(0);

}

void handle_client(pid_t client_pid) {
    char client_to_server_fifo_name[35];
    sprintf(client_to_server_fifo_name, "/tmp/client_to_server_%d_fifo", client_pid);
    mkfifo(client_to_server_fifo_name, 0666);
    int client_to_server_fifo_fd = open(client_to_server_fifo_name, O_RDONLY);

    //server_to_client_fifo_name
    char server_to_client_fifo_name[35];
    sprintf(server_to_client_fifo_name, "/tmp/server_to_client_%d_fifo", client_pid);
    mkfifo(server_to_client_fifo_name, 0666);
    int server_to_client_fifo_fd = open(server_to_client_fifo_name, O_WRONLY);
    if(server_to_client_fifo_fd == -1){
        printf("Error opening server_to_client_fifo_fd\n");
        exit(1);
    }


    while (1) {
        int message_len;
        ssize_t bytes_read = read(client_to_server_fifo_fd, &message_len, sizeof(message_len));

        if (bytes_read <= 0) {
            continue;
        }

        char message[message_len + 1];
        read(client_to_server_fifo_fd, message, message_len);
        message[message_len] = '\0';
        
        if(strcmp(message, "help") == 0){
            char help_message[100];
            sprintf(help_message, "Available commands: help, list, quit, killServer, readF, writeF, upload, download.\n");
            int help_message_len = strlen(help_message);
            write(server_to_client_fifo_fd, &help_message_len, sizeof(help_message_len));
            write(server_to_client_fifo_fd, help_message, help_message_len);
            
        }
        else if(strncmp(message, "help ",5) == 0){
            help(message, server_to_client_fifo_fd);
        }
        else if(strcmp(message, "list") == 0){
            list(server_to_client_fifo_fd);
        }
        else if(strcmp(message, "quit") == 0){
            printf("\nClient %d has disconnected.\n", client_pid);
            quit(client_pid,client_to_server_fifo_fd,server_to_client_fifo_fd);
        }
        else if(strcmp(message, "killServer") == 0){
            kill(getppid(), SIGUSR2); // send signal to parent
            close(client_to_server_fifo_fd);
            unlink(client_to_server_fifo_name);
            exit(0);
        }
        else if(strncmp(message, "readF ", 6) == 0){
            readF(message, server_to_client_fifo_fd);
        }
        else if(strcmp(message, "") == 0){
            printf("Empty message received from client %d\n", client_pid);
            char sent_message[100];
            sprintf(sent_message, "Empty message sent to server");
            int sent_message_len = strlen(sent_message);
            write(server_to_client_fifo_fd, &sent_message_len, sizeof(sent_message_len));
            write(server_to_client_fifo_fd, sent_message, sent_message_len);
        }
        else {
            printf("Received message from client %d: %s\n", client_pid, message);
            char sent_message[100];
            sprintf(sent_message, "not-command");
            int sent_message_len = strlen(sent_message);
            write(server_to_client_fifo_fd, &sent_message_len, sizeof(sent_message_len));
            write(server_to_client_fifo_fd, sent_message, sent_message_len);
        }

    }
}

bool add_client(pid_t client_pid) {
    for (int i = 0; i < server->max_clients; i++) {
        if (server->connected_clients[i] == 0) {
            server->connected_clients[i] = client_pid;
            return true;
        }
    }
    return false;
}

void remove_client(pid_t client_pid) {
    for (int i = 0; i < server->max_clients; i++) {
        if (server->connected_clients[i] == client_pid) {
            server->connected_clients[i] = 0;
            break;
        }
    }

    //server_to_client_fifo_name
    char server_to_client_fifo_name[35];
    sprintf(server_to_client_fifo_name, "/tmp/server_to_client_%d_fifo", client_pid);
    mkfifo(server_to_client_fifo_name, 0666);
    int server_to_client_fifo_fd = open(server_to_client_fifo_name, O_WRONLY);
    if(server_to_client_fifo_fd == -1){
        printf("Error opening server_to_client_fifo_fd\n");
        exit(1);
    }

    //create a string to store the message
    char message[40];
    sprintf(message, "This client has been disconnected.");
    int message_len = strlen(message);
    write(server_to_client_fifo_fd, &message_len, sizeof(message_len));
    write(server_to_client_fifo_fd, message, message_len);

}

//create send_notification function
void send_notification(pid_t client_pid, int flag){
    
    char server_to_client_fifo_name[35];
    sprintf(server_to_client_fifo_name, "/tmp/server_to_client_%d_fifo", client_pid);
    mkfifo(server_to_client_fifo_name, 0666);
    int server_to_client_fifo_fd = open(server_to_client_fifo_name, O_WRONLY);
    if(server_to_client_fifo_fd == -1){
        printf("Error opening server_to_client_fifo_fd\n");
        exit(1);
    }

    int status;
    int message_len;
    if(flag==0){// server is full
        status = 0;
    }
    else if(flag==1){ // you are in queue
        status = 1;
    }
    else if(flag==2){ // you are connected
        status = 2;     
    }
    
    //write the status to the client
    write(server_to_client_fifo_fd, &status, sizeof(status));

}

ServerStruct *create_shared_struct(int max_clients) {
    // Calculate the total size of the shared memory segment
    // ServerStruct size + size for the connected_clients array
    size_t size = sizeof(ServerStruct) + sizeof(pid_t) * max_clients;

    int shmid = shmget(SHM_KEY, size, 0644|IPC_CREAT);
    if (shmid == -1) {
        perror("Shared memory");
        return NULL;
    }

    void *shmem = shmat(shmid, (void *)0, 0);
    if (shmem == (char *)(-1)) {
        perror("Shared memory attach");
        return NULL;
    }

    ServerStruct *server_struct = (ServerStruct *) shmem;
    server_struct->max_clients = max_clients;
    server_struct->num_children = 0;
    server_struct->empty_slot_count = max_clients;

    // Allocate the connected_clients array in shared memory
    server_struct->connected_clients = (pid_t *)(shmem + sizeof(ServerStruct));

    // Initialize all client slots as 0 (not connected)
    for (int i = 0; i < max_clients; i++) {
        server_struct->connected_clients[i] = 0;
    }

    return server_struct;
}

void detach_shared_memory(ServerStruct *shared_struct) {
    if (shmdt(shared_struct) == -1) {
        perror("shmdt");
    }
}

int main(int argc, char *argv[]) {  
    Queue q;
    initializeQueue(&q);
    
    //------------------------ Argument Handling ------------------------//
    if (argc < 3) {
        printf("Usage: ./server <dirname> <max. #ofClients>\n");
        return 1;
    }
    //------------------------------------------------------------------//

    
    //-------------------- CTRL+C handler from clients -----------------//
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = signal_handler;
    sa.sa_flags = SA_SIGINFO;

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("Error setting up SIGUSR1 handler");
        return 1;
    }
    //------------------------------------------------------------------//

    
    //-------------------- killServer handler from clients -------------//
    struct sigaction sa2;
    memset(&sa2, 0, sizeof(sa2));
    sa2.sa_sigaction = signal_handler2;
    sa2.sa_flags = SA_SIGINFO;

    if (sigaction(SIGUSR2, &sa2, NULL) == -1) {
        perror("Error setting up SIGUSR1 handler");
        return 1;
    }
    //------------------------------------------------------------------//

    //-------------------- Create Server Struct ------------------//
    server = create_shared_struct(atoi(argv[2]));
    if (server == NULL) {
        return 1;
    }
    server->max_clients = atoi(argv[2]);
    server->empty_slot_count = server->max_clients;
    server->num_children = 0;
    server->server_pid = getpid();
    strncpy(server->directory_path, argv[1], sizeof(server->directory_path) - 1);
    server->directory_path[sizeof(server->directory_path) - 1] = '\0';
    // Check if the specified directory exists, create it if it doesn't
    struct stat st;
    if (stat(server->directory_path, &st) == -1) {
        // Directory does not exist, create it
        if (mkdir(server->directory_path, 0700) == -1) {
            perror("Error creating directory");
            return 1;
        }
        printf("Directory '%s' created.\n", server->directory_path);
    } else {
        // Directory already exists
        if (!S_ISDIR(st.st_mode)) {
            printf("Specified dirname is not a directory.\n");
            return 1;
        }
        printf("Directory '%s' already exists.\n", server->directory_path);
    }

    //>>>>>>>>>>>>>>>>>>>>>>>> Server FIFO <<<<<<<<<<<<<<<<<<<<<<<<<<<<
    pid_t server_pid = getpid();
    printf("Server PID: %d || Client Capacity: %d \n", server->server_pid,server->empty_slot_count); //print server pid and client capacity
    mkfifo(SERVER_FIFO, 0666); // Create the server FIFO
    int server_fifo_fd = open(SERVER_FIFO, O_RDONLY | O_NONBLOCK); // Open the server FIFO


    pid_t client_pid;
    int connection_mode;
    ssize_t bytes_read;
    
    while (1) {
        
        if(!isEmpty(&q) && server->empty_slot_count >0){ //if there is a client at queue and there is an empty slot
            client_pid = dequeue(&q);
            printf("Client %d is dequeued\n", client_pid);
            send_notification(client_pid, 2);
        }
        else{
            bytes_read = read(server_fifo_fd, &client_pid, sizeof(client_pid));
            
            if(bytes_read > 0){
                read(server_fifo_fd, &connection_mode, sizeof(connection_mode));
            
                bool can_connect = add_client(client_pid); // Check if the client can be connected
                
                // If the client wants to connect immediately and the server is full,
                // close the connection and continue the loop
                if (connection_mode == 0 && !can_connect) {
                    printf("Client %d could not connect. Server is full.\n", client_pid);
                    send_notification(client_pid, 0); // Notify the client that the server is full
                    continue;
                }

                // If the client wants to wait in the queue, add the client to the waiting queue
                if (connection_mode == 1 && !can_connect) {
                    printf("Client %d is waiting in the queue.\n", client_pid);
                    send_notification(client_pid, 1);
                    enqueue(&q, client_pid);
                    continue;
                }

                // Print a message indicating a new client has connected
                send_notification(client_pid, 2);
                printf("Client %d connected\n", client_pid);
            }
            else if (bytes_read <= 0) {
                usleep(50000); // Sleep for 500ms
                continue;
            }
        }

        // Create a new process to handle this client
        pid_t fork_pid = fork();

        if (fork_pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        // Child process
        if (fork_pid == 0) {
            server->empty_slot_count = server->empty_slot_count - 1;
            handle_client(client_pid);
            remove_client(client_pid);
            server->empty_slot_count = server->empty_slot_count + 1;
            exit(EXIT_SUCCESS);
        }
        
    }

    // Wait for all child processes to terminate
    while (waitpid(-1, NULL, 0) > 0);

    // Close and remove the named pipe
    close(server_fifo_fd);
    unlink(SERVER_FIFO);

    return 0;
}



