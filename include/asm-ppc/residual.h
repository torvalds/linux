/* 7/18/95                                                                    */
/*----------------------------------------------------------------------------*/
/*      Residual Data header definitions and prototypes                       */
/*----------------------------------------------------------------------------*/

/* Structure map for RESIDUAL on PowerPC Reference Platform                   */
/* residual.h - Residual data structure passed in r3.                         */
/*              Load point passed in r4 to boot image.                        */
/* For enum's: if given in hex then they are bit significant,                 */
/*             i.e. only one bit is on for each enum                          */
/* Reserved fields must be filled with zeros.                                */

#ifdef __KERNEL__
#ifndef _RESIDUAL_
#define _RESIDUAL_

#ifndef __ASSEMBLY__

#define MAX_CPUS 32                     /* These should be set to the maximum */
#define MAX_MEMS 64                     /* number possible for this system.   */
#define MAX_DEVICES 256                 /* Changing these will change the     */
#define AVE_PNP_SIZE 32                 /* structure, hence the version of    */
#define MAX_MEM_SEGS 64                 /* this header file.                  */

/*----------------------------------------------------------------------------*/
/*               Public structures...                                         */
/*----------------------------------------------------------------------------*/

#include <asm/pnp.h>

typedef enum _L1CACHE_TYPE {
  NoneCAC = 0,
  SplitCAC = 1,
  CombinedCAC = 2
  } L1CACHE_TYPE;

typedef enum _TLB_TYPE {
  NoneTLB = 0,
  SplitTLB = 1,
  CombinedTLB = 2
  } TLB_TYPE;

typedef enum _FIRMWARE_SUPPORT {
  Conventional = 0x01,
  OpenFirmware = 0x02,
  Diagnostics = 0x04,
  LowDebug = 0x08,
  Multiboot = 0x10,
  LowClient = 0x20,
  Hex41 = 0x40,
  FAT = 0x80,
  ISO9660 = 0x0100,
  SCSI_InitiatorID_Override = 0x0200,
  Tape_Boot = 0x0400,
  FW_Boot_Path = 0x0800
  } FIRMWARE_SUPPORT;

typedef enum _FIRMWARE_SUPPLIERS {
  IBMFirmware = 0x00,
  MotoFirmware = 0x01,                  /* 7/18/95                            */
  FirmWorks = 0x02,                     /* 10/5/95                            */
  Bull = 0x03,                          /* 04/03/96                           */
  } FIRMWARE_SUPPLIERS;

typedef enum _ENDIAN_SWITCH_METHODS {
  UsePort92 = 0x01,
  UsePCIConfigA8 = 0x02,
  UseFF001030 = 0x03,
  } ENDIAN_SWITCH_METHODS;

typedef enum _SPREAD_IO_METHODS {
  UsePort850 = 0x00,
/*UsePCIConfigA8 = 0x02,*/
  } SPREAD_IO_METHODS;

typedef struct _VPD {

  /* Box dependent stuff */
  unsigned char PrintableModel[32];     /* Null terminated string.
                                           Must be of the form:
                                           vvv,<20h>,<model designation>,<0x0>
                                           where vvv is the vendor ID
                                           e.g. IBM PPS MODEL 6015<0x0>       */
  unsigned char Serial[16];             /* 12/94:
                                           Serial Number; must be of the form:
                                           vvv<serial number> where vvv is the
                                           vendor ID.
                                           e.g. IBM60151234567<20h><20h>      */
  unsigned char Reserved[48];
  unsigned long FirmwareSupplier;       /* See FirmwareSuppliers enum         */
  unsigned long FirmwareSupports;       /* See FirmwareSupport enum           */
  unsigned long NvramSize;              /* Size of nvram in bytes             */
  unsigned long NumSIMMSlots;
  unsigned short EndianSwitchMethod;    /* See EndianSwitchMethods enum       */
  unsigned short SpreadIOMethod;        /* See SpreadIOMethods enum           */
  unsigned long SmpIar;
  unsigned long RAMErrLogOffset;        /* Heap offset to error log           */
  unsigned long Reserved5;
  unsigned long Reserved6;
  unsigned long ProcessorHz;            /* Processor clock frequency in Hertz */
  unsigned long ProcessorBusHz;         /* Processor bus clock frequency      */
  unsigned long Reserved7;
  unsigned long TimeBaseDivisor;        /* (Bus clocks per timebase tic)*1000 */
  unsigned long WordWidth;              /* Word width in bits                 */
  unsigned long PageSize;               /* Page size in bytes                 */
  unsigned long CoherenceBlockSize;     /* Unit of transfer in/out of cache
                                           for which coherency is maintained;
                                           normally <= CacheLineSize.         */
  unsigned long GranuleSize;            /* Unit of lock allocation to avoid   */
                                        /*   false sharing of locks.          */

  /* L1 Cache variables */
  unsigned long CacheSize;              /* L1 Cache size in KB. This is the   */
                                        /*   total size of the L1, whether    */
                                        /*   combined or split                */
  unsigned long CacheAttrib;            /* L1CACHE_TYPE                       */
  unsigned long CacheAssoc;             /* L1 Cache associativity. Use this
                                           for combined cache. If split, put
                                           zeros here.                        */
  unsigned long CacheLineSize;          /* L1 Cache line size in bytes. Use
                                           for combined cache. If split, put
                                           zeros here.                        */
  /* For split L1 Cache: (= combined if combined cache) */
  unsigned long I_CacheSize;
  unsigned long I_CacheAssoc;
  unsigned long I_CacheLineSize;
  unsigned long D_CacheSize;
  unsigned long D_CacheAssoc;
  unsigned long D_CacheLineSize;

  /* Translation Lookaside Buffer variables */
  unsigned long TLBSize;                /* Total number of TLBs on the system */
  unsigned long TLBAttrib;              /* Combined I+D or split TLB          */
  unsigned long TLBAssoc;               /* TLB Associativity. Use this for
                                           combined TLB. If split, put zeros
                                           here.                              */
  /* For split TLB: (= combined if combined TLB) */
  unsigned long I_TLBSize;
  unsigned long I_TLBAssoc;
  unsigned long D_TLBSize;
  unsigned long D_TLBAssoc;

  unsigned long ExtendedVPD;            /* Offset to extended VPD area;
                                           null if unused                     */
  } VPD;

typedef enum _DEVICE_FLAGS {
  Enabled = 0x4000,                     /* 1 - PCI device is enabled          */
  Integrated = 0x2000,
  Failed = 0x1000,                      /* 1 - device failed POST code tests  */
  Static = 0x0800,                      /* 0 - dynamically configurable
                                           1 - static                         */
  Dock = 0x0400,                        /* 0 - not a docking station device
                                           1 - is a docking station device    */
  Boot = 0x0200,                        /* 0 - device cannot be used for BOOT
                                           1 - can be a BOOT device           */
  Configurable = 0x0100,                /* 1 - device is configurable         */
  Disableable = 0x80,                   /* 1 - device can be disabled         */
  PowerManaged = 0x40,                  /* 0 - not managed; 1 - managed       */
  ReadOnly = 0x20,                      /* 1 - device is read only            */
  Removable = 0x10,                     /* 1 - device is removable            */
  ConsoleIn = 0x08,
  ConsoleOut = 0x04,
  Input = 0x02,
  Output = 0x01
  } DEVICE_FLAGS;

typedef enum _BUS_ID {
  ISADEVICE = 0x01,
  EISADEVICE = 0x02,
  PCIDEVICE = 0x04,
  PCMCIADEVICE = 0x08,
  PNPISADEVICE = 0x10,
  MCADEVICE = 0x20,
  MXDEVICE = 0x40,                      /* Devices on mezzanine bus           */
  PROCESSORDEVICE = 0x80,               /* Devices on processor bus           */
  VMEDEVICE = 0x100,
  } BUS_ID;

typedef struct _DEVICE_ID {
  unsigned long BusId;                  /* See BUS_ID enum above              */
  unsigned long DevId;                  /* Big Endian format                  */
  unsigned long SerialNum;              /* For multiple usage of a single
                                           DevId                              */
  unsigned long Flags;                  /* See DEVICE_FLAGS enum above        */
  unsigned char BaseType;               /* See pnp.h for bit definitions      */
  unsigned char SubType;                /* See pnp.h for bit definitions      */
  unsigned char Interface;              /* See pnp.h for bit definitions      */
  unsigned char Spare;
  } DEVICE_ID;

typedef union _BUS_ACCESS {
  struct _PnPAccess{
    unsigned char CSN;
    unsigned char LogicalDevNumber;
    unsigned short ReadDataPort;
    } PnPAccess;
  struct _ISAAccess{
    unsigned char SlotNumber;           /* ISA Slot Number generally not
                                           available; 0 if unknown            */
    unsigned char LogicalDevNumber;
    unsigned short ISAReserved;
    } ISAAccess;
  struct _MCAAccess{
    unsigned char SlotNumber;
    unsigned char LogicalDevNumber;
    unsigned short MCAReserved;
    } MCAAccess;
  struct _PCMCIAAccess{
    unsigned char SlotNumber;
    unsigned char LogicalDevNumber;
    unsigned short PCMCIAReserved;
    } PCMCIAAccess;
  struct _EISAAccess{
    unsigned char SlotNumber;
    unsigned char FunctionNumber;
    unsigned short EISAReserved;
    } EISAAccess;
  struct _PCIAccess{
    unsigned char BusNumber;
    unsigned char DevFuncNumber;
    unsigned short PCIReserved;
    } PCIAccess;
  struct _ProcBusAccess{
    unsigned char BusNumber;
    unsigned char BUID;
    unsigned short ProcBusReserved;
    } ProcBusAccess;
  } BUS_ACCESS;

/* Per logical device information */
typedef struct _PPC_DEVICE {
  DEVICE_ID DeviceId;
  BUS_ACCESS BusAccess;

  /* The following three are offsets into the DevicePnPHeap */
  /* All are in PnP compressed format                       */
  unsigned long AllocatedOffset;        /* Allocated resource description     */
  unsigned long PossibleOffset;         /* Possible resource description      */
  unsigned long CompatibleOffset;       /* Compatible device identifiers      */
  } PPC_DEVICE;

typedef enum _CPU_STATE {
  CPU_GOOD = 0,                         /* CPU is present, and active         */
  CPU_GOOD_FW = 1,                      /* CPU is present, and in firmware    */
  CPU_OFF = 2,                          /* CPU is present, but inactive       */
  CPU_FAILED = 3,                       /* CPU is present, but failed POST    */
  CPU_NOT_PRESENT = 255                 /* CPU not present                    */
  } CPU_STATE;

typedef struct _PPC_CPU {
  unsigned long CpuType;                /* Result of mfspr from Processor
                                           Version Register (PVR).
                                           PVR(0-15) = Version (e.g. 601)
                                           PVR(16-31 = EC Level               */
  unsigned char CpuNumber;              /* CPU Number for this processor      */
  unsigned char CpuState;               /* CPU State, see CPU_STATE enum      */
  unsigned short Reserved;
  } PPC_CPU;

typedef struct _PPC_MEM {
  unsigned long SIMMSize;               /* 0 - absent or bad
                                           8M, 32M (in MB)                    */
  } PPC_MEM;

typedef enum _MEM_USAGE {
  Other = 0x8000,
  ResumeBlock = 0x4000,                 /* for use by power management        */
  SystemROM = 0x2000,                   /* Flash memory (populated)           */
  UnPopSystemROM = 0x1000,              /* Unpopulated part of SystemROM area */
  IOMemory = 0x0800,
  SystemIO = 0x0400,
  SystemRegs = 0x0200,
  PCIAddr = 0x0100,
  PCIConfig = 0x80,
  ISAAddr = 0x40,
  Unpopulated = 0x20,                   /* Unpopulated part of System Memory  */
  Free = 0x10,                          /* Free part of System Memory         */
  BootImage = 0x08,                     /* BootImage part of System Memory    */
  FirmwareCode = 0x04,                  /* FirmwareCode part of System Memory */
  FirmwareHeap = 0x02,                  /* FirmwareHeap part of System Memory */
  FirmwareStack = 0x01                  /* FirmwareStack part of System Memory*/
  } MEM_USAGE;

typedef struct _MEM_MAP {
  unsigned long Usage;                  /* See MEM_USAGE above                */
  unsigned long BasePage;               /* Page number measured in 4KB pages  */
  unsigned long PageCount;              /* Page count measured in 4KB pages   */
  } MEM_MAP;

typedef struct _RESIDUAL {
  unsigned long ResidualLength;         /* Length of Residual                 */
  unsigned char Version;                /* of this data structure             */
  unsigned char Revision;               /* of this data structure             */
  unsigned short EC;                    /* of this data structure             */
  /* VPD */
  VPD VitalProductData;
  /* CPU */
  unsigned short MaxNumCpus;            /* Max CPUs in this system            */
  unsigned short ActualNumCpus;         /* ActualNumCpus < MaxNumCpus means   */
                                        /* that there are unpopulated or      */
                                        /* otherwise unusable cpu locations   */
  PPC_CPU Cpus[MAX_CPUS];
  /* Memory */
  unsigned long TotalMemory;            /* Total amount of memory installed   */
  unsigned long GoodMemory;             /* Total amount of good memory        */
  unsigned long ActualNumMemSegs;
  MEM_MAP Segs[MAX_MEM_SEGS];
  unsigned long ActualNumMemories;
  PPC_MEM Memories[MAX_MEMS];
  /* Devices */
  unsigned long ActualNumDevices;
  PPC_DEVICE Devices[MAX_DEVICES];
  unsigned char DevicePnPHeap[2*MAX_DEVICES*AVE_PNP_SIZE];
  } RESIDUAL;


/*
 * Forward declaration - we can't include <linux/pci.h> because it
 * breaks the boot loader
 */
struct pci_dev;

extern RESIDUAL *res;
extern void print_residual_device_info(void);
extern PPC_DEVICE *residual_find_device(unsigned long BusMask,
					unsigned char * DevID, int BaseType,
					int SubType, int Interface, int n);
extern int residual_pcidev_irq(struct pci_dev *dev);
extern void residual_irq_mask(char *irq_edge_mask_lo, char *irq_edge_mask_hi);
extern unsigned int residual_isapic_addr(void);
extern PnP_TAG_PACKET *PnP_find_packet(unsigned char *p, unsigned packet_tag,
				       int n);
extern PnP_TAG_PACKET *PnP_find_small_vendor_packet(unsigned char *p,
						    unsigned packet_type,
						    int n);
extern PnP_TAG_PACKET *PnP_find_large_vendor_packet(unsigned char *p,
						    unsigned packet_type,
						    int n);

#ifdef CONFIG_PREP_RESIDUAL
#define have_residual_data	(res && res->ResidualLength)
#else
#define have_residual_data	0
#endif

#endif /* __ASSEMBLY__ */
#endif  /* ndef _RESIDUAL_ */

#endif /* __KERNEL__ */
