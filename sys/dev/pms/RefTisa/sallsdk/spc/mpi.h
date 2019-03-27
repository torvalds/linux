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
/*! \file mpi.h
 *  \brief The file defines the MPI constants and structures
 *
 * The file defines the MPI constants and structures
 *
 */
/*******************************************************************************/

#ifndef __MPI_H__
#define __MPI_H__

/*******************************************************************************/

/*******************************************************************************/
/* CONSTANTS                                                                   */
/*******************************************************************************/
/*******************************************************************************/
#define MPI_QUEUE_PRIORITY_HIGHEST      0xFF  /**< Highest queue priority */
#define MPI_QUEUE_PRIORITY_LOWEST       0x00  /**< Lowest queue priority */

#define MPI_MAX_INBOUND_QUEUES          64     /**< Maximum number of inbound queues */
#define MPI_MAX_OUTBOUND_QUEUES         64     /**< Maximum number of outbound queues */

                                               /**< Max # of memory chunks supported */
#define MPI_MAX_MEM_REGIONS             (MPI_MAX_INBOUND_QUEUES + MPI_MAX_OUTBOUND_QUEUES) + 4
#define MPI_LOGSIZE                     4096  /**< default size */

#define MPI_IB_NUM_MASK                 0x0000FFFF /**< Mask of Inbound Queue Number */
#define MPI_OB_NUM_MASK                 0xFFFF0000 /**< Mask of Outbound Queue Number */
#define MPI_OB_SHIFT                    16         /**< bits shift for outbound queue number */


#define BAR0                            0x10
#define BAR1                            0x14
#define BAR2                            0x18
#define BAR3                            0x1C
#define BAR4                            0x20
#define BAR5                            0x24

/*******************************************************************************/
/*******************************************************************************/
/* ENUMERATIONS                                                                */
/*******************************************************************************/

/*******************************************************************************/
/*******************************************************************************/
/** \enum mpiMsgCategory_e,
 *  \brief MPI message categories
 */
/*******************************************************************************/
enum mpiMsgCategory_e
{
  MPI_CATEGORY_ETHERNET = 0,
  MPI_CATEGORY_FC,
  MPI_CATEGORY_SAS_SATA,
  MPI_CATEGORY_SCSI
};

typedef enum mpiMsgCategory_e mpiMsgCategory_t;

/*******************************************************************************/
/*******************************************************************************/
/* TYPES                                                                       */
/*******************************************************************************/
/*******************************************************************************/


/*******************************************************************************/
/*******************************************************************************/
/* DATA STRUCTURES                                                             */
/*******************************************************************************/
/*******************************************************************************/

/*******************************************************************************/
/** \struct mpiMem_s
 *  \brief Structure that descibes memory regions
 *
 * The mpiMemoryDescriptor_t is used to describe the attributes for a memory
 * region. Each element in the memory chunk has to be physically contiguous.
 * Different elements in the memory chunk do not necessarily have to be
 * contiguous to each other.
 */
/*******************************************************************************/
struct mpiMem_s
{
  void*        virtPtr;       /**< Virtual pointer to the memory region */
  void*        appHandle;     /**< Handle used for the application to free memory */
  bit32        physAddrUpper; /**< Upper 32 bits of physical address */
  bit32        physAddrLower; /**< Lower 32 bits of physical address */
  bit32        totalLength;   /**< Total length in bytes allocated */
  bit32        numElements;   /**< Number of elements */
  bit32        elementSize;   /**< Size in bytes of an element */
  bit32        alignment;     /**< Alignment in bytes needed. A value of one indicates */
                              /**< no specific alignment requirement */
  bit32        type;          /**< Memory type */
  bit32        reserved;      /**< Reserved */
};

typedef struct mpiMem_s mpiMem_t;

/*******************************************************************************/
/** \struct mpiMemReq_s
 *  \brief Describes MPI memory requirements
 *
 * The mpiMemRequirements_t  is used to specify the memory allocation requirement
 * for the MPI. This is the data structure used in the mpiGetRequirements()
 * and the mpiInitialize() function calls
 */
/*******************************************************************************/
struct mpiMemReq_s
{
  bit32     count;                        /**< The number of element in the mpiMemory array */
  mpiMem_t  region[MPI_MAX_MEM_REGIONS];  /**< Pointer to the array of structures that define memroy regions */
};

typedef struct mpiMemReq_s mpiMemReq_t;

/*******************************************************************************/
/** \struct mpiQCQueue_s
 *  \brief Circular Queue descriptor
 *
 * This structure holds outbound circular queue attributes.
 */
/*******************************************************************************/
struct mpiOCQueue_s
{
  bit32                     qNumber;      /**< this queue number */
  bit32                     numElements;  /**< The total number of queue elements. A value 0 disables the queue */
  bit32                     elementSize;  /**< The size of each queue element, in bytes */
  bit32                     priority;     /**< The queue priority. Possible values for this field are */
                                          /**< MPI_QUEUE_PRIORITY_HIGHEST and MPI_QUEUE_PRIORITY_LOWEST */
  bit32                     CIPCIBar;     /**< PCI Bar */
  bit32                     CIPCIOffset;  /**< PCI Offset */
  bit32                     DIntTOffset;  /**< Dynamic Interrupt Coalescing Timeout offset */
  void*                     piPointer;    /**< pointer of PI (virtual address)*/
  mpiMem_t                  memoryRegion; /**< Queue's memory region descriptor */
  bit32                     producerIdx;  /**< Copy of the producer index */
  bit32                     consumerIdx;  /**< Copy of the consumer index */
  bit32                     pcibar;       /**< CPI Logical Bar Number */
  agsaRoot_t                *agRoot;      /**< Pointer of LL Layer structure */
};

typedef struct mpiOCQueue_s mpiOCQueue_t;

/*******************************************************************************/
/** \struct mpiICQueue_s
 *  \brief Circular Queue descriptor
 *
 * This structure holds inbound circular queue attributes.
 */
/*******************************************************************************/
struct mpiICQueue_s
{
  bit32                     qNumber;      /**< this queue number */
  bit32                     numElements;  /**< The total number of queue elements. A value 0 disables the queue */
  bit32                     elementSize;  /**< The size of each queue element, in bytes */
  bit32                     priority;     /**< The queue priority. Possible values for this field are */
                                          /**< MPI_QUEUE_PRIORITY_HIGHEST and MPI_QUEUE_PRIORITY_LOWEST */
  bit32                     PIPCIBar;     /**< PCI Bar */
  bit32                     PIPCIOffset;  /**< PCI Offset */
  void*                     ciPointer;    /**< Pointer of CI (virtual Address) */
  mpiMem_t                  memoryRegion; /**< Queue's memory region descriptor */
  bit32                     producerIdx;  /**< Copy of the producer index */
  bit32                     consumerIdx;  /**< Copy of the consumer index */
#ifdef SA_FW_TEST_BUNCH_STARTS
  bit32                     BunchStarts_QPending;     // un-started bunched IOs on queue
  bit32                     BunchStarts_QPendingTick; // tick value when 1st IO is bunched 
#endif /* SA_FW_TEST_BUNCH_STARTS */
  agsaRoot_t                *agRoot;      /**< Pointer of LL Layer structure */
};

typedef struct mpiICQueue_s mpiICQueue_t;

struct mpiHostLLConfigDescriptor_s
{
  bit32 regDumpPCIBAR;
  bit32 iQNPPD_HPPD_GEvent;                 /**< inbound Queue Process depth */
        /* bit0-7   inbound normal priority process depth */
        /* bit8-15  inbound high priority process depth */
        /* bit16-23 OQ number to receive GENERAL_EVENT Notification */
        /* bit24-31 reserved */
  bit32 outboundHWEventPID0_3;              /**< outbound HW event for PortId 0 to 3 */
        /* bit0-7   outbound queue number of SAS_HW event for PortId 0 */
        /* bit8-15  outbound queue number of SAS_HW event for PortId 1 */
        /* bit16-23 outbound queue number of SAS_HW event for PortId 2 */
        /* bit24-31 outbound queue number of SAS_HW event for PortId 3 */
  bit32 outboundHWEventPID4_7;              /**< outbound HW event for PortId 4 to 7 */
        /* bit0-7   outbound queue number of SAS_HW event for PortId 4 */
        /* bit8-15  outbound queue number of SAS_HW event for PortId 5 */
        /* bit16-23 outbound queue number of SAS_HW event for PortId 6 */
        /* bit24-31 outbound queue number of SAS_HW event for PortId 7 */
  bit32 outboundNCQEventPID0_3;             /**< outbound NCQ event for PortId 0 to 3 */
        /* bit0-7   outbound queue number of SATA_NCQ event for PortId 0 */
        /* bit8-15  outbound queue number of SATA_NCQ event for PortId 1 */
        /* bit16-23 outbound queue number of SATA_NCQ event for PortId 2 */
        /* bit24-31 outbound queue number of SATA_NCQ event for PortId 3 */
  bit32 outboundNCQEventPID4_7;             /**< outbound NCQ event for PortId 4 to 7 */
        /* bit0-7   outbound queue number of SATA_NCQ event for PortId 4 */
        /* bit8-15  outbound queue number of SATA_NCQ event for PortId 5 */
        /* bit16-23 outbound queue number of SATA_NCQ event for PortId 6 */
        /* bit24-31 outbound queue number of SATA_NCQ event for PortId 7 */
  bit32 outboundTargetITNexusEventPID0_3;   /**< outbound target ITNexus Event for PortId 0 to 3 */
        /* bit0-7   outbound queue number of ITNexus event for PortId 0 */
        /* bit8-15  outbound queue number of ITNexus event for PortId 1 */
        /* bit16-23 outbound queue number of ITNexus event for PortId 2 */
        /* bit24-31 outbound queue number of ITNexus event for PortId 3 */
  bit32 outboundTargetITNexusEventPID4_7;   /**< outbound target ITNexus Event for PortId 4 to 7 */
        /* bit0-7   outbound queue number of ITNexus event for PortId 4 */
        /* bit8-15  outbound queue number of ITNexus event for PortId 5 */
        /* bit16-23 outbound queue number of ITNexus event for PortId 6 */
        /* bit24-31 outbound queue number of ITNexus event for PortId 7 */
  bit32 outboundTargetSSPEventPID0_3;       /**< outbound target SSP event for PordId 0 to 3 */
        /* bit0-7   outbound queue number of SSP event for PortId 0 */
        /* bit8-15  outbound queue number of SSP event for PortId 1 */
        /* bit16-23 outbound queue number of SSP event for PortId 2 */
        /* bit24-31 outbound queue number of SSP event for PortId 3 */
  bit32 outboundTargetSSPEventPID4_7;       /**< outbound target SSP event for PordId 4 to 7 */
        /* bit0-7   outbound queue number of SSP event for PortId 4 */
        /* bit8-15  outbound queue number of SSP event for PortId 5 */
        /* bit16-23 outbound queue number of SSP event for PortId 6 */
        /* bit24-31 outbound queue number of SSP event for PortId 7 */
  bit32 ioAbortDelay;   /* was reserved */                 /**< io Abort delay MPI_TABLE_CHANGE */
  bit32 custset;                          /**< custset */
  bit32 upperEventLogAddress;               /**< Upper physical MSGU Event log address */
  bit32 lowerEventLogAddress;               /**< Lower physical MSGU Event log address */
  bit32 eventLogSize;                       /**< Size of MSGU Event log, 0 means log disable */
  bit32 eventLogOption;                     /**< Option of MSGU Event log */
        /* bit3-0 log severity, 0x0 Disable Logging */
        /*                      0x1 Critical Error */
        /*                      0x2 Minor Error    */
        /*                      0x3 Warning        */
        /*                      0x4 Information    */
        /*                      0x5 Debugging      */
        /*                      0x6 - 0xF Reserved */
  bit32 upperIOPeventLogAddress;           /**< Upper physical IOP Event log address */
  bit32 lowerIOPeventLogAddress;           /**< Lower physical IOP Event log address */
  bit32 IOPeventLogSize;                   /**< Size of IOP Event log, 0 means log disable */
  bit32 IOPeventLogOption;                 /**< Option of IOP Event log */
        /* bit3-0 log severity, 0x0 Disable Logging */
        /*                      0x1 Critical Error */
        /*                      0x2 Minor Error    */
        /*                      0x3 Warning        */
        /*                      0x4 Information    */
        /*                      0x5 Debugging      */
        /*                      0x6 - 0xF Reserved */
  bit32 FatalErrorInterrupt;               /**< Fatal Error Interrupt enable and vector */
        /* bit0     Fatal Error Interrupt Enable   */
        /* bit1     PI/CI Address                  */
        /* bit5     enable or disable outbound coalesce   */
        /* bit7-6  reserved */
        /* bit15-8  Fatal Error Interrupt Vector   */
        /* bit31-16 Reserved                       */
  bit32 FatalErrorDumpOffset0;             /**< Fatal Error Register Dump Offset for MSGU */
  bit32 FatalErrorDumpLength0;             /**< Fatal Error Register Dump Length for MSGU */
  bit32 FatalErrorDumpOffset1;             /**< Fatal Error Register Dump Offset for IOP */
  bit32 FatalErrorDumpLength1;             /**< Fatal Error Register Dump Length for IOP */
  bit32 HDAModeFlags;                      /**< HDA Mode Flags */
        /* bit1-0   Bootstrap pins */
        /* bit2     Force HDA Mode bit */
        /* bit3     HDA Firmware load method */
  bit32 analogSetupTblOffset;              /**< Phy Calibration Table offset */
        /* bit23-0  phy calib table offset */
        /* bit31-24 entry size */
  bit32 InterruptVecTblOffset;             /**< DW23 Interrupt Vector Table */
        /* bit23-0  interrupt vector table offset */
        /* bit31-24 entry size */
  bit32 phyAttributeTblOffset;             /**< DW24 SAS Phy Attribute Table Offset */
        /* bit23-0  phy attribute table offset */
        /* bit31-24 entry size */
  bit32 PortRecoveryTimerPortResetTimer;  /**< DW25 Port Recovery Timer and Port Reset Timer */
  bit32 InterruptReassertionDelay;        /**< DW26 Interrupt Reassertion Delay 0:23 Reserved 24:31 */
};

typedef struct mpiHostLLConfigDescriptor_s mpiHostLLConfigDescriptor_t;

/*******************************************************************************/
/** \struct mpiInboundQueueDescriptor_s
 *  \brief MPI inbound queue attributes
 *
 * The mpiInboundQueueDescriptor_t structure is used to describe an inbound queue
 * attributes
 */
/*******************************************************************************/
struct mpiInboundQueueDescriptor_s
{
  bit32                     numElements;     /**< The total number of queue elements. A value 0 disables the queue */
  bit32                     elementSize;     /**< The size of each queue element, in bytes */
  bit32                     priority;        /**< The queue priority. Possible values for this field are */
                                              /**< MPI_QUEUE_PRIORITY_HIGHEST and MPI_QUEUE_PRIORITY_LOWEST */
  bit32                     PIPCIBar;        /**< PI PCIe Bar */
  bit32                     PIOffset;        /**< PI PCI Bar Offset */
  void*                     ciPointer;       /**< Pointer of CI (virtual Address) */
};

typedef struct mpiInboundQueueDescriptor_s mpiInboundQueueDescriptor_t;

/*******************************************************************************/
/** \struct mpiOutboundQueueDescriptor_s
 *  \brief MPI outbound queue attributes
 *
 * The mpiOutboundQueueDescriptor_t structure is used to describe an outbound queue
 * attributes
 */
/*******************************************************************************/
struct mpiOutboundQueueDescriptor_s
{
  bit32                     numElements;        /**< The total number of queue elements. A value 0 disables the queue */
  bit32                     elementSize;        /**< The size of each queue element, in bytes */
  bit32                     interruptDelay;     /**< Delay in microseconds before the interrupt is asserted */
                                                 /**< if the interrupt threshold has not been reached */
  bit32                     interruptThreshold; /**< Number of interrupt events before the interrupt is asserted */
                                                 /**< If set to 0, interrupts for this queue are disablec */
  bit32                     interruptVector;    /**< Interrupt vector assigned to this queue */
  bit32                     CIPCIBar;           /**< offset 0x14:PCI BAR for CI Offset */
  bit32                     CIOffset;           /**< offset 0x18:Offset address for outbound queue CI */
  bit32                     DIntTOffset;        /**< Dynamic Interrupt Coalescing Timeout offset */
  bit32                     interruptEnable;    /**< Interrupt enable flag */
  void*                     piPointer;          /**< pointer of PI (virtual address)*/
};

typedef struct mpiOutboundQueueDescriptor_s mpiOutboundQueueDescriptor_t;

/*******************************************************************************/
/** \struct mpiPhyCalibration_s
 *  \brief MPI Phy Calibration Table
 *
 * The mpiPhyCalibration_s structure is used to set Phy Calibration
 * attributes
 */
/*******************************************************************************/
struct mpiPhyCalibration_s
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

typedef struct mpiPhyCalibration_s mpiPhyCalibration_t;

#define ANALOG_SETUP_ENTRY_NO              10
#define ANALOG_SETUP_ENTRY_SIZE            10


/*******************************************************************************/
/** \struct mpiConfig_s
 *  \brief MPI layer configuration parameters
 *
 * The mpiConfig_s structure is used as a parameter passed in
 * mpiGetRequirements() and mpiInitialize() to describe the MPI software
 * configuration
 */
/*******************************************************************************/
struct mpiVConfig_s
{
  mpiHostLLConfigDescriptor_t  mainConfig;                              /**< main part of configuration table */
  mpiInboundQueueDescriptor_t  inboundQueues[MPI_MAX_INBOUND_QUEUES];   /**< mpiQueueDescriptor structures that provide initialization */
                                                                        /**< attributes for the inbound queues. The maximum number of */
                                                                        /**< inbound queues is MPI_MAX_INBOUND_QUEUES */
  mpiOutboundQueueDescriptor_t outboundQueues[MPI_MAX_OUTBOUND_QUEUES]; /**< mpiQueueDescriptor structures that provide initialization */
                                                                        /**< attributes for the outbound queues. The maximum number of */
                                                                        /**< inbound queues is MPI_MAX_OUTBOUND_QUEUES */
  agsaPhyAnalogSetupTable_t    phyAnalogConfig;
  mpiInterruptVT_t             interruptVTable;
  sasPhyAttribute_t            phyAttributeTable;
  bit16   numInboundQueues;
  bit16   numOutboundQueues;
  bit16   maxNumInboundQueues;
  bit16   maxNumOutboundQueues;
  bit32   queueOption;
};

/*******************************************************************************/
/** \struct mpiConfig_s
 *  \brief MPI layer configuration parameters
 *
 * The mpiConfig_s structure is used as a parameter passed in
 * mpiGetRequirements() and mpiInitialize() to describe the MPI software
 * configuration
 */
/*******************************************************************************/
struct mpiConfig_s
{
  mpiHostLLConfigDescriptor_t  mainConfig;                              /**< main part of configuration table */
  mpiInboundQueueDescriptor_t  inboundQueues[MPI_MAX_INBOUND_QUEUES];   /**< mpiQueueDescriptor structures that provide initialization */
                                                                        /**< attributes for the inbound queues. The maximum number of */
                                                                        /**< inbound queues is MPI_MAX_INBOUND_QUEUES */
  mpiOutboundQueueDescriptor_t outboundQueues[MPI_MAX_OUTBOUND_QUEUES]; /**< mpiQueueDescriptor structures that provide initialization */
                                                                        /**< attributes for the outbound queues. The maximum number of */
                                                                        /**< inbound queues is MPI_MAX_OUTBOUND_QUEUES */
  agsaPhyAnalogSetupTable_t    phyAnalogConfig;
  bit16   numInboundQueues;
  bit16   numOutboundQueues;
  bit16   maxNumInboundQueues;
  bit16   maxNumOutboundQueues;
  bit32   queueOption;
};

typedef struct mpiConfig_s  mpiConfig_t;

#define TX_PORT_CFG1_OFFSET                0x00
#define TX_PORT_CFG2_OFFSET                0x04
#define TX_PORT_CFG3_OFFSET                0x08
#define TX_CFG_OFFSET                      0x0c
#define RV_PORT_CFG1_OFFSET                0x10
#define RV_PORT_CFG2_OFFSET                0x14
#define RV_CFG1_OFFSET                     0x18
#define RV_CFG2_OFFSET                     0x1c

/*******************************************************************************/
/*******************************************************************************/
/* FUNCTIONS                                                                   */
/*******************************************************************************/
/*******************************************************************************/
/*******************************************************************************/
void      mpiRequirementsGet(mpiConfig_t *config, mpiMemReq_t *memoryRequirement);
FORCEINLINE bit32 mpiMsgFreeGet(mpiICQueue_t *circularQ, bit16 messageSize, void** messagePtr);
FORCEINLINE bit32 mpiMsgProduce(mpiICQueue_t *circularQ, void* messagePtr,
                        mpiMsgCategory_t category, bit16 opCode,
                        bit8 responseQueue, bit8 hiPriority);
#ifdef LOOPBACK_MPI
GLOBAL bit32 mpiMsgProduceOQ(mpiOCQueue_t *circularQ, void *messagePtr,
                             mpiMsgCategory_t category, bit16 opCode,
                             bit8 responseQueue, bit8 hiPriority);
GLOBAL bit32 mpiMsgFreeGetOQ(mpiOCQueue_t *circularQ, bit16 messageSize,
                             void** messagePtr);
#endif

#ifdef FAST_IO_TEST
bit32     mpiMsgPrepare(mpiICQueue_t *circularQ, void* messagePtr,
                        mpiMsgCategory_t category, bit16 opCode,
                        bit8 responseQueue, bit8 hiPriority);

bit32     mpiMsgProduceSend(mpiICQueue_t *circularQ, void* messagePtr,
                        mpiMsgCategory_t category, bit16 opCode,
                        bit8 responseQueue, bit8 hiPriority, int sendFl);
GLOBAL void mpiIBQMsgSend(mpiICQueue_t *circularQ);
#define INQ(queueNum) (bit8)(queueNum & MPI_IB_NUM_MASK)
#define OUQ(queueNum) (bit8)((queueNum & MPI_OB_NUM_MASK) >> MPI_OB_SHIFT)
#endif

FORCEINLINE bit32 mpiMsgConsume(mpiOCQueue_t *circularQ, void** messagePtr1, mpiMsgCategory_t *pCategory, bit16* pOpCode, bit8 *pBC);
FORCEINLINE bit32 mpiMsgFreeSet(mpiOCQueue_t *circularQ, void* messagePtr1, bit8 bc);

#endif /* __MPI_H__ */

