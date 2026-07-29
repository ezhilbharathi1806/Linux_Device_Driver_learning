/***************************************************************************//**
*  \file       test_app.c
*  \details    User space app — receives signal from kernel driver
*
*  HOW IT WORKS:
*    1. Install signal handlers (SIGETX and SIGINT)
*    2. Open /dev/etx_device
*    3. Call ioctl(REG_CURRENT_TASK) → register with driver
*    4. Loop waiting for signal from driver
*    5. When driver sends SIGETX (from cat or read) → print the value
*    6. Press Ctrl+C → cleanup and exit
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>           /* sigaction, struct sigaction, SIGINT          */

/* Must match driver definitions exactly */
#define REG_CURRENT_TASK  _IOW('a', 'a', int32_t*)
#define SIGETX            44

/* ── GLOBAL FLAGS ─────────────────────────────────────────────────────────── */
static int done  = 0;   /* set to 1 by Ctrl+C → exits the while loop        */
int        check = 0;   /* set to 1 by SIGETX signal → breaks inner wait     */


/*
 * ctrl_c_handler() — handles SIGINT (Ctrl+C)
 * SA_RESETHAND: after this handler runs once, SIGINT is reset to default.
 * This allows a second Ctrl+C to force-kill if needed.
 */
void ctrl_c_handler(int n, siginfo_t *info, void *unused)
{
    if(n == SIGINT) {
        printf("\nReceived Ctrl+C — exiting...\n");
        done = 1;   /* signal main loop to exit                             */
    }
}

/*
 * sig_event_handler() — handles our custom signal SIGETX (44)
 * Called by the OS when the kernel driver sends signal 44 to this process.
 *
 * @n    : signal number (44 here)
 * @info : siginfo_t containing extra data from the driver:
 *         info->si_int = the si_int value set in kernel (1 in our driver)
 */
void sig_event_handler(int n, siginfo_t *info, void *unused)
{
    if(n == SIGETX) {
        check = info->si_int;   /* read integer payload from kernel          */
        printf("Received signal from kernel : Value = %u\n", check);
    }
}


int main()
{
    int fd;
    int32_t number;
    struct sigaction act;

    printf("*********************************\n");
    printf("*******WWW.EmbeTronicX.com*******\n");
    printf("*********************************\n");

    /*
     * Install Ctrl+C handler (SIGINT).
     * SA_SIGINFO: use sa_sigaction (3-arg handler) instead of sa_handler.
     * SA_RESETHAND: handler is reset to default after first call.
     * sigemptyset: don't block any other signals while handler runs.
     */
    sigemptyset(&act.sa_mask);
    act.sa_flags     = SA_SIGINFO | SA_RESETHAND;
    act.sa_sigaction = ctrl_c_handler;
    sigaction(SIGINT, &act, NULL);

    /*
     * Install custom signal handler for SIGETX (44).
     * SA_RESTART: automatically restart interrupted system calls.
     * SA_SIGINFO: use the 3-argument handler form (gets siginfo_t).
     */
    sigemptyset(&act.sa_mask);
    act.sa_flags     = SA_SIGINFO | SA_RESTART;
    act.sa_sigaction = sig_event_handler;
    sigaction(SIGETX, &act, NULL);

    printf("Installed signal handler for SIGETX = %d\n", SIGETX);

    /* Open the device file */
    printf("\nOpening Driver\n");
    fd = open("/dev/etx_device", O_RDWR);
    if(fd < 0) {
        printf("Cannot open device file...\n");
        return 0;
    }

    /*
     * Register this process with the driver using IOCTL.
     * Triggers etx_ioctl(REG_CURRENT_TASK) in driver:
     *   → task = get_current() → driver stores our task_struct *
     * From this point, driver knows to send signals to THIS process.
     */
    printf("Registering application ...");
    if(ioctl(fd, REG_CURRENT_TASK, (int32_t *) &number)) {
        printf("Failed\n");
        close(fd);
        exit(1);
    }
    printf("Done!!!\n");

    /*
     * Main wait loop — runs until Ctrl+C sets done=1.
     *
     * Inner loop: busy-waits until either:
     *   - done=1 (Ctrl+C) → outer loop also exits
     *   - check=1 (signal received from kernel) → print done, wait again
     *
     * In production: use pause() or sigsuspend() instead of busy-wait.
     * Busy-wait burns CPU — fine for demo, bad for production.
     */
    while(!done) {
        printf("Waiting for signal...\n");

        /* Busy-wait: spin until signal arrives or Ctrl+C */
        while(!done && !check);

        check = 0;   /* reset flag — ready to receive next signal           */
    }

    printf("Closing Driver\n");
    close(fd);   /* triggers etx_release() in driver → clears task pointer  */
    return 0;
}
