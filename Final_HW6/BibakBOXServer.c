#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <time.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define MAX_PATH_LENGTH 1024
#define MAX_FILES 100

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
pthread_mutex_t file_lock;

typedef struct{
    char filepath[MAX_PATH_LENGTH];
    time_t lastModifiedTime;
} FileModificationInfo;

typedef struct {
    int client_sock;
    struct sockaddr_in client_addr;
    pthread_t thread_id;
} ClientInfo;

typedef struct {
    char path[MAX_PATH_LENGTH];
    int isFile;
} FilePath;

typedef struct {
    int count;
    FilePath *paths;
} DirectoryContents;

DirectoryContents contents;
ClientInfo* client_pool;
FileModificationInfo* temp = NULL;
FileModificationInfo* fileInfos = NULL;
char* base_path;
int client_count = 0;
int glob_count = 0;

int getFileSize(char *filename) {
    struct stat fileStat;
    long fileSize;
    if (stat(filename, &fileStat) == 0) {
        fileSize = fileStat.st_size;
        return fileSize;
    } else {
        printf("Failed to get the file size.\n");
        return -1;
    }   
}


void create_folder_structure(const char *path) {
    char *path_copy = strdup(path);  // Make a copy of the path

    // Split the path by '/'
    char *token = strtok(path_copy, "/");
    char current_path[256] = "/";  // Start with the root directory

    // Iterate through the tokens and create the directories
    while (token != NULL) {
        strcat(current_path, token);
        struct stat st;
        if (stat(current_path, &st) == -1) {
            // Check if the path does not exist
            if (mkdir(current_path, 0755) != 0) {
                perror("mkdir error");
                exit(EXIT_FAILURE);
            }
        } else if (!S_ISDIR(st.st_mode)) {
            // Check if the path exists but is not a directory
            fprintf(stderr, "Error: %s is not a directory.\n", current_path);
            exit(EXIT_FAILURE);
        }
        strcat(current_path, "/");
        token = strtok(NULL, "/");
    }

    free(path_copy);
}

char* configure_path(const char* path, const char* base_path) {
    size_t path_length = strlen(path);
    size_t base_path_length = strlen(base_path);
    char* result = (char*)malloc((base_path_length + path_length + 1) * sizeof(char));

    strcpy(result, base_path);
    strcat(result, path);

    return result;
}

void freeDirectoryContents(DirectoryContents contents);

DirectoryContents getDirectoryContents(const char *directoryPath) {
    DirectoryContents contents;
    contents.count = 0;
    contents.paths = NULL;

    DIR *dir = opendir(directoryPath);
    if (dir == NULL) {
        perror("Error opening directory");
        return contents;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        FilePath path;
        snprintf(path.path, sizeof(path.path), "%s/%s", directoryPath, entry->d_name);

        struct stat fileStat;
        if (stat(path.path, &fileStat) < 0) {
            perror("Error getting file status");
            continue;
        }

        path.isFile = S_ISREG(fileStat.st_mode);

        contents.count++;
        contents.paths = realloc(contents.paths, contents.count * sizeof(FilePath));
        contents.paths[contents.count - 1] = path;

        if (!path.isFile && S_ISDIR(fileStat.st_mode)) {
            DirectoryContents subContents = getDirectoryContents(path.path);
            int initialCount = contents.count;
            contents.count += subContents.count;
            contents.paths = realloc(contents.paths, contents.count * sizeof(FilePath));
            memcpy(contents.paths + initialCount, subContents.paths, subContents.count * sizeof(FilePath));
            freeDirectoryContents(subContents);
        }
    }

    closedir(dir);
    return contents;
}

void freeDirectoryContents(DirectoryContents contents) {
    free(contents.paths);
}

int sendDirectoryContents(int sockfd, DirectoryContents contents) {
    int count = contents.count;
    if (send(sockfd, &count, sizeof(count), 0) == -1) {
        perror("Error sending directory count");
        return -1;
    }

    for (int i = 0; i < count; i++) {
        contents.paths[i].path[strlen(contents.paths[i].path)] = '\0';

        if (send(sockfd, &contents.paths[i], sizeof(FilePath), 0) == -1) {
            perror("Error sending directory entry");
            return -1;
        }
    }

    return 0;
}

void sendFile(int socket) {
    for (int i = 0; i < contents.count; i++) {
        if(contents.paths[i].isFile == 0) continue;    
        
        long fileSize = getFileSize(contents.paths[i].path);

        //send fileSize to client
        if (send(socket, &fileSize, sizeof(fileSize), 0) == -1) {
            printf("Failed to send file size: %s\n", contents.paths[i].path);
            return;
        }

        
        FILE* file = fopen(contents.paths[i].path, "rb");
        if (file == NULL) {
            perror("Failed to open the file");
            exit(EXIT_FAILURE);
        }

        
        // Send file data to the receiver
        char buffer[BUFFER_SIZE];
        size_t bytesRead;
        while ((bytesRead = fread(buffer, sizeof(char), BUFFER_SIZE, file)) > 0) {
            if (send(socket, buffer, bytesRead, 0) == -1) {
                perror("Failed to send file data");
                exit(EXIT_FAILURE);
            }
        }

        // Close the file and socket
        fclose(file);
        sleep(1);
    }
}

void handle_client(ClientInfo *client) {
    int client_sock = client->client_sock;
    char buffer[BUFFER_SIZE];
    int bytes_read;
    
    sendDirectoryContents(client_sock, contents);
    sendFile(client_sock);
      
    close(client_sock);

}

void handle_update( int sock){

    //get file name from client
    int64_t file_size;
    if(recv(sock, &file_size, sizeof(file_size), 0) == -1){
        perror("Error receiving file size");
        exit(EXIT_FAILURE);
    }

    sleep(3);
    
    //get file name size from client
    int fileNameSize;
    if(recv(sock, &fileNameSize, sizeof(fileNameSize), 0) == -1){
        perror("Error receiving file name size");
        exit(EXIT_FAILURE);
    }

    char fileName[fileNameSize + 1];  // Add space for the null terminator
    int bytesReceived2 = 0;
    int bytesLeft = fileNameSize;
    
    
    while (bytesLeft > 0) {
        ssize_t bytesRead = recv(sock, fileName + bytesReceived2, bytesLeft, 0);
        if (bytesRead <= 0) {
            perror("Error receiving file name");
            exit(EXIT_FAILURE);
        }
        bytesReceived2 += bytesRead;
        bytesLeft -= bytesRead;
    }

    // Add the null terminator at the end of the received file name
    fileName[fileNameSize] = '\0';
    
    char* result = configure_path(fileName, base_path);

    FILE *file = fopen(result, "wb");
    if (file == NULL) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }

    // Receive file data from the sender
    char* buffer = malloc(BUFFER_SIZE);
    size_t bytesReceived;
    while (file_size > 0) {
            bytesReceived = recv(sock, buffer, BUFFER_SIZE, 0);
            if (bytesReceived == -1) {
                perror("Failed to receive file data");
                exit(EXIT_FAILURE);
            }
            fwrite(buffer, sizeof(char), bytesReceived, file);
            file_size -= bytesReceived;
    }

    fclose(file);
    free(buffer);
    
}

void handle_create( int sock){

    int fileNameSize;
    if(recv(sock, &fileNameSize, sizeof(fileNameSize), 0) == -1){
        perror("Error receiving file name size");
        exit(EXIT_FAILURE);
    }
    
    //read fileNameSize bytes from client
    char* fileName = malloc(fileNameSize + 1);
    int bytesReceived2 = 0;
    int bytesLeft = fileNameSize;

    while (bytesLeft > 0) {
        ssize_t bytesRead = recv(sock, fileName + bytesReceived2, bytesLeft, 0);
        if (bytesRead <= 0) {
            perror("Error receiving file name");
            exit(EXIT_FAILURE);
        }
        bytesReceived2 += bytesRead;
        bytesLeft -= bytesRead;
    }
      
    //read file size from client
    int file_size;
    if(recv(sock, &file_size, sizeof(file_size), 0) == -1){
        perror("Error receiving file size");
        exit(EXIT_FAILURE);
    }
    //print file name and base path
  
    char* result = configure_path(fileName, base_path); 

    
    char *temp = strrchr(result, '/') + 1;  // Extract the filename
    // Check if the filename has an extension
    char *extension = strrchr(temp, '.');
    if (extension == NULL) {
        create_folder_structure(result);  // Create directories
    } else {
        // Remove the filename from the path
        char dir_path[256];
        strncpy(dir_path, result, strlen(result) - strlen(temp));
        dir_path[strlen(result) - strlen(temp)] = '\0';

        create_folder_structure(dir_path);

        FILE *file = fopen(result, "w");  // Create the file
        if (file == NULL) {
            perror("fopen error");
            exit(EXIT_FAILURE);
        }
        fclose(file);
    }


    free(fileName);
}

void handle_delete( int sock){
    //read fileNameSize bytes from client
    int fileNameSize;
    if(recv(sock, &fileNameSize, sizeof(fileNameSize), 0) == -1){
        perror("Error receiving file name size");
        exit(EXIT_FAILURE);
    }

    char *fileName = malloc(fileNameSize);
    int bytesReceived2 = 0;
    int bytesLeft = fileNameSize;

    while (bytesLeft > 0) {
        ssize_t bytesRead = recv(sock, fileName + bytesReceived2, bytesLeft, 0);
        if (bytesRead <= 0) {
            perror("Error receiving file name");
            exit(EXIT_FAILURE);
        }
        bytesReceived2 += bytesRead;
        bytesLeft -= bytesRead;
    }

    char* result = configure_path(fileName, base_path);

    if (remove(result) == 0) {
        printf("File removed successfully.\n");
    } else {
        perror("Error removing file");
    }

    free(fileName);
    free(result);

}

void *thread_function(void *arg) {
   
    ClientInfo *client = (ClientInfo *)arg;
    int client_sock = client->client_sock;
    int bytesReceived, bytesLeft, valread,messageLength;
    
    char* path = malloc(256);
    strcpy(path, base_path);
    
    //send size of path to client
    int pathSize = strlen(path);
    if(send(client_sock, &pathSize, sizeof(pathSize), 0) == -1){
        perror("Error sending path size");
        exit(EXIT_FAILURE);
    }

    //send path to client
    if(send(client_sock, path, strlen(path), 0) == -1){
        perror("Error sending path");
        exit(EXIT_FAILURE);
    }

    sendDirectoryContents(client_sock, contents);
    sendFile(client_sock);
     
    while (1)
    {   
        sleep(1);
        
        char* buffer = malloc(BUFFER_SIZE);
        
        if(recv(client_sock, &messageLength, sizeof(messageLength), MSG_DONTWAIT) == -1){
            free(buffer);
            continue;
        }
        

        if (messageLength == -1) {
            perror("recv failed");
            exit(EXIT_FAILURE);
        } else if (messageLength == 0) {
            // No message received, do some work
            printf("No message received. Performing some work...\n");

            // Clear the buffer
            memset(buffer, 0, sizeof(buffer));
        } else {
            bytesReceived = 0;
            bytesLeft = messageLength;
            while (bytesLeft > 0) {
                ssize_t bytesRead = recv(client_sock, buffer + bytesReceived, bytesLeft, 0);
                if (bytesRead <= 0) {
                    perror("Error receiving file name");
                    exit(EXIT_FAILURE);
                }
                bytesReceived += bytesRead;
                bytesLeft -= bytesRead;
            }

            buffer[messageLength] = '\0';
              
            printf("Received message: %s\n", buffer);
            
            // if received message is "exit", then exit
            if (strncmp(buffer, "!_UPDATE_!\n", strlen("!_UPDATE_!\n")) == 0) {
                handle_update(client_sock);
            }
            if (strncmp(buffer, "!_CREATE_!\n", strlen("!_CREATE_!\n")) == 0) {
                handle_create(client_sock);
            }
            if (strncmp(buffer, "!_DELETE_!\n", strlen("!_DELETE_!\n")) == 0) {
                handle_delete(client_sock);   
            }

            // Clear the buffer
            memset(buffer, 0, sizeof(buffer));
        }
        
        
        
        
        

        free(buffer);
    } //end while
    
}

int main(int argc, char *argv[] ) {

    if( argc != 4) {
        printf("Usage: %s <directory> <threadSize> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
   
    char* directoryPath = argv[1];
    int threadSize = atoi(argv[2]);
    int port = atoi(argv[3]);

    base_path = directoryPath;    
    contents = getDirectoryContents(directoryPath);
  
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len;

    fileInfos = malloc(MAX_FILES * sizeof(FileModificationInfo));
    glob_count = 0;
    
    
    client_pool = malloc(threadSize * sizeof(ClientInfo));

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    
    // Enable address reuse
    int reuse = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("Failed to set socket option");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_sock, threadSize) < 0) {
        perror("Error listening");
        exit(EXIT_FAILURE);
    }

    
    for(int i = 0; i < threadSize; i++) {
        client_pool[i].client_sock = -1;
        client_pool[i].thread_id = 0;
    }

    while (1) {
        
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sock < 0) {
            perror("Error accepting connection");
            exit(EXIT_FAILURE);
        }

        int slot = -1;
        for (int i = 0; i < threadSize; i++) {
            if (client_pool[i].client_sock == -1) {
                slot = i;
                client_count++;
                //print client address
                
                printf("Client %s connected\n", inet_ntoa(client_addr.sin_addr));
                
                break;
            }
        }    

        if(slot == -1){
            printf("Server is too busy. Please try again later.\n");
            close(client_sock);
            continue;
        }

        client_pool[slot].client_sock = client_sock;

        pthread_create(&(client_pool[slot].thread_id), NULL, thread_function, (void *)&client_pool[slot]);
        
        pthread_detach(client_pool[slot].thread_id);

    }
    
    
    for(int i = 0; i < threadSize; i++) {
        pthread_join(client_pool[i].thread_id, NULL);
    }

    close(server_sock);
    free(client_pool);
    free(fileInfos);

    return 0;
}
