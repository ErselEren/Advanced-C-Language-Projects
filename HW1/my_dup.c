#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

int mydup(int oldfd) {
    // Duplicate the file descriptor using fcntl() with F_DUPFD flag
    int newfd = fcntl(oldfd, F_DUPFD, 0);
    
    // Check if fcntl() failed
    if (newfd == -1) {
        // Error occurred, return -1 and set errno
        // print error message by using errno
        perror("dup");
        return -1;
    }
    
    // Return the new file descriptor
    return newfd;
}

int mydup2(int oldfd, int newfd) {
    int flags, res;

    // Check if newfd is valid by checking if fcntl(newfd, F_GETFD) succeeds.
    if(fcntl(newfd, F_GETFD) != -1){
        // newfd is valid, close it
        close(newfd);
    }

    // oldfd equals newfd 
    if(newfd == oldfd){
        //If oldfd is not valid, F_GETFL will return -1 and set errno to EBADF.        
        // Check if oldfd is valid done by checking if fcntl(oldfd, F_GETFL) succeeds. 
        flags = fcntl(oldfd, F_GETFL); 
        if(flags == -1){ //oldfd is not valid, 
            //set errno and return -1
            errno = EBADF;
            return -1;
        }
        return newfd; // oldfd is valid, return newfd
    }
    
    // Duplicate the file descriptor using fcntl() with F_DUPFD flag
    res = fcntl(oldfd,F_DUPFD,newfd);
    if(res == -1){
        perror("dup2");
        return -1;
    }
    // Return the new file descriptor
    return res;
}


int main(int argc, char *argv[]){

    int fd, fd1;

    //check command line arguments
    if(argc<2 || argc>3){
        printf("Error in command-line arguments. \n");
        return 1;
    }
    
    //get filename from command line
    char* filename = argv[1];

    printf("--------------------------------------------------------------------------\n");
    printf("----------------------------------- Q2 -----------------------------------\n");
    printf("--------------------------------------------------------------------------\n");

    // Open a file for reading and writing with read and write permissions
    fd = open(filename, O_RDWR | O_CREAT , S_IRUSR | S_IWUSR);

    if(fd == -1){ // Check if open() failed
        perror("open syscall");
        return 1;
    }

    // Duplicate the file descriptor
    int newfd = dup(fd);
    if(newfd == -1){
        perror("dup"); // Print the error message
        close(fd); // Close the file descriptor before exiting the program
        return 1;
    }

    // Print the original and new file descriptor values
    printf("\nAfter dup()  -> Original fd : %d ||| new fd : %d\n",fd,newfd);

    // Duplicate the file descriptor to a specific value
    int tempfd = 15;
    int res = dup2(fd,tempfd);
    if(res == -1){ // Check if dup2() failed
        perror("dup2"); // Print the error message
        close(fd); // Close the file descriptor before exiting the program
        return 1;
    }

    // Print the original and new file descriptor values
    printf("\nAfter dup2() -> original fd : %d ||| tempfd : %d\n",fd,tempfd);



    printf("\n--------------------------------------------------------------------------\n");
    printf("----------------------------------- Q3 -----------------------------------\n");
    printf("--------------------------------------------------------------------------\n");
    fd1 = dup(fd); //duplicate fd to fd1
    if(fd1 == -1){ //check if dup() failed
        perror("dup");
        return 1;
    }

    off_t offset;
    offset = lseek(fd, 10, SEEK_SET);  //change the file offset of fd1 by 10 bytes
    if(offset == (off_t) -1){ //check if lseek() failed
        perror("lseek"); 
        return 1;
    }

    printf("Offset after lseek for fd: %lld\n", (long long) offset); //print the file offset of fd

    offset = lseek(fd1, 0, SEEK_CUR); //get the current file offset of fd1
    if(offset == (off_t) -1){ //check if lseek() failed
        perror("lseek");
        return 1;
    }

    printf("Offset for fd1: %lld\n\n\n", (long long) offset); //print the file offset of fd1


    //test mydup() and mydup2() with invalid file descriptors
    int invalidfd = 100;
    int invalidfd2 = 101;
    int invalidfd3 = 102;

    int newfd1 = mydup(invalidfd); //call mydup() with invalid file descriptor
    if(newfd1 == -1){ //check if mydup() failed
        perror("mydup");
    }

    int newfd2 = mydup2(invalidfd2, invalidfd3); //call mydup2() with invalid file descriptors
    if(newfd2 == -1){ //check if mydup2() failed
        perror("mydup2");
    }


    printf("--------------------------------------------------------------------------\n");
    
    // Close file descriptors before exiting the program
    if(close(fd) == -1){ //close fd
        perror("close");
        return 1;
    }

    if(close(newfd) == -1){ //close newfd
        perror("close");
        return 1;
    }

    if(close(tempfd) == -1){ //close tempfd
        perror("close");
        return 1;
    }

    return 0;
}
