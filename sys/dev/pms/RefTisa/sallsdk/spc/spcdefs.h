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
/*! \file spcdefs.h
 *  \brief The file defines the MPI Application Programming Interface (API)
 *
 * The file defines the MPI Application Programming Interfacde (API)
 *
 */
/*******************************************************************************/
#ifndef __SPCDEFS_H__
#define __SPCDEFS_H__

/*******************************************************************************/
/*******************************************************************************/
/* CONSTANTS                                                                    */
/*******************************************************************************/
/*******************************************************************************/
/*******************************************************************************/
/* MSGU CONFIGURATION TABLE                                                    */
/*******************************************************************************/
#define SPC_MSGU_CFG_TABLE_UPDATE               0x001   /* Inbound doorbell bit0 */
#define SPC_MSGU_CFG_TABLE_RESET                0x002   /* Inbound doorbell bit1 */
#define SPC_MSGU_CFG_TABLE_FREEZE               0x004   /* Inbound doorbell bit2 */
#define SPC_MSGU_CFG_TABLE_UNFREEZE             0x008   /* Inbound doorbell bit4 */
#define SPCV_MSGU_CFG_TABLE_TRANSFER_DEBUG_INFO 0x080   /* Inbound doorbell bit7 SPCV */
#define SPCV_MSGU_HALT_CPUS                     0x100   /* Inbound doorbell bit8 SPCV */

/***** Notes *****/
/* The firmware side is using Little Endian (MIPs). */
/* So anything sending or receiving from FW must be in Little Endian */
/*******************************************************************************/
/** \struct mpiMsgHeader_s
 *  \brief MPI message header
 *
 * The mpiMsgHeader_s defines the fields in the header of every message
 */
/*******************************************************************************/
/* This structire defines the fields in the header of every message */


struct mpiMsgHeader_s
{
  bit32 Header;             /* Bits [11:0]  - Message operation code */
                            /* Bits [15:12] - Message Category */
                            /* Bits [21:16] - Outboundqueue ID for the operation completion message */
                            /* Bits [23:22] - Reserved */
                            /* Bits [28:24] - Buffer Count, indicates how many buffer are allocated for the massage */
                            /* Bits [30:29] - Reserved */
                            /* Bits [31]    - Message Valid bit */
};

typedef struct mpiMsgHeader_s  mpiMsgHeader_t;

#define V_BIT          0x1

#define V_MASK         0x1
#define BC_MASK        0x1F
#define OBID_MASK      0x3F
#define CAT_MASK       0x0F
#define OPCODE_MASK    0xFFF
#define HEADER_V_MASK  0x80000000
#define HEADER_BC_MASK 0x1f000000

#ifndef SPC_CONFIG
/*******************************************************************************/
/** \struct spc_ConfigMainDescriptor_s
 *  \brief This structure is used to configure main part of Configuration Table
 *
 * This structure specifies all required attributes to configuration table
 */
/*******************************************************************************/
/* new MPI configuration main table */
struct  spc_configMainDescriptor_s
{
  bit8  Signature[4];                      /**< DW0 signature - Indicate coherent table */
  bit32 InterfaceRev;                      /**< DW1 Revsion of Interface */
  bit32 FWRevision;                        /**< DW2 Revsion of FW */
  bit32 MaxOutstandingIO;                  /**< DW3 Max outstanding IO */
  bit32 MDevMaxSGL;                        /**< DW4 Maximum SGL elements  & Max Devices */
        /* bit0-15  Maximum SGL */
        /* bit16-31 Maximum Devices */
  bit32 ContrlCapFlag;                     /**< DW5 Controller Capability */
        /* bit0-7   Max number of inbound queue */
        /* bit8-15  Max number of outbound queue */
        /* bit16    high priority of inbound queue is supported */
        /* bit17    reserved */
        /* bit18    interrupt coalescing is supported, SPCV-reserved */
        /* bit19-24 Maximum number of valid phys */
        /* bit25-31 SAS Revision SPecification */
  bit32 GSTOffset;                         /**< DW6 General Status Table */
  bit32 inboundQueueOffset;                /**< DW7 inbound configuration table offset */
        /* bit23-0  inbound queue table offset */
        /* bit31-24 entry size, new in SPCV */
  bit32 outboundQueueOffset;               /**< DW8 outbound configuration table offset */
        /* bit23-0  outbound queue table offset */
        /* bit31-24 entry size, new in SPCV */
  bit32 iQNPPD_HPPD_GEvent;                /**< DW9 inbound Queue Process depth and General Event */
        /* bit0-7   inbound normal priority process depth */
        /* bit8-15  inbound high priority process depth */
        /* bit16-23 OQ number to receive GENERAL_EVENT Notification */
        /* bit24-31 OQ number to receive DEVICE_HANDLE_REMOVAL Notification */
  bit32 outboundHWEventPID0_3;             /**< DWA outbound HW event for PortId 0 to 3, SPCV-reserved */
        /* bit0-7   outbound queue number of SAS_HW event for PhyId 0 */
        /* bit8-15  outbound queue number of SAS_HW event for PhyId 1 */
        /* bit16-23 outbound queue number of SAS_HW event for PhyId 2 */
        /* bit24-31 outbound queue number of SAS_HW event for PhyId 3 */
  bit32 outboundHWEventPID4_7;             /**< DWB outbound HW event for PortId 4 to 7, SPCV-reserved */
        /* bit0-7   outbound queue number of SAS_HW event for PhyId 4 */
        /* bit8-15  outbound queue number of SAS_HW event for PhyId 5 */
        /* bit16-23 outbound queue number of SAS_HW event for PhyId 6 */
        /* bit24-31 outbound queue number of SAS_HW event for PhyId 7 */
  bit32 outboundNCQEventPID0_3;            /**< DWC outbound NCQ event for PortId 0 to 3, SPCV-reserved */
        /* bit0-7   outbound queue number of SATA_NCQ event for PhyId 0 */
        /* bit8-15  outbound queue number of SATA_NCQ event for PhyId 1 */
        /* bit16-23 outbound queue number of SATA_NCQ event for PhyId 2 */
        /* bit24-31 outbound queue number of SATA_NCQ event for PortId 3 */
  bit32 outboundNCQEventPID4_7;            /**< DWD outbound NCQ event for PortId 4 to 7, SPCV-reserved*/
        /* bit0-7   outbound queue number of SATA_NCQ event for PhyId 4 */
        /* bit8-15  outbound queue number of SATA_NCQ event for PhyId 5 */
        /* bit16-23 outbound queue number of SATA_NCQ event for PhyId 6 */
        /* bit24-31 outbound queue number of SATA_NCQ event for PhyId 7 */
  bit32 outboundTargetITNexusEventPID0_3;  /**< DWE outbound target ITNexus Event for PortId 0 to 3, SPCV-reserved */
        /* bit0-7   outbound queue number of ITNexus event for PhyId 0 */
        /* bit8-15  outbound queue number of ITNexus event for PhyId 1 */
        /* bit16-23 outbound queue number of ITNexus event for PhyId 2 */
        /* bit24-31 outbound queue number of ITNexus event for PhyId 3 */
  bit32 outboundTargetITNexusEventPID4_7;  /**< DWF outbound target ITNexus Event for PortId 4 to 7, SPCV-reserved */
        /* bit0-7   outbound queue number of ITNexus event for PhyId 4 */
        /* bit8-15  outbound queue number of ITNexus event for PhyId 5 */
        /* bit16-23 outbound queue number of ITNexus event for PhyId 6 */
        /* bit24-31 outbound queue number of ITNexus event for PhyId 7 */
  bit32 outboundTargetSSPEventPID0_3;      /**< DW10 outbound target SSP event for PordId 0 to 3, SPCV-reserved */
        /* bit0-7   outbound queue number of SSP event for PhyId 0 */
        /* bit8-15  outbound queue number of SSP event for PhyId 1 */
        /* bit16-23 outbound queue number of SSP event for PhyId 2 */
        /* bit24-31 outbound queue number of SSP event for PhyId 3 */
  bit32 outboundTargetSSPEventPID4_7;      /**< DW11 outbound target SSP event for PordId 4 to 7, SPCV-reserved */
        /* bit0-7   outbound queue number of SSP event for PhyId 4 */
        /* bit8-15  outbound queue number of SSP event for PhyId 5 */
        /* bit16-23 outbound queue number of SSP event for PhyId 6 */
        /* bit24-31 outbound queue number of SSP event for PhyId 7 */
  bit32 ioAbortDelay;                      /**< DW12 IO Abort Delay (bit15:0) MPI_TABLE_CHANGE*/
  bit32 custset;                           /**< DW13 custset */
  bit32 upperEventLogAddress;              /**< DW14 Upper physical MSGU Event log address */
  bit32 lowerEventLogAddress;              /**< DW15 Lower physical MSGU Event log address */
  bit32 eventLogSize;                      /**< DW16 Size of MSGU Event log, 0 means log disable */
  bit32 eventLogOption;                    /**< DW17 Option of MSGU Event log */
        /* bit3-0 log severity, 0x0 Disable Logging */
        /*                      0x1 Critical Error */
        /*                      0x2 Minor Error    */
        /*                      0x3 Warning        */
        /*                      0x4 Information    */
        /*                      0x5 Debugging      */
        /*                      0x6 - 0xF Reserved */
  bit32 upperIOPeventLogAddress;           /**< DW18 Upper physical IOP Event log address */
  bit32 lowerIOPeventLogAddress;           /**< DW19 Lower physical IOP Event log address */
  bit32 IOPeventLogSize;                   /**< DW1A Size of IOP Event log, 0 means log disable */
  bit32 IOPeventLogOption;                 /**< DW1B Option of IOP Event log */
        /* bit3-0 log severity, 0x0 Critical Error */
        /*                      0x1 Minor Error    */
        /*                      0x2 Warning        */
        /*                      0x3 Information    */
        /*                      0x4 Unknown        */
        /*                      0x5 - 0xF Reserved */
  bit32 FatalErrorInterrupt;               /**< DW1C Fatal Error Interrupt enable and vector */
        /* bit0     Fatal Error Interrupt Enable   */
        /* bit1     PI/CI 64bit address            */
        /* bit2     SGPIO IOMB support */
        /* bit6-2   Reserved                       */
        /* bit7     OQ NP/HPriority Path enable    */
        /* bit15-8  Fatal Error Interrupt Vector   */
        /* bit16    Enable IQ/OQ 64                */
        /* bit17    Interrupt Reassertion Enable   */
        /* bit18    Interrupt Reassertion Delay in ms          */
        /* bit31-19 Interrupt Reassertion delay, 0-default 1ms */
  bit32 FatalErrorDumpOffset0;             /**< DW1D FERDOMS-GU Fatal Error Register Dump Offset for MSGU */
  bit32 FatalErrorDumpLength0;             /**< DW1E FERDLMS-GU Fatal Error Register Dump Length for MSGU */
  bit32 FatalErrorDumpOffset1;             /**< DW1F FERDO-SSTRUCPCS Fatal Error Register Dump Offset for IOP */
  bit32 FatalErrorDumpLength1;             /**< DW20 FERDLSTRUCTTPCS  Fatal Error Register Dump Length for IOP */
  bit32 HDAModeFlags;                      /**< DW21 HDA Mode Flags, SPCV-reserved */
  bit32 analogSetupTblOffset;              /**< DW22 SPASTO Phy Calibration Table offset */
        /* bit23-0  phy calib table offset */
        /* bit31-24 entry size */
  bit32 InterruptVecTblOffset;             /**< DW23 Interrupt Vector Table MPI_TABLE_CHANG */
        /* bit23-0  interrupt vector table offset */
        /* bit31-24 entry size */
  bit32 phyAttributeTblOffset;             /**< DW24 SAS Phy Attribute Table Offset MPI_TABLE_CHANG*/
        /* bit23-0  phy attribute table offset */
        /* bit31-24 entry size */
  bit32 portRecoveryResetTimer;            /* Offset 0x25 [31:16] Port recovery timer default that is 0
                                              used for all SAS ports. Granularity of this timer is 100ms. The host can
                                              change the individual port recovery timer by using the PORT_CONTROL
                                              [15:0] Port reset timer default that is used 3 (i.e 300ms) for all
                                              SAS ports. Granularity of this timer is 100ms. Host can change the
                                              individual port recovery timer by using PORT_CONTROL Command */
  bit32 interruptReassertionDelay;         /* Offset 0x26 [23:0] Remind host of outbound completion 0 disabled 100usec per increment */

  bit32     ilaRevision;                   /* Offset 0x27 */
};

/* main configuration offset - byte offset */
#define MAIN_SIGNATURE_OFFSET          0x00    /* DWORD 0x00 (R) */
#define MAIN_INTERFACE_REVISION        0x04    /* DWORD 0x01 (R) */
#define MAIN_FW_REVISION               0x08    /* DWORD 0x02 (R) */
#define MAIN_MAX_OUTSTANDING_IO_OFFSET 0x0C    /* DWORD 0x03 (R) */
#define MAIN_MAX_SGL_OFFSET            0x10    /* DWORD 0x04 (R) */
#define MAIN_CNTRL_CAP_OFFSET          0x14    /* DWORD 0x05 (R) */
#define MAIN_GST_OFFSET                0x18    /* DWORD 0x06 (R) */
#define MAIN_IBQ_OFFSET                0x1C    /* DWORD 0x07 (R) */
#define MAIN_OBQ_OFFSET                0x20    /* DWORD 0x08 (R) */
#define MAIN_IQNPPD_HPPD_OFFSET        0x24    /* DWORD 0x09 (W) */
#define MAIN_OB_HW_EVENT_PID03_OFFSET  0x28    /* DWORD 0x0A (W) */ /* reserved for SPCV */
#define MAIN_OB_HW_EVENT_PID47_OFFSET  0x2C    /* DWORD 0x0B (W) */ /* reserved for SPCV */
#define MAIN_OB_NCQ_EVENT_PID03_OFFSET 0x30    /* DWORD 0x0C (W) */ /* reserved for SPCV */
#define MAIN_OB_NCQ_EVENT_PID47_OFFSET 0x34    /* DWORD 0x0D (W) */ /* reserved for SPCV */
#define MAIN_TITNX_EVENT_PID03_OFFSET  0x38    /* DWORD 0x0E (W) */ /* reserved for SPCV */
#define MAIN_TITNX_EVENT_PID47_OFFSET  0x3C    /* DWORD 0x0F (W) */ /* reserved for SPCV */
#define MAIN_OB_SSP_EVENT_PID03_OFFSET 0x40    /* DWORD 0x10 (W) */ /* reserved for SPCV */
#define MAIN_OB_SSP_EVENT_PID47_OFFSET 0x44    /* DWORD 0x11 (W) */ /* reserved for SPCV */
#define MAIN_IO_ABORT_DELAY            0x48    /* DWORD 0x12 (W) */ /* reserved for SPCV */
#define MAIN_CUSTOMER_SETTING          0x4C    /* DWORD 0x13 (W) */ /* reserved for SPCV */
#define MAIN_EVENT_LOG_ADDR_HI         0x50    /* DWORD 0x14 (W) */
#define MAIN_EVENT_LOG_ADDR_LO         0x54    /* DWORD 0x15 (W) */
#define MAIN_EVENT_LOG_BUFF_SIZE       0x58    /* DWORD 0x16 (W) */
#define MAIN_EVENT_LOG_OPTION          0x5C    /* DWORD 0x17 (W) */
#define MAIN_IOP_EVENT_LOG_ADDR_HI     0x60    /* DWORD 0x18 (W) */
#define MAIN_IOP_EVENT_LOG_ADDR_LO     0x64    /* DWORD 0x19 (W) */
#define MAIN_IOP_EVENT_LOG_BUFF_SIZE   0x68    /* DWORD 0x1A (W) */
#define MAIN_IOP_EVENT_LOG_OPTION      0x6C    /* DWORD 0x1B (W) */
#define MAIN_FATAL_ERROR_INTERRUPT     0x70    /* DWORD 0x1C (W) */
#define MAIN_FATAL_ERROR_RDUMP0_OFFSET 0x74    /* DWORD 0x1D (R) */
#define MAIN_FATAL_ERROR_RDUMP0_LENGTH 0x78    /* DWORD 0x1E (R) */
#define MAIN_FATAL_ERROR_RDUMP1_OFFSET 0x7C    /* DWORD 0x1F (R) */
#define MAIN_FATAL_ERROR_RDUMP1_LENGTH 0x80    /* DWORD 0x20 (R) */
#define MAIN_HDA_FLAGS_OFFSET          0x84    /* DWORD 0x21 (R) */ /* reserved for SPCV */
#define MAIN_ANALOG_SETUP_OFFSET       0x88    /* DWORD 0x22 (R) */
#define MAIN_INT_VEC_TABLE_OFFSET      0x8C    /* DWORD 0x23 (W) */ /*  for SPCV */
#define MAIN_PHY_ATTRIBUTE_OFFSET      0x90    /* DWORD 0x24 (W) */ /*  for SPCV */
#define MAIN_PRECTD_PRESETD            0x94    /* DWORD 0x25 (W) */ /*  for SPCV */
#define MAIN_IRAD_RESERVED             0x98    /* DWORD 0x26 (W) */ /*  for SPCV */
#define MAIN_MOQFOT_MOQFOES            0x9C    /* DWORD 0x27 (W) */ /*  for SPCV */
#define MAIN_MERRDCTO_MERRDCES         0xA0    /* DWORD 0x28 (W) */ /*  for SPCV */
#define MAIN_ILAT_ILAV_ILASMRN_ILAMRN_ILAMJN  0xA4 /* DWORD 0x29 (W) */ /*  for SPCV */
#define MAIN_INACTIVE_ILA_REVSION      0xA8    /* DWORD 0x2A (W) */ /*  for SPCV V 3.02 */
#define MAIN_SEEPROM_REVSION           0xAC    /* DWORD 0x2B (W) */ /*  for SPCV V 3.02 */
#define MAIN_UNKNOWN1                  0xB0    /* DWORD 0x2C (W) */ /*  for SPCV V 3.03 */
#define MAIN_UNKNOWN2                  0xB4    /* DWORD 0x2D (W) */ /*  for SPCV V 3.03 */
#define MAIN_UNKNOWN3                  0xB8    /* DWORD 0x2E (W) */ /*  for SPCV V 3.03 */
#define MAIN_XCBI_REF_TAG_PAT          0xBC    /* DWORD 0x2F (W) */ /*  for SPCV V 3.03 */
#define MAIN_AWT_MIDRANGE              0xC0    /* DWORD 0x30 (W) */ /*  for SPCV V 3.03 */


typedef struct spc_configMainDescriptor_s spc_configMainDescriptor_t;
#define SPC_CONFIG
#endif

/* bit to disable end to end crc checking ins SPCv */
#define MAIN_IO_ABORT_DELAY_END_TO_END_CRC_DISABLE 0x00010000

/* bit mask for field Controller Capability in main part */
#define MAIN_MAX_IB_MASK               0x000000ff  /* bit7-0 */
#define MAIN_MAX_OB_MASK               0x0000ff00  /* bit15-8 */
#define MAIN_PHY_COUNT_MASK            0x01f80000  /* bit24-19 */
#define MAIN_QSUPPORT_BITS             0x0007ffff
#define MAIN_SAS_SUPPORT_BITS          0xfe000000

/* bit mask for field max sgl in main part */
#define MAIN_MAX_SGL_BITS              0xFFFF
#define MAIN_MAX_DEV_BITS              0xFFFF0000

/* bit mask for HDA flags field */
#define MAIN_HDA_FLAG_BITS             0x000000FF

#define FATAL_ERROR_INT_BITS           0xFF
#define INT_REASRT_ENABLE              0x00020000
#define INT_REASRT_MS_ENABLE           0x00040000
#define INT_REASRT_DELAY_BITS          0xFFF80000

#define MAX_VALID_PHYS                 8
#define IB_QUEUE_CFGSIZE               64
#define OB_QUEUE_CFGSIZE               64

/* inbound queue configuration offset - byte offset */
#define IB_PROPERITY_OFFSET            0x00
#define IB_BASE_ADDR_HI_OFFSET         0x04
#define IB_BASE_ADDR_LO_OFFSET         0x08
#define IB_CI_BASE_ADDR_HI_OFFSET      0x0C
#define IB_CI_BASE_ADDR_LO_OFFSET      0x10
#define IB_PIPCI_BAR                   0x14
#define IB_PIPCI_BAR_OFFSET            0x18
#define IB_RESERVED_OFFSET             0x1C

/* outbound queue configuration offset - byte offset */
#define OB_PROPERITY_OFFSET            0x00
#define OB_BASE_ADDR_HI_OFFSET         0x04
#define OB_BASE_ADDR_LO_OFFSET         0x08
#define OB_PI_BASE_ADDR_HI_OFFSET      0x0C
#define OB_PI_BASE_ADDR_LO_OFFSET      0x10
#define OB_CIPCI_BAR                   0x14
#define OB_CIPCI_BAR_OFFSET            0x18
#define OB_INTERRUPT_COALES_OFFSET     0x1C
#define OB_DYNAMIC_COALES_OFFSET       0x20

#define OB_PROPERTY_INT_ENABLE         0x40000000

/* General Status Table offset - byte offset */
#define GST_GSTLEN_MPIS_OFFSET         0x00
#define GST_IQ_FREEZE_STATE0_OFFSET    0x04
#define GST_IQ_FREEZE_STATE1_OFFSET    0x08
#define GST_MSGUTCNT_OFFSET            0x0C
#define GST_IOPTCNT_OFFSET             0x10
#define GST_IOP1TCNT_OFFSET            0x14
#define GST_PHYSTATE_OFFSET            0x18  /* SPCV reserved */
#define GST_PHYSTATE0_OFFSET           0x18  /* SPCV reserved */
#define GST_PHYSTATE1_OFFSET           0x1C  /* SPCV reserved */
#define GST_PHYSTATE2_OFFSET           0x20  /* SPCV reserved */
#define GST_PHYSTATE3_OFFSET           0x24  /* SPCV reserved */
#define GST_PHYSTATE4_OFFSET           0x28  /* SPCV reserved */
#define GST_PHYSTATE5_OFFSET           0x2C  /* SPCV reserved */
#define GST_PHYSTATE6_OFFSET           0x30  /* SPCV reserved */
#define GST_PHYSTATE7_OFFSET           0x34  /* SPCV reserved */
#define GST_GPIO_PINS_OFFSET           0x38
#define GST_RERRINFO_OFFSET            0x44

/* General Status Table - MPI state */
#define GST_MPI_STATE_UNINIT           0x00
#define GST_MPI_STATE_INIT             0x01
#define GST_MPI_STATE_TERMINATION      0x02
#define GST_MPI_STATE_ERROR            0x03
#define GST_MPI_STATE_MASK             0x07

#define GST_INF_STATE_BITS             0xfffe0007


/* MPI fatal and non fatal offset mask */
#define MPI_FATAL_ERROR_TABLE_OFFSET_MASK 0xFFFFFF
#define MPI_FATAL_ERROR_TABLE_SIZE(value) ((0xFF000000 & value) >> SHIFT24)    /*  for SPCV */

/* MPI fatal and non fatal Error dump capture table offset - byte offset */
#define MPI_FATAL_EDUMP_TABLE_LO_OFFSET            0x00     /* HNFBUFL */
#define MPI_FATAL_EDUMP_TABLE_HI_OFFSET            0x04     /* HNFBUFH */
#define MPI_FATAL_EDUMP_TABLE_LENGTH               0x08     /* HNFBLEN */
#define MPI_FATAL_EDUMP_TABLE_HANDSHAKE            0x0C     /* FDDHSHK */
#define MPI_FATAL_EDUMP_TABLE_STATUS               0x10     /* FDDTSTAT */
#define MPI_FATAL_EDUMP_TABLE_ACCUM_LEN            0x14     /* ACCDDLEN */
/*  */
#define MPI_FATAL_EDUMP_HANDSHAKE_RDY              0x1
#define MPI_FATAL_EDUMP_HANDSHAKE_BUSY             0x0
/*  */
#define MPI_FATAL_EDUMP_TABLE_STAT_RSVD                 0x0
#define MPI_FATAL_EDUMP_TABLE_STAT_DMA_FAILED           0x1
#define MPI_FATAL_EDUMP_TABLE_STAT_NF_SUCCESS_MORE_DATA 0x2
#define MPI_FATAL_EDUMP_TABLE_STAT_NF_SUCCESS_DONE      0x3

#define IOCTL_ERROR_NO_FATAL_ERROR           0x77

/*******************************************************************************/
/** \struct spc_GSTableDescriptor_s
 *  \brief This structure is used for SPC MPI General Status Table
 *
 * This structure specifies all required attributes to Gereral Status Table
 */
/*******************************************************************************/
struct spc_GSTableDescriptor_s
{
  bit32    GSTLenMPIS;           /**< DW0 - GST Length, MPI State */
                                  /**< bit02-00 MPI state */
                                  /**< 000 - not initialized, 001 - initialized,
                                       010 - Configuration termination in progress */
                                  /**< bit3 - IQ Frozen */
                                  /**< bit15-04 GST Length */
                                  /**< bit31-16 MPI-S Initialize Error */
  bit32    IQFreezeState0;       /**< DW1 - Inbound Queue Freeze State0 */
  bit32    IQFreezeState1;       /**< DW2 - Inbound Qeue Freeze State1 */
  bit32    MsguTcnt;             /**< DW3 - MSGU Tick count */
  bit32    IopTcnt;              /**< DW4 - IOP Tick count */
  bit32    Iop1Tcnt;             /**< DW5 - IOP1 Tick count */
  bit32    PhyState[MAX_VALID_PHYS];  /* SPCV = reserved */
                                 /**< DW6 to DW 0D - Phy Link state 0 to 7, Phy Start State 0 to 7 */
                                  /**< bit00 Phy Start state n, 0 not started, 1 started */
                                  /**< bit01 Phy Link state n, 0 link down, 1 link up */
                                  /**< bit31-2 Reserved */
  bit32    GPIOpins;             /**< DWE - GPIO pins */
  bit32    reserved1;            /**< DWF - reserved */
  bit32    reserved2;            /**< DW10 - reserved */
  bit32    recoverErrInfo[8];    /**< DW11 to DW18 - Recoverable Error Information */
};

typedef struct spc_GSTableDescriptor_s spc_GSTableDescriptor_t;

/*******************************************************************************/
/** \struct spc_SPASTable_s
 *  \brief SAS Phy Analog Setup Table
 *
 * The spc_SPASTable_s structure is used to set Phy Calibration
 * attributes
 */
/*******************************************************************************/
struct spc_SPASTable_s
{
  bit32   spaReg0;            /* transmitter per port configuration 1 SAS_SATA G1 */
  bit32   spaReg1;            /* transmitter per port configuration 2 SAS_SATA G1*/
  bit32   spaReg2;            /* transmitter per port configuration 3 SAS_SATA G1*/
  bit32   spaReg3;            /* transmitter configuration 1 */
  bit32   spaReg4;            /* reveiver per port configuration 1 SAS_SATA G1G2 */
  bit32   spaReg5;            /* reveiver per port configuration 2 SAS_SATA G3 */
  bit32   spaReg6;            /* reveiver per configuration 1 */
  bit32   spaReg7;            /* reveiver per configuration 2 */
  bit32   reserved[2];        /* reserved */
};

typedef struct spc_SPASTable_s spc_SPASTable_t;

/*******************************************************************************/
/** \struct spc_inboundQueueDescriptor_s
 *  \brief This structure is used to configure inbound queues
 *
 * This structure specifies all required attributes to configure inbound queues
 */
/*******************************************************************************/
struct spc_inboundQueueDescriptor_s
{
  bit32    elementPriSizeCount;  /**< Priority, Size, Count in the queue */
                                  /**< bit00-15 Count */
                                  /**< When set to 0, this queue is disabled */
                                  /**< bit16-29 Size */
                                  /**< bit30-31 Priority 00:Normal, 01:High Priority */
  bit32    upperBaseAddress;     /**< Upper address bits for the queue message buffer pool */
  bit32    lowerBaseAddress;     /**< Lower address bits for the queue message buffer pool */
  bit32    ciUpperBaseAddress;   /**< Upper physical address for inbound queue CI */
  bit32    ciLowerBaseAddress;   /**< Lower physical address for inbound queue CI */
  bit32    PIPCIBar;             /**< PCI BAR for PI Offset */
  bit32    PIOffset;             /**< Offset address for inbound queue PI */
  bit32    reserved;             /**< reserved */
};

typedef struct spc_inboundQueueDescriptor_s spc_inboundQueueDescriptor_t;

/*******************************************************************************/
/** \struct spc_outboundQueueDescriptor_s
 *  \brief This structure is used to configure outbound queues
 *
 * This structure specifies all required attributes to configure outbound queues
 */
/*******************************************************************************/
struct spc_outboundQueueDescriptor_s
{
  bit32    elementSizeCount;      /**< Size & Count of each element (slot) in the queue) */
                                   /**< bit00-15 Count */
                                   /**< When set to 0, this queue is disabled */
                                   /**< bit16-29 Size */
                                   /**< bit30    Interrupt enable/disable */
                                   /**< bit31    reserved */
  bit32    upperBaseAddress;      /**< Upper address bits for the queue message buffer pool */
  bit32    lowerBaseAddress;      /**< Lower address bits for the queue message buffer pool */
  bit32    piUpperBaseAddress;    /**< PI Upper Base Address for outbound queue */
  bit32    piLowerBaseAddress;    /**< PI Lower Base Address for outbound queue */
  bit32    CIPCIBar;              /**< PCI BAR for CI Offset */
  bit32    CIOffset;              /**< Offset address for outbound queue CI */
  bit32    interruptVecCntDelay;  /**< Delay in microseconds before the interrupt is asserted */
                                   /**< if the interrupt threshold has not been reached */
                                   /**< Number of interrupt events before the interrupt is asserted */
                                   /**< If set to 0, interrupts for this queue are disable */
                                   /**< Interrupt vector number for this queue */
                                   /**< Note that the interrupt type can be MSI or MSI-X */
                                   /**< depending on the system configuration */
                                   /**< bit00-15 Delay */
                                   /**< bit16-23 Count */
                                   /**< bit24-31 Vector */
  bit32    DInterruptTOPCIOffset; /**< Dynamic Interrupt Coalescing Timeout PCI Bar Offset */
};

typedef struct spc_outboundQueueDescriptor_s spc_outboundQueueDescriptor_t;

typedef struct InterruptVT_s
{
  bit32 iccict;        /**< DW0 - Interrupt Colescing Control and Timer */
  bit32 iraeirad;      /**< DW1 - Interrupt Reassertion Enable/Delay */
} InterruptVT_t;

typedef struct mpiInterruptVT_s
{
  InterruptVT_t IntVecTble[MAX_NUM_VECTOR << 1];
} mpiInterruptVT_t;

#define INT_VT_Coal_CNT_TO 0
#define INT_VT_Coal_ReAssert_Enab 4

typedef struct phyAttrb_s
{
  bit32    phyState;
  bit32    phyEventOQ;
} phyAttrb_t;

typedef struct sasPhyAttribute_s
{
  phyAttrb_t phyAttribute[MAX_VALID_PHYS];
}sasPhyAttribute_t;


#define PHY_STATE    0
#define PHY_EVENT_OQ 4

/*******************************************************************************/
/** \struct spcMSGUConfig_s
 *  \brief This structure is used to configure controller's message unit
 *
 */
/*******************************************************************************/
typedef struct fwMSGUConfig_s
{
  spc_configMainDescriptor_t      mainConfiguration;                /**< main part of Configuration Table */
  spc_GSTableDescriptor_t         GeneralStatusTable;               /**< MPI general status table */
  spc_inboundQueueDescriptor_t    inboundQueue[IB_QUEUE_CFGSIZE];   /**< Inbound queue configuration array */
  spc_outboundQueueDescriptor_t   outboundQueue[OB_QUEUE_CFGSIZE];  /**< Outbound queue configuration array */
  agsaPhyAnalogSetupTable_t       phyAnalogConfig;
  mpiInterruptVT_t                interruptVTable;
  sasPhyAttribute_t               phyAttributeTable;
}fwMSGUConfig_t;


typedef void (*EnadDisabHandler_t)(
                            agsaRoot_t  *agRoot,
                            bit32       interruptVectorIndex
                              );

typedef bit32 (*InterruptOurs_t)(
                            agsaRoot_t  *agRoot,
                            bit32       interruptVectorIndex
                              );
#endif /* __SPC_DEFS__ */
