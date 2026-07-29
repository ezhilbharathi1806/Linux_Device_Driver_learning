/***************************************************************************//**
*  \details    Userspace application to test the Device driver

ubuntu@primary:~/ldd/06_real_device_driver$ gcc test_app.c -o test_app
ubuntu@primary:~/ldd/06_real_device_driver$ sudo ./test_app 

or implement the below example in the main 

int main() {
    if (chmod("/dev/etx_device", 0666) == -1) {
        perror("chmod failed");
        return 1;
    }

    printf("Permissions changed successfully\n");
    return 0;
}
*******************************************************************************/

/* ── HEADERS ─────────────────────────────────────────────────────────────── */
#include <stdio.h>       /* printf, scanf                                      */
#include <stdlib.h>      /* exit()                                             */
#include <string.h>      /* strlen()                                           */
#include <sys/types.h>   /* required types for open()                          */
#include <sys/stat.h>    /* required for open() flags                          */
#include <fcntl.h>       /* open(), O_RDWR                                     */
#include <unistd.h>      /* read(), write(), close()                           */

int8_t write_buf[1024];		//write_buf: holds data entered by user → will be sent TO the kernel driver
int8_t read_buf[1024];		//read_buf:  holds data received FROM the kernel driver → printed on screen

int main()
{
        int fd;		 /* file descriptor — returned by open(), used by read/write/close */
        char option;	// stores user's menu choice: 1,2 or 3
        printf("*********************************\n");

	/* Open the device file for both reading and writing.
         * /dev/etx_device is the device node created by udev when the driver loaded.
         * O_RDWR = Open for both Read and Write.
         * open() returns a file descriptor (fd >= 0) on success, -1 on failure.
         *
         * Under the hood, this triggers etx_open() in the kernel driver.
         */
        fd = open("/dev/etx_device", O_RDWR);
        if(fd < 0) {
                printf("Cannot open device file...\n");
                return 0;
        }

        while(1) {
                printf("****Please Enter the Option******\n");
                printf("        1. Write               \n");
                printf("        2. Read                 \n");
                printf("        3. Exit                 \n");
                printf("*********************************\n");
                scanf(" %c", &option);
                printf("Your Option = %c\n", option);
                
                switch(option) {
			/* case '1'  WRITE: send data from user to kernel driver ── */
			case '1':
                                printf("Enter the string to write into driver :");
                                scanf("  %[^\t\n]s", write_buf);	//Read a full line from user (stops at newline or tab)
                                printf("Data Writing ...");
                                write(fd, write_buf, strlen(write_buf)+1);	//write() is the system call that triggers etx_write() in the driver.
                                printf("Done!\n");
                                break;
			/*case '2'  READ: receive data from kernel driver ── */
                        case '2':
                                printf("Data Reading ...");
                                read(fd, read_buf, 1024);	//read() is the system call that triggers etx_read() in the driver.
                                printf("Done!\n\n");
                                printf("Data = %s\n\n", read_buf);
                                break;
			 /*case '3' EXIT: close the device and quit ── */
                        case '3':
                                close(fd);
                                exit(1);	/* terminate the application */
                                break;
                        default:
                                printf("Enter Valid option = %c\n",option);
                                break;
                }
        }
        close(fd);	/* Safety close — reached only if the while loop exits unexpectedly.*/
}
