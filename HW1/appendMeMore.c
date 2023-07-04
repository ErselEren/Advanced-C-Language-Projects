#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int write_bytes_to_file(char* filename, int num_bytes, int third_arg){
    char byte = 'a';
    
    //Set the flags for opening the file
    //can be written to but not read from and file should be created if it doesn't already exist
    int flags = O_WRONLY | O_CREAT;
    if(third_arg == 0){
        flags = flags | O_APPEND; //the program open the file with the O_APPEND flag
    }

    // Open the file with the specified flags and file permissions
    int fd = open(filename, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if(fd == -1){ // Check for errors in opening the file
        perror("open");
        return 1;
    }

    for(int i=0; i<num_bytes; ++i){
        off_t offset = 0;
        // Perform lseek if the third argument is 'x'
        if(third_arg){
            offset = lseek(fd, 0, SEEK_END);
            if(offset == -1){
                perror("lseek");
                return 1;
            }
        }
        // Write a single byte to the file
        if(write(fd,&byte,1) == -1){
            perror("write");
            return 1;
        }
    }

    // Close the file
    if(close(fd) == -1){
        perror("close");
        return 1;
    }

    return 0;
}

int main(int argc, char* argv[]){
    char* filename;
    int num_bytes, third_arg;

    // Check if the correct number of command line arguments was provided
    if(argc < 3 || argc > 4){
        printf("Error in command-line arguments. \n");
        return 1;
    }

    // Get the command line arguments
    filename = argv[1];
    num_bytes = atoi(argv[2]); // convert the second argument to an integer
    third_arg = (argc == 4 && argv[3][0] == 'x'); // check if the third argument is 'x'

    // Call the function to write bytes to file
    if(write_bytes_to_file(filename, num_bytes, third_arg) == -1){
        perror("write_bytes_to_file");
        return 1;
    }

    return 0;
}
