#include <linux/errno.h>    // Linux error codes
#include <linux/module.h>   // Module macros
#include <linux/kthread.h>  // kernel threads
#include <linux/slab.h>     // kmalloc and friends
#include "axi_dma_iface.h"
#include "types.h"

/**
 * axi_dma_reset - Reset the DMA core
 *
 * @axi_addr: Memory mapped address of the AXI-DMA core
 *
 * This function resets the RX and TX channels of the core.
 *
 * This function return zero in case of success, and an error code otherwise.
 */
int axi_dma_reset(void *axi_addr) {
    if (!axi_addr)
        return -EINVAL;

    // Set the reset bit in MM2S and S2MM control regs and all others to zero
    reg_wr(((uint32_t)1) << AXI_MM2S_DMACR_Reset, axi_addr, AXI_MM2S_DMACR);
    reg_wr(((uint32_t)1) << AXI_S2MM_DMACR_Reset, axi_addr, AXI_S2MM_DMACR);
    return 0;
}

/**
 * axi_dma_halt - Halt both channels
 *
 * @axi_addr: Memory mapped address of the AXI-DMA core
 *
 * This function halts the RX and TX channels of the core.
 *
 * This function return zero in case of success, and an error code otherwise.
 */
int axi_dma_halt(void *axi_addr) {
    if (!axi_addr)
        return -EINVAL;

    // Writing all zeros to the control regs suffices
    reg_wr(0, axi_addr, AXI_MM2S_DMACR);
    reg_wr(0, axi_addr, AXI_S2MM_DMACR);
    return 0;
}

/**
 * axi_dma_setup_tx - Set up MM2S channel for sending data
 *
 * @axi_addr: Memory mapped address of the AXI-DMA core
 * @src: Address of the source data buffer
 *
 * This function sets up the DMA core for transfer from the specified
 * memory location to the peripheral.
 *
 * This function return zero in case of success, and an error code otherwise.
 */
int axi_dma_setup_tx(void *axi_addr, dma_addr_t src) {
    uint32_t reg_val = 0;
    if (!axi_addr || !src)
        return -EINVAL;

    // Set the source address
    reg_wr((uint32_t)src, axi_addr, AXI_MM2S_SA);

    // Start channel with masked interrupts
    reg_val |= (((uint32_t)1) << AXI_MM2S_DMACR_RS);
    reg_val |= (((uint32_t)1) << AXI_MM2S_DMACR_IOC_IrqEn);
    reg_val |= (((uint32_t)1) << AXI_MM2S_DMACR_Dly_IrqEn);
    reg_val |= (((uint32_t)1) << AXI_MM2S_DMACR_Err_IrqEn);
    reg_wr(reg_val, axi_addr, AXI_MM2S_DMACR);
    
    return 0;
}

/**
 * axi_dma_start_tx - Start a previously set up MM2S transfer
 *
 * @axi_addr: Memory mapped address of the AXI-DMA core
 * @sz: The number of bytes to transmit from the source buffer
 *
 * This function starts a DMA transfer from memory to the peripheral.
 * The call will not block.
 *
 * This function return zero in case of success, and an error code otherwise.
 */
int axi_dma_start_tx(void *axi_addr, size_t sz) {
    // Start the transfer
    reg_wr((uint32_t)sz, axi_addr, AXI_MM2S_LENGTH);
    return 0;
}

/**
 * axi_dma_setup_rx - Setup S2MM channel for receiving data
 *
 * @axi_addr: Memory mapped address of the AXI-DMA core
 * @dest: Destination data buffer
 * @sz: Number of bytes in the destination buffer
 *
 * This function sets up the S2MM channel for streaming data
 * from the peripheral to the specified location in memory.
 *
 * This function return zero in case of success, and an error code otherwise.
 */
int axi_dma_setup_rx(void *axi_addr, dma_addr_t dest, size_t sz) {
    uint32_t reg_val = 0;
    if (!axi_addr || !dest)
        return -EINVAL;

    // Set the destinations address
    reg_wr((uint32_t)dest, axi_addr, AXI_S2MM_DA);

    // Setup channel with masked interrupts and write length to enable channel to receive data
    reg_val |= (((uint32_t)1) << AXI_S2MM_DMACR_RS);
    reg_val |= (((uint32_t)1) << AXI_S2MM_DMACR_IOC_IRqEn);
    reg_val |= (((uint32_t)1) << AXI_S2MM_DMACR_Dly_IrqEn);
    reg_val |= (((uint32_t)1) << AXI_S2MM_DMACR_Err_IrqEn);
    reg_wr(reg_val, axi_addr, AXI_S2MM_DMACR);
    reg_wr((uint32_t)sz, axi_addr, AXI_S2MM_LENGTH);
    return 0;
}

/**
 * axi_dma_sync_tx - Synchronize the MM2S channel
 *
 * @axi_addr: Memory mapped address of the AXI-DMA core
 *
 * Wait until TX channel is idle. This can be
 * used to check if data has been completely transfered.
 *
 * This function return zero in case of transfer complete, and an error code otherwise.
 */
int axi_dma_sync_tx(void *axi_addr) {
    uint32_t reg_val = 0;
    if (!axi_addr)
        return -EINVAL;

    reg_val = reg_rd(axi_addr, AXI_MM2S_DMASR);
    while (!(reg_val & ((uint32_t)1 << AXI_MM2S_DMASR_Idle)) 
        || !(reg_val & ((uint32_t)1 << AXI_MM2S_DMASR_IOC_Irq))) {
        reg_val = reg_rd(axi_addr, AXI_MM2S_DMASR);
    }

    return 0;
}

// Wait until RX channel is idle, note that this is a kernel thread
// function that is also in charge of releasing the hardware mutex once
// the operation is complete

/**
 * axi_dma_sync_rx - Synchronize the S2MM channel
 *
 * @data: Pointer to a data structure containing information for synchronization
 *
 * Thi function is called as a kernel thread. It waits until the S2MM transfer
 * is complete and until it is stopped.
 * This is currently the case when the initiating process makes an ioctl() call
 * to synchronize its receive buffer.
 * Note that other processes can use the hardware even if this thread has not been
 * stopped, as it releases the hardware mutex once it sees that the S2MM transfer
 * has completed.
 *
 * This function return zero when the thread is stopped, and an error code otherwise.
 */
int axi_dma_sync_rx(void *data) {
    uint32_t reg_val = 0;
    struct rx_sync_dat *sync;
    if (!data)
        return -EINVAL;

    sync = (struct rx_sync_dat*)data;
    reg_val = reg_rd(sync->axi_addr, AXI_S2MM_DMASR);
    while ((!(reg_val & ((uint32_t)1 << AXI_S2MM_DMASR_Idle)) 
        || !(reg_val & ((uint32_t)1 << AXI_S2MM_DMASR_IOC_Irq)))
        && !kthread_should_stop()) {
        reg_val = reg_rd(sync->axi_addr, AXI_S2MM_DMASR);
    }

    // Unlock the mutex
    mutex_unlock(sync->hw_lock);

    // Unlock the process-specific mutex to signal that that receiving data has completed
    mutex_unlock(&sync->instp->rx_lock);
    kzfree(sync);

    // Loop until the thread is stopped
    while (!kthread_should_stop());
    return 0;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("FuzzyLogic");
MODULE_DESCRIPTION("A simple DMA device proxy driver");
MODULE_VERSION("1.0");