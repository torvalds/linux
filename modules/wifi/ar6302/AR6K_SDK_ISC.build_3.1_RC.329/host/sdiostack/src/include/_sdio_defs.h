// Copyright (c) 2004-2006 Atheros Communications Inc.
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
// Portions of this code were developed with information supplied from the 
// SD Card Association Simplified Specifications. The following conditions and disclaimers may apply:
//
//  The following conditions apply to the release of the SD simplified specification (“Simplified
//  Specification”) by the SD Card Association. The Simplified Specification is a subset of the complete 
//  SD Specification which is owned by the SD Card Association. This Simplified Specification is provided 
//  on a non-confidential basis subject to the disclaimers below. Any implementation of the Simplified 
//  Specification may require a license from the SD Card Association or other third parties.
//  Disclaimers:
//  The information contained in the Simplified Specification is presented only as a standard 
//  specification for SD Cards and SD Host/Ancillary products and is provided "AS-IS" without any 
//  representations or warranties of any kind. No responsibility is assumed by the SD Card Association for 
//  any damages, any infringements of patents or other right of the SD Card Association or any third 
//  parties, which may result from its use. No license is granted by implication, estoppel or otherwise 
//  under any patent or other rights of the SD Card Association or any third party. Nothing herein shall 
//  be construed as an obligation by the SD Card Association to disclose or distribute any technical 
//  information, know-how or other confidential information to any third party.
//
//
// The initial developers of the original code are Seung Yi and Paul Lever
//
// sdio@atheros.com
//
//

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@file: _sdio_defs.h

@abstract: SD/SDIO definitions 
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#ifndef ___SDIO_DEFS_H___
#define ___SDIO_DEFS_H___

#define SD_INIT_BUS_CLOCK   100000    /* initialization clock in hz */
#define SPI_INIT_BUS_CLOCK  100000    /* initialization clock in hz */
#define SD_MAX_BUS_CLOCK    25000000  /* max clock speed in hz */
#define SD_HS_MAX_BUS_CLOCK 50000000  /* SD high speed max clock speed in hz */
#define SDIO_LOW_SPEED_MAX_BUS_CLOCK 400000 /* max low speed clock in hz */
#define SDMMC_MIN_INIT_CLOCKS   80    /* minimun number of initialization clocks */  
#define SDIO_EMPC_CURRENT_THRESHOLD  300  /* SDIO 1.10 , EMPC (mA) threshold, we add some overhead */
                             
/* commands */
#define CMD0    0
#define CMD1    1
#define CMD2    2
#define CMD3    3
#define CMD4    4
#define CMD5    5
#define CMD6    6
#define CMD7    7
#define CMD9    9
#define CMD10   10
#define CMD12   12
#define CMD13   13
#define CMD15   15
#define CMD16   16
#define CMD17   17
#define CMD18   18
#define CMD24   24
#define CMD25   25
#define CMD27   27
#define CMD28   28
#define CMD29   29
#define CMD30   30
#define CMD32   32
#define CMD33   33
#define CMD38   38
#define CMD42   42
#define CMD52   52
#define CMD53   53
#define CMD55   55
#define CMD56   56
#define CMD58   58
#define CMD59   59
#define ACMD6   6
#define ACMD13  13
#define ACMD22  22
#define ACMD23  23
#define ACMD41  41
#define ACMD42  42
#define ACMD51  51

#define SD_ACMD6_BUS_WIDTH_1_BIT         0x00
#define SD_ACMD6_BUS_WIDTH_4_BIT         0x02

#define SD_CMD59_CRC_OFF            0x00000000
#define SD_CMD59_CRC_ON             0x00000001

/* SD/SPI max response size */
#define SD_MAX_CMD_RESPONSE_BYTES SD_R2_RESPONSE_BYTES

#define SD_R1_RESPONSE_BYTES  6
#define SD_R1B_RESPONSE_BYTES SD_R1_RESPONSE_BYTES
#define SD_R1_GET_CMD(pR) ((pR)[5] & 0xC0))
#define SD_R1_SET_CMD(pR,cmd)  (pR)[5] = (cmd) & 0xC0
#define SD_R1_GET_CARD_STATUS(pR) (((UINT32)((pR)[1]))        |  \
                                  (((UINT32)((pR)[2])) << 8)  |  \
                                  (((UINT32)((pR)[3])) << 16) |  \
                                  (((UINT32)((pR)[4])) << 24) )
#define SD_R1_SET_CMD_STATUS(pR,status) \
{                                      \
    (pR)[1] = (UINT8)(status);         \
    (pR)[2] = (UINT8)((status) >> 8);  \
    (pR)[3] = (UINT8)((status) >> 16); \
    (pR)[4] = (UINT8)((status) >> 24); \
}

/* SD R1 card status bit masks */
#define SD_CS_CMD_OUT_OF_RANGE  ((UINT32)(1u << 31))
#define SD_CS_ADDRESS_ERR       (1 << 30)
#define SD_CS_BLK_LEN_ERR       (1 << 29)
#define SD_CS_ERASE_SEQ_ERR     (1 << 28)
#define SD_CS_ERASE_PARAM_ERR   (1 << 27)
#define SD_CS_WP_ERR            (1 << 26)
#define SD_CS_CARD_LOCKED       (1 << 25)
#define SD_CS_LK_UNLK_FAILED    (1 << 24)
#define SD_CS_PREV_CMD_CRC_ERR  (1 << 23)
#define SD_CS_ILLEGAL_CMD_ERR   (1 << 22)
#define SD_CS_ECC_FAILED        (1 << 21)
#define SD_CS_CARD_INTERNAL_ERR (1 << 20)
#define SD_CS_GENERAL_ERR       (1 << 19)
#define SD_CS_CSD_OVERWR_ERR    (1 << 16)
#define SD_CS_WP_ERASE_SKIP     (1 << 15)
#define SD_CS_ECC_DISABLED      (1 << 14)
#define SD_CS_ERASE_RESET       (1 << 13)
#define SD_CS_GET_STATE(status) (((status) >> 9) & 0x0f)
#define SD_CS_SET_STATE(status, state) \
{                               \
    (status) &= ~(0x0F << 9);   \
    (status) |= (state) << 9    \
}

#define SD_CS_TRANSFER_ERRORS \
                ( SD_CS_ADDRESS_ERR       | \
                  SD_CS_BLK_LEN_ERR       | \
                  SD_CS_ERASE_SEQ_ERR     | \
                  SD_CS_ERASE_PARAM_ERR   | \
                  SD_CS_WP_ERR            | \
                  SD_CS_ECC_FAILED        | \
                  SD_CS_CARD_INTERNAL_ERR | \
                  SD_CS_GENERAL_ERR )
                  
#define SD_CS_STATE_IDLE   0
#define SD_CS_STATE_READY  1
#define SD_CS_STATE_IDENT  2
#define SD_CS_STATE_STBY   3
#define SD_CS_STATE_TRANS  4
#define SD_CS_STATE_DATA   5
#define SD_CS_STATE_RCV    6
#define SD_CS_STATE_PRG    7
#define SD_CS_STATE_DIS    8
#define SD_CS_READY_FOR_DATA    (1 << 8)
#define SD_CS_APP_CMD           (1 << 5)
#define SD_CS_AKE_SEQ_ERR       (1 << 3)

/* SD R2 response */
#define SD_R2_RESPONSE_BYTES  17
#define MAX_CSD_CID_BYTES     16
#define SD_R2_SET_STUFF_BITS(pR)   (pR)[16] = 0x3F
#define GET_SD_CSD_TRANS_SPEED(pR) (pR)[12]
#define GET_SD_CID_MANFID(pR)      (pR)[15]
#define GET_SD_CID_PN_1(pR)        (pR)[12]
#define GET_SD_CID_PN_2(pR)        (pR)[11]
#define GET_SD_CID_PN_3(pR)        (pR)[10]
#define GET_SD_CID_PN_4(pR)        (pR)[9]
#define GET_SD_CID_PN_5(pR)        (pR)[8]
#define GET_SD_CID_PN_6(pR)        (pR)[7]

#define GET_SD_CID_OEMID(pR)      ((((UINT16)(pR)[14]) << 8 )| (UINT16)((pR)[13]))
#define SDMMC_OCR_VOLTAGE_MASK 0x7FFFFFFF
/* SD R3 response */
#define SD_R3_RESPONSE_BYTES 6                                            
#define SD_R3_GET_OCR(pR) ((((UINT32)((pR)[1])) |  \
                           (((UINT32)((pR)[2])) << 8)  |  \
                           (((UINT32)((pR)[3])) << 16) | \
                           (((UINT32)((pR)[4])) << 24)) & SDMMC_OCR_VOLTAGE_MASK)
#define SD_R3_IS_CARD_READY(pR)  (((pR)[4] & 0x80) == 0x80)

/* OCR bit definitions */
#define SD_OCR_CARD_PWR_UP_STATUS  ((UINT32)(1 << 31))
#define SD_OCR_3_5_TO_3_6_VDD      (1 << 23)
#define SD_OCR_3_4_TO_3_5_VDD      (1 << 22)
#define SD_OCR_3_3_TO_3_4_VDD      (1 << 21)
#define SD_OCR_3_2_TO_3_3_VDD      (1 << 20)
#define SD_OCR_3_1_TO_3_2_VDD      (1 << 19)
#define SD_OCR_3_0_TO_3_1_VDD      (1 << 18)
#define SD_OCR_2_9_TO_3_0_VDD      (1 << 17)
#define SD_OCR_2_8_TO_2_9_VDD      (1 << 16)
#define SD_OCR_2_7_TO_2_8_VDD      (1 << 15)
#define SD_OCR_2_6_TO_2_7_VDD      (1 << 14)
#define SD_OCR_2_5_TO_2_6_VDD      (1 << 13)
#define SD_OCR_2_4_TO_2_5_VDD      (1 << 12)
#define SD_OCR_2_3_TO_2_4_VDD      (1 << 11)
#define SD_OCR_2_2_TO_2_3_VDD      (1 << 10)
#define SD_OCR_2_1_TO_2_2_VDD      (1 << 9)
#define SD_OCR_2_0_TO_2_1_VDD      (1 << 8)
#define SD_OCR_1_9_TO_2_0_VDD      (1 << 7)
#define SD_OCR_1_8_TO_1_9_VDD      (1 << 6)
#define SD_OCR_1_7_TO_1_8_VDD      (1 << 5)
#define SD_OCR_1_6_TO_1_7_VDD      (1 << 4)

/* SD Status data block */
#define SD_STATUS_DATA_BYTES        64
#define SDS_GET_DATA_WIDTH(buffer)  ((buffer)[0] & 0xC0)
#define SDS_BUS_1_BIT               0x00
#define SDS_BUS_4_BIT               0x80
#define SDS_GET_SECURE_MODE(buffer) ((buffer)[0] & 0x20)
#define SDS_CARD_SECURE_MODE        0x20
#define SDS_GET_CARD_TYPE(buffer)   ((buffer)[60] & 0x0F)
#define SDS_SD_CARD_RW              0x00
#define SDS_SD_CARD_ROM             0x01

/* SD R6 response */
#define SD_R6_RESPONSE_BYTES 6 
#define SD_R6_GET_RCA(pR) ((UINT16)((pR)[3]) | (((UINT16)((pR)[4])) << 8)) 
#define SD_R6_GET_CS(pR)  ((UINT16)((pR)[1]) | (((UINT16)((pR)[2])) << 8)) 

/* SD Configuration Register (SCR) */
#define SD_SCR_BYTES            8
#define SCR_REV_1_0             0x00
#define SCR_SD_SPEC_1_00        0x00
#define SCR_SD_SPEC_1_10        0x01
#define SCR_BUS_SUPPORTS_1_BIT  0x01   
#define SCR_BUS_SUPPORTS_4_BIT  0x04
#define SCR_SD_SECURITY_MASK    0x70
#define SCR_SD_NO_SECURITY      0x00
#define SCR_SD_SECURITY_1_0     0x10
#define SCR_SD_SECURITY_2_0     0x20
#define SCR_DATA_STATUS_1_AFTER_ERASE  0x80

#define GET_SD_SCR_STRUCT_VER(pB) ((pB)[7] >> 4) 
#define GET_SD_SCR_SDSPEC_VER(pB) ((pB)[7] & 0x0F) 
#define GET_SD_SCR_BUSWIDTHS(pB)  ((pB)[6] & 0x0F) 
#define GET_SD_SCR_BUSWIDTHS_FLAGS(pB)  (pB)[6]
#define GET_SD_SCR_SECURITY(pB)   (((pB)[6] >> 4) & 0x07) 
#define GET_SD_SCR_DATA_STAT_AFTER_ERASE(pB) (((pB)[6] >> 7) & 0x01) 

/* SDIO R4 Response */
#define SD_SDIO_R4_RESPONSE_BYTES 6
#define SD_SDIO_R4_GET_OCR(pR) ((UINT32)((pR)[1])        |  \
                          (((UINT32)(pR)[2]) << 8)  |  \
                          (((UINT32)(pR)[3]) << 16))
#define SD_SDIO_R4_IS_MEMORY_PRESENT(pR)   (((pR)[4] & 0x08) == 0x08)
#define SD_SDIO_R4_GET_IO_FUNC_COUNT(pR)   (((pR)[4] >> 4) & 0x07)
#define SD_SDIO_R4_IS_CARD_READY(pR)       (((pR)[4] & 0x80) == 0x80)

/* SDIO R5 response */
#define SD_SDIO_R5_RESPONSE_BYTES      6
#define SD_SDIO_R5_READ_DATA_OFFSET    1
#define SD_R5_GET_READ_DATA(pR)  (pR)[SD_SDIO_R5_READ_DATA_OFFSET]
#define SD_R5_RESP_FLAGS_OFFSET   2
#define SD_R5_GET_RESP_FLAGS(pR) (pR)[SD_R5_RESP_FLAGS_OFFSET]
#define SD_R5_SET_CMD(pR,cmd)  (pR)[5] = (cmd) & 0xC0
#define SD_R5_RESP_CMD_ERR  (1 << 7) /* for previous cmd */
#define SD_R5_ILLEGAL_CMD   (1 << 6)
#define SD_R5_GENERAL_ERR   (1 << 3)
#define SD_R5_INVALID_FUNC  (1 << 1)
#define SD_R5_ARG_RANGE_ERR (1 << 0)
#define SD_R5_CURRENT_CMD_ERRORS (SD_R5_ILLEGAL_CMD | SD_R5_GENERAL_ERR \
                                 | SD_R5_INVALID_FUNC | SD_R5_ARG_RANGE_ERR)
#define SD_R5_ERRORS (SD_R5_CURRENT_CMD_ERRORS)

#define SD_R5_GET_IO_STATE(pR) (((pR)[2] >> 4) & 0x03) 
#define SD_R5_STATE_DIS 0x00
#define SD_R5_STATE_CMD 0x01
#define SD_R5_STATE_TRN 0x02

/* SDIO Modified R6 Response */
#define SD_SDIO_R6_RESPONSE_BYTES 6
#define SD_SDIO_R6_GET_RCA(pR)  ((UINT16)((pR)[3]) | ((UINT16)((pR)[4]) << 8)) 
#define SD_SDIO_R6_GET_CSTAT(pR)((UINT16)((pR)[1]) | ((UINT16)((pR)[2]) << 8)) 

/* SPI mode R1 response */
#define SPI_R1_RESPONSE_BYTES   1    
#define GET_SPI_R1_RESP_TOKEN(pR) (pR)[0]
#define SPI_CS_STATE_IDLE       0x01
#define SPI_CS_ERASE_RESET      (1 << 1)
#define SPI_CS_ILLEGAL_CMD      (1 << 2)
#define SPI_CS_CMD_CRC_ERR      (1 << 3)
#define SPI_CS_ERASE_SEQ_ERR    (1 << 4)
#define SPI_CS_ADDRESS_ERR      (1 << 5)
#define SPI_CS_PARAM_ERR        (1 << 6)
#define SPI_CS_ERR_MASK         0x7c

/* SPI mode R2 response */
#define SPI_R2_RESPONSE_BYTES  2
#define GET_SPI_R2_RESP_TOKEN(pR)   (pR)[1]
#define GET_SPI_R2_STATUS_TOKEN(pR) (pR)[0]
/* the first response byte is defined above */
/* the second response byte is defined below */
#define SPI_CS_CARD_IS_LOCKED      (1 << 0)
#define SPI_CS_LOCK_UNLOCK_FAILED  (1 << 1)
#define SPI_CS_ERROR               (1 << 2)
#define SPI_CS_INTERNAL_ERROR      (1 << 3)
#define SPI_CS_ECC_FAILED          (1 << 4)
#define SPI_CS_WP_VIOLATION        (1 << 5)
#define SPI_CS_ERASE_PARAM_ERR     (1 << 6)
#define SPI_CS_OUT_OF_RANGE        (1 << 7)

/* SPI mode R3 response */
#define SPI_R3_RESPONSE_BYTES 5                                            
#define SPI_R3_GET_OCR(pR) ((((UINT32)((pR)[0])) |         \
                            (((UINT32)((pR)[1])) << 8)  |  \
                            (((UINT32)((pR)[2])) << 16) |  \
                            (((UINT32)((pR)[3])) << 24)) & SDMMC_OCR_VOLTAGE_MASK)
#define SPI_R3_IS_CARD_READY(pR)  (((pR)[3] & 0x80) == 0x80)
#define GET_SPI_R3_RESP_TOKEN(pR) (pR)[4]  

/* SPI mode SDIO R4 response */
#define SPI_SDIO_R4_RESPONSE_BYTES 5
#define SPI_SDIO_R4_GET_OCR(pR) ((UINT32)((pR)[0])        |  \
                          (((UINT32)(pR)[1]) << 8)   |  \
                          (((UINT32)(pR)[2]) << 16))
#define SPI_SDIO_R4_IS_MEMORY_PRESENT(pR)   (((pR)[3] & 0x08) == 0x08)
#define SPI_SDIO_R4_GET_IO_FUNC_COUNT(pR)   (((pR)[3] >> 4) & 0x07)
#define SPI_SDIO_R4_IS_CARD_READY(pR)       (((pR)[3] & 0x80) == 0x80)
#define GET_SPI_SDIO_R4_RESP_TOKEN(pR)  (pR)[4]  

/* SPI Mode SDIO R5 response */
#define SPI_SDIO_R5_RESPONSE_BYTES 2
#define GET_SPI_SDIO_R5_RESP_TOKEN(pR)     (pR)[1]
#define GET_SPI_SDIO_R5_RESPONSE_RDATA(pR) (pR)[0]
#define SPI_R5_IDLE_STATE   0x01
#define SPI_R5_ILLEGAL_CMD  (1 << 2)
#define SPI_R5_CMD_CRC      (1 << 3)
#define SPI_R5_FUNC_ERR     (1 << 4)
#define SPI_R5_PARAM_ERR    (1 << 6)

/* SDIO COMMAND 52 Definitions */
#define CMD52_READ  0
#define CMD52_WRITE 1
#define CMD52_READ_AFTER_WRITE 1
#define CMD52_NORMAL_WRITE     0   
#define SDIO_SET_CMD52_ARG(arg,rw,func,raw,address,writedata) \
    (arg) = (((rw) & 1u) << 31)          | \
            (((func) & 0x7) << 28)       | \
            (((raw) & 1) << 27)          | \
            (1 << 26)                    | \
            (((address) & 0x1FFFF) << 9) | \
            (1 << 8)                     | \
            ((writedata) & 0xFF)
#define SDIO_SET_CMD52_READ_ARG(arg,func,address) \
    SDIO_SET_CMD52_ARG(arg,CMD52_READ,(func),0,address,0x00)
#define SDIO_SET_CMD52_WRITE_ARG(arg,func,address,value) \
    SDIO_SET_CMD52_ARG(arg,CMD52_WRITE,(func),CMD52_NORMAL_WRITE,address,value)
        
/* SDIO COMMAND 53 Definitions */
#define CMD53_READ          0
#define CMD53_WRITE         1
#define CMD53_BLOCK_BASIS   1 
#define CMD53_BYTE_BASIS    0
#define CMD53_FIXED_ADDRESS 0
#define CMD53_INCR_ADDRESS  1
#define SDIO_SET_CMD53_ARG(arg,rw,func,mode,opcode,address,bytes_blocks) \
    (arg) = (((rw) & 1) << 31)                  | \
            (((func) & 0x7) << 28)              | \
            (((mode) & 1) << 27)                | \
            (((opcode) & 1) << 26)              | \
            (((address) & 0x1FFFF) << 9)        | \
            ((bytes_blocks) & 0x1FF)
            
#define SDIO_MAX_LENGTH_BYTE_BASIS  512
#define SDIO_MAX_BLOCKS_BLOCK_BASIS 511
#define SDIO_MAX_BYTES_PER_BLOCK    2048
#define SDIO_COMMON_AREA_FUNCTION_NUMBER 0
#define SDIO_FIRST_FUNCTION_NUMBER       1
#define SDIO_LAST_FUNCTION_NUMBER        7

#define CMD53_CONVERT_BYTE_BASIS_BLK_LENGTH_PARAM(b) (((b) < SDIO_MAX_LENGTH_BYTE_BASIS) ? (b) : 0)
#define CMD53_CONVERT_BLOCK_BASIS_BLK_COUNT_PARAM(b) (((b) <= SDIO_MAX_BLOCKS_BLOCK_BASIS) ? (b) : 0)


/* SDIO COMMON Registers */

/* revision register */
#define CCCR_SDIO_REVISION_REG  0x00
#define CCCR_REV_MASK           0x0F
#define CCCR_REV_1_0            0x00
#define CCCR_REV_1_1            0x01
#define CCCR_REV_1_2            0x02
#define SDIO_REV_MASK           0xF0
#define SDIO_REV_1_00           0x00
#define SDIO_REV_1_10           0x10
#define SDIO_REV_1_20           0x20
#define SDIO_REV_2_00           0x30
/* SD physical spec revision */
#define SD_SPEC_REVISION_REG    0x01
#define SD_REV_MASK             0x0F
#define SD_REV_1_01             0x00
#define SD_REV_1_10             0x01
/* I/O Enable  */
#define SDIO_ENABLE_REG         0x02
/* I/O Ready */
#define SDIO_READY_REG          0x03
/* Interrupt Enable */
#define SDIO_INT_ENABLE_REG     0x04
#define SDIO_INT_MASTER_ENABLE  0x01
#define SDIO_INT_ALL_ENABLE     0xFE
/* Interrupt Pending */
#define SDIO_INT_PENDING_REG    0x05 
#define SDIO_INT_PEND_MASK      0xFE
/* I/O Abort */
#define SDIO_IO_ABORT_REG       0x06
#define SDIO_IO_RESET           (1 << 3) 
/* Bus Interface */
#define SDIO_BUS_IF_REG         0x07
#define CARD_DETECT_DISABLE     0x80
#define SDIO_BUS_WIDTH_1_BIT    0x00
#define SDIO_BUS_WIDTH_4_BIT    0x02
/* Card Capabilities */
#define SDIO_CARD_CAPS_REG          0x08
#define SDIO_CAPS_CMD52_WHILE_DATA  0x01   /* card can issue CMD52 while data transfer */
#define SDIO_CAPS_MULTI_BLOCK       0x02   /* card supports multi-block data transfers */
#define SDIO_CAPS_READ_WAIT         0x04   /* card supports read-wait protocol */
#define SDIO_CAPS_SUSPEND_RESUME    0x08   /* card supports I/O function suspend/resume */
#define SDIO_CAPS_INT_MULTI_BLK     0x10   /* interrupts between multi-block data capable */
#define SDIO_CAPS_ENB_INT_MULTI_BLK 0x20   /* enable ints between muli-block data */
#define SDIO_CAPS_LOW_SPEED         0x40   /* low speed card */
#define SDIO_CAPS_4BIT_LS           0x80   /* 4 bit low speed card */
/* Common CIS pointer */
#define SDIO_CMN_CIS_PTR_LOW_REG    0x09
#define SDIO_CMN_CIS_PTR_MID_REG    0x0a
#define SDIO_CMN_CIS_PTR_HI_REG     0x0b
/* Bus suspend */
#define SDIO_BUS_SUSPEND_REG            0x0c
#define SDIO_FUNC_SUSPEND_STATUS_MASK   0x01 /* selected function is suspended */
#define SDIO_SUSPEND_FUNCTION           0x02 /* suspend the current selected function */
/* Function select (for bus suspension) */
#define SDIO_FUNCTION_SELECT_REG        0x0d
#define SDIO_SUSPEND_FUNCTION_0         0x00 
#define SDIO_SUSPEND_MEMORY_FUNC_MASK    0x08
/* Function Execution */
#define SDIO_FUNCTION_EXEC_REG          0x0e
#define SDIO_MEMORY_FUNC_EXEC_MASK      0x01
/* Function Ready */
#define SDIO_FUNCTION_READY_REG          0x0f
#define SDIO_MEMORY_FUNC_BUSY_MASK       0x01

/* power control 1.10 only  */
#define SDIO_POWER_CONTROL_REG            0x12
#define SDIO_POWER_CONTROL_SMPC           0x01
#define SDIO_POWER_CONTROL_EMPC           0x02

/* high speed control , 1.20 only */
#define SDIO_HS_CONTROL_REG               0x13
#define SDIO_HS_CONTROL_SHS               0x01
#define SDIO_HS_CONTROL_EHS               0x02

/* Function Base Registers */
#define xFUNCTION_FBR_OFFSET(funcNo) (0x100*(funcNo))
/* offset calculation that does not use multiplication */
static INLINE UINT32 CalculateFBROffset(UCHAR FuncNo) {
    UCHAR i = FuncNo;
    UINT32 offset = 0;
    while (i) {
        offset += 0x100;
        i--; 
    }   
    return offset; 
}
/* Function info */
#define FBR_FUNC_INFO_REG_OFFSET(fbr)   ((fbr) + 0x00)
#define FUNC_INFO_SUPPORTS_CSA_MASK     0x40
#define FUNC_INFO_ENABLE_CSA            0x80
#define FUNC_INFO_DEVICE_CODE_MASK      0x0F
#define FUNC_INFO_DEVICE_CODE_LAST      0x0F
#define FBR_FUNC_EXT_DEVICE_CODE_OFFSET(fbr) ((fbr) + 0x01)
/* Function Power selection */
#define FBR_FUNC_POWER_SELECT_OFFSET(fbr)    ((fbr) + 0x02)
#define FUNC_POWER_SELECT_SPS           0x01
#define FUNC_POWER_SELECT_EPS           0x02
/* Function CIS ptr */
#define FBR_FUNC_CIS_LOW_OFFSET(fbr)   ((fbr) + 0x09)
#define FBR_FUNC_CIS_MID_OFFSET(fbr)   ((fbr) + 0x0a)
#define FBR_FUNC_CIS_HI_OFFSET(fbr)    ((fbr) + 0x0b)
/* Function CSA ptr */
#define FBR_FUNC_CSA_LOW_OFFSET(fbr)   ((fbr) + 0x0c)
#define FBR_FUNC_CSA_MID_OFFSET(fbr)   ((fbr) + 0x0d)
#define FBR_FUNC_CSA_HI_OFFSET(fbr)    ((fbr) + 0x0e)
/* Function CSA data window */
#define FBR_FUNC_CSA_DATA_OFFSET(fbr)  ((fbr) + 0x0f)
/* Function Block Size Control */
#define FBR_FUNC_BLK_SIZE_LOW_OFFSET(fbr)  ((fbr) + 0x10)
#define FBR_FUNC_BLK_SIZE_HI_OFFSET(fbr)   ((fbr) + 0x11)
#define SDIO_CIS_AREA_BEGIN   0x00001000
#define SDIO_CIS_AREA_END     0x00017fff
/* Tuple definitions */
#define CISTPL_NULL         0x00
#define CISTPL_CHECKSUM     0x10
#define CISTPL_VERS_1       0x15
#define CISTPL_ALTSTR       0x16
#define CISTPL_MANFID       0x20
#define CISTPL_FUNCID       0x21
#define CISTPL_FUNCE        0x22
#define CISTPL_VENDOR       0x91
#define CISTPL_END          0xff
#define CISTPL_LINK_END     0xff


/* these structures must be packed */

#include "ctstartpack.h"  

/* Manufacturer ID tuple */
struct SDIO_MANFID_TPL {
    UINT16  ManufacturerCode;   /* jedec code */
    UINT16  ManufacturerInfo;   /* manufacturer specific code */
}CT_PACK_STRUCT;

/* Function ID Tuple */
struct SDIO_FUNC_ID_TPL {
    UINT8  DeviceCode;  /* device code */
    UINT8  InitMask;    /* system initialization mask (not used) */
}CT_PACK_STRUCT;

    /* Extended Function Tuple (Common) */
struct SDIO_FUNC_EXT_COMMON_TPL {
    UINT8   Type;                               /* type */
    UINT16  Func0_MaxBlockSize;                 /* max function 0 block transfer size */
    UINT8   MaxTransSpeed;                      /* max transfer speed (encoded) */
#define TRANSFER_UNIT_MULTIPIER_MASK  0x07
#define TIME_VALUE_MASK               0x78
#define TIME_VALUE_SHIFT              3
}CT_PACK_STRUCT;

/* Extended Function Tuple (Per Function) */
struct SDIO_FUNC_EXT_FUNCTION_TPL {
    UINT8   Type;                               /* type */
#define SDIO_FUNC_INFO_WAKEUP_SUPPORT 0x01
    UINT8   FunctionInfo;                       /* function info */
    UINT8   SDIORev;                            /* revision */
    UINT32  CardPSN;                            /* product serial number */
    UINT32  CSASize;                            /* CSA size */
    UINT8   CSAProperties;                      /* CSA properties */
    UINT16  MaxBlockSize;                       /* max block size for block transfers */
    UINT32  FunctionOCR;                        /* optimal function OCR */
    UINT8   OpMinPwr;                           /* operational min power */
    UINT8   OpAvgPwr;                           /* operational average power */
    UINT8   OpMaxPwr;                           /* operation maximum power */
    UINT8   SbMinPwr;                           /* standby minimum power */
    UINT8   SbAvgPwr;                           /* standby average power */
    UINT8   SbMaxPwr;                           /* standby maximum power */
    UINT16  MinBandWidth;                       /* minimum bus bandwidth */    
    UINT16  OptBandWidth;                       /* optimalbus bandwitdh */
}CT_PACK_STRUCT;

struct SDIO_FUNC_EXT_FUNCTION_TPL_1_1 {
    struct SDIO_FUNC_EXT_FUNCTION_TPL CommonInfo;  /* from 1.0*/
    UINT16  EnableTimeOut;                  /* timeout for enable */
    UINT16  OperPwrMaxPwr;
    UINT16  OperPwrAvgPwr;
    UINT16  HiPwrMaxPwr;
    UINT16  HiPwrAvgPwr;
    UINT16  LowPwrMaxPwr;
    UINT16  LowPwrAvgPwr;
}CT_PACK_STRUCT;

#include "ctendpack.h" 

static INLINE SDIO_STATUS ConvertCMD52ResponseToSDIOStatus(UINT8 CMD52ResponseFlags) {
    if (!(CMD52ResponseFlags & SD_R5_ERRORS)) {
        return SDIO_STATUS_SUCCESS;
    }
    if (CMD52ResponseFlags & SD_R5_ILLEGAL_CMD) {
        return SDIO_STATUS_DATA_STATE_INVALID;  
    } else if (CMD52ResponseFlags & SD_R5_INVALID_FUNC) {
        return SDIO_STATUS_INVALID_FUNC;
    } else if (CMD52ResponseFlags & SD_R5_ARG_RANGE_ERR) {
        return SDIO_STATUS_FUNC_ARG_ERROR; 
    } else {
        return SDIO_STATUS_DATA_ERROR_UNKNOWN;   
    }    
}
         
/* CMD6 mode switch definitions */         
    
#define SD_SWITCH_FUNC_CHECK    0
#define SD_SWITCH_FUNC_SET      ((UINT32)(1u << 31))
#define SD_FUNC_NO_SELECT_MASK  0x00FFFFFF
#define SD_SWITCH_GRP_1         0
#define SD_SWITCH_GRP_2         1
#define SD_SWITCH_GRP_3         2
#define SD_SWITCH_GRP_4         3
#define SD_SWITCH_GRP_5         4
#define SD_SWITCH_GRP_6         5

#define SD_SWITCH_HIGH_SPEED_GROUP     SD_SWITCH_GRP_1
#define SD_SWITCH_HIGH_SPEED_FUNC_NO   1

#define SD_SWITCH_MAKE_SHIFT(grp) ((grp) * 4)

#define SD_SWITCH_MAKE_GRP_PATTERN(FuncGrp,FuncNo) \
     ((SD_FUNC_NO_SELECT_MASK & (~(0xF << SD_SWITCH_MAKE_SHIFT(FuncGrp)))) |  \
        (((FuncNo) & 0xF) << SD_SWITCH_MAKE_SHIFT(FuncGrp)))                 \
        
#define SD_SWITCH_FUNC_ARG_GROUP_CHECK(FuncGrp,FuncNo) \
    (SD_SWITCH_FUNC_CHECK | SD_SWITCH_MAKE_GRP_PATTERN((FuncGrp),(FuncNo)))

#define SD_SWITCH_FUNC_ARG_GROUP_SET(FuncGrp,FuncNo)   \
    (SD_SWITCH_FUNC_SET | SD_SWITCH_MAKE_GRP_PATTERN((FuncGrp),(FuncNo)))

#define SD_SWITCH_FUNC_STATUS_BLOCK_BYTES 64 

#define SD_SWITCH_FUNC_STATUS_GET_GRP_BIT_MASK(pBuffer,FuncGrp) \
    (USHORT)((pBuffer)[50 + ((FuncGrp)*2)] | ((pBuffer)[51 + ((FuncGrp)*2)] << 8))

#define SD_SWITCH_FUNC_STATUS_GET_MAX_CURRENT(pBuffer) \
     (USHORT)((pBuffer)[62] | ((pBuffer)[63] << 8))
     
static INLINE UINT8 SDSwitchGetSwitchResult(PUINT8 pBuffer, UINT8 FuncGrp)
{ 
    switch (FuncGrp) {
        case 0:
            return (pBuffer[47] & 0xF);
        case 1:
            return (pBuffer[47] >> 4);
        case 2:
            return (pBuffer[48] & 0xF);
        case 3:
            return (pBuffer[48] >> 4);
        case 4:
            return (pBuffer[49] & 0xF);
        case 5:
            return (pBuffer[49] >> 4);
        default:
            return 0xF;
    }    
}

#endif
