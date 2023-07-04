#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <stdbool.h>
#include <time.h>


#define MAX_PATH_LENGTH 1024
#define BUFFER_SIZE 1024
#define MAX_FILES 100

// Global variables
char **newPaths;  // String array
int arraySize = 0;   // Size of the array
int arrayCapacity = 5; // Initial capacity of the array

typedef struct{
    char path[MAX_PATH_LENGTH];
    int isFile;
} FilePath;

typedef struct{
    int count;
    FilePath *paths;
} DirectoryContents;

typedef struct{
    char filepath[MAX_PATH_LENGTH];
    time_t lastModifiedTime;
} FileModificationInfo;

FileModificationInfo* temp = NULL;
FileModificationInfo* fileInfos = NULL;
DirectoryContents contents;
int glob_count = 0;
char* base_path;


//remove base path from path to get relative path of file
void remove_base_path(const char* path, const char* base_path, char* new_path) { 
    size_t path_len = strlen(path);
    size_t base_path_len = strlen(base_path);

    // Check if the base_path is a prefix of the path
    if (path_len >= base_path_len && strncmp(path, base_path, base_path_len) == 0) {
        // Determine the remaining path after removing the base_path
        const char* remaining_path = path + base_path_len;

        // Check if the remaining_path starts with a directory separator
        if (remaining_path[0] == '/' || remaining_path[0] == '\\') {
            // Move the remaining_path one character forward to exclude the directory separator
            remaining_path++;
        }

        // Copy the remaining_path to the new_path
        strcpy(new_path, remaining_path);
    } else {
        // If the base_path is not a prefix of the path, copy the path as it is
        strcpy(new_path, path);
    }

    // Ensure new_path has a leading '/'
    if (new_path[0] != '/') {
        memmove(new_path + 1, new_path, strlen(new_path) + 1);
        new_path[0] = '/';
    }
}

void saveFileModificationTimes(const char* directory, FileModificationInfo* files, int* count) {
    DIR* dir = opendir(directory);

    if (dir == NULL) {
        printf("Failed to open the directory.\n");
        return;
    }

    struct dirent* entry;
    struct stat fileStat;
    char filepath[MAX_PATH_LENGTH];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(filepath, sizeof(filepath), "%s/%s", directory, entry->d_name);

        if (stat(filepath, &fileStat) == 0) {
            if (S_ISDIR(fileStat.st_mode)) {
                saveFileModificationTimes(filepath, files, count);  // Recursively call for subdirectory
            } else {
                if (*count >= MAX_FILES) {
                    printf("Maximum file count exceeded. Exiting traversal.\n");
                    break;
                }
                
                // Get initial modification time
                struct tm initialTime;
                initialTime = *localtime(&(fileStat.st_mtime));

                files[*count].lastModifiedTime = fileStat.st_mtime;
                strncpy(files[*count].filepath, filepath, sizeof(files[*count].filepath));
                files[*count].filepath[sizeof(files[*count].filepath) - 1] = '\0';
                (*count)++;
            }
        } else {
            printf("Failed to retrieve file information for %s\n", entry->d_name);
        }
    }

    closedir(dir);
}

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

void createdDetected(int sock, char* file_path, char* base_path){
    //send message to server
    char *message = "!_CREATE_!\n";
    int len = strlen(message);

    //send message len to server
    if (send(sock, &len, sizeof(len), 0) == -1) {
        printf("Failed to send message length: %s\n", message);
        return;
    }
    sleep(1);
    if(send(sock, message, strlen(message), 0) == -1){
        printf("Failed to send message: %s\n", message);
        return;
    }

    char* new_path = malloc(MAX_PATH_LENGTH);
    remove_base_path(file_path, base_path, new_path);
    //send size of file name to client
    int fileNameSize = strlen(new_path);
    if (send(sock, &fileNameSize, sizeof(fileNameSize), 0) == -1) {
        printf("Failed to send file name size: %s\n", file_path);
        return;
    }
   
    if (send(sock, new_path, strlen(new_path), 0) == -1) {
        printf("Failed to send file name: %s\n", new_path);
        return;
    }

    //send file size to client
    int fileSize = getFileSize(file_path);
    if (send(sock, &fileSize, sizeof(fileSize), 0) == -1) {
        printf("Failed to send file size: %s\n", file_path);
        return;
    }
   
    free(new_path);

}

void deletedDetected( int sock, char* file_path, char* base_path){
    //send message to server
    char *message = "!_DELETE_!\n";
    int len = strlen(message);
    //send message len to server
    if (send(sock, &len, sizeof(len), 0) == -1) {
        printf("Failed to send message length: %s\n", message);
        return;
    }

    if(send(sock, message, strlen(message), 0) == -1){
        printf("Failed to send message: %s\n", message);
        return;
    }

    char* new_path = malloc(MAX_PATH_LENGTH);
    remove_base_path(file_path, base_path, new_path);

    //send size of file name to client
    int fileNameSize = strlen(new_path);
    if (send(sock, &fileNameSize, sizeof(fileNameSize), 0) == -1) {
        printf("Failed to send file name size: %s\n", file_path);
        return;
    }

    if (send(sock, new_path, strlen(new_path), 0) == -1) {
        printf("Failed to send file name: %s\n", file_path);
        return;
    }

    free(new_path);
}

void check_delete(char *dir_path, int sock){
    
    temp = malloc(MAX_FILES * sizeof(FileModificationInfo));
    int count = 0;

    saveFileModificationTimes(dir_path, temp, &count);
    if(count >= glob_count){
        return;
    }


    for (int i = 0; i < glob_count; i++) {
        bool found = false;
        for (int j = 0; j < count; j++) {
           
            if (strcmp(fileInfos[i].filepath, temp[j].filepath) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            deletedDetected(sock, fileInfos[i].filepath,dir_path);
            free(fileInfos);
            fileInfos = malloc(MAX_FILES * sizeof(FileModificationInfo));
            glob_count = 0;
            saveFileModificationTimes(dir_path, fileInfos, &glob_count);         
            break;
        }
    }
   
    free(temp);
}

void check_create(char *dir_path, FileModificationInfo* fileInfos, int sock){
    temp = malloc(MAX_FILES * sizeof(FileModificationInfo));
    int count = 0;

    saveFileModificationTimes(dir_path, temp, &count);
    if(count <= glob_count){
        return;
    }

    //check if there is created file in temp
    for(int i=0;i<count;i++){
        bool found = false;
        for(int j=0;j<glob_count;j++){
            if(strcmp(temp[i].filepath, fileInfos[j].filepath) == 0){
                found = true;
                break;
            }
        }
        if(!found){
            createdDetected(sock, temp[i].filepath, dir_path);
            free(fileInfos);
            fileInfos = malloc(MAX_FILES * sizeof(FileModificationInfo));
            glob_count = 0;
            saveFileModificationTimes(dir_path, fileInfos, &glob_count);
            break;
        }
    }

    free(temp);
    
}

void remove_file_from_path( char* filepath, FileModificationInfo *fileInfos){
    int i, j;
    for (i = 0; i < glob_count; i++) {
        if (strcmp(fileInfos[i].filepath, filepath) == 0) {
            // Shift the elements after the removed filepath
            for (j = i; j < glob_count - 1; j++) {
                strcpy(fileInfos[j].filepath, fileInfos[j + 1].filepath);
                fileInfos[j].lastModifiedTime = fileInfos[j + 1].lastModifiedTime;
            }
            glob_count--;
            break;
        }
    }
}

void updateDetected(int sock, char* file_path, char* base_path){
    //send message to server
    char *message = "!_UPDATE_!\n";
    int len = strlen(message);
    
    // send len to server
    if (send(sock, &len, sizeof(len), 0) == -1) {
        printf("Failed to send message length: %s\n", file_path);
        return;
    }

    if(send(sock, message, strlen(message), 0) == -1){
        printf("Failed to send message: %s\n", message);
        return;
    }

    long fileSize = getFileSize(file_path);

    //send fileSize to client
    if (send(sock, &fileSize, sizeof(fileSize), 0) == -1) {
        printf("Failed to send file size: %s\n", file_path);
        return;
    }
    
    char* new_path = malloc(MAX_PATH_LENGTH);
    remove_base_path(file_path, base_path, new_path);

    //send size of file name to client
    int fileNameSize = strlen(new_path);
    if (send(sock, &fileNameSize, sizeof(fileNameSize), 0) == -1) {
        printf("Failed to send file name size: %s\n", file_path);
        return;
    }

    if (send(sock, new_path, strlen(new_path), 0) == -1) {
        printf("Failed to send file name: %s\n", new_path);
        return;
    }


    //send file to server
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        printf("2Failed to open file: %s\n", file_path);
        
        return;
    }


    char* buffer = malloc(BUFFER_SIZE * sizeof(char));

    size_t bytesRead;
    while ((bytesRead = fread(buffer, sizeof(char), BUFFER_SIZE, file)) > 0) {
        if (send(sock, buffer, bytesRead, 0) == -1) {
                perror("Failed to send file data");
                exit(EXIT_FAILURE);
        }
    }
    free(buffer);
    fclose(file);
}

char* configure_path(const char* path, const char* base_path) {
    size_t path_length = strlen(path);
    size_t base_path_length = strlen(base_path);
    char* result = (char*)malloc((base_path_length + path_length + 1) * sizeof(char));

    strcpy(result, base_path);
    strcat(result, path);

    return result;
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

int check_update(FileModificationInfo* fileInfos, int count, int sock) {
    for (int i = 0; i < count; i++) {
        FILE* file = fopen(fileInfos[i].filepath, "r");
        if (file == NULL) {
            remove_file_from_path(fileInfos[i].filepath, fileInfos);
            continue;
        }

        struct stat fileStat;
        if (stat(fileInfos[i].filepath, &fileStat) != 0) {
            printf("Failed to retrieve file information for: %s\n", fileInfos[i].filepath);
            fclose(file);
            continue;
        }

        // Get initial modification time
        time_t initialTime = fileInfos[i].lastModifiedTime;
        // Get current modification time
        time_t currentTime = fileStat.st_mtime;

        if (initialTime == 0) {
            // First check, update the last modified time and continue to the next file
            fileInfos[i].lastModifiedTime = currentTime;
            fclose(file);
            continue;
        }

        if (difftime(currentTime, initialTime) > 0) {
            // File has been modified since the last check
            fileInfos[i].lastModifiedTime = currentTime;
            fclose(file);
            return i;
        }

        fclose(file);
    }

    return -1;
}

void checkFileModificationTimes(FileModificationInfo* fileInfos, int count, int sock, char* source_dir) {
    
    int bytesReceived, bytesLeft, valread,messageLength;

    while(1){
        sleep(1);
        check_create(source_dir,fileInfos,sock);
        check_delete(source_dir, sock);
        
        int index = check_update(fileInfos, count, sock);
        if(index != -1){
            updateDetected(sock, fileInfos[index].filepath, source_dir);
        }

    }
    
}

void clearDirectory(char* directory_path){

    DIR* directory = opendir(directory_path);
    if (directory == NULL) {
        perror("Unable to open directory");
        return;
    }

    struct dirent* entry;
    char file_path[1024];
    
    while ((entry = readdir(directory)) != NULL) {
        
        snprintf(file_path, sizeof(file_path), "%s/%s", directory_path, entry->d_name);
        struct stat file_stat;
        if (stat(file_path, &file_stat) == 0) {
            if (S_ISDIR(file_stat.st_mode)) {
                // Recursively clear subdirectories
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                    clearDirectory(file_path);
                    rmdir(file_path); // Remove the empty subdirectory
                }
            } else if (S_ISREG(file_stat.st_mode)) {
                // Delete regular files
                if (unlink(file_path) != 0) {
                    perror("Unable to delete file");
                    closedir(directory);
                    return;
                }
            }
        }
    }

    closedir(directory);
}

void addStringToArray(char *str) {
    // Check if the array is full
    if (arraySize == arrayCapacity) {
        // Increase the capacity
        arrayCapacity *= 2;
        
        // Reallocate memory for the string array
        newPaths = (char **)realloc(newPaths, arrayCapacity * sizeof(char *));
    }

    // Allocate memory for the new string
    newPaths[arraySize] = (char *)malloc((strlen(str) + 1) * sizeof(char));

    // Copy the string to the array
    strcpy(newPaths[arraySize], str);

    // Increment the array size
    arraySize++;
}

void saveFile(int socket) {
    
    char buffer[BUFFER_SIZE];

    for (int i = 0; i < arraySize; i++) {
        
        //receive fileSize 
        int64_t fileSize;
        if (recv(socket, &fileSize, sizeof(int64_t), 0) == -1) {
            printf("Failed to receive file size\n");
            return;
        }
        
        // Open the file for writing
        FILE* file = fopen(newPaths[i], "wb");
        if (file == NULL) {
            perror("Failed to open the file for writing");
            exit(EXIT_FAILURE);
        }

        // Receive file data from the sender
        
        size_t bytesReceived;
        while (fileSize > 0) {
            bytesReceived = recv(socket, buffer, BUFFER_SIZE, 0);
            if (bytesReceived == -1) {
                perror("Failed to receive file data");
                exit(EXIT_FAILURE);
            }
            fwrite(buffer, sizeof(char), bytesReceived, file);
            fileSize -= bytesReceived;
        }

        fclose(file);

    }

}

int receiveDirectoryContents(int fd, DirectoryContents *contents) {
    int count;
    if (read(fd, &count, sizeof(count)) == -1) {
        perror("Error reading directory count");
        return -1;
    }

    contents->count = count;
    contents->paths = malloc(count * sizeof(FilePath));

    for (int i = 0; i < count; i++) {
        if (read(fd, &(contents->paths[i]), sizeof(FilePath)) == -1) {
            perror("Error reading directory entry");
            return -1;
        }
    }

    return 0;
}

void freeDirectoryContents(DirectoryContents contents) {
    free(contents.paths);
}

int createDirectory(const char *path) {
    int status = mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (status == -1) {
        if (errno == EEXIST) {
            return 0;  // Directory already exists
        } else {
            perror("Error creating directory");
            return -1;
        }
    }
    return 1;  // Directory created successfully
}

void createSameDirectoryStructure(const char *sourceDirectory, const char *destinationDirectory, DirectoryContents contents){
    // Traverse the files and folders
    char destinationPath[MAX_PATH_LENGTH];
    for (int i = 0; i < contents.count; i++) {
        const char *sourcePath = contents.paths[i].path; //t3.txt

        snprintf(destinationPath, MAX_PATH_LENGTH, "%s/%s", destinationDirectory, sourcePath + strlen(sourceDirectory) + 1); //
       

        if (contents.paths[i].isFile) {
            addStringToArray(destinationPath);

            FILE *destinationFile = fopen(destinationPath, "w");
            if (destinationFile == NULL) {
                perror("Error creating destination file");
                continue;
            }

            fclose(destinationFile);
        } else {
            // Create the directory in the destination directory
            int status = createDirectory(destinationPath);
            if (status == -1) {
                continue;
            }
        }
    }

}

int main(int argc, char *argv[] ) {
    
    if(argc != 4 && argc != 3){
        printf("Usage: %s <directory> <port> <IP> \n", argv[0]);
        printf("or\n");
        printf("Usage: %s <directory> <port> \n", argv[0]);
        exit(1);
    }

    char* IP;
    if(argc == 4){
        IP = argv[3];
    }
    else{
        IP = "127.0.0.1";
    }

    char* directoryPath = argv[1];
    int port = atoi(argv[2]);
    
    base_path = directoryPath;
    clearDirectory(directoryPath);

    newPaths = (char **)malloc(arrayCapacity * sizeof(char *));
    int client_sock;
    struct sockaddr_in server_addr;
    pid_t client_pid = getpid();
    char buffer[BUFFER_SIZE];
    int bytes_sent, bytes_received;
    
    client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sock < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, IP, &(server_addr.sin_addr)) <= 0) {
        perror("Invalid address");
        exit(EXIT_FAILURE);
    }
   
    if (connect(client_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error connecting to the server");
        exit(EXIT_FAILURE);
    }

    printf("Connected to the server\n");

    //read length of the path from server
    int pathLength;
    if (read(client_sock, &pathLength, sizeof(pathLength)) == -1) {
        perror("Error reading path length");
        return -1;
    }

    //read path from server
    char *path = malloc(pathLength * sizeof(char));
    if (read(client_sock, path, pathLength) == -1) {
        perror("Error reading path");
        return -1;
    }

    printf("Path: %s\n", path);
    usleep(250000);

    receiveDirectoryContents(client_sock, &contents);
    createSameDirectoryStructure(path, directoryPath, contents);
    saveFile(client_sock);
    
    
    fileInfos = malloc(MAX_FILES * sizeof(FileModificationInfo));
    glob_count = 0;
    saveFileModificationTimes(directoryPath, fileInfos, &glob_count);
    
    
    checkFileModificationTimes(fileInfos, glob_count, client_sock,directoryPath);      
    
         
    close(client_sock);
    free(fileInfos);
    free(newPaths);
    free(path);
    return 0;
}
