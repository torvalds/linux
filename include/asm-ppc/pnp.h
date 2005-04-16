#ifdef __KERNEL__
/* 11/02/95                                                                   */
/*----------------------------------------------------------------------------*/
/*      Plug and Play header definitions                                      */
/*----------------------------------------------------------------------------*/

/* Structure map for PnP on PowerPC Reference Platform                        */
/* See Plug and Play ISA Specification, Version 1.0, May 28, 1993.  It        */
/* (or later versions) is available on Compuserve in the PLUGPLAY area.       */
/* This code has extensions to that specification, namely new short and       */
/* long tag types for platform dependent information                          */

/* Warning: LE notation used throughout this file                             */

/* For enum's: if given in hex then they are bit significant, i.e.            */
/* only one bit is on for each enum                                           */

#ifndef _PNP_
#define _PNP_

#ifndef __ASSEMBLY__
#define MAX_MEM_REGISTERS 9
#define MAX_IO_PORTS 20
#define MAX_IRQS 7
/*#define MAX_DMA_CHANNELS 7*/

/* Interrupt controllers */

#define PNPinterrupt0 "PNP0000"      /* AT Interrupt Controller               */
#define PNPinterrupt1 "PNP0001"      /* EISA Interrupt Controller             */
#define PNPinterrupt2 "PNP0002"      /* MCA Interrupt Controller              */
#define PNPinterrupt3 "PNP0003"      /* APIC                                  */
#define PNPExtInt     "IBM000D"      /* PowerPC Extended Interrupt Controller */

/* Timers */

#define PNPtimer0     "PNP0100"      /* AT Timer                              */
#define PNPtimer1     "PNP0101"      /* EISA Timer                            */
#define PNPtimer2     "PNP0102"      /* MCA Timer                             */

/* DMA controllers */

#define PNPdma0       "PNP0200"      /* AT DMA Controller                     */
#define PNPdma1       "PNP0201"      /* EISA DMA Controller                   */
#define PNPdma2       "PNP0202"      /* MCA DMA Controller                    */

/* start of August 15, 1994 additions */
/* CMOS */
#define PNPCMOS       "IBM0009"      /* CMOS                                  */

/* L2 Cache */
#define PNPL2         "IBM0007"      /* L2 Cache                              */

/* NVRAM */
#define PNPNVRAM      "IBM0008"      /* NVRAM                                 */

/* Power Management */
#define PNPPM         "IBM0005"      /* Power Management                      */
/* end of August 15, 1994 additions */

/* Keyboards */

#define PNPkeyboard0  "PNP0300"      /* IBM PC/XT KB Cntlr (83 key, no mouse) */
#define PNPkeyboard1  "PNP0301"      /* Olivetti ICO (102 key)                */
#define PNPkeyboard2  "PNP0302"      /* IBM PC/AT KB Cntlr (84 key)           */
#define PNPkeyboard3  "PNP0303"      /* IBM Enhanced (101/2 key, PS/2 mouse)  */
#define PNPkeyboard4  "PNP0304"      /* Nokia 1050 KB Cntlr                   */
#define PNPkeyboard5  "PNP0305"      /* Nokia 9140 KB Cntlr                   */
#define PNPkeyboard6  "PNP0306"      /* Standard Japanese KB Cntlr            */
#define PNPkeyboard7  "PNP0307"      /* Microsoft Windows (R) KB Cntlr        */

/* Parallel port controllers */

#define PNPparallel0 "PNP0400"       /* Standard LPT Parallel Port            */
#define PNPparallel1 "PNP0401"       /* ECP Parallel Port                     */
#define PNPepp       "IBM001C"       /* EPP Parallel Port                     */

/* Serial port controllers */

#define PNPserial0   "PNP0500"       /* Standard PC Serial port               */
#define PNPSerial1   "PNP0501"       /* 16550A Compatible Serial port         */

/* Disk controllers */

#define PNPdisk0     "PNP0600"       /* Generic ESDI/IDE/ATA Compat HD Cntlr  */
#define PNPdisk1     "PNP0601"       /* Plus Hardcard II                      */
#define PNPdisk2     "PNP0602"       /* Plus Hardcard IIXL/EZ                 */

/* Diskette controllers */

#define PNPdiskette0 "PNP0700"       /* PC Standard Floppy Disk Controller    */

/* Display controllers */

#define PNPdisplay0  "PNP0900"       /* VGA Compatible                        */
#define PNPdisplay1  "PNP0901"       /* Video Seven VGA                       */
#define PNPdisplay2  "PNP0902"       /* 8514/A Compatible                     */
#define PNPdisplay3  "PNP0903"       /* Trident VGA                           */
#define PNPdisplay4  "PNP0904"       /* Cirrus Logic Laptop VGA               */
#define PNPdisplay5  "PNP0905"       /* Cirrus Logic VGA                      */
#define PNPdisplay6  "PNP0906"       /* Tseng ET4000 or ET4000/W32            */
#define PNPdisplay7  "PNP0907"       /* Western Digital VGA                   */
#define PNPdisplay8  "PNP0908"       /* Western Digital Laptop VGA            */
#define PNPdisplay9  "PNP0909"       /* S3                                    */
#define PNPdisplayA  "PNP090A"       /* ATI Ultra Pro/Plus (Mach 32)          */
#define PNPdisplayB  "PNP090B"       /* ATI Ultra (Mach 8)                    */
#define PNPdisplayC  "PNP090C"       /* XGA Compatible                        */
#define PNPdisplayD  "PNP090D"       /* ATI VGA Wonder                        */
#define PNPdisplayE  "PNP090E"       /* Weitek P9000 Graphics Adapter         */
#define PNPdisplayF  "PNP090F"       /* Oak Technology VGA                    */

/* Peripheral busses */

#define PNPbuses0    "PNP0A00"       /* ISA Bus                               */
#define PNPbuses1    "PNP0A01"       /* EISA Bus                              */
#define PNPbuses2    "PNP0A02"       /* MCA Bus                               */
#define PNPbuses3    "PNP0A03"       /* PCI Bus                               */
#define PNPbuses4    "PNP0A04"       /* VESA/VL Bus                           */

/* RTC, BIOS, planar devices */

#define PNPspeaker0  "PNP0800"       /* AT Style Speaker Sound                */
#define PNPrtc0      "PNP0B00"       /* AT RTC                                */
#define PNPpnpbios0  "PNP0C00"       /* PNP BIOS (only created by root enum)  */
#define PNPpnpbios1  "PNP0C01"       /* System Board Memory Device            */
#define PNPpnpbios2  "PNP0C02"       /* Math Coprocessor                      */
#define PNPpnpbios3  "PNP0C03"       /* PNP BIOS Event Notification Interrupt */

/* PCMCIA controller */

#define PNPpcmcia0   "PNP0E00"       /* Intel 82365 Compatible PCMCIA Cntlr   */

/* Mice */

#define PNPmouse0    "PNP0F00"       /* Microsoft Bus Mouse                   */
#define PNPmouse1    "PNP0F01"       /* Microsoft Serial Mouse                */
#define PNPmouse2    "PNP0F02"       /* Microsoft Inport Mouse                */
#define PNPmouse3    "PNP0F03"       /* Microsoft PS/2 Mouse                  */
#define PNPmouse4    "PNP0F04"       /* Mousesystems Mouse                    */
#define PNPmouse5    "PNP0F05"       /* Mousesystems 3 Button Mouse - COM2    */
#define PNPmouse6    "PNP0F06"       /* Genius Mouse - COM1                   */
#define PNPmouse7    "PNP0F07"       /* Genius Mouse - COM2                   */
#define PNPmouse8    "PNP0F08"       /* Logitech Serial Mouse                 */
#define PNPmouse9    "PNP0F09"       /* Microsoft Ballpoint Serial Mouse      */
#define PNPmouseA    "PNP0F0A"       /* Microsoft PNP Mouse                   */
#define PNPmouseB    "PNP0F0B"       /* Microsoft PNP Ballpoint Mouse         */

/* Modems */

#define PNPmodem0    "PNP9000"       /* Specific IDs TBD                      */

/* Network controllers */

#define PNPnetworkC9 "PNP80C9"       /* IBM Token Ring                        */
#define PNPnetworkCA "PNP80CA"       /* IBM Token Ring II                     */
#define PNPnetworkCB "PNP80CB"       /* IBM Token Ring II/Short               */
#define PNPnetworkCC "PNP80CC"       /* IBM Token Ring 4/16Mbs                */
#define PNPnetwork27 "PNP8327"       /* IBM Token Ring (All types)            */
#define PNPnetworket "IBM0010"       /* IBM Ethernet used by Power PC         */
#define PNPneteisaet "IBM2001"       /* IBM Ethernet EISA adapter             */
#define PNPAMD79C970 "IBM0016"       /* AMD 79C970 (PCI Ethernet)             */

/* SCSI controllers */

#define PNPscsi0     "PNPA000"       /* Adaptec 154x Compatible SCSI Cntlr    */
#define PNPscsi1     "PNPA001"       /* Adaptec 174x Compatible SCSI Cntlr    */
#define PNPscsi2     "PNPA002"       /* Future Domain 16-700 Compat SCSI Cntlr*/
#define PNPscsi3     "PNPA003"       /* Panasonic CDROM Adapter (SBPro/SB16)  */
#define PNPscsiF     "IBM000F"       /* NCR 810 SCSI Controller               */
#define PNPscsi825   "IBM001B"       /* NCR 825 SCSI Controller               */
#define PNPscsi875   "IBM0018"       /* NCR 875 SCSI Controller               */

/* Sound/Video, Multimedia */

#define PNPmm0       "PNPB000"       /* Sound Blaster Compatible Sound Device */
#define PNPmm1       "PNPB001"       /* MS Windows Sound System Compat Device */
#define PNPmmF       "IBM000E"       /* Crystal CS4231 Audio Device           */
#define PNPv7310     "IBM0015"       /* ASCII V7310 Video Capture Device      */
#define PNPmm4232    "IBM0017"       /* Crystal CS4232 Audio Device           */
#define PNPpmsyn     "IBM001D"       /* YMF 289B chip (Yamaha)                */
#define PNPgp4232    "IBM0012"       /* Crystal CS4232 Game Port              */
#define PNPmidi4232  "IBM0013"       /* Crystal CS4232 MIDI                   */

/* Operator Panel */
#define PNPopctl     "IBM000B"       /* Operator's panel                      */

/* Service Processor */
#define PNPsp        "IBM0011"       /* IBM Service Processor                 */
#define PNPLTsp      "IBM001E"       /* Lightning/Terlingua Support Processor */
#define PNPLTmsp     "IBM001F"       /* Lightning/Terlingua Mini-SP           */

/* Memory Controller */
#define PNPmemctl    "IBM000A"       /* Memory controller                     */

/* Graphics Assist */
#define PNPg_assist  "IBM0014"       /* Graphics Assist                       */

/* Miscellaneous Device Controllers */
#define PNPtablet    "IBM0019"       /* IBM Tablet Controller                 */

/* PNP Packet Handles */

#define S1_Packet                0x0A   /* Version resource                   */
#define S2_Packet                0x15   /* Logical DEVID (without flags)      */
#define S2_Packet_flags          0x16   /* Logical DEVID (with flags)         */
#define S3_Packet                0x1C   /* Compatible device ID               */
#define S4_Packet                0x22   /* IRQ resource (without flags)       */
#define S4_Packet_flags          0x23   /* IRQ resource (with flags)          */
#define S5_Packet                0x2A   /* DMA resource                       */
#define S6_Packet                0x30   /* Depend funct start (w/o priority)  */
#define S6_Packet_priority       0x31   /* Depend funct start (w/ priority)   */
#define S7_Packet                0x38   /* Depend funct end                   */
#define S8_Packet                0x47   /* I/O port resource (w/o fixed loc)  */
#define S9_Packet_fixed          0x4B   /* I/O port resource (w/ fixed loc)   */
#define S14_Packet               0x71   /* Vendor defined                     */
#define S15_Packet               0x78   /* End of resource (w/o checksum)     */
#define S15_Packet_checksum      0x79   /* End of resource (w/ checksum)      */
#define L1_Packet                0x81   /* Memory range                       */
#define L1_Shadow                0x20   /* Memory is shadowable               */
#define L1_32bit_mem             0x18   /* 32-bit memory only                 */
#define L1_8_16bit_mem           0x10   /* 8- and 16-bit supported            */
#define L1_Decode_Hi             0x04   /* decode supports high address       */
#define L1_Cache                 0x02   /* read cacheable, write-through      */
#define L1_Writeable             0x01   /* Memory is writeable                */
#define L2_Packet                0x82   /* ANSI ID string                     */
#define L3_Packet                0x83   /* Unicode ID string                  */
#define L4_Packet                0x84   /* Vendor defined                     */
#define L5_Packet                0x85   /* Large I/O                          */
#define L6_Packet                0x86   /* 32-bit Fixed Loc Mem Range Desc    */
#define END_TAG                  0x78   /* End of resource                    */
#define DF_START_TAG             0x30   /* Dependent function start           */
#define DF_START_TAG_priority    0x31   /* Dependent function start           */
#define DF_END_TAG               0x38   /* Dependent function end             */
#define SUBOPTIMAL_CONFIGURATION 0x2    /* Priority byte sub optimal config   */

/* Device Base Type Codes */

typedef enum _PnP_BASE_TYPE {
  Reserved = 0,
  MassStorageDevice = 1,
  NetworkInterfaceController = 2,
  DisplayController = 3,
  MultimediaController = 4,
  MemoryController = 5,
  BridgeController = 6,
  CommunicationsDevice = 7,
  SystemPeripheral = 8,
  InputDevice = 9,
  ServiceProcessor = 0x0A,              /* 11/2/95                            */
  } PnP_BASE_TYPE;

/* Device Sub Type Codes */

typedef enum _PnP_SUB_TYPE {
  SCSIController = 0,
  IDEController = 1,
  FloppyController = 2,
  IPIController = 3,
  OtherMassStorageController = 0x80,

  EthernetController = 0,
  TokenRingController = 1,
  FDDIController = 2,
  OtherNetworkController = 0x80,

  VGAController= 0,
  SVGAController= 1,
  XGAController= 2,
  OtherDisplayController = 0x80,

  VideoController = 0,
  AudioController = 1,
  OtherMultimediaController = 0x80,

  RAM = 0,
  FLASH = 1,
  OtherMemoryDevice = 0x80,

  HostProcessorBridge = 0,
  ISABridge = 1,
  EISABridge = 2,
  MicroChannelBridge = 3,
  PCIBridge = 4,
  PCMCIABridge = 5,
  VMEBridge = 6,
  OtherBridgeDevice = 0x80,

  RS232Device = 0,
  ATCompatibleParallelPort = 1,
  OtherCommunicationsDevice = 0x80,

  ProgrammableInterruptController = 0,
  DMAController = 1,
  SystemTimer = 2,
  RealTimeClock = 3,
  L2Cache = 4,
  NVRAM = 5,
  PowerManagement = 6,
  CMOS = 7,
  OperatorPanel = 8,
  ServiceProcessorClass1 = 9,
  ServiceProcessorClass2 = 0xA,
  ServiceProcessorClass3 = 0xB,
  GraphicAssist = 0xC,
  SystemPlanar = 0xF,                   /* 10/5/95                            */
  OtherSystemPeripheral = 0x80,

  KeyboardController = 0,
  Digitizer = 1,
  MouseController = 2,
  TabletController = 3,                 /* 10/27/95                           */
  OtherInputController = 0x80,

  GeneralMemoryController = 0,
  } PnP_SUB_TYPE;

/* Device Interface Type Codes */

typedef enum _PnP_INTERFACE {
  General = 0,
  GeneralSCSI = 0,
  GeneralIDE = 0,
  ATACompatible = 1,

  GeneralFloppy = 0,
  Compatible765 = 1,
  NS398_Floppy = 2,                     /* NS Super I/O wired to use index
                                           register at port 398 and data
                                           register at port 399               */
  NS26E_Floppy = 3,                     /* Ports 26E and 26F                  */
  NS15C_Floppy = 4,                     /* Ports 15C and 15D                  */
  NS2E_Floppy = 5,                      /* Ports 2E and 2F                    */
  CHRP_Floppy = 6,                      /* CHRP Floppy in PR*P system         */

  GeneralIPI = 0,

  GeneralEther = 0,
  GeneralToken = 0,
  GeneralFDDI = 0,

  GeneralVGA = 0,
  GeneralSVGA = 0,
  GeneralXGA = 0,

  GeneralVideo = 0,
  GeneralAudio = 0,
  CS4232Audio = 1,                      /* CS 4232 Plug 'n Play Configured    */

  GeneralRAM = 0,
  GeneralFLASH = 0,
  PCIMemoryController = 0,              /* PCI Config Method                  */
  RS6KMemoryController = 1,             /* RS6K Config Method                 */

  GeneralHostBridge = 0,
  GeneralISABridge = 0,
  GeneralEISABridge = 0,
  GeneralMCABridge = 0,
  GeneralPCIBridge = 0,
  PCIBridgeDirect = 0,
  PCIBridgeIndirect = 1,
  PCIBridgeRS6K = 2,
  GeneralPCMCIABridge = 0,
  GeneralVMEBridge = 0,

  GeneralRS232 = 0,
  COMx = 1,
  Compatible16450 = 2,
  Compatible16550 = 3,
  NS398SerPort = 4,                     /* NS Super I/O wired to use index
                                           register at port 398 and data
                                           register at port 399               */
  NS26ESerPort = 5,                     /* Ports 26E and 26F                  */
  NS15CSerPort = 6,                     /* Ports 15C and 15D                  */
  NS2ESerPort = 7,                      /* Ports 2E and 2F                    */

  GeneralParPort = 0,
  LPTx = 1,
  NS398ParPort = 2,                     /* NS Super I/O wired to use index
                                           register at port 398 and data
                                           register at port 399               */
  NS26EParPort = 3,                     /* Ports 26E and 26F                  */
  NS15CParPort = 4,                     /* Ports 15C and 15D                  */
  NS2EParPort = 5,                      /* Ports 2E and 2F                    */

  GeneralPIC = 0,
  ISA_PIC = 1,
  EISA_PIC = 2,
  MPIC = 3,
  RS6K_PIC = 4,

  GeneralDMA = 0,
  ISA_DMA = 1,
  EISA_DMA = 2,

  GeneralTimer = 0,
  ISA_Timer = 1,
  EISA_Timer = 2,
  GeneralRTC = 0,
  ISA_RTC = 1,

  StoreThruOnly = 1,
  StoreInEnabled = 2,
  RS6KL2Cache = 3,

  IndirectNVRAM = 0,                    /* Indirectly addressed               */
  DirectNVRAM = 1,                      /* Memory Mapped                      */
  IndirectNVRAM24 = 2,                  /* Indirectly addressed - 24 bit      */

  GeneralPowerManagement = 0,
  EPOWPowerManagement = 1,
  PowerControl = 2,                    // d1378

  GeneralCMOS = 0,

  GeneralOPPanel = 0,
  HarddiskLight = 1,
  CDROMLight = 2,
  PowerLight = 3,
  KeyLock = 4,
  ANDisplay = 5,                        /* AlphaNumeric Display               */
  SystemStatusLED = 6,                  /* 3 digit 7 segment LED              */
  CHRP_SystemStatusLED = 7,             /* CHRP LEDs in PR*P system           */

  GeneralServiceProcessor = 0,

  TransferData = 1,
  IGMC32 = 2,
  IGMC64 = 3,

  GeneralSystemPlanar = 0,              /* 10/5/95                            */

  } PnP_INTERFACE;

/* PnP resources */

/* Compressed ASCII is 5 bits per char; 00001=A ... 11010=Z */

typedef struct _SERIAL_ID {
  unsigned char VendorID0;              /*    Bit(7)=0                        */
                                        /*    Bits(6:2)=1st character in      */
                                        /*       compressed ASCII             */
                                        /*    Bits(1:0)=2nd character in      */
                                        /*       compressed ASCII bits(4:3)   */
  unsigned char VendorID1;              /*    Bits(7:5)=2nd character in      */
                                        /*       compressed ASCII bits(2:0)   */
                                        /*    Bits(4:0)=3rd character in      */
                                        /*       compressed ASCII             */
  unsigned char VendorID2;              /* Product number - vendor assigned   */
  unsigned char VendorID3;              /* Product number - vendor assigned   */

/* Serial number is to provide uniqueness if more than one board of same      */
/* type is in system.  Must be "FFFFFFFF" if feature not supported.           */

  unsigned char Serial0;                /* Unique serial number bits (7:0)    */
  unsigned char Serial1;                /* Unique serial number bits (15:8)   */
  unsigned char Serial2;                /* Unique serial number bits (23:16)  */
  unsigned char Serial3;                /* Unique serial number bits (31:24)  */
  unsigned char Checksum;
  } SERIAL_ID;

typedef enum _PnPItemName {
  Unused = 0,
  PnPVersion = 1,
  LogicalDevice = 2,
  CompatibleDevice = 3,
  IRQFormat = 4,
  DMAFormat = 5,
  StartDepFunc = 6,
  EndDepFunc = 7,
  IOPort = 8,
  FixedIOPort = 9,
  Res1 = 10,
  Res2 = 11,
  Res3 = 12,
  SmallVendorItem = 14,
  EndTag = 15,
  MemoryRange = 1,
  ANSIIdentifier = 2,
  UnicodeIdentifier = 3,
  LargeVendorItem = 4,
  MemoryRange32 = 5,
  MemoryRangeFixed32 = 6,
  } PnPItemName;

/* Define a bunch of access functions for the bits in the tag field */

/* Tag type - 0 = small; 1 = large */
#define tag_type(t) (((t) & 0x80)>>7)
#define set_tag_type(t,v) (t = (t & 0x7f) | ((v)<<7))

/* Small item name is 4 bits - one of PnPItemName enum above */
#define tag_small_item_name(t) (((t) & 0x78)>>3)
#define set_tag_small_item_name(t,v) (t = (t & 0x07) | ((v)<<3))

/* Small item count is 3 bits - count of further bytes in packet */
#define tag_small_count(t) ((t) & 0x07)
#define set_tag_count(t,v) (t = (t & 0x78) | (v))

/* Large item name is 7 bits - one of PnPItemName enum above */
#define tag_large_item_name(t) ((t) & 0x7f)
#define set_tag_large_item_name(t,v) (t = (t | 0x80) | (v))

/* a PnP resource is a bunch of contiguous TAG packets ending with an end tag */

typedef union _PnP_TAG_PACKET {
  struct _S1_Pack{                      /* VERSION PACKET                     */
    unsigned char Tag;                  /* small tag = 0x0a                   */
    unsigned char Version[2];           /* PnP version, Vendor version        */
    } S1_Pack;

  struct _S2_Pack{                      /* LOGICAL DEVICE ID PACKET           */
    unsigned char Tag;                  /* small tag = 0x15 or 0x16           */
    unsigned char DevId[4];             /* Logical device id                  */
    unsigned char Flags[2];             /* bit(0) boot device;                */
                                        /* bit(7:1) cmd in range x31-x37      */
                                        /* bit(7:0) cmd in range x28-x3f (opt)*/
    } S2_Pack;

  struct _S3_Pack{                      /* COMPATIBLE DEVICE ID PACKET        */
    unsigned char Tag;                  /* small tag = 0x1c                   */
    unsigned char CompatId[4];          /* Compatible device id               */
    } S3_Pack;

  struct _S4_Pack{                      /* IRQ PACKET                         */
    unsigned char Tag;                  /* small tag = 0x22 or 0x23           */
    unsigned char IRQMask[2];           /* bit(0) is IRQ0, ...;               */
                                        /* bit(0) is IRQ8 ...                 */
    unsigned char IRQInfo;              /* optional; assume bit(0)=1; else    */
                                        /*  bit(0) - high true edge sensitive */
                                        /*  bit(1) - low true edge sensitive  */
                                        /*  bit(2) - high true level sensitive*/
                                        /*  bit(3) - low true level sensitive */
                                        /*  bit(7:4) - must be 0              */
    } S4_Pack;

  struct _S5_Pack{                      /* DMA PACKET                         */
    unsigned char Tag;                  /* small tag = 0x2a                   */
    unsigned char DMAMask;              /* bit(0) is channel 0 ...            */
    unsigned char DMAInfo;
    } S5_Pack;

  struct _S6_Pack{                      /* START DEPENDENT FUNCTION PACKET    */
    unsigned char Tag;                  /* small tag = 0x30 or 0x31           */
    unsigned char Priority;             /* Optional; if missing then x01; else*/
                                        /*  x00 = best possible               */
                                        /*  x01 = acceptible                  */
                                        /*  x02 = sub-optimal but functional  */
    } S6_Pack;

  struct _S7_Pack{                      /* END DEPENDENT FUNCTION PACKET      */
    unsigned char Tag;                  /* small tag = 0x38                   */
    } S7_Pack;

  struct _S8_Pack{                      /* VARIABLE I/O PORT PACKET           */
    unsigned char Tag;                  /* small tag x47                      */
    unsigned char IOInfo;               /* x0  = decode only bits(9:0);       */
#define  ISAAddr16bit         0x01      /* x01 = decode bits(15:0)            */
    unsigned char RangeMin[2];          /* Min base address                   */
    unsigned char RangeMax[2];          /* Max base address                   */
    unsigned char IOAlign;              /* base alignmt, incr in 1B blocks    */
    unsigned char IONum;                /* number of contiguous I/O ports     */
    } S8_Pack;

  struct _S9_Pack{                      /* FIXED I/O PORT PACKET              */
    unsigned char Tag;                  /* small tag = 0x4b                   */
    unsigned char Range[2];             /* base address 10 bits               */
    unsigned char IONum;                /* number of contiguous I/O ports     */
    } S9_Pack;

  struct _S14_Pack{                     /* VENDOR DEFINED PACKET              */
    unsigned char Tag;                  /* small tag = 0x7m m = 1-7           */
    union _S14_Data{
      unsigned char Data[7];            /* Vendor defined                     */
      struct _S14_PPCPack{              /* Pr*p s14 pack                      */
         unsigned char Type;            /* 00=non-IBM                         */
         unsigned char PPCData[6];      /* Vendor defined                     */
        } S14_PPCPack;
      } S14_Data;
    } S14_Pack;

  struct _S15_Pack{                     /* END PACKET                         */
    unsigned char Tag;                  /* small tag = 0x78 or 0x79           */
    unsigned char Check;                /* optional - checksum                */
    } S15_Pack;

  struct _L1_Pack{                      /* MEMORY RANGE PACKET                */
    unsigned char Tag;                  /* large tag = 0x81                   */
    unsigned char Count0;               /* x09                                */
    unsigned char Count1;               /* x00                                */
    unsigned char Data[9];              /* a variable array of bytes,         */
                                        /* count in tag                       */
    } L1_Pack;

  struct _L2_Pack{                      /* ANSI ID STRING PACKET              */
    unsigned char Tag;                  /* large tag = 0x82                   */
    unsigned char Count0;               /* Length of string                   */
    unsigned char Count1;
    unsigned char Identifier[1];        /* a variable array of bytes,         */
                                        /* count in tag                       */
    } L2_Pack;

  struct _L3_Pack{                      /* UNICODE ID STRING PACKET           */
    unsigned char Tag;                  /* large tag = 0x83                   */
    unsigned char Count0;               /* Length + 2 of string               */
    unsigned char Count1;
    unsigned char Country0;             /* TBD                                */
    unsigned char Country1;             /* TBD                                */
    unsigned char Identifier[1];        /* a variable array of bytes,         */
                                        /* count in tag                       */
    } L3_Pack;

  struct _L4_Pack{                      /* VENDOR DEFINED PACKET              */
    unsigned char Tag;                  /* large tag = 0x84                   */
    unsigned char Count0;
    unsigned char Count1;
    union _L4_Data{
      unsigned char Data[1];            /* a variable array of bytes,         */
                                        /* count in tag                       */
      struct _L4_PPCPack{               /* Pr*p L4 packet                     */
         unsigned char Type;            /* 00=non-IBM                         */
         unsigned char PPCData[1];      /* a variable array of bytes,         */
                                        /* count in tag                       */
        } L4_PPCPack;
      } L4_Data;
    } L4_Pack;

  struct _L5_Pack{
    unsigned char Tag;                  /* large tag = 0x85                   */
    unsigned char Count0;               /* Count = 17                         */
    unsigned char Count1;
    unsigned char Data[17];
    } L5_Pack;

  struct _L6_Pack{
    unsigned char Tag;                  /* large tag = 0x86                   */
    unsigned char Count0;               /* Count = 9                          */
    unsigned char Count1;
    unsigned char Data[9];
    } L6_Pack;

  } PnP_TAG_PACKET;

#endif /* __ASSEMBLY__ */
#endif  /* ndef _PNP_ */
#endif /* __KERNEL__ */
