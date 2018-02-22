#ifndef __TEST_DMA_INV_H_
#define __TEST_DMA_INV_H_

/************************************************************************************
* Test case declarations
************************************************************************************/
int test_max_open(void);
int test_single_inv(void);


/************************************************************************************
* Declarations and definitions
************************************************************************************/
#define NUM_TESTS   2
#define MAX_CHARS   100

#define DMAPROXY_IOCTMAGIC  0x89                                // Magic number
#define DMAPROXY_IOCTCBUF   _IOW(DMAPROXY_IOCTMAGIC, 0, size_t) // Create a kernel DMA buffer for the process 
#define DMAPROXY_IOCTRBUF   _IO(DMAPROXY_IOCTMAGIC, 1)          // Remove kernel DMA buffer for process
#define DMAPROXY_IOCTSTART  _IOW(DMAPROXY_IOCTMAGIC, 2, size_t) // Set up and start a DMA transfer to invert data
#define DMAPROXY_IOCTSTATUS _IOW(DMAPROXY_IOCTMAGIC, 3, size_t) // Get a vector of status bits
#define DMAPROXY_IOCTRXSYNC _IO(DMAPROXY_IOCTMAGIC, 4)          // Block until the process-specific RX lock is released

struct test_case {
    int (*func)(void);
    char name[MAX_CHARS];
};

// This array contains the individual test cases
struct test_case test_cases[NUM_TESTS] = {
    {test_max_open, "Maximum number of device opens (test_max_open)"},
    {test_single_inv, "Single inversion test (test_single_inv)"}
};


#endif