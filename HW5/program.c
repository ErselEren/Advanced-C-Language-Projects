#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>

// Buffer size for copying files
#define BUF_SIZE 1024 

#define TABLE_SIZE 10  // Initial size of the hashtable
#define LOAD_FACTOR 0.7 // Load factor threshold for resizing

// Task to be processed by a worker thread
typedef struct{
    int src_fd; // File descriptor for the source file
    int dest_fd; // File descriptor for the destination file
    const char* filename; // Name of the file to be copied
}Task; 

// Statistics about the copy process
typedef struct{
    int regular_files; // Number of regular files copied
    int directories; // Number of directories copied
    int bytes_copied; // Number of bytes copied
}CopyStatistics; 

// Conditional Variables and Mutexes for Synchronization 
pthread_mutex_t buffer_mutex;
pthread_mutex_t output_mutex;
pthread_cond_t buffer_not_full;
pthread_cond_t buffer_not_empty;

int done = 0; // Flag to indicate that the traversal is done
int tasks_completed = 0; // Number of tasks completed
Task *buffer; // Buffer for tasks
int buffer_in = 0; // Index of the next task to be added to the buffer
int buffer_out = 0; // Index of the next task to be processed from the buffer
int buffer_size = 0; // Number of tasks in the buffer
int num_threads = 0; // Number of worker threads
int fifos_copied = 0; // Number of FIFO files copied
CopyStatistics copy_stats = {0, 0, 0}; // Statistics about the copy process

// Function Declarations
void* traverse_directory(void* args);
void* worker_thread(void* arg);
void copy_file(const char* src_path, const char* dest_path);
void add_task_to_buffer(int src_fd, int dest_fd, const char* filename);
void process_task_from_buffer();
void copy_fd(int src_fd, int dest_fd, const char* filename);

// Node structure for storing key-value pairs
typedef struct Node{
    char* key;
    int value;
    struct Node* next;
}Node;

// Hashtable structure
typedef struct Hashtable{
    int size;
    int count;
    Node** buckets;
}Hashtable;

Hashtable* hashtable;

// Hash function to calculate the index for a given key
unsigned int hash(char* key, int size){
    unsigned int hash = 0;
    int len = strlen(key);
    for(int i=0; i<len; i++){
        hash = (hash*31 + key[i]);
    }
    return hash % size;
}

// Create a new node with the given key and value
Node* createNode(char* key, int value){
    Node* newNode = (Node*)malloc(sizeof(Node));
    newNode->key = strdup(key);
    newNode->value = value;
    newNode->next = NULL;
    return newNode;
}

// Initialize a new hashtable
Hashtable* createHashtable(){
    Hashtable* hashtable = (Hashtable*)malloc(sizeof(Hashtable));
    hashtable->size = TABLE_SIZE;
    hashtable->count = 0;
    hashtable->buckets = (Node**)calloc(hashtable->size, sizeof(Node*));
    return hashtable;
}

// Insert a key-value pair into the hashtable
void insert(Hashtable* hashtable, char* key, int value) {
    // Calculate the hash index for the key
    unsigned int index = hash(key, hashtable->size);

    // Check if the key already exists in the hashtable
    Node* currentNode = hashtable->buckets[index];
    while (currentNode != NULL) {
        if (strcmp(currentNode->key, key) == 0) {
            currentNode->value++;
            return;
        }
        currentNode = currentNode->next;
    }

    // Create a new node for the key-value pair
    Node* newNode = createNode(key, value);

    // Insert the new node at the head of the linked list
    newNode->next = hashtable->buckets[index];
    hashtable->buckets[index] = newNode;

    // Increment the count of elements in the hashtable
    hashtable->count++;
}

// Retrieve the value associated with a key in the hashtable
int get(Hashtable* hashtable, char* key) {
    // Calculate the hash index for the key
    unsigned int index = hash(key, hashtable->size);

    // Traverse the linked list at the index and find the matching key
    Node* currentNode = hashtable->buckets[index];
    while (currentNode != NULL) {
        if (strcmp(currentNode->key, key) == 0) {
            return currentNode->value;
        }
        currentNode = currentNode->next;
    }

    // Key not found, return 0 as the default value
    return 0;
}

// Clean up the hashtable and free memory
void destroyHashtable(Hashtable* hashtable) {
    for (int i = 0; i < hashtable->size; i++) {
        Node* currentNode = hashtable->buckets[i];
        while (currentNode != NULL) {
            Node* nextNode = currentNode->next;
            free(currentNode->key);
            free(currentNode);
            currentNode = nextNode;
        }
    }
    free(hashtable->buckets);
    free(hashtable);
}


// Signal handler for Ctrl+C
void handle_signal(int signal) {
    if (signal == SIGINT) {
        printf("Ctrl+C received. Exiting...\n");
        // Add any cleanup code here if needed
        exit(0);
    }
}

// Traverses a directory and adds tasks to the buffer for the worker threads to process 
void* traverse_directory(void* args){ 

    // Get the source and destination directories from the arguments
    const char* source_dir = ((const char**)args)[0];
    const char* dest_dir = ((const char**)args)[1];
    pthread_t thread_id = pthread_self();
    struct dirent* entry;
    char src_path[1024];
    char dest_path[1024];
    struct stat st;

    // Open the source directory
    DIR* dir = opendir(source_dir);
    if(dir == NULL){ // Check if the directory exists and can be opened 
        pthread_mutex_lock(&output_mutex);
        fprintf(stderr, "Failed to open directory: %s\n", source_dir);
        pthread_mutex_unlock(&output_mutex);
        pthread_exit((void*)thread_id);
    }

    
    while((entry = readdir(dir)) != NULL){ // Iterate over the entries in the directory     
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) { // Skip . and ..
            continue;
        }

        // Create the source and destination paths for the entry within the source and destination directories
        snprintf(src_path, sizeof(src_path), "%s/%s", source_dir, entry->d_name);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, entry->d_name);
        
        if(stat(src_path, &st) < 0){ // Get file/directory information
            pthread_mutex_lock(&output_mutex);
            fprintf(stderr, "Failed to get file/directory information: %s\n", src_path);
            pthread_mutex_unlock(&output_mutex);
            continue;
        }

        if(S_ISDIR(st.st_mode)){ // If the entry is a directory, create a new thread to traverse it
            // Create the destination subdirectory within the destination directory
            char dest_subdir[1024];
            snprintf(dest_subdir, sizeof(dest_subdir), "%s/%s", dest_dir, entry->d_name);
            int mkdir_result = mkdir(dest_subdir, 0755);
            if(mkdir_result != 0 && errno != EEXIST){ // If the directory already exists, continue
                pthread_mutex_lock(&output_mutex);
                fprintf(stderr, "Failed to create directory: %s\n", dest_subdir);
                pthread_mutex_unlock(&output_mutex);
                continue;
            }

            // Directory created/opened, traverse it
            const char* sub_dir_args[2] = {src_path, dest_subdir}; // Arguments for the new thread
            pthread_t thread;
            if(pthread_create(&thread, NULL, traverse_directory, (void*)sub_dir_args) != 0){ // Create a new thread to traverse the subdirectory 
                pthread_mutex_lock(&output_mutex);
                fprintf(stderr, "Failed to create thread for directory: %s\n", src_path);
                pthread_mutex_unlock(&output_mutex);
                continue;
            }
            
            // Wait for the thread to finish
            pthread_join(thread, NULL);
            pthread_mutex_lock(&output_mutex);
            copy_stats.directories++;
            pthread_mutex_unlock(&output_mutex);
        }
        else if(S_ISREG(st.st_mode)){ // Regular file, add it to the buffer
            // Regular file, add it to the buffer
            int src_fd = open(src_path, O_RDONLY);
            if(src_fd < 0){ // If the file cannot be opened, skip it
                pthread_mutex_lock(&output_mutex);
                fprintf(stderr, "Failed to open file: %s\n", src_path);
                pthread_mutex_unlock(&output_mutex);
                continue;
            }

            int dest_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644); // Create the destination file within the destination directory
            if(dest_fd < 0){ // If the file cannot be created, skip it
                pthread_mutex_lock(&output_mutex);
                fprintf(stderr, "Failed to create file: %s\n", dest_path);
                pthread_mutex_unlock(&output_mutex);
                close(src_fd);
                continue;
            }
            
            char* extension = strrchr(entry->d_name, '.');
            char* fileExtension;
            if(extension == NULL){ // If the file has no extension, set the extension to "unidentified file"
                fileExtension = strdup("unidentified file");
            } 
            else{
                extension++; // Skip the dot in the extension
                fileExtension = strdup(extension);
            }

            // Insert the file type into the hashtable
            insert(hashtable, fileExtension, get(hashtable, fileExtension) + 1);

            add_task_to_buffer(src_fd, dest_fd, dest_path);
            pthread_mutex_lock(&output_mutex);    
            copy_stats.regular_files++;
            free(fileExtension);
            pthread_mutex_unlock(&output_mutex);
            
        } 
        else if(S_ISFIFO(st.st_mode)){
            // Check if FIFO file already exists
            if(access(dest_path, F_OK) == 0){
                // FIFO file exists, delete it
                if(unlink(dest_path) != 0){ // Delete the FIFO file in the destination directory 
                    pthread_mutex_lock(&output_mutex);
                    fprintf(stderr, "Failed to delete existing FIFO: %s\n", dest_path);
                    pthread_mutex_unlock(&output_mutex);
                    continue;
                }
            }

            // Create FIFO in the destination directory
            if(mkfifo(dest_path, st.st_mode) < 0){ // Create the FIFO file in the destination directory
                pthread_mutex_lock(&output_mutex);
                fprintf(stderr, "Failed to create FIFO: %s\n", dest_path);
                pthread_mutex_unlock(&output_mutex);
                continue;
            }

            pthread_mutex_lock(&output_mutex);
            insert(hashtable, "fifo", get(hashtable, "fifo") + 1);
            fifos_copied++;  // Increment the count of copied FIFO files
            pthread_mutex_unlock(&output_mutex);
        }
    }
    closedir(dir); // Close the directory
    pthread_exit((void*)thread_id); // Exit the thread
}


// Worker thread function (used for copying regular files) 
void* worker_thread(void* arg){ 
    pthread_t thread_id = pthread_self(); 

    while(1){ // Loop until the thread is terminated by the main thread 
        pthread_mutex_lock(&buffer_mutex);

        while(buffer_in == buffer_out && !done){ // Wait until the buffer is not empty 
            pthread_cond_wait(&buffer_not_empty, &buffer_mutex);
        }

        if(done && buffer_in == buffer_out){ // If the buffer is empty and the main thread is done, terminate the thread 
            pthread_mutex_unlock(&buffer_mutex);
            pthread_cond_signal(&buffer_not_full);
            break;
        }

        process_task_from_buffer(); // Process the task at the front of the buffer
        pthread_cond_signal(&buffer_not_full); // Signal that the buffer is not full
        pthread_mutex_unlock(&buffer_mutex); // Unlock the buffer mutex
    }

    pthread_exit((void*)thread_id);
}

// Copy a file from src_path to dest_path (used for copying FIFO files)
void copy_file(const char* src_path, const char* dest_path){
    struct stat st;
    if(lstat(src_path, &st) < 0){ // Get file/directory information
        pthread_mutex_lock(&output_mutex);
        fprintf(stderr, "Failed to get file/directory information: %s\n", src_path);
        pthread_mutex_unlock(&output_mutex);
        return;
    }

    //printf("Copying %s to %s\n", src_path, dest_path);

    if(S_ISFIFO(st.st_mode)){
        // If source is a FIFO, create a FIFO in the destination directory
        //printf("FIFO found: %s\n", src_path);
        if(mkfifo(dest_path, st.st_mode) < 0){
            pthread_mutex_lock(&output_mutex);
            fprintf(stderr, "Failed to create FIFO: %s\n", dest_path);
            pthread_mutex_unlock(&output_mutex);
            return;
        }
        return;
    }

    // Proceed with regular file copying
    int src_fd = open(src_path, O_RDONLY);
    if(src_fd < 0){ // If the file does not exist
        pthread_mutex_lock(&output_mutex);
        fprintf(stderr, "Failed to open file: %s\n", src_path);
        pthread_mutex_unlock(&output_mutex);
        return;
    }

    int dest_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    if(dest_fd < 0){ // If the file already exists 
        pthread_mutex_lock(&output_mutex);
        fprintf(stderr, "Failed to create file: %s\n", dest_path);
        pthread_mutex_unlock(&output_mutex);
        close(src_fd);
        return;
    }

    copy_fd(src_fd, dest_fd, dest_path); 
    
    // Close the file descriptors
    close(src_fd);
    close(dest_fd);
}


// Add a task to the buffer (copy a file)
void add_task_to_buffer(int src_fd, int dest_fd, const char* filename){
    pthread_mutex_lock(&buffer_mutex);

    while(((buffer_in + 1) % buffer_size) == buffer_out)
        pthread_cond_wait(&buffer_not_full, &buffer_mutex);
    

    buffer[buffer_in].src_fd = src_fd;
    buffer[buffer_in].dest_fd = dest_fd;
    buffer[buffer_in].filename = filename;
    buffer_in = (buffer_in + 1) % buffer_size;

    pthread_cond_signal(&buffer_not_empty);
    pthread_mutex_unlock(&buffer_mutex);
}


// Process a task from the buffer (copy a file) and update the statistics accordingly 
void process_task_from_buffer(){
    int src_fd = buffer[buffer_out].src_fd;
    int dest_fd = buffer[buffer_out].dest_fd;
    const char* filename = buffer[buffer_out].filename;
    buffer_out = (buffer_out + 1) % buffer_size;
    copy_fd(src_fd, dest_fd, filename);

    close(src_fd);
    close(dest_fd);
}


// Copy data from src_fd to dest_fd and update the statistics accordingly 
void copy_fd(int src_fd, int dest_fd, const char* filename){
    char buf[BUF_SIZE];
    ssize_t bytesRead, bytesWritten;

    while((bytesRead = read(src_fd, buf, BUF_SIZE)) > 0){
        bytesWritten = write(dest_fd, buf, bytesRead);
        if(bytesWritten < 0){
            pthread_mutex_lock(&output_mutex);
            fprintf(stderr, "Failed to write to file: %s\n", filename);
            pthread_mutex_unlock(&output_mutex);
            break;
        }

        pthread_mutex_lock(&output_mutex);
        copy_stats.bytes_copied += bytesWritten;
        pthread_mutex_unlock(&output_mutex);
    }

    if(bytesRead < 0){
        pthread_mutex_lock(&output_mutex);
        fprintf(stderr, "Failed to read from file: %s\n", filename);
        pthread_mutex_unlock(&output_mutex);
    }
}

// Get the last folder of a path (e.g. "/home/user/file.txt" -> "file.txt") 
char* getLastFolder(const char* path){
    // Check if the path is empty or NULL
    if(path == NULL || strlen(path) == 0){
        return NULL;
    }

    // Find the last occurrence of the '/' character
    char* lastSlash = strrchr(path, '/');

    // Check if a slash was found
    if(lastSlash == NULL){
        // If no slash found, return a copy of the original path
        return strdup(path);
    }

    // Extract the substring after the last slash
    char* lastFolder = strdup(lastSlash + 1);
    
    return lastFolder;
}

int main(int argc, char* argv[]){
    hashtable = createHashtable();
    struct sigaction sa;

    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    // Register the signal handler
    if(sigaction(SIGINT, &sa, NULL) == -1){
        printf("Error registering signal handler.\n");
        return 1;
    }

    // Check the number of arguments passed to the program and print an error message if it is not correct
    if(argc < 5){
        fprintf(stderr, "Usage: %s <buffer_size> <num_threads> <source_directory> <destination_directory>\n", argv[0]);
        return 1;
    }

    // Parse the arguments passed to the program and print an error message if they are not correct
    buffer_size = atoi(argv[1]);
    num_threads = atoi(argv[2]);
    const char* source_dir = argv[3]; // Source directory path name 
    const char* dest_dir = argv[4]; // Destination directory path name


    buffer = (Task*)malloc(buffer_size * sizeof(Task));
    if(buffer == NULL){
        fprintf(stderr, "Failed to allocate memory for the buffer.\n");
        return 1;
    }

    struct timeval start_time, end_time;
    long seconds, microseconds;

    // Get the starting time
    gettimeofday(&start_time, NULL);

    char dest_path[BUF_SIZE]; // Destination path name

    // Create the destination directory
    if(mkdir(dest_dir, 0755) == -1){
        if (errno == EEXIST){
            // Get the last folder name from the source directory path
            char* lastFolder = getLastFolder(source_dir); 
            
            // Append the last folder name to the destination directory path
            snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, lastFolder); 
            dest_dir = dest_path;
            
            if(mkdir(dest_path, 0755) == -1){
                if(errno == EEXIST){
                    //printf("2Directory already exists.\n");
                    //strncat(dest_path, "/HW5", sizeof(dest_path) - strlen(dest_path) - 1);
                    //printf("2dest_path: %s\n", dest_path);
                }
                else{
                    printf("Failed to create directory. Error code: %d\n", errno);
                    return 1;
                }
            }
        } 
        else{
            printf("Failed to create directory. Error code: %d\n", errno);
            return 1;
        }
    } 
    else{
        printf("Directory created successfully.\n");
        snprintf(dest_path, sizeof(dest_path), "%s", dest_dir);
    }

    // Create the threads array and initialize the threads with the worker_thread function 
    pthread_t threads[num_threads];
    
    // Initialize the mutexes
    pthread_mutex_init(&buffer_mutex, NULL);
    pthread_mutex_init(&output_mutex, NULL);
    
    // Initialize the condition variables
    pthread_cond_init(&buffer_not_full, NULL);
    pthread_cond_init(&buffer_not_empty, NULL);

    
    const char* args[2] = {source_dir, dest_path}; // Arguments to be passed to the root thread
    
    pthread_t root_thread;
    if(pthread_create(&root_thread, NULL, traverse_directory, (void*)args) != 0){
        fprintf(stderr, "Failed to create root thread for directory: %s\n", source_dir);
        return 1;
    }

    for(int i=0; i<num_threads; i++){
        if(pthread_create(&threads[i], NULL, worker_thread, NULL) != 0){
            fprintf(stderr, "Failed to create worker thread %d\n", i);
            return 1;
        }
    }

    pthread_join(root_thread, NULL);

    pthread_mutex_lock(&output_mutex);
    done = 1;
    pthread_mutex_unlock(&output_mutex);
    pthread_cond_broadcast(&buffer_not_empty);

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&buffer_mutex);
    pthread_mutex_destroy(&output_mutex);
    pthread_cond_destroy(&buffer_not_full);
    pthread_cond_destroy(&buffer_not_empty);

    printf("Copy complete\n");
    printf("Regular files copied: %d\n", copy_stats.regular_files);
    printf("Directories copied: %d\n", copy_stats.directories);
    printf("Total bytes copied: %d\n", copy_stats.bytes_copied);

    
    // Get the ending time
    gettimeofday(&end_time, NULL);

    // Calculate the elapsed time
    seconds = end_time.tv_sec - start_time.tv_sec;
    microseconds = end_time.tv_usec - start_time.tv_usec;

    // If the microseconds are negative, adjust the values
    if(microseconds < 0){
        seconds--;
        microseconds += 1000000;
    }

    printf("Elapsed time: %ld seconds %ld microseconds\n", seconds, microseconds);

    printf("\n\n>>>>>>>>>>>>>>>>>>> File types <<<<<<<<<<<<<<<<<<<<< \n");
    // Print the total count of each file type
    Node* currentNode;
    for(int i=0; i<hashtable->size;i++){
        currentNode = hashtable->buckets[i];
        while(currentNode != NULL){
            printf("%s = %d\n", currentNode->key, currentNode->value);
            currentNode = currentNode->next;
        }
    }

    destroyHashtable(hashtable);

    return 0;
}
