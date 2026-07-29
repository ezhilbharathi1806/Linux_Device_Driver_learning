/***************************************************************************//**
*  \file       epoll_userspace.c
*  \details    epoll user space app — monitors /dev/etx_device
*
*  KEY DIFFERENCES from poll/select apps:
*    poll:   struct pollfd + poll()
*    select: fd_set + FD_ZERO/SET + select()
*    epoll:  epoll_create + epoll_ctl + epoll_wait (3-step setup, then loop)
*
*  ADVANTAGE: fd setup done ONCE before loop (no reinit like select!)
*
*  Build:  gcc -o epoll_app epoll_userspace.c
*  Run:    sudo ./epoll_app
*******************************************************************************/

#include <assert.h>
#include <fcntl.h>
#include <sys/epoll.h>  /* epoll_create, epoll_ctl, epoll_wait, struct epoll_event */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define EPOLL_SIZE  ( 256 )   /* hint for epoll_create (ignored since 2.6.8)   */
#define MAX_EVENTS  (  20 )   /* max events epoll_wait can return per call      */

int main()
{
    char kernel_val[20];
    int  fd, epoll_fd, ret, n;
    struct epoll_event ev;             /* used by epoll_ctl to register fd      */
    struct epoll_event events[20];     /* epoll_wait fills this with ready events */

    /* Open device in non-blocking mode.
     * O_NONBLOCK: read/write won't block — epoll handles waiting. */
    fd = open("/dev/etx_device", O_RDWR | O_NONBLOCK);
    if (fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    /*
     * ── STEP 1: Create epoll instance ──
     * epoll_create(256): creates the kernel epoll object.
     * Returns epoll_fd — used for all subsequent epoll operations.
     * 256 = size hint, ignored by modern kernels (must be > 0).
     * Alternative: epoll_create1(0) — same but cleaner API.
     * Must close(epoll_fd) when done to free kernel resources.
     */
    epoll_fd = epoll_create(EPOLL_SIZE);
    if (epoll_fd < 0) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }

    /*
     * ── STEP 2: Register fd with epoll (done ONCE, before the loop) ──
     * Unlike select (reinit each iteration) or poll (array reused but re-checked),
     * epoll registers fds ONCE and kernel tracks them persistently.
     *
     * ev.data.fd = fd:
     *   Store fd in event's data union. When epoll_wait returns, we use
     *   events[n].data.fd to know WHICH fd triggered the event.
     *
     * ev.events = (EPOLLIN | EPOLLOUT):
     *   Watch for: read-ready (EPOLLIN) AND write-ready (EPOLLOUT).
     *   Default behavior: LEVEL TRIGGERED.
     *   For edge triggered: add EPOLLET → ev.events = (EPOLLIN|EPOLLOUT|EPOLLET)
     *
     * EPOLL_CTL_ADD: register fd in epoll instance.
     *   Once registered, epoll monitors this fd automatically until EPOLL_CTL_DEL.
     *   No need to re-register before each epoll_wait call!
     */
    ev.data.fd = fd;
    ev.events  = (EPOLLIN | EPOLLOUT);  /* watch for read + write readiness */

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev)) {
        perror("Failed to add file descriptor to epoll\n");
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    /* ── MAIN LOOP ── */
    while (1) {
        puts("Starting epoll...");

        /*
         * ── STEP 3: Wait for events ──
         * epoll_wait(epfd, events, maxevents, timeout):
         *   Waits up to 5000ms (5 seconds) for any registered fd to become ready.
         *   On event: fills events[] with ONLY the ready fds — very efficient!
         *
         * KEY ADVANTAGE over poll/select:
         *   poll/select scan ALL registered fds every call → O(n) slow with many fds
         *   epoll_wait returns ONLY ready fds → O(1) regardless of total fd count
         *
         * NO reinit needed (unlike select's FD_ZERO/FD_SET every iteration).
         * events[] is output-only — kernel fills it fresh each call.
         *
         * Returns:
         *   > 0 = number of events filled in events[] (ready fds)
         *     0 = timeout expired → loop continues "Starting epoll..."
         *    -1 = error
         */
        ret = epoll_wait(epoll_fd, events, MAX_EVENTS, 5000);

        if (ret < 0) {
            perror("epoll_wait");
            close(epoll_fd);
            assert(0);
        }

        /*
         * ── STEP 4: Process ready events ──
         * Loop through ONLY the ret ready events — no scanning all fds!
         * events[n].events = what happened on this fd (EPOLLIN or EPOLLOUT)
         * events[n].data.fd = which fd triggered (the fd we stored in ev.data.fd)
         */
        for (n = 0; n < ret; n++) {

            /* Check if this event is EPOLLIN (read ready) */
            if ((events[n].events & EPOLLIN) == EPOLLIN) {
                /* Driver returned POLLIN → data available → read it */
                read(events[n].data.fd, &kernel_val, sizeof(kernel_val));
                printf("EPOLLIN : Kernel_val = %s\n", kernel_val);
            }

            /* Check if this event is EPOLLOUT (write ready) */
            if ((events[n].events & EPOLLOUT) == EPOLLOUT) {
                /* Driver returned POLLOUT → write space available → send data */
                strcpy(kernel_val, "User Space");
                write(events[n].data.fd, &kernel_val, strlen(kernel_val));
                printf("EPOLLOUT : Kernel_val = %s\n", kernel_val);
            }
        }
    }   /* end while(1) — loop restarts "Starting epoll..." after timeout */

    /* Cleanup — close both epoll instance and device fd */
    if (close(epoll_fd)) { perror("Failed to close epoll file descriptor\n"); }
    if (close(fd))       { perror("Failed to close file descriptor\n"); }

    return 0;
}
