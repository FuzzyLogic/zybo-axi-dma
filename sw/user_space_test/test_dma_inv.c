#include <stdio.h>      // printf
#include <fcntl.h>      // open
#include <unistd.h>     // close
#include <sys/ioctl.h>  // ioctl
#include <sys/mman.h>   // mmap/munmap
#include <stdlib.h>     // malloc/free
#include "test_dma_inv.h"

int main(void) {
    printf("---Starting DMA inversion tests---\n");

    int i;
    for (i = 0; i < NUM_TESTS; i++) {
        if (test_cases[i].func()) {
            printf("Error executing test \"%s\"\n", test_cases[i].name);
            return -1;
        } else
            printf("Test case \"%s\" passed\n", test_cases[i].name);
    }

    printf("---All tests successfully passed---\n");
    return 0;
}


/************************************************************************************
* Test case definitions
************************************************************************************/

// Try to call open the maximum amount of times
int test_max_open(void) {
    int fd1, fd2, fd3, fd4, fd5;

    // These should all be successful
    fd1 = 0;
    fd1 = open("/dev/dma_proxy", O_RDWR);
    if (!fd1)
        return -1;

    fd2 = 0;
    fd2 = open("/dev/dma_proxy", O_RDWR);
    if (!fd2)
        return -1;

    fd3 = 0;
    fd3 = open("/dev/dma_proxy", O_RDWR);
    if (!fd3)
        return -1;

    fd4 = 0;
    fd4 = open("/dev/dma_proxy", O_RDWR);
    if (!fd4)
        return -1;

    // This one should fail
    fd5 = 0;
    fd5 = open("/dev/dma_proxy", O_RDWR);
    if (fd5 >= 0)
        return -1;

    // Close all
    close(fd1);
    close(fd2);
    close(fd3);
    close(fd4);
    return 0;
}

// Test a single inversion operation using DMA peripheral
int test_single_inv(void) {
    int err = 0;
    int i;
    int fd = open("/dev/dma_proxy", O_RDWR);
    if (!fd)
        return -1;

    // Create buffer
    size_t buf_sz = 4096;
    err = ioctl(fd, DMAPROXY_IOCTCBUF, &buf_sz);
    if (err)
        return -1;

    // Map the buffer into user-space
    char *buf_orig = (char *)malloc(buf_sz*sizeof(char));
    char *buf = (char *)mmap(NULL, buf_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    // Write some data into the buffer
    for (i = 0; i < buf_sz; i++) {
        buf[i] = i*i;
        buf_orig[i] = buf[i];
    }

    // Start the DMA transfer
    err = ioctl(fd, DMAPROXY_IOCTSTART, &buf_sz);
    if (err)
        return -1;

    // Synchronize and check the inverted data
    if (ioctl(fd, DMAPROXY_IOCTRXSYNC))
        return -1;
    
    for (i = 0; i < buf_sz; i++) {
        if (buf[i] != (char)(~buf_orig[i]))
            return -1;
    }

    // Unmap the buffer
    munmap(buf, buf_sz);
    free(buf_orig);
    close(fd);
    return 0;
}