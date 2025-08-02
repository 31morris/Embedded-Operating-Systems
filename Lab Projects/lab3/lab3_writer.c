#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <student ID>\n", argv[0]);
        return 1;
    }
    
    char *student_id = argv[1]; 

    int fd;

    // Open the device driver file
    fd = open("/dev/etx_device", O_RDWR);

    if (fd < 0) {
        perror("Failed to open the driver");
        return -1;
    }

    // Send the student ID to the driver
    if (write(fd, student_id, strlen(student_id)) < 0) {
        perror("Failed to write student ID to driver");
        close(fd);
        return -1;
    }

    // Close the device driver file
    close(fd);

    return 0;
}