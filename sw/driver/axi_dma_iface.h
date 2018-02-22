#ifndef __AXI_DMA_IFACE_H_
#define __AXI_DMA_IFACE_H_

#include <linux/types.h>        // uintX_t and friends
#include <asm/io.h>             // iowrite32 and ioread32
#include "types.h"    


/************************************************************************************
* AXI DMA register-related defines (see Table 2-7 in AXI DMA documentation)
************************************************************************************/

// MM2S DMA Control Register
#define AXI_MM2S_DMACR              0x00
#define AXI_MM2S_DMACR_RS           0
#define AXI_MM2S_DMACR_Reset        2
#define AXI_MM2S_DMACR_IOC_IrqEn    12
#define AXI_MM2S_DMACR_Dly_IrqEn    13
#define AXI_MM2S_DMACR_Err_IrqEn    14

// MM2S DMA Status Register
#define AXI_MM2S_DMASR          0x04
#define AXI_MM2S_DMASR_Idle     1
#define AXI_MM2S_DMASR_IOC_Irq  12

// MM2S Source Address
#define AXI_MM2S_SA     0x18

// MM2S Transfer Length (Bytes)
#define AXI_MM2S_LENGTH 0x28

// S2MM DMA Control Register
#define AXI_S2MM_DMACR              0x30
#define AXI_S2MM_DMACR_RS           0
#define AXI_S2MM_DMACR_Reset        2
#define AXI_S2MM_DMACR_IOC_IRqEn    12
#define AXI_S2MM_DMACR_Dly_IrqEn    13
#define AXI_S2MM_DMACR_Err_IrqEn    14

// S2MM DMA Status Register
#define AXI_S2MM_DMASR          0x34
#define AXI_S2MM_DMASR_Idle     1
#define AXI_S2MM_DMASR_IOC_Irq  12

// S2MM Destination Address
#define AXI_S2MM_DA     0x48

// S2MM BUffer Length (Bytes)
#define AXI_S2MM_LENGTH 0x58


/************************************************************************************
* Global variables related to synchronizing the hardware
************************************************************************************/
static struct task_struct   *sync_rx_thread = NULL;


/************************************************************************************
* Basic wrappers for working with MMIO
************************************************************************************/

// Write a word to the specified register
static inline void reg_wr(uint32_t val, void *mm_addr, uint8_t reg_num) 
{
    if (mm_addr)
        iowrite32(val, ((uint8_t *)mm_addr) + reg_num);
}

// Read a word from the specified register
static inline uint32_t reg_rd(void *mm_addr, uint8_t reg_num)
{
    if (mm_addr)
        return ioread32(((uint8_t *)mm_addr) + reg_num);

    return 0;
}


/************************************************************************************
* AXI DMA interfacing function declarations
************************************************************************************/
int axi_dma_reset(void *axi_addr);
int axi_dma_halt(void *axi_addr);
int axi_dma_setup_tx(void *axi_addr, dma_addr_t src);
int axi_dma_start_tx(void *axi_addr, size_t sz);
int axi_dma_setup_rx(void *axi_addr, dma_addr_t dest, size_t sz);
int axi_dma_sync_tx(void *axi_addr);
int axi_dma_sync_rx(void *axi_addr);

#endif  // __AXI_DMA_IFACE_H_