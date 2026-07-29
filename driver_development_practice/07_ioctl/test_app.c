/***************************************************************************//**
*  \file       test_app.c
*
*  \details    Userspace application to test the Device driver
*
*******************************************************************************/

/* ── HEADERS ─────────────────────────────────────────────────────────────── */
#include <stdio.h>        /* printf, scanf                                     */
#include <stdlib.h>       /* exit()                                            */
#include <string.h>       /* string utilities                                  */
#include <sys/types.h>    /* type definitions for system calls                 */
#include <sys/stat.h>     /* file status flags                                 */
#include <fcntl.h>        /* open(), O_RDWR                                    */
#include <unistd.h>       /* close() and the header that lets your program talk directly to the operating system read(), write(), close()*/
#include <sys/ioctl.h>    /* ioctl() system call — REQUIRED for IOCTL          */


/* ── IOCTL COMMAND DEFINITIONS ───────────────────────────────────────────── */
/*
 * These MUST be identical to the definitions in driver.c.
 * The same magic number ('a'), command numbers ('a','b'), and type (int32_t*)
 * ensure that the numeric value of the command matches between driver and app.
 *
 * If these don't match → ioctl() sends a wrong command number → driver's
 * switch() hits the default case → command is silently ignored.
 */
#define WR_VALUE _IOW('a','a',int32_t*)		//command to write value to driver
#define RD_VALUE _IOR('a','b',int32_t*)		//command to read value from driver
 
int main()
{
	int     fd;       /* file descriptor for the device file               */
        int32_t value;    /* holds the value read back FROM the kernel driver  */
        int32_t number;   /* holds the value typed by user → sent TO kernel    */

        printf("*********************************\n");

        printf("\nOpening Driver\n");
        fd = open("/dev/etx_device", O_RDWR);	//This triggers etx_open() in the kernel driver.
        if(fd < 0) {
                printf("Cannot open device file...\n");
                return 0;
        }
 	
	/* Get a number from the user to send to the kernel driver */
        printf("Enter the Value to send\n");
        scanf("%d",&number);
        printf("Writing Value to Driver\n");
        ioctl(fd, WR_VALUE, (int32_t*) &number); 

	/* Read the value back from the kernel driver using RD_VALUE IOCTL. */
        printf("Reading Value from Driver\n");
        ioctl(fd, RD_VALUE, (int32_t*) &value);
        printf("Value is %d\n", value);
 
        printf("Closing Driver\n");
        close(fd);	 /* Close the device file — triggers etx_release() in the kernel driver */
}
