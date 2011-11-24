//------------------------------------------------------------------------------
// <copyright file="ath_spi_hcd_if.h" company="Atheros">
//    Copyright (c) 2007-2008 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

#ifndef __ATH_SPI_HCD_IF_H___
#define __ATH_SPI_HCD_IF_H___

    /* HCD base name */
#define SDIO_RAW_BD_BASE "sdiobd_spi_raw"

typedef UINT8 ATH_TRANS_CMD;

    /* transfer SPI frame size flags for HCD internal use */
#define ATH_TRANS_DS_8       0x00
#define ATH_TRANS_DS_16      0x01
#define ATH_TRANS_DS_24      0x02  /* 24 bit transfer, for HCD internal use only! */
#define ATH_TRANS_DS_32      0x03


#define ATH_TRANS_DS_MASK    0x03 
#define ATH_TRANS_DMA        0x04  /* special DMA transfer */
#define ATH_TRANS_TYPE_MASK  0x07
    /* register access is internal or external */
#define ATH_TRANS_INT_TRANS   0x00   /* internal register (SPI internal) transfer */
#define ATH_TRANS_EXT_TRANS   0x10   /* external register transfer */
    /* transfer to fixed or incrementing address (external transfers only) */
#define ATH_TRANS_EXT_TRANS_ADDR_INC     0x00 
#define ATH_TRANS_EXT_TRANS_ADDR_FIXED   0x20 
    /* direction of operation */
#define ATH_TRANS_READ       0x80
#define ATH_TRANS_WRITE      0x00 

#define ATH_TRANS_EXT_MAX_PIO_BYTES 32

    /* address mask */
#define ATH_TRANS_ADDR_MASK 0x7FFF  
    /* macro to get the transfer type from a command */
#define ATH_GET_TRANS_TYPE(cmd) ((cmd) & ATH_TRANS_TYPE_MASK)
    /* macro to get the transfer data size */
#define ATH_GET_TRANS_DS(cmd)  ((cmd) & ATH_TRANS_DS_MASK)

#define ATH_TRANS_IS_DIR_READ(cmd)       ((cmd) & ATH_TRANS_READ)
#define ATH_TRANS_IS_DIR_WRITE(cmd)      !ATH_TRANS_IS_DIR_READ(cmd)

    /* macros for passing values in writes and getting read data and status values */
#define ATH_GET_PIO_WRITE_VALUE(pReq)                   (pReq)->Parameters[1].As32bit
#define ATH_SET_PIO_WRITE_VALUE(pReq,v)                 (pReq)->Parameters[1].As32bit = (v)
#define ATH_GET_PIO_INTERNAL_READ_RESULT(pReq)  (UINT16)(pReq)->Parameters[3].As32bit
#define ATH_SET_PIO_INTERNAL_READ_RESULT(pReq,result)   (pReq)->Parameters[3].As32bit = (result)

#define ATH_GET_IO_CMD(pReq)                            (pReq)->Parameters[0].As16bit[0]
#define ATH_SET_IO_CMD(pReq,cmd)                        (pReq)->Parameters[0].As16bit[0] = (cmd)
#define ATH_GET_IO_DS(pReq) ATH_GET_TRANS_DS(ATH_GET_IO_CMD(pReq))

#define ATH_GET_IO_ADDRESS(pReq)                        (pReq)->Parameters[2].As16bit[0]
#define ATH_SET_IO_ADDRESS(pReq,addr)                   (pReq)->Parameters[2].As16bit[0] = (addr)
#define ATH_GET_DMA_TRANSFER_BYTES(pReq)                (pReq)->Parameters[2].As16bit[1]
#define ATH_GET_EXT_TRANSFER_BYTES(pReq)                (pReq)->Parameters[2].As16bit[1]
#define ATH_SET_TRANSFER_BYTES(pReq,bytes)              (pReq)->Parameters[2].As16bit[1] = (bytes)

#define ATH_IS_TRANS_READ(pReq) (ATH_TRANS_IS_DIR_READ(ATH_GET_IO_CMD(pReq)))
#define ATH_IS_TRANS_WRITE(pReq) (!ATH_IS_TRANS_READ(pReq))
#define ATH_IS_TRANS_DMA(pReq)  (ATH_GET_IO_CMD(pReq) & ATH_TRANS_DMA)
#define ATH_IS_TRANS_PIO(pReq)  (!ATH_IS_TRANS_DMA(pReq))
#define ATH_IS_TRANS_PIO_EXTERNAL(pReq) (ATH_GET_IO_CMD(pReq) & ATH_TRANS_EXT_TRANS)
#define ATH_IS_TRANS_PIO_INTERNAL(pReq) !ATH_IS_TRANS_PIO_EXTERNAL(pReq)
#define ATH_IS_TRANS_EXT_ADDR_FIXED(pReq) (ATH_GET_IO_CMD(pReq) & ATH_TRANS_EXT_TRANS_ADDR_FIXED)
#define ATH_IS_TRANS_EXT_ADDR_INC(pReq)  !ATH_IS_TRANS_EXT_ADDR_FIXED(pReq)

    /* macros to setup PIO write transfer requests */
#define ATH_SET_PIO_INTERNAL_WRITE_OPERATION(pReq,addr,value)        \
{  ATH_SET_IO_CMD(pReq, ATH_TRANS_WRITE | ATH_TRANS_INT_TRANS);      \
   ATH_SET_IO_ADDRESS(pReq, (UINT16)((addr) & ATH_TRANS_ADDR_MASK)); \
   ATH_SET_PIO_WRITE_VALUE(pReq, value);                             \
}
      
    /* macros to setup PIO read transfer requests */
#define ATH_SET_PIO_INTERNAL_READ_OPERATION(pReq,addr)               \
{  ATH_SET_IO_CMD(pReq, ATH_TRANS_READ | ATH_TRANS_INT_TRANS);       \
   ATH_SET_IO_ADDRESS(pReq,(UINT16)((addr) & ATH_TRANS_ADDR_MASK));  \
}

    /* macro to setup external register transfer request 
     * note: caller must set pReq->pDataBuffer separately */
#define ATH_SET_PIO_EXTERNAL_OPERATION(pReq,type,addr,incaddr,length) \
{  ATH_SET_IO_CMD(pReq, (type) | ATH_TRANS_EXT_TRANS | \
                ((incaddr) ? ATH_TRANS_EXT_TRANS_ADDR_INC : ATH_TRANS_EXT_TRANS_ADDR_FIXED)); \
   ATH_SET_IO_ADDRESS(pReq,(UINT16)((addr) & ATH_TRANS_ADDR_MASK)); \
   ATH_SET_TRANSFER_BYTES(pReq, (UINT16)(length));                  \
}

#define ATH_SET_PIO_EXTERNAL_WRITE_OPERATION(pReq,addr,incaddr,length) \
    ATH_SET_PIO_EXTERNAL_OPERATION(pReq,ATH_TRANS_WRITE,addr,incaddr,length)
                                         
#define ATH_SET_PIO_EXTERNAL_READ_OPERATION(pReq,addr,incaddr,length) \
    ATH_SET_PIO_EXTERNAL_OPERATION(pReq,ATH_TRANS_READ,addr,incaddr,length)                                         
                                         
  
    /* macros to setup DMA transfer requests, 64K max transfers  
     * note, the caller must set pReq->pDataBuffer separately
     * and the number bytes (length) must be aligned to the DMA data width */
#define ATH_SET_DMA_OPERATION(pReq,dir,addr,length)       \
{  ATH_SET_IO_CMD(pReq, ATH_TRANS_DMA | (dir)) ;                    \
   ATH_SET_IO_ADDRESS(pReq,(UINT16)((addr) & ATH_TRANS_ADDR_MASK)); \
   ATH_SET_TRANSFER_BYTES(pReq,(UINT16)(length));             \
}

#define ATH_SPI_DMA_SIZE_REG      (0x0100) /* internal */
#define ATH_SPI_DMA_SIZE_MAX      (4095)   /* 12 bit counter 0-4095 are valid */

#define ATH_SPI_WRBUF_SPC_AVA_REG     (0x0200) /* internal */

#define ATH_SPI_RDBUF_BYTE_AVA_REG    (0x0300) /* internal */

#define ATH_SPI_CONFIG_REG        (0x0400) /* internal */
#define ATH_SPI_CONFIG_RESET            (1 << 15)  /* SPI interface reset */
#define ATH_SPI_CONFIG_MBOX_INTR_EN     (1 << 8)
#define ATH_SPI_CONFIG_IO_ENABLE        (1 << 7)
#define ATH_SPI_CONFIG_CORE_RESET       (1 << 6)   /* core reset */
#define ATH_SPI_CONFIG_KEEP_AWAKE_ON_INTR (1 << 4)
#define ATH_SPI_CONFIG_KEEP_AWAKE       (1 << 3)
#define ATH_SPI_CONFIG_BYTE_SWAP        (1 << 2)
#define ATH_SPI_CONFIG_SWAP_16BIT       (1 << 1) 
#define ATH_SPI_CONFIG_PREFETCH_MODE_RR (1 << 0)

#define ATH_SPI_CONFIG_MISO_MUXSEL_MASK_SHIFT  9

#define ATH_SPI_STATUS_REG          (0x0500) /* internal */
#define ATH_SPI_STATUS_MBOX_FLOW_CTRL    (1 << 5)
#define ATH_SPI_STATUS_WRBUF_ERROR       (1 << 4)
#define ATH_SPI_STATUS_RDBUF_ERROR       (1 << 3)
#define ATH_SPI_STATUS_RTC_STATE_MASK    (0x3 << 1)
#define ATH_SPI_STATUS_RTC_STATE_WAKEUP  (0x3 << 1)
#define ATH_SPI_STATUS_RTC_STATE_SLEEP   (0x2 << 1)
#define ATH_SPI_STATUS_RTC_STATE_ON      (0x1 << 1)
#define ATH_SPI_STATUS_RTC_STATE_SHUTDOWN (0x0)
#define ATH_SPI_STATUS_HOST_ACCESS_DONE  (1 << 0)
#define ATH_SPI_STATUS_ERRORS (ATH_SPI_STATUS_WRBUF_ERROR | \
                               ATH_SPI_STATUS_RDBUF_ERROR)


#define ATH_SPI_HOST_CTRL_BYTE_SIZE_REG  (0x0600) /* internal */
#define ATH_SPI_HOST_CTRL_NO_ADDR_INC   (1 << 6)
#define ATH_SPI_HOST_CTRL_MAX_BYTES      32

#define ATH_SPI_HOST_CTRL_CONFIG_REG       (0x0700) /* internal */
#define ATH_SPI_HOST_CTRL_CONFIG_ENABLE    (1 << 15)
#define ATH_SPI_HOST_CTRL_CONFIG_DIR_WRITE (1 << 14)

#define ATH_SPI_HOST_CTRL_RD_PORT_REG      (0x0800) /* internal */
#define ATH_SPI_HOST_CTRL_WR_PORT_REG      (0x0A00) /* internal */

#define ATH_SPI_INTR_CAUSE_REG   (0x0C00)   /* internal */
#define ATH_SPI_INTR_ENABLE_REG   (0x0D00)   /* internal */
#define ATH_SPI_INTR_WRBUF_BELOW_WMARK (1 << 10)
#define ATH_SPI_INTR_HOST_CTRL_RD_DONE (1 << 9)
#define ATH_SPI_INTR_HOST_CTRL_WR_DONE (1 << 8)
#define ATH_SPI_INTR_HOST_CTRL_ACCESS_DONE (ATH_SPI_INTR_HOST_CTRL_RD_DONE | ATH_SPI_INTR_HOST_CTRL_WR_DONE)

#define ATH_SPI_INTR_CPU_INTR          (1 << 7)
#define ATH_SPI_INTR_CPU_ON            (1 << 6)
#define ATH_SPI_INTR_COUNTER_INTR      (1 << 5)
#define ATH_SPI_INTR_LOCAL_CPU_INTR    (1 << 4)
#define ATH_SPI_INTR_ADDRESS_ERROR     (1 << 3)
#define ATH_SPI_INTR_WRBUF_ERROR       (1 << 2)
#define ATH_SPI_INTR_RDBUF_ERROR       (1 << 1)
#define ATH_SPI_INTR_PKT_AVAIL         (1 << 0)

#define ATH_SPI_RDBUF_WATERMARK_REG  (0x1200)
#define ATH_SPI_RDBUF_WATERMARK_MAX         0x0FFF
#define ATH_SPI_WRBUF_WATERMARK_REG (0x1300)

#define ATH_SPI_WRBUF_WRPTR_REG   (0x0E00) /* internal */
#define ATH_SPI_WRBUF_RDPTR_REG   (0x0F00) /* internal */
#define ATH_SPI_RDBUF_WRPTR_REG   (0x1000) /* internal */
#define ATH_SPI_RDBUF_RDPTR_REG   (0x1100) /* internal */

#define ATH_SPI_WRBUF_RSVD_BYTES   5      /* the SPI write buffer also contains 4 bytes
                                             for mailbox and length, these bytes must be
                                             taken into account when the write space is checked */

    /* the 4-byte look ahead is split into 2 registers */
#define ATH_SPI_RDBUF_LOOKAHEAD1_REG  (0x1400)
#define ATH_SPI_GET_LOOKAHEAD1_BYTE_0(word) ((UINT8)((word) >> 8))
#define ATH_SPI_GET_LOOKAHEAD1_BYTE_1(word) ((UINT8)(word))  
#define ATH_SPI_RDBUF_LOOKAHEAD2_REG  (0x1500)
#define ATH_SPI_GET_LOOKAHEAD2_BYTE_3(word) ((UINT8)((word) >> 8))
#define ATH_SPI_GET_LOOKAHEAD2_BYTE_4(word) ((UINT8)(word))  

    /* get/set clock, takes an UINT32 parameter */
#define ATH_SPI_CONFIG_SET_CLOCK  (SDCONFIG_PUT_HOST_CUSTOM + 2)
#define ATH_SPI_CONFIG_GET_CLOCK  (SDCONFIG_GET_HOST_CUSTOM + 2)

    /* for testing purposes, set the DMA data frame width 
     * takes a UINT8 paramter with data size */
#define ATH_SPI_CONFIG_SET_DMA_DATA_WIDTH  (SDCONFIG_PUT_HOST_CUSTOM + 3)

    /* for testing purposes, set the host access data frame width 
     * takes a UINT8 paramter with data size */
#define ATH_SPI_CONFIG_SET_HOST_ACCESS_DATA_WIDTH  (SDCONFIG_PUT_HOST_CUSTOM + 4)

    /* for testing purposes dump the SPI registers, takes no data */
#define ATH_SPI_CONFIG_DUMP_SPI_INTERNAL_REGISTERS  (SDCONFIG_PUT_HOST_CUSTOM + 5)

    /* turn on/off the power of the SPI bus */
#define ATH_SPI_CONFIG_SET_POWER  (SDCONFIG_PUT_HOST_CUSTOM + 6)

#endif /* __RAW_SPI_HCD_IF_H___ */
