/***************************************************************************//**
*  \file       select_userspace.c
*  \details    select() user space app — monitors /dev/etx_device
*
*  KEY DIFFERENCES from poll app:
*    poll uses: struct pollfd, pfd.events, pfd.revents, poll()
*    select uses: fd_set, FD_ZERO, FD_SET, FD_ISSET, select()
*
*  Build:  gcc -o select_app select_userspace.c
*  Run:    sudo ./select_app
*******************************************************************************/

#include <assert.h>
#include <fcntl.h>
#include <poll.h>     /* still needed for some constants */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>   /* read(), write()                  */
#include <string.h>

int main()
{
    char   kernel_val[20];
    fd_set read_fd, write_fd;   /* fd sets — one for read events, one for write */
    struct timeval timeout;      /* how long select() will wait                  */
    int    ret;

    /* Open /dev/etx_device in non-blocking mode.
     * O_NONBLOCK: ensures read/write don't block if device not ready.
     * select() handles the waiting — device should be non-blocking. */
    int fd = open("/dev/etx_device", O_RDWR | O_NONBLOCK);

    if (fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    while (1) {
        puts("Starting Select...");

        /*
         * ── STEP 1: Initialize fd_sets ──
         * MUST reinitialize EVERY loop iteration because:
         *   select() CLEARS the sets on return — removes unready fds.
         *   So after each select() call, the sets are partially or fully cleared.
         *   Without reinit, next iteration monitors nothing → always timeout.
         *
         * FD_ZERO: clears ALL bits in the fd_set (removes all fds from set).
         * FD_SET: adds our fd to the set (sets its bit in the bitmask).
         */
        FD_ZERO(&read_fd);       /* clear read set              */
        FD_SET(fd, &read_fd);    /* add fd to read set          */
        FD_ZERO(&write_fd);      /* clear write set             */
        FD_SET(fd, &write_fd);   /* add fd to write set         */

        /*
         * ── STEP 2: Initialize timeout ──
         * MUST reinitialize EVERY loop iteration because:
         *   select() leaves timeout value UNDEFINED after returning.
         *   (On Linux it's modified to show remaining time, but POSIX says undefined.)
         *   So always reset before calling select() again.
         *
         * tv_sec = 5, tv_usec = 0 → wait up to 5 seconds.
         */
        timeout.tv_sec  = 5;   /* 5 seconds                */
        timeout.tv_usec = 0;   /* 0 microseconds           */

        /*
         * ── STEP 3: Call select() ──
         * Monitors fd for readability AND writability simultaneously.
         *
         * FD_SETSIZE = 1024 (safe upper bound for nfds).
         * Alternatively: use (fd + 1) as nfds for efficiency.
         *   FD_SETSIZE tells select() to scan bits 0 to 1023.
         *   Using (fd+1) instead: scan only 0 to fd — faster.
         *
         * Why NOT just use fd+1 instead of FD_SETSIZE?
         *   FD_SETSIZE is safer when you're not sure of the max fd.
         *   But for a single fd, (fd+1) is more efficient.
         *
         * Internally: select() calls the driver's etx_poll() via poll_wait().
         * Same as poll() — no difference at the kernel driver level.
         *
         * Returns:
         *   > 0 = number of ready fds (at least one set has a ready fd)
         *     0 = timeout expired, nothing ready → loop continues
         *    -1 = error
         */
        ret = select(FD_SETSIZE, &read_fd, &write_fd, NULL, &timeout);

        if (ret < 0) {
            perror("select");
            assert(0);   /* abort on error */
        }

        /*
         * ── STEP 4: Check results with FD_ISSET ──
         * After select() returns, readfds and writefds contain ONLY the
         * fds that are actually ready. All unready fds were removed by kernel.
         *
         * FD_ISSET(fd, &read_fd):
         *   Returns non-zero if fd is STILL in read_fd (= ready to read).
         *   Returns 0 if fd was removed (= not ready for reading).
         *
         * Same logic for write_fd.
         */

        /* Check if our fd is ready for READING */
        if (FD_ISSET(fd, &read_fd)) {
            /* Driver returned POLLIN → read data from /dev/etx_device */
            read(fd, &kernel_val, sizeof(kernel_val));
            printf("READ : Kernel_val = %s\n", kernel_val);
        }

        /* Check if our fd is ready for WRITING */
        if (FD_ISSET(fd, &write_fd)) {
            /* Driver returned POLLOUT → write data to /dev/etx_device */
            strcpy(kernel_val, "User Space");
            write(fd, &kernel_val, strlen(kernel_val));
            printf("WRITE : Kernel_val = %s\n", kernel_val);
        }

        /* If neither fd was ready → timeout occurred → loop prints "Starting Select..." */
    }

    return 0;
}
