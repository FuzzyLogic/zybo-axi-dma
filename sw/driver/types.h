#ifndef __TYPES_H_
#define __TYPES_H_

/************************************************************************************
* Type declarations
************************************************************************************/

// To be stored in private_data of struct file for each process 
struct dma_proxy_inst {
    size_t          buf_sz;         // The size of the kernel buffer
    dma_addr_t      dma_buf_phys;   // The physical address that can be used by the DMA controller
    void            *dma_buf_virt;  // The virtual address of the DMA buffer used by the CPU
    struct mutex    rx_lock;        // Mutex for the S2MM channel, used to test whether receiving has been completed
};

// Information stored about the AXI DMA core
struct core_info {
    void                    *base_addr; // Base address of the AXI-DMA core
    struct resource         *res;       // Kernel resource struct
    unsigned long           remap_sz;   // Size of the MMIO address space mapped to the driver
    struct platform_device  *ofdev;     // Kernel platform device
    struct mutex            hw_lock;    // Used to mediate general races on the hardware between processes
};

// This struct is the information passed to the RX synchronization thread
struct rx_sync_dat {
    struct mutex            *hw_lock;       // The global hardware mutex, used to protect the AXI-DMA instance from races
    struct dma_proxy_inst   *instp;         // The process instance
    void                    *axi_addr;      // Address of the AXI-DMA core
};

#endif