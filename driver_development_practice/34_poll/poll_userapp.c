/***************************************************************************//**
*  \file       poll_userspace.c
*
*  \details    User space poll application
*  Monitors /dev/etx_device with poll() — reads or writes when driver signals.
*
*  Build:  gcc -o poll_app poll_userspace.c
*  Run:    sudo ./poll_app
*******************************************************************************/

#include <assert.h>
#include <fcntl.h>
#include <poll.h>     /* poll(), struct pollfd, POLLIN, POLLOUT              */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>   /* read(), write()                                     */
#include <string.h>

int main()
{
    char kernel_val[20];
    int fd, ret, n;
    struct pollfd pfd;

    /* Open /dev/etx_device in non-blocking mode.
     * O_NONBLOCK: open() doesn't block waiting for device.
     * Also means read/write won't block if not ready.
     * Required when using poll() — blocking fd with poll is unnecessary. */
    fd = open("/dev/etx_device", O_RDWR | O_NONBLOCK);

    if (fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    /* Configure struct pollfd — which fd to monitor and what events to watch */
    pfd.fd = fd;
    pfd.events = (POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM);
    /*
     * POLLIN  | POLLRDNORM = watch for data available to read
     * POLLOUT | POLLWRNORM = watch for write space available
     * Both: we want notifications for either read OR write readiness.
     */

    while (1) {
        puts("Starting poll...");

        /*
         * poll(&pfd, 1, 5000):
         *   Monitor 1 file descriptor (pfd) for up to 5000ms (5 seconds).
         *   If driver calls wake_up() before 5s → poll returns immediately.
         *   If no event in 5 seconds → timeout → returns 0 → loop restarts.
         *
         *   Return values:
         *     > 0 : number of fds with events (check pfd.revents)
         *       0 : timeout expired, nothing happened → loop restarts
         *      -1 : error
         */
        ret = poll(&pfd, (unsigned long)1, 5000);

        if (ret < 0) {
            perror("poll");
            assert(0);
        }

        /*
         * Check pfd.revents — kernel wrote here what actually happened.
         * Bitwise AND to check if specific event occurred.
         */

        /* POLLIN: driver has data ready to read → call read() */
        if ((pfd.revents & POLLIN) == POLLIN) {
            read(pfd.fd, &kernel_val, sizeof(kernel_val));   /* read from driver */
            printf("POLLIN : Kernel_val = %s\n", kernel_val);
        }

        /* POLLOUT: driver has space and wants us to write → call write() */
        if ((pfd.revents & POLLOUT) == POLLOUT) {
            strcpy(kernel_val, "User Space");                /* prepare data     */
            write(pfd.fd, &kernel_val, strlen(kernel_val)); /* write to driver  */
            printf("POLLOUT : Kernel_val = %s\n", kernel_val);
        }
        /* If neither happened → poll timed out → loop back to "Starting poll..." */
    }
}
