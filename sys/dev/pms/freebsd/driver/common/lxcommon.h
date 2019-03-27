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
*******************************************************************************/
/******************************************************************************

Module Name:  
  lxcommon.h
Abstract:  
  TISA Initiator/target driver module constant define header file
Environment:  
  Kernel or loadable module  

******************************************************************************/


#include <dev/pms/RefTisa/tisa/api/titypes.h>


#define LINUX_DMA_MEM_MAX       0x1ffe0   /* 128k - 32, real 128k - 24 */
#define DEK_MAX_TABLE_ITEMS     DEK_MAX_TABLE_ENTRIES // from tisa/api/titypes.h

/*
** IP address length based on character.
*/
#ifdef AGTIAPI_IP6_SUPPORT
#  define IP_ADDR_CHAR_LEN      64
#else
#  define IP_ADDR_CHAR_LEN      16
#endif

#define MSEC_PER_TICK               (1000/hz)     /* milisecond per tick */
#define USEC_PER_TICK               (1000000/hz)  /* microsecond per tick */
#define AGTIAPI_64BIT_ALIGN     8       /* 64 bit environment alignment */

/*
** Max device supported
*/
#define AGTIAPI_MAX_CARDS           4   /* card supported up to system limit */
#define AGTIAPI_TOO_MANY_CARDS     -1   /* beyond defined max support */
#define AGTIAPI_MAX_PORTALS         16   /* max portal per card */
/* max device per portal */

/*
** Adjustable Parameter Options
*/
#define AGTIAPI_OPTION_ON       1       /* adjustable parameter available */
#define AGTIAPI_KEY_MAX         64      /* max number of keys */
#define AGTIAPI_STRING_MAX      512     /* max length for string */
#define AGTIAPI_PARAM_MAX       256     /* max number of parameters */
#ifdef TARGET_DRIVER 
#define AGTIAPI_DMA_MEM_LIST_MAX    4096 /* max number of DMA memory list */
#define AGTIAPI_CACHE_MEM_LIST_MAX  24  /* max number of CACHE memory list */
#else /* INITIATOR_DRIVER */
#define AGTIAPI_DMA_MEM_LIST_MAX    1024 /* max number of DMA memory list */
#define AGTIAPI_CACHE_MEM_LIST_MAX  1024 /* max number of CACHE memory list */
#endif
#ifndef AGTIAPI_DYNAMIC_MAX
#define AGTIAPI_DYNAMIC_MAX     4096    /* max unreleased dynamic memory */
#endif
#define AGTIAPI_LOOP_MAX        4       /* max loop for init process */

#define AGTIAPI_MAX_NAME        70      // Max string name length
#define AGTIAPI_MIN_NAME        10      // minimum space for SAS name string
#define AGTIAPI_MAX_ID          8       // Max string id length

/* 
** Card-port status definitions
*/
#define AGTIAPI_INIT_TIME           0x00000001
#define AGTIAPI_SOFT_RESET          0x00000002
#define AGTIAPI_HAD_RESET           0x00000004 // ###
#define AGTIAPI_DISC_DONE           0x00000008
#define AGTIAPI_INSTALLED           0x00000010
#define AGTIAPI_RESET               0x00000020
#define AGTIAPI_FLAG_UP             0x00000040
#define AGTIAPI_CB_DONE             0x00000080
#define AGTIAPI_DISC_COMPLETE       0x00000100
#define AGTIAPI_IOREGION_REQUESTED  0x00000200
#define AGTIAPI_IRQ_REQUESTED       0x00000400
#define AGTIAPI_SCSI_REGISTERED     0x00000800
#define AGTIAPI_NAME_SERVER_UP      0x00001000
#define AGTIAPI_PORT_INITIALIZED    0x00002000
#define AGTIAPI_PORT_LINK_UP        0x00004000
#define AGTIAPI_LGN_LINK_UP         0x00008000
#define AGTIAPI_PORT_PANIC          0x00010000
#define AGTIAPI_RESET_SUCCESS       0x00020000
#define AGTIAPI_PORT_START          0x00040000
#define AGTIAPI_PORT_STOPPED        0x00080000
#define AGTIAPI_PORT_SHUTDOWN       0x00100000
#define AGTIAPI_IN_USE              0x00200000
#define AGTIAPI_SYS_INTR_ON         0x00400000
#define AGTIAPI_PORT_DISC_READY     0x00800000
#define AGTIAPI_SIG_DOWN            0x01000000
#define AGTIAPI_SIG_UP              0x02000000
#define AGTIAPI_TASK                0x04000000
#define AGTIAPI_INITIATOR           0x08000000
#define AGTIAPI_TARGET              0x10000000
#define AGTIAPI_TIMER_ON            0x20000000
#define AGTIAPI_SHUT_DOWN           0x40000000
/* reserved for ccb flag TASK_MANAGEMENT
#define AGTIAPI_RESERVED            0x80000000
*/
#define AGTIAPI_RESET_ALL           0xFFFFFFFF

/*
** PCI defines
*/
#ifndef PCI_VENDOR_ID_HP
#define PCI_VENDOR_ID_HP             0x103c
#endif

#ifndef PCI_VENDOR_ID_PMC_SIERRA
#define PCI_VENDOR_ID_PMC_SIERRA     0x11F8
#endif

#ifndef PCI_VENDOR_ID_AGILENT
#define PCI_VENDOR_ID_AGILENT        0x15bc
#endif

#ifndef PCI_VENDOR_ID_CYCLONE
#define PCI_VENDOR_ID_CYCLONE        0x113C
#endif

#ifndef PCI_VENDOR_ID_SPCV_FPGA
#define PCI_VENDOR_ID_SPCV_FPGA      0x1855
#endif

#ifndef PCI_VENDOR_ID_HIALEAH
#define PCI_VENDOR_ID_HIALEAH        0x9005
#endif

#define PCI_DEVICE_ID_HP_TS          0x102a
#define PCI_DEVICE_ID_HP_TL          0x1028
#define PCI_DEVICE_ID_HP_XL2         0x1029
#define PCI_DEVICE_ID_AG_DX2         0x0100
#define PCI_DEVICE_ID_AG_DX2PLUS     0x0101
#define PCI_DEVICE_ID_AG_QX2         0x0102
#define PCI_DEVICE_ID_AG_QX4         0x0103
#define PCI_DEVICE_ID_AG_QE4         0x1200
#define PCI_DEVICE_ID_AG_DE4         0x1203
#define PCI_DEVICE_ID_AG_XL10        0x0104
#define PCI_DEVICE_ID_AG_DX4PLUS     0x0105
#define PCI_DEVICE_ID_AG_DIXL        0x0110
#define PCI_DEVICE_ID_AG_IDX1        0x050A
#define PCI_DEVICE_ID_PMC_SIERRA_SPC        0x8001
#define PCI_DEVICE_ID_PMC_SIERRA_SPCV       0x8008
#define PCI_DEVICE_ID_PMC_SIERRA_SPCVE      0x8009
#define PCI_DEVICE_ID_PMC_SIERRA_SPCVPLUS   0x8018
#define PCI_DEVICE_ID_PMC_SIERRA_SPCVE_16   0x8019
#define PCI_DEVICE_ID_SPCV_FPGA             0xabcd
#define PCI_DEVICE_ID_PMC_SIERRA_SPCV12G     0x8070
#define PCI_DEVICE_ID_PMC_SIERRA_SPCVE12G    0x8071
#define PCI_DEVICE_ID_PMC_SIERRA_SPCV12G_16  0x8072
#define PCI_DEVICE_ID_PMC_SIERRA_SPCVE12G_16 0x8073
#define PCI_DEVICE_ID_HIALEAH_HBA_SPC        0x8081
#define PCI_DEVICE_ID_HIALEAH_RAID_SPC       0x8091
#define PCI_DEVICE_ID_HIALEAH_HBA_SPCV       0x8088
#define PCI_DEVICE_ID_HIALEAH_RAID_SPCV      0x8098
#define PCI_DEVICE_ID_HIALEAH_HBA_SPCVE      0x8089
#define PCI_DEVICE_ID_HIALEAH_RAID_SPCVE     0x8099
#define PCI_DEVICE_ID_DELRAY_HBA_8PORTS_SPCV       0x8074
#define PCI_DEVICE_ID_DELRAY_HBA_8PORTS_SPCVE      0x8075
#define PCI_DEVICE_ID_DELRAY_HBA_16PORTS_SPCV      0x8076
#define PCI_DEVICE_ID_DELRAY_HBA_16PORTS_SPCVE     0x8077
#define PCI_DEVICE_ID_DELRAY_HBA_16PORTS_SPCV_SATA 0x8006


#define PCI_SUB_VENDOR_ID_HP         PCI_VENDOR_ID_HP
#define PCI_SUB_VENDOR_ID_AG         PCI_VENDOR_ID_AGILENT
#define PCI_SUB_VENDOR_ID_MASK       0xFFFF
#define PCI_SUB_SYSTEM_ID_AG         0x0001 
#define PCI_BASE_MEM_MASK            (~0x0F)

#define PCI_DEVICE_ID_CYCLONE        0xB555
#define PCI_ENABLE_VALUE             0x0157
#ifdef PMC_SPC
#define PCI_NUMBER_BARS              6        
#endif
#define IOCTL_MN_GET_CARD_INFO          		0x11
/*
** Constant defines
*/
#define _08B      8
#define _16B     16
#define _24B     24
#define _32B     32
#define _64B     64
#define _128B   128
#define _256B   256
#define _512B   512

#define _1K    1024
#define _2K    2048
#define _4K    4096
#define _128K  (128*(_1K))

// Card property related info.
typedef struct _ag_card_id {
        U16 vendorId;                   /* pci vendor id */
        U16 deviceId;                   /* pci device id */
        S32 cardNameIndex;              /* structure index */
        U16 membar;                     /* pci memory bar offset */
        U16 iobar1;                     /* pci io bar 1 offset */
        U16 iobar2;                     /* pci io bar 2 offest */
        U16 reg;                        /* pci memory bar number */
} ag_card_id_t;


#define PCI_BASE_ADDRESS_0 PCIR_BAR(0)
#define PCI_BASE_ADDRESS_1 PCIR_BAR(1)
#define PCI_BASE_ADDRESS_2 PCIR_BAR(2)
#define PCI_BASE_ADDRESS_3 PCIR_BAR(3)
#define PCI_BASE_ADDRESS_4 PCIR_BAR(4)


ag_card_id_t ag_card_type[] = {
#ifdef AGTIAPI_ISCSI
  {PCI_VENDOR_ID_AGILENTj, PCI_DEVICE_ID_AG_DIXL, 1,
    PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_1, PCI_BASE_ADDRESS_0, 0},
  {PCI_VENDOR_ID_AGILENT, PCI_DEVICE_ID_AG_IDX1, 2,
    PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_1, PCI_BASE_ADDRESS_0, 0},
#endif
#ifdef AGTIAPI_FC
  {PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_TS, 3,
    PCI_BASE_ADDRESS_3, PCI_BASE_ADDRESS_1, PCI_BASE_ADDRESS_2, 3},
  {PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_TL, 4,
    PCI_BASE_ADDRESS_3, PCI_BASE_ADDRESS_1, PCI_BASE_ADDRESS_2, 3},
  {PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_XL2, 5,
    PCI_BASE_ADDRESS_3, PCI_BASE_ADDRESS_1, PCI_BASE_ADDRESS_2, 3},
  {PCI_VENDOR_ID_AGILENT, PCI_DEVICE_ID_AG_DX2, 6,
    PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 4},
  {PCI_VENDOR_ID_AGILENT, PCI_DEVICE_ID_AG_DX2PLUS, 7,
    PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 4},
  {PCI_VENDOR_ID_AGILENT, PCI_DEVICE_ID_AG_DX4PLUS, 8,
    PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 4},
  {PCI_VENDOR_ID_AGILENT, PCI_DEVICE_ID_AG_QX2, 9,
    PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 4},
  {PCI_VENDOR_ID_AGILENT, PCI_DEVICE_ID_AG_QX4, 10,
    PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 4},
  {PCI_VENDOR_ID_AGILENT, PCI_DEVICE_ID_AG_DE4, 11,
    PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 4},
  {PCI_VENDOR_ID_AGILENT, PCI_DEVICE_ID_AG_QE4, 12,
    PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 4},
  {PCI_VENDOR_ID_AGILENT, PCI_DEVICE_ID_AG_XL10, 13,
    PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 4},
#endif
#ifdef AGTIAPI_SA
#ifdef PMC_SPC
  {PCI_VENDOR_ID_PMC_SIERRA, PCI_DEVICE_ID_PMC_SIERRA_SPC, 14, 
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_PMC_SIERRA, PCI_DEVICE_ID_PMC_SIERRA_SPCV, 15,
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_PMC_SIERRA, PCI_DEVICE_ID_PMC_SIERRA_SPCVE, 16,
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_PMC_SIERRA, PCI_DEVICE_ID_PMC_SIERRA_SPCVPLUS, 17,
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_PMC_SIERRA, PCI_DEVICE_ID_PMC_SIERRA_SPCVE_16, 18,
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_SPCV_FPGA, PCI_DEVICE_ID_SPCV_FPGA, 19,
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_PMC_SIERRA, PCI_DEVICE_ID_PMC_SIERRA_SPCV12G, 20,
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_PMC_SIERRA, PCI_DEVICE_ID_PMC_SIERRA_SPCVE12G, 21,
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_PMC_SIERRA, PCI_DEVICE_ID_PMC_SIERRA_SPCV12G_16, 22,
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_PMC_SIERRA, PCI_DEVICE_ID_PMC_SIERRA_SPCVE12G_16, 23,
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_HIALEAH, PCI_DEVICE_ID_HIALEAH_HBA_SPC, 24, 
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_HIALEAH, PCI_DEVICE_ID_HIALEAH_RAID_SPC, 25, 
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_HIALEAH, PCI_DEVICE_ID_HIALEAH_HBA_SPCV, 26, 
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_HIALEAH, PCI_DEVICE_ID_HIALEAH_RAID_SPCV, 27, 
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_HIALEAH, PCI_DEVICE_ID_HIALEAH_HBA_SPCVE, 28, 
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_HIALEAH, PCI_DEVICE_ID_HIALEAH_RAID_SPCVE, 29, 
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_HIALEAH, PCI_DEVICE_ID_DELRAY_HBA_8PORTS_SPCV, 30, 
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_HIALEAH, PCI_DEVICE_ID_DELRAY_HBA_8PORTS_SPCVE, 31, 
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_HIALEAH, PCI_DEVICE_ID_DELRAY_HBA_16PORTS_SPCV, 32, 
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_HIALEAH, PCI_DEVICE_ID_DELRAY_HBA_16PORTS_SPCVE, 33, 
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
  {PCI_VENDOR_ID_HIALEAH, PCI_DEVICE_ID_DELRAY_HBA_16PORTS_SPCV_SATA, 34, 
   PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3, 0},
         
#endif  
#endif   //AGTIAPI_SA
};

static const char *ag_card_names[] = {
  "Unknown",
  "iSCSI DiXL Card",
  "iSCSI iDX1 Card",
  "Tachyon TS Fibre Channel Card",
  "Tachyon TL Fibre Channel Card",
  "Tachyon XL2 Fibre Channel Card",
  "Tachyon DX2 Fibre Channel Card",
  "Tachyon DX2+ Fibre Channel Card",
  "Tachyon DX4+ Fibre Channel Card",
  "Tachyon QX2 Fibre Channel Card",
  "Tachyon QX4 Fibre Channel Card",
  "Tachyon DE4 Fibre Channel Card",
  "Tachyon QE4 Fibre Channel Card",
  "Tachyon XL10 Fibre Channel Card",
#ifdef AGTIAPI_SA
#ifdef PMC_SPC
  "PMC Sierra SPC SAS-SATA Card",
  "PMC Sierra SPC-V SAS-SATA Card",
  "PMC Sierra SPC-VE SAS-SATA Card",
  "PMC Sierra SPC-V 16 Port SAS-SATA Card",
  "PMC Sierra SPC-VE 16 Port SAS-SATA Card",
  "PMC Sierra FPGA",
  "PMC Sierra SPC-V SAS-SATA Card 12Gig",
  "PMC Sierra SPC-VE SAS-SATA Card 12Gig",
  "PMC Sierra SPC-V 16 Port SAS-SATA Card 12Gig",
  "PMC Sierra SPC-VE 16 Port SAS-SATA Card 12Gig",
  "Adaptec Hialeah 4/8 Port SAS-SATA HBA Card 6Gig",
  "Adaptec Hialeah 4/8 Port SAS-SATA RAID Card 6Gig",
  "Adaptec Hialeah 8/16 Port SAS-SATA HBA Card 6Gig",
  "Adaptec Hialeah 8/16 Port SAS-SATA RAID Card 6Gig",
  "Adaptec Hialeah 8/16 Port SAS-SATA HBA Encryption Card 6Gig",
  "Adaptec Hialeah 8/16 Port SAS-SATA RAID Encryption Card 6Gig",
  "Adaptec Delray 8 Port SAS-SATA HBA Card 12Gig",
  "Adaptec Delray 8 Port SAS-SATA HBA Encryption Card 12Gig",
  "Adaptec Delray 16 Port SAS-SATA HBA Card 12Gig",
  "Adaptec Delray 16 Port SAS-SATA HBA Encryption Card 12Gig",
  "Adaptec SATA Adapter",
       
#endif  
#endif  
};



/*
**  Resource Info Structure
*/
typedef struct _ag_resource_info {
  tiLoLevelResource_t   tiLoLevelResource;    // Low level resource required
  tiInitiatorResource_t tiInitiatorResource;  // Initiator resource required
  tiTargetResource_t    tiTargetResource;     // Target resource required
  tiTdSharedMem_t       tiSharedMem;          // Shared memory by ti and td
} ag_resource_info_t;


//  DMA memory address pair
typedef struct _ag_dma_addr {
  void         *dmaVirtAddr;
  vm_paddr_t    dmaPhysAddr;
  U32           memSize;
  bit32         type;
  bus_addr_t    nocache_busaddr;
  void         *nocache_mem;
} ag_dma_addr_t;


typedef struct _CardInfo
{
  U32                 pciIOAddrLow;    /* PCI IOBASE lower */
  U32                 pciIOAddrUp;     /* PCI IOBASE Upper */
  U32_64    	      pciMemBase;      /* PCI MEMBASE, physical */
  U32_64    	      pciMemBaseSpc[PCI_NUMBER_BARS]; // PCI MEMBASE, physical
  U16	  		 	  deviceId;  // PCI device id
  U16	   			  vendorId;  // PCI Vendor id
  U32                 busNum;                  
  U32                 deviceNum;               
}CardInfo_t;

// Card info. for all cards and drivers
typedef struct _ag_card_info {
  struct mtx         pmIOLock;
  device_t           pPCIDev;         // PCI device pointer
  void              *pCard;           // pointer to per card data structure
  S32                cardNameIndex;
  U32                cardID;          // card system ID
  U32                cardIdIndex;
  U32                pciIOAddrLow;    // PCI IOBASE lower
  U32                pciIOAddrUp;     // PCI IOBASE Upper
  U32_64             pciMemBase;      // PCI MEMBASE, physical
  caddr_t            pciMemVirtAddr;  // PCI MEMBASE, virtual ptr
  U32                pciMemSize;      // PCI MEMBASE memory size
#ifdef AGTIAPI_SA
#ifdef FPGA_CARD
  U32_64             pciMemBase0;     // PCI MEMBASE, physical
  caddr_t            pciMemVirtAddr0; // PCI MEMBASE, virtual ptr
  U32                pciMemSize0;     // PCI MEMBASE memory size
#endif
#ifdef PMC_SPC
  struct resource    *pciMemBaseRscSpc[PCI_NUMBER_BARS];
  int                pciMemBaseRIDSpc[PCI_NUMBER_BARS];
  U32_64             pciMemBaseSpc[PCI_NUMBER_BARS];  // PCI MEMBASE, physical
  caddr_t            pciMemVirtAddrSpc[PCI_NUMBER_BARS];//PCI MEMBASE, virt ptr
  U32                pciMemSizeSpc[PCI_NUMBER_BARS]; // PCI MEMBASE memory size
#endif
#endif
  U16                 memBar;
  U16                 memReg;
  U32                 cacheIndex;
  U32                 dmaIndex;
  ag_dma_addr_t       tiDmaMem[AGTIAPI_DMA_MEM_LIST_MAX]; // dma addr list

  // all (free and allocated) mem slots
  ag_dma_addr_t       dynamicMem[AGTIAPI_DYNAMIC_MAX];

  // ptr to free mem slots
  ag_dma_addr_t       *freeDynamicMem[AGTIAPI_DYNAMIC_MAX]; 

  U16                 topOfFreeDynamicMem; // idx to the first free slot ptr

  void               *tiCachedMem[AGTIAPI_CACHE_MEM_LIST_MAX];// cached mem list
  ag_resource_info_t  tiRscInfo;  /* low level resource requirement */    
  U08                 WWN[AGTIAPI_MAX_NAME];  /* WWN for this card */
  U08                 WWNLen;

// #define MAX_MSIX_NUM_VECTOR 64 ##
#define MAX_MSIX_NUM_VECTOR 16 // 1 then 16 just for testing; 
#define MAX_MSIX_NUM_DPC    64 // 16
#define MAX_MSIX_NUM_ISR    64 // 16
#ifdef SPC_MSIX_INTR

                         // ## use as a map instead of presirq
  struct resource   *msix_entries[MAX_MSIX_NUM_VECTOR];
#endif
  U32                 maxInterruptVectors;
} ag_card_info_t;
 
/*
** Optional Adjustable Parameters Structures.
** Not using pointer structure for easy read and access tree structure.
** In the future if more layer of key tree involved, it might be a good
** idea to change the structure and program. 
*/
typedef struct _ag_param_value{
  char                   valueName[AGTIAPI_MAX_NAME];
  char                   valueString[AGTIAPI_STRING_MAX];
  struct _ag_param_value *next;
} ag_value_t;

typedef struct _ag_param_key{
  char                 keyName[AGTIAPI_MAX_NAME];
  ag_value_t           *pValueHead;
  ag_value_t           *pValueTail;
  struct _ag_param_key *pSubkeyHead;
  struct _ag_param_key *pSubkeyTail;
  struct _ag_param_key *next;
} ag_key_t;

/*
**  Portal info data structure
*/
typedef struct _ag_portal_info {
  U32               portID;
  U32               portStatus;
  U32               devTotal;
  U32               devPrev;
  tiPortInfo_t      tiPortInfo;
  tiPortalContext_t tiPortalContext;
#ifdef INITIATOR_DRIVER
  void              *pDevList[AGTIAPI_HW_LIMIT_DEVICE];
#endif
} ag_portal_info_t;

#define MAP_TABLE_ENTRY(pC, c, d, l) (pC->encrypt_map +                        \
                                     (c * pC->devDiscover * AGTIAPI_MAX_LUN) + \
                                     (d * AGTIAPI_MAX_LUN) +                   \
                                     (l))

#ifdef  CHAR_DEVICE
/*************************************************************************
Purpose: Payload Wraper for ioctl commands
***********************************************************************/
typedef struct datatosendt{
bit32 datasize; //buffer size
bit8 *data; //buffer
}datatosend;
/***********************************************************************/
#define AGTIAPI_IOCTL_BASE  'x'
#define AGTIAPI_IOCTL    _IOWR(AGTIAPI_IOCTL_BASE, 0,datatosend ) //receiving payload here//
#define AGTIAPI_IOCTL_MAX  1
#endif

#ifdef AGTIAPI_FLOW_DEBUG
#define AGTIAPI_FLOW(format, a...)  printf(format, ## a)
#else
#define AGTIAPI_FLOW(format, a...)
#endif

#ifdef AGTIAPI_DEBUG
#define AGTIAPI_PRINTK(format, a...)  printf(format, ## a)
#else
#define AGTIAPI_PRINTK(format, a...)
#endif

#ifdef AGTIAPI_INIT_DEBUG
#define AGTIAPI_INIT(format, a...)  printf(format, ## a)
/* to avoid losing the logs */
#define AGTIAPI_INIT_MDELAY(dly)  mdelay(dly)
#else
#define AGTIAPI_INIT(format, a...)
#define AGTIAPI_INIT_MDELAY(dly)
#endif

#ifdef AGTIAPI_INIT2_DEBUG
#define AGTIAPI_INIT2(format, a...)  printf(format, ## a)
#else
#define AGTIAPI_INIT2(format, a...)
#endif

#ifdef AGTIAPI_INIT_MEM_DEBUG
#define AGTIAPI_INITMEM(format, a...)  printf(format, ## a)
#else
#define AGTIAPI_INITMEM(format, a...)
#endif

#ifdef AGTIAPI_IO_DEBUG
#define AGTIAPI_IO(format, a...)       printf(format, ## a)
#else
#define AGTIAPI_IO(format, a...)
#endif

#ifdef AGTIAPI_LOAD_DELAY
#define AGTIAPI_INIT_DELAY(delay_time)  \
    {  \
      agtiapi_DelayMSec(delay_time);  \
    }
#else
#define AGTIAPI_INIT_DELAY(delay_time)
#endif

/*
 * AGTIAPI_KDB() will be used to drop into kernel debugger 
 * from driver code if kdb is involved.
 */
#ifdef AGTIAPI_KDB_ENABLE
#define AGTIAPI_KDB()  KDB_ENTER()
#else
#define AGTIAPI_KDB()
#endif

#if (BITS_PER_LONG == 64)
//#if 1
#define LOW_32_BITS(addr)   (U32)(addr & 0xffffffff)
#define HIGH_32_BITS(addr)  (U32)((addr >> 32) & 0xffffffff)
#else
#define LOW_32_BITS(addr)   (U32)addr
#define HIGH_32_BITS(addr)  0
#endif

#define AG_SWAP16(data)   (((data<<8) & 0xFF00) | (data>>8))
#define AG_SWAP24(data)   (((data<<16) & 0xFF0000) | \
                          ((data>>16) & 0xFF) | (data & 0xFF00))
#define AG_SWAP32(data)   ((data<<24) | ((data<<8) & 0xFF0000) | \
                          ((data>>8) & 0xFF00) | (data>>24))

#define AG_PCI_DEV_INFO(pdev)  ( \
  AGTIAPI_PRINTK("vendor id 0x%x device id 0x%x, slot %d, function %d\n", \
    pdev->vendor, pdev->device, PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn)) \
                               )

#define COUNT(arr)  (sizeof(arr) / sizeof(arr[0]))

#define PORTAL_CONTEXT_TO_PORTALDATA(pPortalContext) \
  ((ag_portal_data_t *)(((tiPortalContext_t *)pPortalContext)->osData))
#define PORTAL_STATUS(pPortalData) (pPortalData->portalInfo.portStatus)

#if (defined(DEFINE_OSTI_PORT_EVENT_IN_IBE)) || \
    (defined(DEFINE_OSTI_PORT_EVENT_IN_TFE))
#define TIROOT_TO_CARD(ptiRoot) \
          ((ag_card_t *)(((appRoot_t *)(ptiRoot->osData))->oscData))
#define TIROOT_TO_CARDINFO(ptiRoot) (TIROOT_TO_CARD(ptiRoot)->pCardInfo)
#define TIROOT_TO_PCIDEV(ptiRoot) (TIROOT_TO_CARDINFO(ptiRoot)->pPCIDev)
#else

#define TIROOT_TO_CARD(ptiRoot)     ((struct agtiapi_softc *)(ptiRoot->osData))
#define TIROOT_TO_CARDINFO(ptiRoot) (TIROOT_TO_CARD(ptiRoot)->pCardInfo)
#define TIROOT_TO_PCIDEV(ptiRoot)   (TIROOT_TO_CARD(ptiRoot)->my_dev)

#endif


#define Is_ADP7H(pmsc)		((0x90058088 == (pmsc->VidDid))?1:\
					(0x90058089 == (pmsc->VidDid))?1:0)
#define Is_ADP8H(pmsc)		((0x90058074 == (pmsc->VidDid))?1:\
					(0x90058076 == (pmsc->VidDid))?1:0)


#define __cacheline_aligned __attribute__((__aligned__(CACHE_LINE_SIZE)))

/*
** link data, need to be included at the start (offset 0) 
** of any strutures that are to be stored in the link list
*/
typedef struct _LINK_NODE
{
  struct _LINK_NODE *pNext;
  struct _LINK_NODE *pPrev;

  /* 
  ** for assertion purpose only
  */
  struct _LINK_NODE * pHead;     // track the link list the link is a member of
  void * pad;

} LINK_NODE, * PLINK_NODE __cacheline_aligned;


/*
** link list basic pointers
*/
typedef struct _LINK_LIST
{
  PLINK_NODE pHead;
  bit32   Count;
  LINK_NODE  Head __cacheline_aligned; // always one link to speed up insert&rm
} LINK_LIST, * PLINK_LIST __cacheline_aligned;


/********************************************************************
** MACROS
********************************************************************/
/*******************************************************************************
**
** MODULE NAME: comListInitialize            
**
** PURPOSE:     Initialize a link list.
**
** PARAMETERS:  PLINK_LIST  OUT - Link list definition.
**
** SIDE EFFECTS & CAVEATS:
**
** ALGORITHM:
**
*******************************************************************************/
#define comListInitialize(pList) {(pList)->pHead        = &((pList)->Head); \
                                  (pList)->pHead->pNext = (pList)->pHead;   \
                                  (pList)->pHead->pPrev = (pList)->pHead;   \
                                  (pList)->Count        = 0;                \
                                 }

/*******************************************************************************
**
** MODULE NAME: comLinkInitialize            
**
** PURPOSE:     Initialize a link.
**              This function should be used to initialize a new link before it
**              is used in the linked list. This will initialize the link so 
**              the assertion will work
**
** PARAMETERS:  PLINK_NODE      IN  - Link to be initialized.
**
** SIDE EFFECTS & CAVEATS:
**
** ALGORITHM:
**
*******************************************************************************/

#define comLinkInitialize(pLink) { (pLink)->pHead = NULL;    \
                                   (pLink)->pNext = NULL;    \
                                   (pLink)->pPrev = NULL;    \
                                 }

/*******************************************************************************
**
** MODULE NAME: comListAdd                   
**
** PURPOSE:     add a link at the tail of the list
**
** PARAMETERS:  PLINK_LIST OUT - Link list definition.
**              PLINK_NODE      IN  - Link to be inserted.
**
** SIDE EFFECTS & CAVEATS:
**   !!! assumes that fcllistInitialize has been called on the linklist
**   !!! if not, this function behavior is un-predictable
**
** ALGORITHM:
**
*******************************************************************************/
#define comListAdd(pList, pLink) {                                          \
                             (pLink)->pNext        = (pList)->pHead;        \
                             (pLink)->pPrev        = (pList)->pHead->pPrev; \
                             (pLink)->pPrev->pNext = (pLink);               \
                             (pList)->pHead->pPrev = (pLink);               \
                             (pList)->Count ++;                             \
                             (pLink)->pHead = (pList)->pHead;               \
                             }

/*******************************************************************************
**
** MODULE NAME: comListInsert                       
**
** PURPOSE:     insert a link preceding the given one
**
** PARAMETERS:  PLINK_LIST OUT - Link list definition.
**              PLINK_NODE      IN  - Link to be inserted after.
**              PLINK_NODE      IN  - Link to be inserted.
**
** SIDE EFFECTS & CAVEATS:
**   !!! assumes that fcllistInitialize has been called on the linklist
**   !!! if not, this function behavior is un-predictable
**
** ALGORITHM:
**
*******************************************************************************/

#define comListInsert(pList, pLink, pNew) {                                 \
                                 (pNew)->pNext        = (pLink);            \
                                 (pNew)->pPrev        = (pLink)->pPrev;     \
                                 (pNew)->pPrev->pNext = (pNew);             \
                                 (pLink)->pPrev       = (pNew);             \
                                 (pList)->Count ++;                         \
                                 (pNew)->pHead = (pList)->pHead;            \
                                 }

/*******************************************************************************
**
** MODULE NAME: comListRemove                
**
** PURPOSE:     remove the link from the list.
**
** PARAMETERS:  PLINK_LIST OUT  - Link list definition.
**              PLINK_NODE      IN   - Link to delet from list
**
** SIDE EFFECTS & CAVEATS:
**   !!! assumes that fcllistInitialize has been called on the linklist
**   !!! if not, this function behavior is un-predictable
**
**   !!! No validation is made on the list or the validity of the link
**   !!! the caller must make sure that the link is in the list 
**
**
** ALGORITHM:
**
*******************************************************************************/
#define comListRemove(pList, pLink) {                                   \
                           (pLink)->pPrev->pNext = (pLink)->pNext;      \
                           (pLink)->pNext->pPrev = (pLink)->pPrev;      \
                           (pLink)->pHead = NULL;                       \
                           (pList)->Count --;                           \
                           }

/*******************************************************************************
**
** MODULE NAME: comListGetHead         
**
** PURPOSE:     get the link following the head link.
**
** PARAMETERS:  PLINK_LIST  OUT - Link list definition.
**              RETURNS - PLINK_NODE   the link following the head
**                                  NULL if the following link is the head
**
** SIDE EFFECTS & CAVEATS:
**   !!! assumes that fcllistInitialize has been called on the linklist
**   !!! if not, this function behavior is un-predictable
**
** ALGORITHM:
**
*******************************************************************************/
#define comListGetHead(pList) comListGetNext(pList,(pList)->pHead)

/*******************************************************************************
**
** MODULE NAME: comListGetTail                     
**
** PURPOSE:     get the link preceding the tail link.
**
** PARAMETERS:  PLINK_LIST  OUT - Link list definition.
**              RETURNS - PLINK_NODE   the link preceding the head 
**                                  NULL if the preceding link is the head
**
** SIDE EFFECTS & CAVEATS:
**
** ALGORITHM:
**
*******************************************************************************/
#define comListGetTail(pList) comListGetPrev((pList), (pList)->pHead)

/*******************************************************************************
**
** MODULE NAME: comListGetCount                    
**
** PURPOSE:     get the number of links in the list excluding head and tail.
**
** PARAMETERS:  LINK_LIST  OUT - Link list definition.
**
** SIDE EFFECTS & CAVEATS:
**   !!! assumes that fcllistInitialize has been called on the linklist
**   !!! if not, this function behavior is un-predictable
**
** ALGORITHM:
**
*******************************************************************************/

#define comListGetCount(pList) ((pList)->Count)



/*******************************************************************************
**
** MODULE NAME: comListGetNext            
**
** PURPOSE:     get the next link in the list. (one toward tail)
**
** PARAMETERS:  PLINK_LIST  OUT - Link list definition.
**              PLINK_NODE       IN  - Link to get next to
**
**           return PLINK  - points to next link
**                           NULL if next link is head
**
** SIDE EFFECTS & CAVEATS:
**   !!! assumes that fcllistInitialize has been called on the linklist
**   !!! if not, this function behavior is un-predictable
**
**   !!! No validation is made on the list or the validity of the link
**   !!! the caller must make sure that the link is in the list 
**
** ALGORITHM:
**
*******************************************************************************/

#define comListGetNext(pList, pLink) (((pLink)->pNext == (pList)->pHead) ?  \
                                      NULL : (pLink)->pNext)                


/*******************************************************************************
**
** MODULE NAME: comListGetPrev            
**
** PURPOSE:     get the previous link in the list. (one toward head)
**
** PARAMETERS:  PLINK_LIST  OUT - Link list definition.
**              PLINK_NODE       IN  - Link to get prev to
**
**           return PLINK  - points to previous link
**                           NULL if previous link is head
**
** SIDE EFFECTS & CAVEATS:
**   !!! assumes that fcllistInitialize has been called on the linklist
**   !!! if not, this function behavior is un-predictable
**
**   !!! No validation is made on the list or the validity of the link
**   !!! the caller must make sure that the link is in the list 
**
** ALGORITHM:
**
*******************************************************************************/

/*lint -emacro(613,fiLlistGetPrev) */

#define comListGetPrev(pList, pLink) (((pLink)->pPrev == (pList)->pHead) ?  \
                                      NULL : (pLink)->pPrev)

#define AGT_INTERRUPT      IRQF_DISABLED
#define AGT_SAMPLE_RANDOM  IRQF_SAMPLE_RANDOM
#define AGT_SHIRQ          IRQF_SHARED
#define AGT_PROBEIRQ       IRQF_PROBE_SHARED
#define AGT_PERCPU         IRQF_PERCPU


#include "lxproto.h"

