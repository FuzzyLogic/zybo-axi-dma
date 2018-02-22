#include <linux/module.h>           // Module macros
#include <linux/platform_device.h>  // Platform_device struct and related functions
#include <linux/slab.h>             // kmalloc and friends
#include <linux/dma-mapping.h>      // DMA mapping API, dma_*_coherent
#include <asm/io.h>                 // MMIO via ioremap
#include <asm/uaccess.h>            // copy_from_user
#include <linux/kthread.h>          // kernel threads
#include "dma_proxy_driver.h"
#include "axi_dma_iface.h"
#include "types.h"

/************************************************************************************
* Helper functions
************************************************************************************/

/**
 * release_inst - Remove a single instance of resources
 *
 * @instp: a pointer to the instance to be freed
 *
 * This function releases a single dma_proxy_inst.
 */
static void release_inst(struct dma_proxy_inst *instp) {
    if (instp && instp->dma_buf_virt) {
        dma_free_coherent(&ip_info.ofdev->dev, instp->buf_sz, instp->dma_buf_virt, instp->dma_buf_phys);

        // Finally, release private_data
        kzfree(instp);
    }
}

/**
 * release_all_resources - Remove all resources managed by the driver
 *
 * This function releases all resources and should only be called
 * in the module exit function, which should typically not be called anyways
 */
static void release_all_resources(void) {
    int i;
    for (i = 0; i < MAX_INST; i++) {
        if (instances[i] != NULL) {
            release_inst(instances[i]);
            instances[i] = NULL;
        }
    }

    // Kill the RX synchronization thread if it is running
    if (sync_rx_thread)
        kthread_stop(sync_rx_thread);

    kzfree(instances);
}

/************************************************************************************
* File operation functions
************************************************************************************/

/**
 * dma_proxy_open - open() syscall implementation
 *
 * @inodep: Unused
 * @filep: A pointer to a struct file, corresponding to the file descriptor received by the
 *         calling process
 *
 * This function implements the open() syscall for the driver.
 * It allocates a dma_proxy_inst for the process and stores it in the private data.
 *
 * This function returns zero on success, and an error code otherwise.
 */
static int dma_proxy_open(struct inode *inodep, struct file *filep) {
    struct dma_proxy_inst *instp;
    int i;

    if (num_open < MAX_INST)
        printk(KERN_INFO "dma_proxy: device file opened\n");
    else
        return -EBUSY;

    // Allocate private data for process
    instp = kzalloc(sizeof(struct dma_proxy_inst), GFP_KERNEL);
    if (!instp)
        return -ENOMEM;

    // Initialize instance
    instp->dma_buf_phys = 0;
    instp->dma_buf_virt = NULL;
    instp->buf_sz = 0;
    mutex_init(&instp->rx_lock);

    filep->private_data = instp;

    // Track resources
    for (i = 0; i < MAX_INST; i++) {
        if (instances[i] == NULL) {
            instances[i] = instp;
            break;
        }
    }
    
    num_open++;
    return 0;
}

/**
 * dma_proxy_read - read() syscall implementation
 *
 * @filep: Unused
 * @buf: Unused
 * @len: Unused
 * @offsetp: Unused
 *
 * This function is currently not implemented.
 *
 * This function currently always returns zero.
 */
static ssize_t dma_proxy_read(struct file *filep, char *buf, size_t len, loff_t *offsetp) {
    printk(KERN_INFO "dma_proxy: device file read\n");
    return 0;
}

/**
 * dma_proxy_write - write() syscall implementation
 *
 * @filep: Unused
 * @buf: Unused
 * @len: Unused
 * @offsetp: Unused
 *
 * This function is currently not implemented.
 *
 * This function currently always returns zero.
 */
static ssize_t dma_proxy_write(struct file *filep, const char *buf, size_t len, loff_t *offsetp) {
    printk(KERN_INFO "dma_proxy: device file write\n");
    return 0;
}

/**
 * dma_proxy_release - close() syscall implementation
 *
 * @inodep: Unused
 * @filep: A pointer to a representation of the open file descriptor
 *
 * This function removes all of the process-specific resources, i.e.
 * the dma_proxy_inst held for it and does a little bit of cleanup.
 *
 * This function currently always returns zero.
 */
static int dma_proxy_release(struct inode *inodep, struct file *filep) {
    int i;

    printk(KERN_INFO "dma_proxy: device file release\n");

    // Clean up process-related data and remove private_data
    if (filep->private_data) {
        // Find the tracked resource and stop tracking it
        for (i = 0; i < MAX_INST; i++) {
            if (instances[i] == (struct dma_proxy_inst *)filep->private_data) {
                instances[i] = NULL;
                break;
            }
        }

        // Free the kernel data buffer for the process if it did not do this by itself
        release_inst((struct dma_proxy_inst *)filep->private_data);
    }

    if (num_open > 0)
        num_open--;
    return 0;
}

/**
 * dma_proxy_ioctl - ioctl() syscall implementation
 *
 * @filep: A pointer representing the open file descriptor
 * @cmd: ioctl command code
 * @arg: Optional argument data that may be part the command
 *
 * This function is used to control use of the DMA peripheral.
 * It provides the following command codes:
 *  - DMAPROXY_IOCTCBUF: Allocate a cache-coherent kernel buffer to be used for DMA
 *                       for the calling process. The extra argument specifies the
 *                       size of the buffer. Note that this may not be larger than
 *                       MAX_BUF_SZ. Note also that only one buffer is allowed per
 *                       open file descriptor.
 *  - DMAPROXY_IOCTRBUF: Free a previously allocated buffer.
 *  - DMAPROXY_IOCTSTART: Start a DMA transfer to the peripheral. Data will be taken
 *                        from the buffer corresponding to the file descriptor. The
 *                        additional argument specifies the number of bytes from the
 *                        buffer to transmit. Note that this call blocks until the
 *                        DMA transfer is complete.
 *  - DMAPROXY_IOCTRXSYNC: This call simply blocks until a currently active DMA transfer
 *                         from the peripheral back to the buffer corresponding to the
 *                         current file descriptor has finished.
 *
 * This function returns zero in case of success, and an error code otherwise.
 */
static long dma_proxy_ioctl(struct file *filep, unsigned int cmd, unsigned long arg) {
    size_t sz = 0;
    struct dma_proxy_inst *instp;
    int err = 0;
    struct rx_sync_dat *sync;

    // Process command
    switch (cmd) {
        // Allocate a DMA buffer for the process to use until it no longer requires it
        case DMAPROXY_IOCTCBUF:
            // Arg is the kernel buffer size
            if (arg) {
                if (copy_from_user(&sz, (void *)arg, sizeof(size_t)))
                    return -EIO;
            }
            else 
                return -EINVAL;

            if (filep->private_data && sz < MAX_BUF_SZ) {
                // Check if buffer already allocated
                instp = (struct dma_proxy_inst *)filep->private_data;
                if (instp->dma_buf_virt)
                    return -EINVAL;
                else {
                    instp->dma_buf_virt = dma_alloc_coherent(&ip_info.ofdev->dev, sz, &instp->dma_buf_phys, GFP_KERNEL);
                    if (IS_ERR(instp->dma_buf_virt))
                        return PTR_ERR(instp->dma_buf_virt);
                    instp->buf_sz = sz;
                }
            } else
                return -EINVAL;
            break;

        // Free a previously set up DMA buffer
        case DMAPROXY_IOCTRBUF:
            // Simply free the buffer
            if (filep->private_data) {
                // Check if buffer already allocated
                instp = (struct dma_proxy_inst *)filep->private_data;
                if (instp->dma_buf_virt)
                    dma_free_coherent(&ip_info.ofdev->dev, instp->buf_sz, instp->dma_buf_virt, instp->dma_buf_phys);
                else
                    return -EFAULT;
            } else
                return -EINVAL;
            break;

        // Start inverting data
        case DMAPROXY_IOCTSTART:
            // Arg will be the number of bytes to send
            if (arg) {
                if (copy_from_user(&sz, (void *)arg, sizeof(size_t)))
                    return -EIO;
            }
            else 
                return -EINVAL;

            if (filep->private_data && sz < MAX_BUF_SZ) {
                // Check if buffer already allocated and that sz is not greater than the buffer length
                instp = (struct dma_proxy_inst *)filep->private_data;
                if (!instp->dma_buf_phys || !instp->dma_buf_virt)
                    return -EINVAL;
                else if (sz > instp->buf_sz)
                    return -EINVAL;
                else {
                    // Try to acquire hardware, block if necessary...
                    // Note that if acquired, the mutex will be freed by the rx synchronization thread
                    mutex_lock(&ip_info.hw_lock);

                    // Setup a transfer to slave
                    err = axi_dma_setup_tx(ip_info.base_addr, instp->dma_buf_phys);
                    if (err) {
                        mutex_lock(&ip_info.hw_lock);
                        return err;
                    }
                    
                    // Setup the receive channel accordingly
                    err = axi_dma_setup_rx(ip_info.base_addr, instp->dma_buf_phys, sz);
                    if (err) {
                        mutex_lock(&ip_info.hw_lock);
                        return err;
                    }

                    // Initiate the transfer
                    err = axi_dma_start_tx(ip_info.base_addr, sz);
                    if (err) {
                        mutex_lock(&ip_info.hw_lock);
                        return err;
                    }

                    // Synchronize TX, this will block until the MM2S transfer is complete
                    axi_dma_sync_tx(ip_info.base_addr);

                    // Start a kernel thread that will synchronize the RX channel and release the hardware
                    sync = (struct rx_sync_dat*)kzalloc(sizeof(struct rx_sync_dat), GFP_KERNEL);
                    sync->hw_lock = &ip_info.hw_lock;
                    sync->instp = instp;
                    sync->axi_addr = ip_info.base_addr;
                    sync_rx_thread = kthread_run(axi_dma_sync_rx, sync, "dma_proxy_sync");
                    if (IS_ERR(sync_rx_thread)) {
                        mutex_unlock(&ip_info.hw_lock);
                        return PTR_ERR(sync_rx_thread);
                    }
                }
            } else
                return -EINVAL;
            break;

        // Return status about device and the current process' context
        case DMAPROXY_IOCTRXSYNC:
            if (filep->private_data) {
                // Wait until the S2MM lock is no longer used by the RX synchronization thread
                instp = (struct dma_proxy_inst *)filep->private_data;
                mutex_lock(&instp->rx_lock);
                mutex_unlock(&instp->rx_lock);    

                // Stop the thread
                kthread_stop(sync_rx_thread);
                sync_rx_thread = NULL;            
            } else
                return -EINVAL;

            break;

        default:
            return -EINVAL;
    }

    return 0;
}

/**
 * dma_proxy_mmap - mmap() syscall implementation
 *
 * @filep: A pointer to a representation of the open file descriptor
 * @vma: A vm_area_struct pointer containing details about the region to map to
 *
 * This mmap handler is used to map kernel buffers into user-space.
 *
 * This function return zero in case of success, and an error code otherwise.
 */
static int dma_proxy_mmap(struct file *filep, struct vm_area_struct *vma) {
    int req_sz = 0;
    struct dma_proxy_inst *instp = NULL;

    // Get the requested size and make sure it is not bigger than the internal buffer
    req_sz = vma->vm_end - vma->vm_start;
    if (!filep->private_data)
        return -EFAULT;
                
    instp = (struct dma_proxy_inst *)filep->private_data;
    if (req_sz > instp->buf_sz)
        return -EINVAL;

    // Mark the prot value as uncacheable
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    return remap_pfn_range(vma, vma->vm_start, virt_to_pfn(instp->dma_buf_virt), req_sz, vma->vm_page_prot);
}


/************************************************************************************
* Platform driver specific functions
************************************************************************************/

/**
 * dma_proxy_probe- The driver probe function
 *
 * @devp: Platform device pointer
 *
 * This function is in charge of setting up the driver, which includes setting
 * up the device file, mapping the DMA controller memory to the driver,
 * as well as resetting the DMA core.
 *
 * This function return zero in case of success, and an error code otherwise.
 */
static int dma_proxy_probe(struct platform_device *devp) {
    int err = 0;

    // Get resource information for device
    ip_info.ofdev = devp; 
    ip_info.res = platform_get_resource(devp, IORESOURCE_MEM, 0);
    if (!ip_info.res ) {
        dev_err(&ip_info.ofdev->dev, "No memory resource information available\n");
        err = -ENODEV;
        goto res_err;
    }

    // Get memory size for ioremap and request memory region for mapping
    ip_info.remap_sz = ip_info.res->end - ip_info.res->start + 1; 
    if (!request_mem_region(ip_info.res->start, ip_info.remap_sz, devp->name)) {
        dev_err(&ip_info.ofdev->dev, "Could not setup memory region for remap\n");
        err = -ENXIO;
        goto mem_err;
    }

    // Map the physical MMIO space of the core to virtual kernel space memory
    ip_info.base_addr = ioremap(ip_info.res->start, ip_info.remap_sz);
    if (ip_info.base_addr == NULL) {
        dev_err(&ip_info.ofdev->dev, "Could not ioremap MMIO at 0x%08lx\n", (unsigned long)ip_info.res->start);
        err = -ENOMEM;
        goto err_ioremap;
    }
 
    // Try to dynamically allocate a major number for the device
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0){
        dev_err(&ip_info.ofdev->dev, "Failed to register major number\n");
        err = major_number;
        goto err_chrdev;
    }
    
    // Register the device class
    dma_proxy_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(dma_proxy_class)){
        dev_err(&ip_info.ofdev->dev, "Failed to register device class\n");
        err = PTR_ERR(dma_proxy_class);
        goto err_class;
    }
    
    // Register the device driver
    dev_entry = device_create(dma_proxy_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(dev_entry)){
     dev_err(&ip_info.ofdev->dev, "Failed to register device driver\n");
        err = PTR_ERR(dev_entry);
        goto err_dev;
    }

    // Create internal structures for tracking resources
    instances = kzalloc(sizeof(struct dma_proxy_inst *)*MAX_INST, GFP_KERNEL);
    if (!instances) {
        dev_err(&ip_info.ofdev->dev, "Error acquiring resources\n");
        err = -ENOMEM;    
        goto err_inst_setup;
    }

    // Setup the AXI DMA channels (i.s. reset and halt)
    err = axi_dma_reset(ip_info.base_addr);
    if (err)
        goto err_inst_setup;
    err = axi_dma_halt(ip_info.base_addr);
    if (err)
        goto err_inst_setup;

    // Set up a mutex to arbitrate access to the hardware
    mutex_init(&ip_info.hw_lock);
    goto done;

// Handle errors, revert previous steps
err_inst_setup:
    device_destroy(dma_proxy_class, MKDEV(major_number, 0)); 
    class_unregister(dma_proxy_class);                     
    class_destroy(dma_proxy_class);                        
    unregister_chrdev(major_number, DEVICE_NAME);
err_dev:
    class_destroy(dma_proxy_class);
err_class:
    unregister_chrdev(major_number, DEVICE_NAME);
err_chrdev:
    iounmap(ip_info.base_addr);
err_ioremap:
    release_mem_region(ip_info.res->start, ip_info.remap_sz);
mem_err:
res_err:
    return err;

// All steps successful, driver is ready
done:
    return 0;
}

/**
 * dma_proxy_remove - remove handler for the device
 *
 * @devp: Platform device pointer
 *
 * This function is called when the device is removed.
 *
 * Currently this function always returns zero.
 */
static int dma_proxy_remove(struct platform_device *devp) {
    release_all_resources();
    device_destroy(dma_proxy_class, MKDEV(major_number, 0)); 
    class_unregister(dma_proxy_class);                     
    class_destroy(dma_proxy_class);                        
    unregister_chrdev(major_number, DEVICE_NAME);        
    iounmap(ip_info.base_addr);
    release_mem_region(ip_info.res->start, ip_info.remap_sz);
    return 0;
}

// Not implemented
static void dma_proxy_shutdown(struct platform_device *devp) {
}


/************************************************************************************
* Driver registration and information
************************************************************************************/

// Table used to match this driver with an entry in the device tree
static const struct of_device_id dma_proxy_of_match[] = {
    {.compatible = "xlnx,axi-dma-1.00.a"},
    {}
};

MODULE_DEVICE_TABLE(of, dma_proxy_of_match);

// Platform driver structure for the AXI-DMA core
static struct platform_driver dma_proxy_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
        .of_match_table = dma_proxy_of_match
    },
    .probe = dma_proxy_probe,
    .remove = dma_proxy_remove,
    .shutdown = dma_proxy_shutdown
};

// Register the platform driver with the kernel
module_platform_driver(dma_proxy_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("FuzzyLogic");
MODULE_DESCRIPTION("A simple DMA device proxy driver");
MODULE_VERSION("1.0");