#ifndef __DMA_PROXY_DRIVER_H_
#define __DMA_PROXY_DRIVER_H_

#include <linux/fs.h>               // struct file_operations
#include <linux/device.h>           // device related data structures
#include <linux/kernel.h>           // kernel data structures
#include <linux/ioctl.h>            // Macros for ioctl command code definitions
#include <linux/mutex.h>            // For locking the RX channel of a single instance
#include <linux/platform_device.h>  // struct platform_device
#include "types.h"


/************************************************************************************
* Driver-related defines
************************************************************************************/
#define DRIVER_NAME         "dma_proxy_driver"
#define DEVICE_NAME         "dma_proxy"
#define CLASS_NAME          "dmaprx"
#define MAX_INST            4               // Maximum number of simultaneous "opens" on the device
#define MAX_BUF_SZ          8192            // Maximum number of bytes in a DMA buffer
#define AXI_DMA_BASE_ADDR   0x40400000      // DMA core AXI-Lite interface base address
#define AXI_DMA_ADDR_SZ     0xFFFF          // Address space for AXI-Lite interface

// ioctl command codes
#define DMAPROXY_IOCTMAGIC  0x89                                // Magic number
#define DMAPROXY_IOCTCBUF   _IOW(DMAPROXY_IOCTMAGIC, 0, size_t) // Create a kernel DMA buffer for the process 
#define DMAPROXY_IOCTRBUF   _IO(DMAPROXY_IOCTMAGIC, 1)          // Remove kernel DMA buffer for process
#define DMAPROXY_IOCTSTART  _IOW(DMAPROXY_IOCTMAGIC, 2, size_t) // Set up and start a DMA transfer to invert data
#define DMAPROXY_IOCTRXSYNC _IO(DMAPROXY_IOCTMAGIC, 4)          // Block until the process-specific RX lock is released


/************************************************************************************
* Miscellaneous static variables global to driver
************************************************************************************/
static int                      major_number;                 
static struct class             *dma_proxy_class = NULL; 
static struct device            *dev_entry   = NULL;
static int                      num_open = 0;
static struct dma_proxy_inst    **instances = NULL;
static struct core_info         ip_info = {.base_addr = NULL, .res = NULL, .remap_sz = 0, .ofdev = NULL};


/************************************************************************************
* Function declarations
************************************************************************************/
static int      dma_proxy_open(struct inode *, struct file *);
static ssize_t  dma_proxy_read(struct file *, char *, size_t, loff_t *);
static ssize_t  dma_proxy_write(struct file *, const char *, size_t, loff_t *);
static int      dma_proxy_release(struct inode *, struct file *);
static long     dma_proxy_ioctl(struct file *, unsigned int, unsigned long);
static int      dma_proxy_mmap(struct file *filep, struct vm_area_struct *vma);


/************************************************************************************
* File operation structure definition
************************************************************************************/
static struct file_operations fops =
{
    .open           = dma_proxy_open,
    .read           = dma_proxy_read,
    .write          = dma_proxy_write,
    .release        = dma_proxy_release,
    .unlocked_ioctl = dma_proxy_ioctl,
    .mmap           = dma_proxy_mmap,
};

#endif // __DMA_PROXY_DRIVER_H_