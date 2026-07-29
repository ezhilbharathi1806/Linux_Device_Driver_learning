#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct ioctl_arg {
    unsigned int val;
};

#define IOC_MAGIC '\x66'
#define IOCTL_VALSET _IOW(IOC_MAGIC, 0, struct ioctl_arg)
#define IOCTL_VALGET _IOR(IOC_MAGIC, 1, struct ioctl_arg)

int main()
{
    int fd;
    struct ioctl_arg data;

    fd = open("/dev/ioctltest", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Set value
    data.val = 0x55;
    ioctl(fd, IOCTL_VALSET, &data);

    // Get value
    ioctl(fd, IOCTL_VALGET, &data);
    printf("Value from driver: 0x%x\n", data.val);

    close(fd);
    return 0;
}
