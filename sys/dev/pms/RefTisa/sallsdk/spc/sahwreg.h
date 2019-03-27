/*******************************************************************************
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
*following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided
*with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED 
*WARRANTIES,INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
*FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
*NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
*LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
*SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*
* $FreeBSD$
*
********************************************************************************/
/*******************************************************************************/
/*! \file sahwreg.h
 *  \brief The file defines the register offset of hardware
 */
/******************************************************************************/
#ifndef  __SAHWREG_H__

#define __SAHWREG_H__

/* #define MSGU_ACCESS_VIA_XCBI  */ /* Defined in build script now */

/* Message Unit Registers - BAR0(0x10), BAR0(win) */
#ifdef SPC_I2O_ENABLE
/* i2o=1 space register offsets - MU_I2O_ENABLE */
/* Currently FPGA use these offset */
#define MSGU_IBDB_SET                            0x20
#define MSGU_HOST_INT_STATUS                     0x30
#define MSGU_HOST_INT_MASK                       0x34
#define MSGU_IOPIB_INT_STATUS                    0x40
#define MSGU_IOPIB_INT_MASK                      0x44
#define MSGU_IBDB_CLEAR                          0x70
#define MSGU_MSGU_CONTROL                        0x74
#define MSGU_ODR                                 0x9C
#define MSGU_ODCR                                0xA0
#define MSGU_SCRATCH_PAD_0                       0xB0
#define MSGU_SCRATCH_PAD_1                       0xB4
#define MSGU_SCRATCH_PAD_2                       0xB8
#define MSGU_SCRATCH_PAD_3                       0xBC
#else
/* i2o=0 space register offsets - ~MU_I2O_ENABLE */
#define MSGU_IBDB_SET                            0x04  /* RevA - Write only, RevB - Read/Write */
#define MSGU_HOST_INT_STATUS                     0x08
#define MSGU_HOST_INT_MASK                       0x0C
#define MSGU_IOPIB_INT_STATUS                    0x18
#define MSGU_IOPIB_INT_MASK                      0x1C
#define MSGU_IBDB_CLEAR                          0x20  /* RevB - Host not use */
#define MSGU_MSGU_CONTROL                        0x24
#define MSGU_ODR                                 0x3C  /* RevB */
#define MSGU_ODCR                                0x40  /* RevB */
#define MSGU_SCRATCH_PAD_0                       0x44
#define MSGU_SCRATCH_PAD_1                       0x48
#define MSGU_SCRATCH_PAD_2                       0x4C
#define MSGU_SCRATCH_PAD_3                       0x50
#define MSGU_HOST_SCRATCH_PAD_0                  0x54
#define MSGU_HOST_SCRATCH_PAD_1                  0x58
#define MSGU_HOST_SCRATCH_PAD_2                  0x5C
#define MSGU_HOST_SCRATCH_PAD_3                  0x60
#define MSGU_HOST_SCRATCH_PAD_4                  0x64
#define MSGU_HOST_SCRATCH_PAD_5                  0x68
#define MSGU_HOST_SCRATCH_PAD_6                  0x6C
#define MSGU_HOST_SCRATCH_PAD_7                  0x70
#define MSGU_ODMR                                0x74  /* RevB */
#endif




/*
Table 215   Messaging Unit Address Map
Offset (Hex) Name Access Internal Offset Internal Name Comment
*/

#define V_Inbound_Doorbell_Set_Register          0x00    /* Host R/W Local INT 0x0 MSGU - Inbound Doorbell Set */
#define V_Inbound_Doorbell_Set_RegisterU         0x04    /* Host R/W Local INT 0x4 MSGU - Inbound Doorbell Set */
#define V_Inbound_Doorbell_Clear_Register        0x08    /* Host No access Local  W, R all 0s 0x8 MSGU - Inbound Doorbell Clear */
#define V_Inbound_Doorbell_Clear_RegisterU       0x0C    /* Host No access Local  W, R all 0s 0xC MSGU - Inbound Doorbell Clear */
#define V_Inbound_Doorbell_Mask_Set_Register     0x10    /* Host RO Local R/W 0x10 MSGU - Inbound Doorbell Mask Set New in SPCv */
#define V_Inbound_Doorbell_Mask_Set_RegisterU    0x14    /* Host RO Local R/W 0x14 MSGU - Inbound Doorbell Mask Set New in SPCv */
#define V_Inbound_Doorbell_Mask_Clear_Register   0x18    /* Host RO Local W, R all 0s 0x18 MSGU - Inbound Doorbell Mask Clear New in SPCv */
#define V_Inbound_Doorbell_Mask_Clear_RegisterU  0x1C    /* Host RO Local W, R all 0s 0x1C MSGU - Inbound Doorbell Mask Clear New in SPCv */
#define V_Outbound_Doorbell_Set_Register         0x20    /* Host RO Local R/W 0x20 MSGU - Outbound Doorbell Set */
#define V_Outbound_Doorbell_Set_RegisterU        0x24    /* Host RO Local R/W 0x24 MSGU - Outbound Doorbell Set */
#define V_Outbound_Doorbell_Clear_Register       0x28    /* Host W, R all 0s Local  RO 0x28 MSGU - Outbound Doorbell Clear */
#define V_Outbound_Doorbell_Clear_RegisterU      0x2C    /* Host W, R all 0s Local  RO 0x2C MSGU - Outbound Doorbell Clear */
#define V_Outbound_Doorbell_Mask_Set_Register    0x30    /* Host RW  Local RO 0x30 MSGU - Outbound Doorbell Mask Set 1's set */
#define V_Outbound_Doorbell_Mask_Set_RegisterU   0x34    /* Host RW  Local RO 0x30 MSGU - Outbound Doorbell Mask Set 1's set */
#define V_Outbound_Doorbell_Mask_Clear_Register  0x38    /* Host W, R all 0s Local RO 0x38 MSGU - Outbound Doorbell Mask Clear New in SPCv 1's clear */
#define V_Outbound_Doorbell_Mask_Clear_RegisterU 0x3C    /* Host W, R all 0s Local RO 0x38 MSGU - Outbound Doorbell Mask Clear New in SPCv 1's clear */
/* 0x40 Reserved  R all 0s */
#define V_Scratchpad_0_Register                 0x44    /* Host RO Local R/W 0x120 MSGU - Scratchpad 0 */
#define V_Scratchpad_1_Register                 0x48    /* Host RO Local R/W 0x128 MSGU - Scratchpad 1 */
#define V_Scratchpad_2_Register                 0x4C    /* Host RO Local R/W 0x130 MSGU - Scratchpad 2 */
#define V_Scratchpad_3_Register                 0x50    /* Host RO Local R/W 0x138 MSGU - Scratchpad 3 */
#define V_Host_Scratchpad_0_Register            0x54    /* Host RW Local RO 0x140 MSGU - Scratchpad 4 */
#define V_Host_Scratchpad_1_Register            0x58    /* Host RW Local RO 0x148 MSGU - Scratchpad 5 */
#define V_Host_Scratchpad_2_Register            0x5C    /* Host RW Local RO 0x150 MSGU - Scratchpad 6 */
#define V_Host_Scratchpad_3_Register            0x60    /* Host RW Local RO 0x158 MSGU - Scratchpad 7 */
#define V_Host_Scratchpad_4_Register            0x64    /* Host RW Local R/W 0x160 MSGU - Scratchpad 8 */
#define V_Host_Scratchpad_5_Register            0x68    /* Host RW Local R/W 0x168 MSGU - Scratchpad 9 */
#define V_Scratchpad_Rsvd_0_Register            0x6C    /* Host RW Local R/W 0x170 MSGU - Scratchpad 10 */
#define V_Scratchpad_Rsvd_1_Register            0x70    /* Host RW Local R/W 0x178 MSGU - Scratchpad 11 */
/* 0x74 - 0xFF Reserved R all 0s */
#define V_Outbound_Queue_Consumer_Indices_Base  0x100  /*  typical value real offset is read from table to 0x1FF Host RW Local RO 0x1F100 – 0x1F1FF In DQ storage area*/
#define V_Inbound_Queue_Producer_Indices        0x200  /*  typical value real offset is read from table to 0x3FF Host RW Local RO 0x1F200 – 0x1F3FF In DQ storage area, also mapped as WSM*/
/*
               SPC_V                                                 SPC
     Bar     Name                                 Offset     Bar     Name                    Offset
  PCIBAR0, V_Inbound_Doorbell_Set_Register,         0x00   PCIBAR0, MSGU_IBDB_SET,            0x04
  PCIBAR0, V_Inbound_Doorbell_Clear_Register,       0x08       NA
  PCIBAR0, V_Inbound_Doorbell_Mask_Set_Register,    0x10       NA
  PCIBAR0, V_Inbound_Doorbell_Mask_Clear_Register,  0x18       NA
  PCIBAR0, V_Outbound_Doorbell_Set_Register,        0x20   PCIBAR0, MSGU_ODR,                 0x3C
  PCIBAR0, V_Outbound_Doorbell_Clear_Register,      0x28   PCIBAR0, MSGU_ODCR,                0x40
  PCIBAR0, V_Outbound_Doorbell_Mask_Set_Register,   0x30   PCIBAR0, MSGU_ODMR,                0x74
  PCIBAR0, V_Outbound_Doorbell_Mask_Clear_Register, 0x38       NA
  PCIBAR0, V_Scratchpad_0_Register,                 0x44   PCIBAR0, MSGU_SCRATCH_PAD_0,       0x44
  PCIBAR0, V_Scratchpad_1_Register,                 0x48   PCIBAR0, MSGU_SCRATCH_PAD_1,       0x48
  PCIBAR0, V_Scratchpad_2_Register,                 0x4C   PCIBAR0, MSGU_SCRATCH_PAD_2,       0x4C
  PCIBAR0, V_Scratchpad_3_Register,                 0x50   PCIBAR0, MSGU_SCRATCH_PAD_3,       0x50
  PCIBAR0, V_Host_Scratchpad_0_Register,            0x54   PCIBAR0, MSGU_HOST_SCRATCH_PAD_0,  0x54
  PCIBAR0, V_Host_Scratchpad_1_Register,            0x58   PCIBAR0, MSGU_HOST_SCRATCH_PAD_1,  0x58
  PCIBAR0, V_Host_Scratchpad_2_Register,            0x5C   PCIBAR0, MSGU_HOST_SCRATCH_PAD_2,  0x5C
  PCIBAR0, V_Host_Scratchpad_3_Register,            0x60   PCIBAR0, MSGU_HOST_SCRATCH_PAD_3,  0x60

*/


#define V_RamEccDbErr               0x00000018
#define V_SoftResetRegister        0x1000
#define V_MEMBASE_II_ShiftRegister 0x1010

#define V_GsmConfigReset                0
#define V_GsmReadAddrParityCheck    0x38
#define V_GsmWriteAddrParityCheck   0x40
#define V_GsmWriteDataParityCheck   0x48
#define V_GsmReadAddrParityIndic    0x58
#define V_GsmWriteAddrParityIndic   0x60
#define V_GsmWriteDataParityIndic   0x68


#define SPCv_Reset_Reserved             0xFFFFFF3C
#define SPCv_Reset_Read_Mask                  0xC0
#define SPCv_Reset_Read_NoReset               0x0
#define SPCv_Reset_Read_NormalResetOccurred   0x40
#define SPCv_Reset_Read_SoftResetHDAOccurred  0x80
#define SPCv_Reset_Read_ChipResetOccurred     0xC0


#define SPCv_Reset_Write_NormalReset      0x1
#define SPCv_Reset_Write_SoftResetHDA     0x2
#define SPCv_Reset_Write_ChipReset        0x3

/* [31:8] Reserved -- Reserved Host R / Local R/W */

/* Indicator that a controller soft reset has occurred.
The bootloader sets this field when a soft reset occurs. Host is read only.
[7:6]
b00: No soft reset occurred. Device reset value.
b01: Normal soft reset occurred.
b10: Soft reset HDA mode occurred.
b11: Chip reset occurred.
Soft Reset Occurred SFT_RST_OCR
[5:2] Reserved -- Reserved b0000 Reserved
Host R/W / Local R
The controller soft reset type that is required by the host side. The host sets this field and the bootloader clears it.
b00: Ready for soft reset / normal status.
b01: Normal soft reset.
b10: Soft reset HDA mode.
b11: Chip reset.
Soft Reset Requested
SFT_RST_RQST
[1:0]
 */




/***** RevB - ODAR - Outbound DoorBell Auto-Clearing Register
              ICT  - Interrupt Coalescing Timer Register
              ICC  - Interrupt Coalescing Control Register
            - BAR2(0x18), BAR1(win) *****/
/****************** 64 KB BAR *****************/
#define SPC_ODAR                                 0x00335C
#define SPC_ICTIMER                              0x0033C0
#define SPC_ICCONTROL                            0x0033C4

/* BAR2(0x18), BAR1(win) */
#define MSGU_XCBI_IBDB_REG                       0x003034 /* PCIE - Message Unit Inbound Doorbell register */
#define MSGU_XCBI_OBDB_REG                       0x003354 /* PCIE - Message Unit Outbound Doorbell Interrupt Register */
#define MSGU_XCBI_OBDB_MASK                      0x003358 /* PCIE - Message Unit Outbound Doorbell Interrupt Mask Register */
#define MSGU_XCBI_OBDB_CLEAR                     0x00303C /* PCIE - Message Unit Outbound Doorbell Interrupt Clear Register */

/* RB6 offset */
#define SPC_RB6_OFFSET                           0x80C0

#define RB6_MAGIC_NUMBER_RST                     0x1234   /* Magic number of soft reset for RB6 */

#ifdef MSGU_ACCESS_VIA_XCBI
#define MSGU_READ_IDR  ossaHwRegReadExt(agRoot, PCIBAR1, MSGU_XCBI_IBDB_REG)
#define MSGU_READ_ODMR ossaHwRegReadExt(agRoot, PCIBAR1, MSGU_XCBI_OBDB_MASK)
#define MSGU_READ_ODR  ossaHwRegReadExt(agRoot, PCIBAR1, MSGU_XCBI_OBDB_REG)
#define MSGU_READ_ODCR ossaHwRegReadExt(agRoot, PCIBAR1, MSGU_XCBI_OBDB_CLEAR)
#else
#define MSGU_READ_IDR  siHalRegReadExt(agRoot, GEN_MSGU_IBDB_SET, MSGU_IBDB_SET)
#define MSGU_READ_ODMR siHalRegReadExt(agRoot, GEN_MSGU_ODMR,     MSGU_ODMR)
#define MSGU_READ_ODR  siHalRegReadExt(agRoot, GEN_MSGU_ODR,      MSGU_ODR)
#define MSGU_READ_ODCR siHalRegReadExt(agRoot, GEN_MSGU_ODCR,     MSGU_ODCR)
#endif

/* bit definition for ODMR register */
#define ODMR_MASK_ALL                            0xFFFFFFFF   /* mask all interrupt vector */
#define ODMR_CLEAR_ALL                           0            /* clear all interrupt vector */
/* bit definition for ODMR register */
#define ODCR_CLEAR_ALL                           0xFFFFFFFF   /* mask all interrupt vector */

/* bit definition for Inbound Doorbell register */
#define IBDB_IBQ_UNFREEZE                        0x08         /* Inbound doorbell bit3 */
#define IBDB_IBQ_FREEZE                          0x04         /* Inbound doorbell bit2 */
#define IBDB_CFG_TABLE_RESET                     0x02         /* Inbound doorbell bit1 */
#define IBDB_CFG_TABLE_UPDATE                    0x01         /* Inbound doorbell bit0 */

#define IBDB_MPIIU                               0x08         /* Inbound doorbell bit3 - Unfreeze */
#define IBDB_MPIIF                               0x04         /* Inbound doorbell bit2 - Freeze */
#define IBDB_MPICT                               0x02         /* Inbound doorbell bit1 - Termination */
#define IBDB_MPIINI                              0x01         /* Inbound doorbell bit0 - Initialization */

/* bit mask definition for Scratch Pad0 register */
#define SCRATCH_PAD0_BAR_MASK                    0xFC000000   /* bit31-26 - mask bar */
#define SCRATCH_PAD0_OFFSET_MASK                 0x03FFFFFF   /* bit25-0  - offset mask */
#define SCRATCH_PAD0_AAPERR_MASK                 0xFFFFFFFF   /* if AAP error state */

/* state definition for Scratch Pad1 register */
#define SCRATCH_PAD1_POR                         0x00         /* power on reset state */
#define SCRATCH_PAD1_SFR                         0x01         /* soft reset state */
#define SCRATCH_PAD1_ERR                         0x02         /* error state */
#define SCRATCH_PAD1_RDY                         0x03         /* ready state */
#define SCRATCH_PAD1_RST                         0x04         /* soft reset toggle flag */
#define SCRATCH_PAD1_AAP1RDY_RST                 0x08         /* AAP1 ready for soft reset */
#define SCRATCH_PAD1_STATE_MASK                  0xFFFFFFF0   /* ScratchPad1 Mask other bits 31:4, bit1-0 State */
#define SCRATCH_PAD1_RESERVED                    0x000000F0   /* Scratch Pad1 Reserved bit 4 to 7 */



#define SCRATCH_PAD1_V_RAAE_MASK                 0x00000003   /* 0 1 also  ready */
#define SCRATCH_PAD1_V_RAAE_ERR                  0x00000002   /* 1 */
#define SCRATCH_PAD1_V_ILA_MASK                  0x0000000C   /* 2 3 also  ready */
#define SCRATCH_PAD1_V_ILA_ERR                   0x00000008   /* 3  */
#define SCRATCH_PAD1_V_BOOTSTATE_MASK            0x00000070   /* 456 */
#define SCRATCH_PAD1_V_BOOTSTATE_SUCESS          0x00000000   /* Load successful */
#define SCRATCH_PAD1_V_BOOTSTATE_HDA_SEEPROM     0x00000010   /* HDA Mode SEEPROM Setting */
#define SCRATCH_PAD1_V_BOOTSTATE_HDA_BOOTSTRAP   0x00000020   /* HDA Mode BootStrap Setting */
#define SCRATCH_PAD1_V_BOOTSTATE_HDA_SOFTRESET   0x00000030   /* HDA Mode Soft Reset */
#define SCRATCH_PAD1_V_BOOTSTATE_CRIT_ERROR      0x00000040   /* HDA Mode due to critical error */
#define SCRATCH_PAD1_V_BOOTSTATE_R1              0x00000050   /* Reserved */
#define SCRATCH_PAD1_V_BOOTSTATE_R2              0x00000060   /* Reserved */
#define SCRATCH_PAD1_V_BOOTSTATE_FATAL           0x00000070   /* Fatal Error  Boot process halted */


#define SCRATCH_PAD1_V_ILA_IMAGE                 0x00000080   /* 7 */
#define SCRATCH_PAD1_V_FW_IMAGE                  0x00000100   /* 8 */
#define SCRATCH_PAD1_V_BIT9_RESERVED             0x00000200   /* 9 */
#define SCRATCH_PAD1_V_IOP0_MASK                 0x00000C00   /* 10 11 also ready  */
#define SCRATCH_PAD1_V_IOP0_ERR                  0x00000800   /* 11   */
#define SCRATCH_PAD1_V_IOP1_MASK                 0x00003000   /* 12 13 also ready */
#define SCRATCH_PAD1_V_IOP1_ERR                  0x00002000   /* 13  */
#define SCRATCH_PAD1_V_RESERVED                  0xFFFFC000   /* 14-31  */

#define SCRATCH_PAD1_V_READY                    ( SCRATCH_PAD1_V_RAAE_MASK | SCRATCH_PAD1_V_ILA_MASK | SCRATCH_PAD1_V_IOP0_MASK ) /*  */
#define SCRATCH_PAD1_V_ERROR                    ( SCRATCH_PAD1_V_RAAE_ERR  | SCRATCH_PAD1_V_ILA_ERR  | SCRATCH_PAD1_V_IOP0_ERR  | SCRATCH_PAD1_V_IOP1_ERR  )  /* Scratch Pad1 13 11 3 1 */

#define SCRATCH_PAD1_V_ILA_ERROR_STATE(ScratchPad1)  ((((ScratchPad1) & SCRATCH_PAD1_V_ILA_MASK ) == SCRATCH_PAD1_V_ILA_MASK) ?  0: \
                                                      (((ScratchPad1) & SCRATCH_PAD1_V_ILA_MASK ) == SCRATCH_PAD1_V_ILA_ERR ) ?  SCRATCH_PAD1_V_ILA_ERR : 0 )

#define SCRATCH_PAD1_V_RAAE_ERROR_STATE(ScratchPad1) ((((ScratchPad1) & SCRATCH_PAD1_V_RAAE_MASK ) == SCRATCH_PAD1_V_RAAE_MASK) ?  0: \
                                                      (((ScratchPad1) & SCRATCH_PAD1_V_RAAE_MASK ) == SCRATCH_PAD1_V_RAAE_ERR)  ?  SCRATCH_PAD1_V_RAAE_ERR : 0 )

#define SCRATCH_PAD1_V_IOP0_ERROR_STATE(ScratchPad1) ((((ScratchPad1) & SCRATCH_PAD1_V_IOP0_MASK ) == SCRATCH_PAD1_V_IOP0_MASK) ?  0: \
                                                      (((ScratchPad1) & SCRATCH_PAD1_V_IOP0_MASK ) == SCRATCH_PAD1_V_IOP0_ERR)  ?  SCRATCH_PAD1_V_IOP0_ERR : 0 )

#define SCRATCH_PAD1_V_IOP1_ERROR_STATE(ScratchPad1) ((((ScratchPad1) & SCRATCH_PAD1_V_IOP1_MASK ) == SCRATCH_PAD1_V_IOP1_MASK) ?  0: \
                                                      (((ScratchPad1) & SCRATCH_PAD1_V_IOP1_MASK ) == SCRATCH_PAD1_V_IOP1_ERR)  ?  SCRATCH_PAD1_V_IOP1_ERR : 0 )

#define SCRATCH_PAD1_V_ERROR_STATE(ScratchPad1) ( SCRATCH_PAD1_V_ILA_ERROR_STATE(ScratchPad1)  | \
                                                  SCRATCH_PAD1_V_RAAE_ERROR_STATE(ScratchPad1) | \
                                                  SCRATCH_PAD1_V_IOP0_ERROR_STATE(ScratchPad1) | \
                                                  SCRATCH_PAD1_V_IOP1_ERROR_STATE(ScratchPad1) )

#define SCRATCH_PAD1_V_BOOTLDR_ERROR             0x00000070   /* Scratch Pad1 (6 5 4) */


/* error bit definition */
#define SCRATCH_PAD1_BDMA_ERR                    0x80000000   /* bit31 */
#define SCRATCH_PAD1_GSM_ERR                     0x40000000   /* bit30 */
#define SCRATCH_PAD1_MBIC1_ERR                   0x20000000   /* bit29 */
#define SCRATCH_PAD1_MBIC1_SET0_ERR              0x10000000   /* bit28 */
#define SCRATCH_PAD1_MBIC1_SET1_ERR              0x08000000   /* bit27 */
#define SCRATCH_PAD1_PMIC1_ERR                   0x04000000   /* bit26 */
#define SCRATCH_PAD1_PMIC2_ERR                   0x02000000   /* bit25 */
#define SCRATCH_PAD1_PMIC_EVENT_ERR              0x01000000   /* bit24 */
#define SCRATCH_PAD1_OSSP_ERR                    0x00800000   /* bit23 */
#define SCRATCH_PAD1_SSPA_ERR                    0x00400000   /* bit22 */
#define SCRATCH_PAD1_SSPL_ERR                    0x00200000   /* bit21 */
#define SCRATCH_PAD1_HSST_ERR                    0x00100000   /* bit20 */
#define SCRATCH_PAD1_PCS_ERR                     0x00080000   /* bit19 */
#define SCRATCH_PAD1_FW_INIT_ERR                 0x00008000   /* bit15 */
#define SCRATCH_PAD1_FW_ASRT_ERR                 0x00004000   /* bit14 */
#define SCRATCH_PAD1_FW_WDG_ERR                  0x00002000   /* bit13 */
#define SCRATCH_PAD1_AAP_ERROR_STATE             0x00000002   /* bit1 */
#define SCRATCH_PAD1_AAP_READY                   0x00000003   /* bit1 & bit0 */


/* state definition for Scratch Pad2 register */
#define SCRATCH_PAD2_POR                         0x00         /* power on state */
#define SCRATCH_PAD2_SFR                         0x01         /* soft reset state */
#define SCRATCH_PAD2_ERR                         0x02         /* error state */
#define SCRATCH_PAD2_RDY                         0x03         /* ready state */
#define SCRATCH_PAD2_FWRDY_RST                   0x04         /* FW ready for soft reset rdy flag */
#define SCRATCH_PAD2_IOPRDY_RST                  0x08         /* IOP ready for soft reset */
#define SCRATCH_PAD2_STATE_MASK                  0xFFFFFFF0   /* ScratchPad 2 Mask for other bits 31:4, bit1-0 State*/
#define SCRATCH_PAD2_RESERVED                    0x000000F0   /* Scratch Pad1 Reserved bit 4 to 7 */

/* error bit definition */
#define SCRATCH_PAD2_BDMA_ERR                    0x80000000   /* bit31 */
#define SCRATCH_PAD2_GSM_ERR                     0x40000000   /* bit30 */
#define SCRATCH_PAD2_MBIC3_ERR                   0x20000000   /* bit29 */
#define SCRATCH_PAD2_MBIC3_SET0_ERR              0x10000000   /* bit28 */
#define SCRATCH_PAD2_MBIC3_SET1_ERR              0x08000000   /* bit27 */
#define SCRATCH_PAD2_PMIC1_ERR                   0x04000000   /* bit26 */
#define SCRATCH_PAD2_PMIC2_ERR                   0x02000000   /* bit25 */
#define SCRATCH_PAD2_PMIC_EVENT_ERR              0x01000000   /* bit24 */
#define SCRATCH_PAD2_OSSP_ERR                    0x00800000   /* bit23 */
#define SCRATCH_PAD2_SSPA_ERR                    0x00400000   /* bit22 */
#define SCRATCH_PAD2_SSPL_ERR                    0x00200000   /* bit21 */
#define SCRATCH_PAD2_HSST_ERR                    0x00100000   /* bit20 */
#define SCRATCH_PAD2_PCS_ERR                     0x00080000   /* bit19 */

#define SCRATCH_PAD2_FW_BOOT_ROM_ERROR           0x00010000   /* bit16 */
#define SCRATCH_PAD2_FW_ILA_ERR                  0x00008000   /* bit15 */
#define SCRATCH_PAD2_FW_FLM_ERR                  0x00004000   /* bit14 */
#define SCRATCH_PAD2_FW_FW_ASRT_ERR              0x00002000   /* bit13 */
#define SCRATCH_PAD2_FW_HW_WDG_ERR               0x00001000   /* bit12 */
#define SCRATCH_PAD2_FW_GEN_EXCEPTION_ERR        0x00000800   /* bit11 */
#define SCRATCH_PAD2_FW_UNDTMN_ERR               0x00000400   /* bit10 */
#define SCRATCH_PAD2_FW_HW_FATAL_ERR             0x00000200   /* bit9 */
#define SCRATCH_PAD2_FW_HW_NON_FATAL_ERR         0x00000100   /* bit8 */
#define SCRATCH_PAD2_FW_HW_MASK                  0x000000FF
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_PCS_ERR                     0x00
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_GSM_ERR                     0x01
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_OSSP0_ERR                   0x02
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_OSSP1_ERR                   0x03
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_OSSP2_ERR                   0x04
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_ERAAE_ERR                   0x05
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_SDS_ERR                     0x06
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_PCIE_CORE_ERR               0x08
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_PCIE_AL_ERR                 0x0C
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_MSGU_ERR                    0x0E
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_SPBC_ERR                    0x0F
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_BDMA_ERR                    0x10
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_MCPSL2B_ERR                 0x13
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_MCPSDC_ERR                  0x14
#define SCRATCH_PAD2_HW_ERROR_INT_INDX_UNDETERMINED_ERROR_OCCURRED 0xFF



#define SCRATCH_PAD_ERROR_MASK                   0xFFFFFF00   /* Error mask bits 31:8 */
#define SCRATCH_PAD_STATE_MASK                   0x00000003   /* State Mask bits 1:0 */

#define SPCV_RAAE_STATE_MASK                          0x3
#define SPCV_IOP0_STATE_MASK                          ((1 << 10) | (1 << 11))
#define SPCV_IOP1_STATE_MASK                          ((1 << 12) | (1 << 13))
#define SPCV_ERROR_VALUE                              0x2


#define SCRATCH_PAD3_FW_IMAGE_MASK               0x0000000F   /* SPC 8x6G boots from Image */
#define SCRATCH_PAD3_FW_IMAGE_FLAG_VALID         0x00000008   /* Image flag is valid */
#define SCRATCH_PAD3_FW_IMAGE_B_VALID            0x00000004   /* Image B is valid */
#define SCRATCH_PAD3_FW_IMAGE_A_VALID            0x00000002   /* Image A is valid */
#define SCRATCH_PAD3_FW_IMAGE_B_ACTIVE           0x00000001   /* Image B is active */


#define SCRATCH_PAD3_V_            0x00000001   /* Image B is valid */

#define SCRATCH_PAD3_V_ENC_DISABLED              0x00000000   /*  */
#define SCRATCH_PAD3_V_ENC_DIS_ERR               0x00000001   /*  */
#define SCRATCH_PAD3_V_ENC_ENA_ERR               0x00000002   /*  */
#define SCRATCH_PAD3_V_ENC_READY                 0x00000003   /*  */
#define SCRATCH_PAD3_V_ENC_MASK    SCRATCH_PAD3_V_ENC_READY   /*  */

#define SCRATCH_PAD3_V_AUT                        0x00000008    /* AUT Operator authentication*/
#define SCRATCH_PAD3_V_ARF                        0x00000004    /* ARF factory mode. */

#define SCRATCH_PAD3_V_XTS_ENABLED               (1 << SHIFT14) /*  */
#define SCRATCH_PAD3_V_SMA_ENABLED               (1 << SHIFT4 ) /*  */
#define SCRATCH_PAD3_V_SMB_ENABLED               (1 << SHIFT5 ) /*  */
#define SCRATCH_PAD3_V_SMF_ENABLED               0 /*  */
#define SCRATCH_PAD3_V_SM_MASK                   0x000000F0    /*  */
#define SCRATCH_PAD3_V_ERR_CODE                  0x00FF0000    /*  */


/* Dynamic map through Bar4 - 0x00700000 */
#define GSM_CONFIG_RESET                         0x00000000
#define RAM_ECC_DB_ERR                           0x00000018
#define GSM_READ_ADDR_PARITY_INDIC               0x00000058
#define GSM_WRITE_ADDR_PARITY_INDIC              0x00000060
#define GSM_WRITE_DATA_PARITY_INDIC              0x00000068
#define GSM_READ_ADDR_PARITY_CHECK               0x00000038
#define GSM_WRITE_ADDR_PARITY_CHECK              0x00000040
#define GSM_WRITE_DATA_PARITY_CHECK              0x00000048

/* signature defintion for host scratch pad0 register */
#define SPC_SOFT_RESET_SIGNATURE                 0x252acbcd   /* Signature for Soft Reset */
#define SPC_HDASOFT_RESET_SIGNATURE              0xa5aa27d7   /* Signature for HDA Soft Reset without PCIe resetting */

/**** SPC Top-level Registers definition for Soft Reset/HDA mode ****/
/****************** 64 KB BAR *****************/
/* SPC Reset register - BAR4(0x20), BAR2(win) (need dynamic mapping) */
#define SPC_REG_RESET                            0x000000   /* reset register */
#define SPC_REG_DEVICE_LCLK                      0x000058   /* Device LCLK generation register */

#define SPC_READ_RESET_REG siHalRegReadExt(agRoot, GEN_SPC_REG_RESET, SPC_REG_RESET)

#define SPC_WRITE_RESET_REG(value) ossaHwRegWriteExt(agRoot, PCIBAR2, SPC_REG_RESET, value);
/* NMI register - BAR4(0x20), BAR2(win) 0x060000/0x070000 */
//#define MBIC_RAW_NMI_STAT_VPE0_IOP               0x0004C8 not used anymore
//#define MBIC_RAW_NMI_STAT_VPE0_AAP1              0x0104C8 not used anymore
#define MBIC_NMI_ENABLE_VPE0_IOP                 0x000418
#define MBIC_NMI_ENABLE_VPE0_AAP1                0x000418

/* PCIE registers - BAR2(0x18), BAR1(win) 0x010000 */
#define PCIE_EVENT_INTERRUPT_ENABLE              0x003040
#define PCIE_EVENT_INTERRUPT                     0x003044
#define PCIE_ERROR_INTERRUPT_ENABLE              0x003048
#define PCIE_ERROR_INTERRUPT                     0x00304C

/* PCIe Message Unit Configuration Registers offset - BAR2(0x18), BAR1(win) 0x010000 */
#define SPC_REG_MSGU_CONFIG                      0x003018
#define PMIC_MU_CFG_1_BITMSK_MU_MEM_ENABLE       0x00000010

/* bit difination for SPC_RESET register */
#define   SPC_REG_RESET_OSSP                     0x00000001
#define   SPC_REG_RESET_RAAE                     0x00000002
#define   SPC_REG_RESET_PCS_SPBC                 0x00000004
#define   SPC_REG_RESET_PCS_IOP_SS               0x00000008
#define   SPC_REG_RESET_PCS_AAP1_SS              0x00000010
#define   SPC_REG_RESET_PCS_AAP2_SS              0x00000020
#define   SPC_REG_RESET_PCS_LM                   0x00000040
#define   SPC_REG_RESET_PCS                      0x00000080
#define   SPC_REG_RESET_GSM                      0x00000100
#define   SPC_REG_RESET_DDR2                     0x00010000
#define   SPC_REG_RESET_BDMA_CORE                0x00020000
#define   SPC_REG_RESET_BDMA_SXCBI               0x00040000
#define   SPC_REG_RESET_PCIE_AL_SXCBI            0x00080000
#define   SPC_REG_RESET_PCIE_PWR                 0x00100000
#define   SPC_REG_RESET_PCIE_SFT                 0x00200000
#define   SPC_REG_RESET_PCS_SXCBI                0x00400000
#define   SPC_REG_RESET_LMS_SXCBI                0x00800000
#define   SPC_REG_RESET_PMIC_SXCBI               0x01000000
#define   SPC_REG_RESET_PMIC_CORE                0x02000000
#define   SPC_REG_RESET_PCIE_PC_SXCBI            0x04000000
#define   SPC_REG_RESET_DEVICE                   0x80000000

/* bit definition for SPC Device Revision register - BAR1 */
#define SPC_REG_DEVICE_REV                       0x000024
#define SPC_REG_DEVICE_REV_MASK                  0x0000000F


/* bit definition for SPC_REG_TOP_DEVICE_ID  - BAR2 */
#define SPC_REG_TOP_DEVICE_ID                    0x20
#define SPC_TOP_DEVICE_ID                        0x8001

#define SPC_REG_TOP_BOOT_STRAP                   0x8
#define SPC_TOP_BOOT_STRAP                       0x02C0A682


/* For PHY Error */
#define COUNT_OFFSET                             0x4000
#define LCLK_CLEAR                               0x2
#define LCLK                                     0x1
#define CNTL_OFFSET                              0x100
#define L0_LCLK_CLEAR                            0x2
#define L0_LCLK                                  0x1
#define DEVICE_LCLK_CLEAR                        0x40

/****************** 64 KB BAR *****************/
/* PHY Error Count Registers - BAR4(0x20), BAR2(win) (need dynamic mapping) */
#define SPC_SSPL_COUNTER_CNTL                    0x001030
#define SPC_INVALID_DW_COUNT                     0x001034
#define SPC_RUN_DISP_ERROR_COUNT                 0x001038
#define SPC_CODE_VIOLATION_COUNT                 0x00103C
#define SPC_LOSS_DW_SYNC_COUNT                   0x001040
#define SPC_PHY_RESET_PROBLEM_COUNT              0x001044
#define SPC_READ_DEV_REV ossaHwRegReadExt(agRoot, PCIBAR2, SPC_REG_DEVICE_REV);

#define SPC_READ_COUNTER_CNTL(phyId) ossaHwRegReadExt(agRoot, PCIBAR2, SPC_SSPL_COUNTER_CNTL + (COUNT_OFFSET * phyId))
#define SPC_WRITE_COUNTER_CNTL(phyId, value) ossaHwRegWriteExt(agRoot, PCIBAR2, SPC_SSPL_COUNTER_CNTL + (COUNT_OFFSET * phyId), value)
#define SPC_READ_INV_DW_COUNT(phyId) ossaHwRegReadExt(agRoot, PCIBAR2, SPC_INVALID_DW_COUNT + (COUNT_OFFSET * phyId))
#define SPC_READ_DISP_ERR_COUNT(phyId) ossaHwRegReadExt(agRoot, PCIBAR2, SPC_RUN_DISP_ERROR_COUNT + (COUNT_OFFSET * phyId))
#define SPC_READ_CODE_VIO_COUNT(phyId) ossaHwRegReadExt(agRoot, PCIBAR2, SPC_CODE_VIOLATION_COUNT + (COUNT_OFFSET * phyId))
#define SPC_READ_LOSS_DW_COUNT(phyId) ossaHwRegReadExt(agRoot, PCIBAR2, SPC_LOSS_DW_SYNC_COUNT + (COUNT_OFFSET * phyId))
#define SPC_READ_PHY_RESET_COUNT(phyId) ossaHwRegReadExt(agRoot, PCIBAR2, SPC_PHY_RESET_PROBLEM_COUNT + (COUNT_OFFSET * phyId))
/* PHY Error Count Control Registers - BAR2(0x18), BAR1(win) */
#define SPC_L0_ERR_CNT_CNTL                      0x0041B0
#define SPC_READ_L0ERR_CNT_CNTL(phyId) ossaHwRegReadExt(agRoot, PCIBAR1, SPC_L0_ERR_CNT_CNTL + (CNTL_OFFSET * phyId))
#define SPC_WRITE_L0ERR_CNT_CNTL(phyId, value) ossaHwRegWriteExt(agRoot, PCIBAR1, SPC_L0_ERR_CNT_CNTL + (CNTL_OFFSET * phyId), value)

/* registers for BAR Shifting - BAR2(0x18), BAR1(win) */
#define SPC_IBW_AXI_TRANSLATION_LOW              0x003258

/* HDA mode definitions */
/* 256KB */
#define HDA_CMD_OFFSET256K                       0x0003FFC0
#define HDA_RSP_OFFSET256K                       0x0003FFE0

/* 512KB */
#define HDA_CMD_OFFSET512K                       0x0007FFC0
#define HDA_RSP_OFFSET512K                       0x0007FFE0

/* 768KB */
#define HDA_CMD_OFFSET768K                       0x000BFFC0
#define HDA_RSP_OFFSET768K                       0x000BFFE0

/* 1024KB - by default */
#define HDA_CMD_OFFSET1MB                        0x0000FEC0
#define HDA_RSP_OFFSET1MB                        0x0000FEE0



/*  Table 27 Boot ROM HDA Protocol Command Format */
typedef struct spcv_hda_cmd_s {
/*  Offset Byte 3 Byte 2 Byte 1 Byte 0 */
  bit32 cmdparm_0;            /*  0 Command Parameter 0 */
  bit32 cmdparm_1;            /*  4 Command Parameter 1 */
  bit32 cmdparm_2;            /*  8 Command Parameter 2 */
  bit32 cmdparm_3;            /*  12 Command Parameter 3 */
  bit32 cmdparm_4;            /*  16 Command Parameter 4 */
  bit32 cmdparm_5;            /*  20 Command Parameter 5 */
  bit32 cmdparm_6;            /*  24 Command Parameter 6 */
  bit32 C_PA_SEQ_ID_CMD_CODE; /*  28 C_PA SEQ_ID CMD_CODE */
} spcv_hda_cmd_t;

/* Table 28 Boot ROM HDA Protocol Response Format  */
typedef struct spcv_hda_rsp_s {
/*  Offset Byte 3 Byte 2 Byte 1 Byte 0 */
  bit32 cmdparm_0;            /*  0 Command Parameter 0 */
  bit32 cmdparm_1;            /*  4 Command Parameter 1 */
  bit32 cmdparm_2;            /*  8 Command Parameter 2 */
  bit32 cmdparm_3;            /*  12 Command Parameter 3 */
  bit32 cmdparm_4;            /*  16 Command Parameter 4 */
  bit32 cmdparm_5;            /*  20 Command Parameter 5 */
  bit32 cmdparm_6;            /*  24 Command Parameter 6 */
  bit32 R_PA_SEQ_ID_RSP_CODE; /*  28 C_PA SEQ_ID CMD_CODE */
} spcv_hda_rsp_t;

#define SPC_V_HDA_COMMAND_OFFSET  0x000042c0
#define SPC_V_HDA_RESPONSE_OFFSET 0x000042e0


#define HDA_C_PA_OFFSET                          0x1F
#define HDA_SEQ_ID_OFFSET                        0x1E
#define HDA_PAR_LEN_OFFSET                       0x04
#define HDA_CMD_CODE_OFFSET                      0x1C
#define HDA_RSP_CODE_OFFSET                      0x1C
#define SM_HDA_RSP_OFFSET1MB_PLUS_HDA_RSP_CODE_OFFSET    (HDA_RSP_OFFSET1MB + HDA_RSP_CODE_OFFSET)

/* commands */
#define SPC_V_HDAC_PA                         0xCB
#define SPC_V_HDAC_BUF_INFO                   0x0001
#define SPC_V_HDAC_EXEC                       0x0002
#define SPC_V_HDAC_RESET                      0x0003
#define SPC_V_HDAC_DMA                        0x0004

#define SPC_V_HDAC_PA_MASK                    0xFF000000
#define SPC_V_HDAC_SEQID_MASK                 0x00FF0000
#define SPC_V_HDAC_CMDCODE_MASK               0x0000FFFF

/* responses */
#define SPC_V_HDAR_PA                         0xDB
#define SPC_V_HDAR_BUF_INFO                   0x8001
#define SPC_V_HDAR_IDLE                       0x8002
#define SPC_V_HDAR_BAD_IMG                    0x8003
#define SPC_V_HDAR_BAD_CMD                    0x8004
#define SPC_V_HDAR_INTL_ERR                   0x8005
#define SPC_V_HDAR_EXEC                       0x8006

#define SPC_V_HDAR_PA_MASK                    0xFF000000
#define SPC_V_HDAR_SEQID_MASK                 0x00FF0000
#define SPC_V_HDAR_RSPCODE_MASK               0x0000FFFF

#define ILAHDA_RAAE_IMG_GET                   0x11
#define ILAHDA_IOP_IMG_GET                    0x10

#define ILAHDAC_RAAE_IMG_DONE                 0x81


#define HDA_AES_DIF_FUNC                      0xFEDFAE1F


/* Set MSGU Mapping Registers in BAR0 */
#define PMIC_MU_CFG_1_BITMSK_MU_IO_ENABLE        0x00000001
#define PMIC_MU_CFG_1_BITMSK_MU_IO_WIR           0x0000000C
#define PMIC_MU_CFG_1_BITMSK_MU_MEM_ENABLE       0x00000010
#define PMIC_MU_CFG_1_BITMSK_MU_MEM_OFFSET       0xFFFFFC00

/* PMIC Init */
#define MU_MEM_OFFSET                            0x0
#define MSGU_MU_IO_WIR                           0x8            /* Window 0 */

#define BOOTTLOADERHDA_IDLE                      0x8002
#define HDAR_BAD_IMG                             0x8003
#define HDAR_BAD_CMD                             0x8004
#define HDAR_EXEC                                0x8006

#define CEILING(X, rem) ((((bit32)X % rem) > 0) ? (bit32)(X/rem+1) : (bit32)(X/rem))

#define GSMSM_AXI_LOWERADDR                      0x00400000
#define SHIFT_MASK                               0xFFFF0000
#define OFFSET_MASK                              0x0000FFFF
#define SIZE_64KB                                0x00010000
#define ILA_ISTR_ADDROFFSETHDA                   0x0007E000
#define HDA_STATUS_BITS                          0x0000FFFF

/* Scratchpad Reg: bit[31]: 1-CMDFlag 0-RSPFlag; bit[30,24]:CMD/RSP; bit[23,0]:Offset/Size - Shared with the host driver */
/* ILA: Mandatory response / state codes in MSGU Scratchpad 0 */
#define ILAHDA_IOP_IMG_GET                       0x10
#define ILAHDA_AAP1_IMG_GET                      0x11
#define ILAHDA_AAP2_IMG_GET                      0x12
#define ILAHDA_EXITGOOD                          0x1F

/* HOST: Mandatory command codes in Host Scratchpad 3 */
#define ILAHDAC_IOP_IMG_DONE                     0x00000080
#define ILAHDAC_AAP1_IMG_DONE                    0x00000081
#define ILAHDAC_AAP2_IMG_DONE                    0x00000082
#define ILAHDAC_ISTR_IMG_DONE                    0x00000083
#define ILAHDAC_GOTOHDA                          0x000000ff

#define HDA_ISTR_DONE                            (bit32)(ILAHDAC_ISTR_IMG_DONE << 24)
#define HDA_AAP1_DONE                            (bit32)(ILAHDAC_AAP1_IMG_DONE << 24)
#define HDA_IOP_DONE                             (bit32)(ILAHDAC_IOP_IMG_DONE << 24)

#define RB6_ACCESS_REG                           0x6A0000
#define HDAC_EXEC_CMD                            0x0002
#define HDA_C_PA                                 0xcb
#define HDA_SEQ_ID_BITS                          0x00ff0000
#define HDA_GSM_OFFSET_BITS                      0x00FFFFFF
#define MBIC_AAP1_ADDR_BASE                      0x060000
#define MBIC_GSM_SM_BASE                         0x04F0000
#define MBIC_IOP_ADDR_BASE                       0x070000
#define GSM_ADDR_BASE                            0x0700000
#define SPC_TOP_LEVEL_ADDR_BASE                  0x000000
#define GSM_CONFIG_RESET_VALUE                   0x00003b00
#define GPIO_ADDR_BASE                           0x00090000
#define GPIO_GPIO_0_0UTPUT_CTL_OFFSET            0x0000010c


/* Scratchpad registers for fatal errors */
#define SA_FATAL_ERROR_SP1_AAP1_ERR_MASK        0x3
#define SA_FATAL_ERROR_SP2_IOP_ERR_MASK         0x3
#define SA_FATAL_ERROR_FATAL_ERROR              0x2

/* PCIe Analyzer trigger */
#define PCIE_TRIGGER_ON_REGISTER_READ          V_Host_Scratchpad_2_Register    /* PCI trigger on this offset */

#define PCI_TRIGGER_INIT_TEST                 1 /* Setting adjustable paramater PciTrigger to match this value */
#define PCI_TRIGGER_OFFSET_MISMATCH           2 /* Setting adjustable paramater PciTrigger to match this value */
#define PCI_TRIGGER_COAL_IOMB_ERROR           4 /* Setting adjustable paramater PciTrigger to match this value */
#define PCI_TRIGGER_COAL_INVALID              8 /* Setting adjustable paramater PciTrigger to match this value */




/*                                                                   */

enum spc_spcv_offsetmap_e
{
  GEN_MSGU_IBDB_SET=0,
  GEN_MSGU_ODR,
  GEN_MSGU_ODCR,
  GEN_MSGU_SCRATCH_PAD_0,
  GEN_MSGU_SCRATCH_PAD_1,
  GEN_MSGU_SCRATCH_PAD_2,
  GEN_MSGU_SCRATCH_PAD_3,
  GEN_MSGU_HOST_SCRATCH_PAD_0,
  GEN_MSGU_HOST_SCRATCH_PAD_1,
  GEN_MSGU_HOST_SCRATCH_PAD_2,
  GEN_MSGU_HOST_SCRATCH_PAD_3,
  GEN_MSGU_ODMR,
  GEN_PCIE_TRIGGER,
  GEN_SPC_REG_RESET,
};


#endif  /*__SAHWREG_H__ */




