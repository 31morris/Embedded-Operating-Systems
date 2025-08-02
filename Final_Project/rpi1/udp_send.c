#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define WIDTH 640
#define HEIGHT 480
#define DEST_IP "192.168.222.100"
#define BUFFER_COUNT 4

struct buffer {
    void   *start;
    size_t  length;
};

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <video_device> <dest_port>\n", argv[0]);
        return 1;
    }

    const char *device = argv[1];
    int port = atoi(argv[2]);

    int fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct v4l2_format fmt = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE
    };
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        return 1;
    }

    struct v4l2_requestbuffers req = {
        .count = BUFFER_COUNT,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP
    };
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return 1;
    }

    struct buffer *buffers = calloc(req.count, sizeof(*buffers));
    for (int i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index = i
        };

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            return 1;
        }

        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("mmap");
            return 1;
        }

        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return 1;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = inet_addr(DEST_IP)
    };

    printf("📷 Streaming from %s to %s:%d\n", device, DEST_IP, port);

    while (1) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP
        };

        if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            perror("VIDIOC_DQBUF");
            continue;
        }

        ssize_t sent = sendto(sock, buffers[buf.index].start, buf.bytesused, 0,
                              (struct sockaddr*)&dest_addr, sizeof(dest_addr));

        if (sent > 0) {
            printf("Sent MJPEG frame (%ld bytes) from %s\n", sent, device);
        }

        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF (requeue)");
            break;
        }

        usleep(100000); // ~10 fps
    }

    ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (int i = 0; i < req.count; ++i) {
        munmap(buffers[i].start, buffers[i].length);
    }
    free(buffers);
    close(fd);
    close(sock);
    return 0;
}
