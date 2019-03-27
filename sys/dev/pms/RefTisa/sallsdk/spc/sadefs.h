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
/*! \file  sadefs.h
 *  \brief The file defines the constants used by LL layer
 */

/*******************************************************************************/

#ifndef  __SADEFS_H__

#define __SADEFS_H__

#define SA_LL_IBQ_PROTECT

#define AGSA_MAX_VALID_PORTS                      AGSA_MAX_VALID_PHYS     /**< defines the maximum number of ports */

#define NUM_TIMERS                                2                       /**< defines the maximum number of timers */
#define SA_USECS_PER_TICK                         1000000                 /**< defines the heart beat of the LL layer 1us */
#define MAX_ACTIVE_IO_REQUESTS                    4096                    /**< Maximum Active IO Requests */
#define SMP_RESPONSE_FRAMES                       AGSA_MAX_VALID_PHYS     /**< SMP Response Frame Buffer */
#define MAX_NUM_VECTOR                            64                      /**< Maximum Number of Interrupt Vectors */
#define REGISTER_DUMP_BUFF_SIZE                   0x4000                  /**< Maximum Fatal Error Register Dump Buffer Size */
#define KBYTES                                    1024

/* number of IQ/OQ */
#define IQ_NUM_32                                 32
#define OQ_NUM_32                                 32

/* default value of Inbound/Outbound element size */
#define INBOUND_DEPTH_SIZE                        512
#define OUTBOUND_DEPTH_SIZE                       512

/* Priority of Queue */
#define MPI_QUEUE_NORMAL                          0
#define MPI_QUEUE_PRIORITY                        1

/* size of IOMB - multiple with 32 bytes */
#define IOMB_SIZE64                               64
#define IOMB_SIZE96                               96
#define IOMB_SIZE128                              128
#define IOMB_SIZE256                              256

/* DIR bit of IOMB for SSP read/write command */
#define DIR_NODATA                                0x000
#define DIR_READ                                  0x100
#define DIR_WRITE                                 0x200

/* TLR bits mask */
#define TLR_MASK                                  0x00000003
/* port and phy Id bits Mask */


#define PORTID_MASK                               0x0000000F
#define PORTID_V_MASK                             0x000000FF
#define PHYID_MASK                                0x0000000F
#define PHYID_V_MASK                              0x000000FF
#define PORT_STATE_MASK                           0x0000000F
#define PHY_IN_PORT_MASK                          0x000000F0

#define SM_PHYID_MASK   (smIS_SPC(agRoot) ? PHYID_MASK  : PHYID_V_MASK )
#define SM_PORTID_MASK  (smIS_SPC(agRoot) ? PORTID_MASK : PORTID_V_MASK )

/* the index for memory requirement, must be continious */
#define LLROOT_MEM_INDEX                          0              /**< the index of root memory */
#define DEVICELINK_MEM_INDEX         (LLROOT_MEM_INDEX + 1)      /**< the index of device descriptors memory */
#define IOREQLINK_MEM_INDEX          (DEVICELINK_MEM_INDEX+1)    /**< the index of IO requests memory */

#ifdef SA_ENABLE_HDA_FUNCTIONS
#define  HDA_DMA_BUFFER              (IOREQLINK_MEM_INDEX+1)     /** HDA Buffer */
#else  /* SA_ENABLE_HDA_FUNCTIONS */
#define  HDA_DMA_BUFFER              (IOREQLINK_MEM_INDEX)       /** HDA Buffer */
#endif /* SA_ENABLE_HDA_FUNCTIONS */

#ifdef SA_ENABLE_TRACE_FUNCTIONS
#define  LL_FUNCTION_TRACE              (HDA_DMA_BUFFER+1)      /**TraceLog */
#else /* SA_ENABLE_TRACE_FUNCTIONS */
#define  LL_FUNCTION_TRACE               HDA_DMA_BUFFER         /**TraceLog */
#endif /* END SA_ENABLE_TRACE_FUNCTIONS */

#define TIMERLINK_MEM_INDEX              (LL_FUNCTION_TRACE+1)   /**< the index of timers memory */

#ifdef FAST_IO_TEST
#define LL_FAST_IO                        (TIMERLINK_MEM_INDEX+1)
#define MPI_IBQ_OBQ_INDEX                    (LL_FAST_IO + 1)

#else /* FAST_IO_TEST */

#define LL_FAST_IO                         TIMERLINK_MEM_INDEX
#define MPI_IBQ_OBQ_INDEX                     (LL_FAST_IO + 1)
#endif /* FAST_IO_TEST */

#define MPI_MEM_INDEX                             (MPI_IBQ_OBQ_INDEX - LLROOT_MEM_INDEX)

#define MPI_EVENTLOG_INDEX                        0
#define MPI_IOP_EVENTLOG_INDEX                    1
#define MPI_CI_INDEX                              2
/* The following is a reference index */
#define MPI_PI_INDEX                              (MPI_CI_INDEX + 1)
#define MPI_IBQ_INDEX                             (MPI_PI_INDEX + 1)
#define MPI_OBQ_INDEX                             (MPI_IBQ_INDEX + MPI_MAX_INBOUND_QUEUES)

#define TOTAL_MPI_MEM_CHUNKS                      (MPI_MAX_INBOUND_QUEUES * 2) + MPI_IBQ_INDEX


#define LL_DEVICE_LOCK 0
#define LL_PORT_LOCK          (LL_DEVICE_LOCK+1)
#define LL_TIMER_LOCK         (LL_PORT_LOCK+1)
#define LL_IOREQ_LOCKEQ_LOCK  (LL_TIMER_LOCK+1)

#ifdef FAST_IO_TEST
#define LL_FAST_IO_LOCK       (LL_IOREQ_LOCKEQ_LOCK+1)
#else /* FAST_IO_TEST   */
#define LL_FAST_IO_LOCK       (LL_IOREQ_LOCKEQ_LOCK)
#endif /* FAST_IO_TEST   */

#ifdef SA_ENABLE_TRACE_FUNCTIONS
#define LL_TRACE_LOCK       (LL_FAST_IO_LOCK+1)
#else /* SA_ENABLE_TRACE_FUNCTIONS   */
#define LL_TRACE_LOCK       (LL_FAST_IO_LOCK)
#endif /* SA_ENABLE_TRACE_FUNCTIONS   */

#ifdef  MPI_DEBUG_TRACE_ENABLE
#define LL_IOMB_TRACE_LOCK (LL_TRACE_LOCK+1)
#else /* MPI_DEBUG_TRACE_ENABLE */
#define LL_IOMB_TRACE_LOCK (LL_TRACE_LOCK)
#endif /* MPI_DEBUG_TRACE_ENABLE */

#define LL_IOREQ_OBQ_LOCK     (LL_IOMB_TRACE_LOCK+1)

#define LL_IOREQ_IBQ_LOCK      (LL_IOREQ_OBQ_LOCK +1)
#define LL_IOREQ_IBQ_LOCK_PARM (LL_IOREQ_OBQ_LOCK + queueConfig->numOutboundQueues  +1)
#define LL_IOREQ_IBQ0_LOCK     (LL_IOREQ_OBQ_LOCK + saRoot->QueueConfig.numOutboundQueues  +1)



/* define phy states */
#define PHY_STOPPED                               0x00000000              /**< flag indicates phy stopped */
#define PHY_UP                                    0x00000001              /**< flag indicates phy up */
#define PHY_DOWN                                  0x00000002              /**< flag indicates phy down */

/* define port states */
#define PORT_NORMAL                               0x0000
#define PORT_INVALIDATING                         0x0002

/* define chip status */
#define CHIP_NORMAL                               0x0000
#define CHIP_SHUTDOWN                             0x0001
#define CHIP_RESETTING                            0x0002
#define CHIP_RESET_FW                             0x0004
#define CHIP_FATAL_ERROR                          0x0008

/* define device types */
#define SAS_SATA_UNKNOWN_DEVICE                   0xFF       /**< SAS SATA unknown device type */

#define STP_DEVICE                                0x00       /**< SATA device behind an expander */
#define SSP_SMP_DEVICE                            0x01       /**< SSP or SMP device type */
#define DIRECT_SATA_DEVICE                        0x02       /**< SATA direct device type */

/* SATA */
#define SATA_FIS_MASK                             0x00000001
#define MAX_SATARESP_SUPPORT_BYTES                44

#define MARK_OFF                                  0xFFFFFFFF
#define PORT_MARK_OFF                             0xFFFFFFFF
#define NO_FATAL_ERROR_VECTOR                     0xFFFFFFFF

#define SATA_PROTOCOL_RSRT_ASSERT                 0x01
#define SATA_PROTOCOL_RSRT_DEASSERT               0x02
#define SATA_NON_DATA_PROTOCOL                    0x0d
#define SATA_PIO_READ_PROTOCOL                    0x0e
#define SATA_DMA_READ_PROTOCOL                    0x0f
#define SATA_FPDMA_READ_PROTOCOL                  0x10
#define SATA_PIO_WRITE_PROTOCOL                   0x11
#define SATA_DMA_WRITE_PROTOCOL                   0x12
#define SATA_FPDMA_WRITE_PROTOCOL                 0x13
#define SATA_DEVICE_RESET_PROTOCOL                0x14

/* Definition for bit shift */
#define SHIFT0                                    0
#define SHIFT1                                    1
#define SHIFT2                                    2
#define SHIFT3                                    3
#define SHIFT4                                    4
#define SHIFT5                                    5
#define SHIFT6                                    6
#define SHIFT7                                    7
#define SHIFT8                                    8
#define SHIFT9                                    9
#define SHIFT10                                   10
#define SHIFT11                                   11
#define SHIFT12                                   12
#define SHIFT13                                   13
#define SHIFT14                                   14
#define SHIFT15                                   15
#define SHIFT16                                   16
#define SHIFT17                                   17
#define SHIFT18                                   18
#define SHIFT19                                   19
#define SHIFT20                                   20
#define SHIFT21                                   21
#define SHIFT22                                   22
#define SHIFT23                                   23
#define SHIFT24                                   24
#define SHIFT25                                   25
#define SHIFT26                                   26
#define SHIFT27                                   27
#define SHIFT28                                   28
#define SHIFT29                                   29
#define SHIFT30                                   30
#define SHIFT31                                   31

/* These flags used for saSSPAbort(), saSATAAbort() */
#define ABORT_MASK                                0x3
#define ABORT_SINGLE                              0x0
#define ABORT_SCOPE                               0x3 /* bits 0-1*/
#define ABORT_ALL                                 0x1
#define ABORT_TSDK_QUARANTINE                     0x4
#define ABORT_QUARANTINE_SPC                      0x4
#define ABORT_QUARANTINE_SPCV                     0x8

/* These flags used for saGetRegDump() */
#define REG_DUMP_NUM0                             0x0
#define REG_DUMP_NUM1                             0x1
#define REG_DUMP_NONFLASH                         0x0
#define REG_DUMP_FLASH                            0x1

/* MSIX Interupts */
#define MSIX_TABLE_OFFSET                         0x2000
#define MSIX_TABLE_ELEMENT_SIZE                   0x10
#define MSIX_INTERRUPT_CONTROL_OFFSET             0xC
#define MSIX_TABLE_BASE                   (MSIX_TABLE_OFFSET+MSIX_INTERRUPT_CONTROL_OFFSET)
#define MSIX_INTERRUPT_DISABLE                    0x1
#define MSIX_INTERRUPT_ENABLE                     0x0

#define MAX_QUEUE_EACH_MEM                        8

#define NUM_MEM_CHUNKS(Q, rem) ((((bit32)Q % rem) > 0) ? (bit32)(Q/rem+1) : (bit32)(Q/rem))
#define NUM_QUEUES_IN_MEM(Q, rem) ((((bit32)Q % rem) > 0) ? (bit32)(Q%rem) : (bit32)(MAX_QUEUE_EACH_MEM))

#define MAX_DEV_BITS                              0xFFFF0000
#define PHY_COUNT_BITS                            0x01f80000
#define Q_SUPPORT_BITS                            0x0007ffff
#define SAS_SPEC_BITS                             0xfe000000
#define HP_SUPPORT_BIT                            0x00010000
#define INT_COL_BIT                               0x00040000
#define INT_DELAY_BITS                            0xFFFF
#define INT_THR_BITS                              0xFF
#define INT_VEC_BITS                              0xFF

#define AUTO_HARD_RESET_DEREG_FLAG                0x00000001
#define AUTO_FW_CLEANUP_DEREG_FLAG                0x00000002

#define BYTE_MASK                                 0xff

#define INT_OPTION                                0x7FFF
#define SMP_TO_DEFAULT                            100
#define ITL_TO_DEFAULT                            0xFFFF


/*
agsaHwConfig_s  hwOption
*/
#define HW_CFG_PICI_EFFECTIVE_ADDRESS             0x1

/* SPC or SPCv ven dev Id */

#define SUBID_SPC                                 0x00000000
#define SUBID_SPCV                                0x56781234

#define VEN_DEV_SPC                               0x80010000
#define VEN_DEV_HIL                               0x80810000

#define VEN_DEV_SPCV                              0x80080000
#define VEN_DEV_SPCVE                             0x80090000
#define VEN_DEV_SPCVP                             0x80180000
#define VEN_DEV_SPCVEP                            0x80190000

#define VEN_DEV_SPC12V                            0x80700000
#define VEN_DEV_SPC12VE                           0x80710000
#define VEN_DEV_SPC12VP                           0x80720000
#define VEN_DEV_SPC12VEP                          0x80730000
#define VEN_DEV_9015                              0x90150000
#define VEN_DEV_9060                              0x90600000

#define VEN_DEV_ADAPVEP                           0x80890000
#define VEN_DEV_ADAPVP                            0x80880000


#define VEN_DEV_SFC                               0x80250000

/*DelRay PCIid */
#define VEN_DEV_SPC12ADP                          0x80740000 /* 8 ports */
#define VEN_DEV_SPC12ADPE                         0x80750000 /* 8 ports encrypt */
#define VEN_DEV_SPC12ADPP                         0x80760000 /* 16 ports  */
#define VEN_DEV_SPC12ADPEP                        0x80770000 /* 16 ports encrypt */
#define VEN_DEV_SPC12SATA                         0x80060000 /* SATA HBA */

#endif  /*__SADEFS_H__ */
