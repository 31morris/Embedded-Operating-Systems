#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <string>\n", argv[0]);
        return 1;
    }

    char *input_string = argv[1];
    int string_length = strlen(input_string);
    printf("string_length = %d\n", string_length);

    int fd;

    // Open the  driver file
    fd = open("/dev/mydev", O_RDWR);

    if (fd < 0) {
        perror("Failed to open the driver");
        return -1;
    }

    // Send one letter every second
    for (int i = 0; i < string_length; i++) {
        // Send the current letter to the driver
        if (write(fd, &input_string[i], 1) < 0) {
            perror("Failed to write letter to driver");
            close(fd);
            return -1;
        }

        printf("Sent letter: %c\n", input_string[i]);

        // Sleep for 1 second
        sleep(1);
    }

    /// Close the driver file
    close(fd);
    printf("sent letter complete.\n");

    return 0;
}