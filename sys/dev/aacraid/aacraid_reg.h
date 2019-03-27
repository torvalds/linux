/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000-2001 Scott Long
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2001-2010 Adaptec, Inc.
 * Copyright (c) 2010-2012 PMC-Sierra, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

/*
 * Data structures defining the interface between the driver and the Adaptec
 * 'FSA' adapters.  Note that many field names and comments here are taken
 * verbatim from the Adaptec driver source in order to make comparing the
 * two slightly easier.
 */

/*
 * Misc. magic numbers.
 */
#define AAC_MAX_CONTAINERS	64
#define AAC_BLOCK_SIZE		512

/*
 * Communications interface.
 *
 * Where datastructure layouts are closely parallel to the Adaptec sample code,
 * retain their naming conventions (for now) to aid in cross-referencing.
 */

/* transport FIB header (PMC) */
struct aac_fib_xporthdr {
	u_int64_t	HostAddress;	/* FIB host address w/o xport header */
	u_int32_t	Size;			/* FIB size excluding xport header */
	u_int32_t	Handle;			/* driver handle to reference the FIB */
	u_int64_t	Reserved[2];
} __packed;

/*
 * List structure used to chain FIBs (used by the adapter - we hang FIBs off
 * our private command structure and don't touch these)
 */
struct aac_fib_list_entry {
	u_int32_t	Flink;
	u_int32_t	Blink;
} __packed;

/*
 * FIB (FSA Interface Block?); this is the datastructure passed between the host
 * and adapter.
 */
struct aac_fib_header {
	u_int32_t		XferState;
	u_int16_t		Command;
	u_int8_t		StructType;
	u_int8_t		Unused;
	u_int16_t		Size;
	u_int16_t		SenderSize;
	u_int32_t		SenderFibAddress;
	union {
		u_int32_t	ReceiverFibAddress;
		u_int32_t	SenderFibAddressHigh;
		u_int32_t	TimeStamp;
	} u;
	u_int32_t		Handle;
	u_int32_t		Previous;
	u_int32_t		Next;
} __packed;

#define AAC_FIB_DATASIZE	(512 - sizeof(struct aac_fib_header))

struct aac_fib {
	struct aac_fib_header	Header;
	u_int8_t	data[AAC_FIB_DATASIZE];
} __packed;

/*
 * FIB commands
 */
typedef enum {
	TestCommandResponse =		1,
	TestAdapterCommand =		2,

	/* lowlevel and comm commands */
	LastTestCommand =		100,
	ReinitHostNormCommandQueue =	101,
	ReinitHostHighCommandQueue =	102,
	ReinitHostHighRespQueue =	103,
	ReinitHostNormRespQueue =	104,
	ReinitAdapNormCommandQueue =	105,
	ReinitAdapHighCommandQueue =	107,
	ReinitAdapHighRespQueue =	108,
	ReinitAdapNormRespQueue =	109,
	InterfaceShutdown =		110,
	DmaCommandFib =			120,
	StartProfile =			121,
	TermProfile =			122,
	SpeedTest =			123,
	TakeABreakPt =			124,
	RequestPerfData =		125,
	SetInterruptDefTimer=		126,
	SetInterruptDefCount=		127,
	GetInterruptDefStatus=		128,
	LastCommCommand =		129,

	/* filesystem commands */
	NuFileSystem =			300,
	UFS =				301,
	HostFileSystem =		302,
	LastFileSystemCommand =		303,

	/* Container Commands */
	ContainerCommand =		500,
	ContainerCommand64 =		501,
	RawIo = 			502,	
	RawIo2 = 			503,	

	/* Cluster Commands */
	ClusterCommand =		550,

	/* Scsi Port commands (scsi passthrough) */
	ScsiPortCommand =		600,
	ScsiPortCommandU64 =		601,
	SataPortCommandU64 =		602,
	SasSmpPassThrough =		603,
	SasRequestPhyInfo =		612,

	/* misc house keeping and generic adapter initiated commands */
	AifRequest =			700,
	CheckRevision =			701,
	FsaHostShutdown =		702,
	RequestAdapterInfo =		703,
	IsAdapterPaused =		704,
	SendHostTime =			705,
	RequestSupplementAdapterInfo =	706,	/* Supp. Info for set in UCC
						 * use only if supported 
						 * (RequestAdapterInfo first) */
	LastMiscCommand =		707,
  
	OnLineDiagnostic =		800,      
	FduAdapterTest =		801, 
	RequestCompatibilityId =	802,
	AdapterEnvironmentInfo =	803,	/* temp. sensors */
	NvsramEventLog =		900,
	ResetNvsramEventLogPointers =	901,
	EnableEventLog =		902,
	DisableEventLog =		903,
	EncryptedKeyTransportFIB=	904,    
	KeyableFeaturesFIB=		905     
} AAC_FibCommands;

/*
 * FIB types
 */
#define AAC_FIBTYPE_TFIB		1
#define AAC_FIBTYPE_TQE			2
#define AAC_FIBTYPE_TCTPERF		3
#define AAC_FIBTYPE_TFIB2		4
#define AAC_FIBTYPE_TFIB2_64	5

/*
 * FIB transfer state
 */
#define AAC_FIBSTATE_HOSTOWNED		(1<<0)	/* owned by the host */
#define AAC_FIBSTATE_ADAPTEROWNED	(1<<1)	/* owned by the adapter */
#define AAC_FIBSTATE_INITIALISED	(1<<2)	/* initialised */
#define AAC_FIBSTATE_EMPTY		(1<<3)	/* empty */
#define AAC_FIBSTATE_FROMPOOL		(1<<4)	/* allocated from pool */
#define AAC_FIBSTATE_FROMHOST		(1<<5)	/* sent from the host */
#define AAC_FIBSTATE_FROMADAP		(1<<6)	/* sent from the adapter */
#define AAC_FIBSTATE_REXPECTED		(1<<7)	/* response is expected */
#define AAC_FIBSTATE_RNOTEXPECTED	(1<<8)	/* response is not expected */
#define AAC_FIBSTATE_DONEADAP		(1<<9)	/* processed by the adapter */
#define AAC_FIBSTATE_DONEHOST		(1<<10)	/* processed by the host */
#define AAC_FIBSTATE_HIGH		(1<<11)	/* high priority */
#define AAC_FIBSTATE_NORM		(1<<12)	/* normal priority */
#define AAC_FIBSTATE_ASYNC		(1<<13)
#define AAC_FIBSTATE_ASYNCIO		(1<<13)	/* to be removed */
#define AAC_FIBSTATE_PAGEFILEIO		(1<<14)	/* to be removed */
#define AAC_FIBSTATE_SHUTDOWN		(1<<15)
#define AAC_FIBSTATE_LAZYWRITE		(1<<16)	/* to be removed */
#define AAC_FIBSTATE_ADAPMICROFIB	(1<<17)
#define	AAC_FIBSTATE_BIOSFIB		(1<<18)
#define AAC_FIBSTATE_FAST_RESPONSE	(1<<19)	/* fast response capable */
#define AAC_FIBSTATE_APIFIB		(1<<20)
#define AAC_FIBSTATE_NOMOREAIF		(1<<21)

/*
 * FIB error values
 */
#define AAC_ERROR_NORMAL			0x00
#define AAC_ERROR_PENDING			0x01
#define AAC_ERROR_FATAL				0x02
#define AAC_ERROR_INVALID_QUEUE			0x03
#define AAC_ERROR_NOENTRIES			0x04
#define AAC_ERROR_SENDFAILED			0x05
#define AAC_ERROR_INVALID_QUEUE_PRIORITY	0x06
#define AAC_ERROR_FIB_ALLOCATION_FAILED		0x07
#define AAC_ERROR_FIB_DEALLOCATION_FAILED	0x08

/*
 * Adapter Init Structure: this is passed to the adapter with the 
 * AAC_MONKER_INITSTRUCT command to point it at our control structures.
 */
struct aac_adapter_init {
	u_int32_t	InitStructRevision;
#define AAC_INIT_STRUCT_REVISION		3
#define AAC_INIT_STRUCT_REVISION_4		4
#define AAC_INIT_STRUCT_REVISION_6		6
#define AAC_INIT_STRUCT_REVISION_7		7
	u_int32_t	NoOfMSIXVectors;
	u_int32_t	FilesystemRevision;
	u_int32_t	CommHeaderAddress;
	u_int32_t	FastIoCommAreaAddress;
	u_int32_t	AdapterFibsPhysicalAddress;
	u_int32_t 	AdapterFibsVirtualAddress;
	u_int32_t	AdapterFibsSize;
	u_int32_t	AdapterFibAlign;
	u_int32_t	PrintfBufferAddress;
	u_int32_t	PrintfBufferSize;
#define	AAC_PAGE_SIZE				4096
	u_int32_t	HostPhysMemPages;
	u_int32_t	HostElapsedSeconds;
	/* ADAPTER_INIT_STRUCT_REVISION_4 begins here */
	u_int32_t	InitFlags;			/* flags for supported features */
#define AAC_INITFLAGS_NEW_COMM_SUPPORTED	1
#define AAC_INITFLAGS_DRIVER_USES_UTC_TIME	0x10
#define AAC_INITFLAGS_DRIVER_SUPPORTS_PM	0x20
#define AAC_INITFLAGS_NEW_COMM_TYPE1_SUPPORTED	0x40
#define AAC_INITFLAGS_FAST_JBOD_SUPPORTED	0x80
#define AAC_INITFLAGS_NEW_COMM_TYPE2_SUPPORTED	0x100
	u_int32_t	MaxIoCommands;		/* max outstanding commands */
	u_int32_t	MaxIoSize;			/* largest I/O command */
	u_int32_t	MaxFibSize;			/* largest FIB to adapter */
	/* ADAPTER_INIT_STRUCT_REVISION_5 begins here */
	u_int32_t	MaxNumAif;	        /* max number of aif */ 
	/* ADAPTER_INIT_STRUCT_REVISION_6 begins here */
	u_int32_t	HostRRQ_AddrLow;
	u_int32_t	HostRRQ_AddrHigh;	/* Host RRQ (response queue) for SRC */
} __packed;

/*
 * Shared data types
 */
/*
 * Container types
 */
typedef enum {
	CT_NONE = 0,
	CT_VOLUME,
	CT_MIRROR,
	CT_STRIPE,
	CT_RAID5,
	CT_SSRW,
	CT_SSRO,
	CT_MORPH,
	CT_PASSTHRU,
	CT_RAID4,
	CT_RAID10,                  /* stripe of mirror */
	CT_RAID00,                  /* stripe of stripe */
	CT_VOLUME_OF_MIRRORS,       /* volume of mirror */
	CT_PSEUDO_RAID3,            /* really raid4 */
	CT_RAID50,		    /* stripe of raid5 */
	CT_RAID5D,		    /* raid5 distributed hot-sparing */
	CT_RAID5D0,
	CT_RAID1E,		    /* extended raid1 mirroring */
	CT_RAID6,
	CT_RAID60,
} AAC_FSAVolType;

/*
 * Host-addressable object types
 */
typedef enum {
	FT_REG = 1,     /* regular file */
	FT_DIR,         /* directory */
	FT_BLK,         /* "block" device - reserved */
	FT_CHR,         /* "character special" device - reserved */
	FT_LNK,         /* symbolic link */
	FT_SOCK,        /* socket */
	FT_FIFO,        /* fifo */
	FT_FILESYS,     /* ADAPTEC's "FSA"(tm) filesystem */
	FT_DRIVE,       /* physical disk - addressable in scsi by b/t/l */
	FT_SLICE,       /* virtual disk - raw volume - slice */
	FT_PARTITION,   /* FSA partition - carved out of a slice - building
			 * block for containers */
	FT_VOLUME,      /* Container - Volume Set */
	FT_STRIPE,      /* Container - Stripe Set */
	FT_MIRROR,      /* Container - Mirror Set */
	FT_RAID5,       /* Container - Raid 5 Set */
	FT_DATABASE     /* Storage object with "foreign" content manager */
} AAC_FType;

/*
 * Host-side scatter/gather list for 32-bit commands.
 */
struct aac_sg_entry {
	u_int32_t	SgAddress;
	u_int32_t	SgByteCount;
} __packed;

struct aac_sg_entry64 {
	u_int64_t	SgAddress;
	u_int32_t	SgByteCount;
} __packed;

struct aac_sg_entryraw {
	u_int32_t	Next;		/* reserved for FW use */
	u_int32_t	Prev;		/* reserved for FW use */
	u_int64_t	SgAddress;
	u_int32_t	SgByteCount;
	u_int32_t	Flags;		/* reserved for FW use */
} __packed;

struct aac_sg_table {
	u_int32_t		SgCount;
	struct aac_sg_entry	SgEntry[0];
} __packed;

/*
 * Host-side scatter/gather list for 64-bit commands.
 */
struct aac_sg_table64 {
	u_int32_t	SgCount;
	struct aac_sg_entry64	SgEntry64[0];
} __packed;

/*
 * s/g list for raw commands
 */
struct aac_sg_tableraw {
	u_int32_t	SgCount;
	struct aac_sg_entryraw	SgEntryRaw[0];
} __packed;

/*
 * new ieee1212 s/g element
 */
struct aac_sge_ieee1212 {
	u_int32_t	addrLow;
	u_int32_t	addrHigh;
	u_int32_t	length;
	u_int32_t	flags;	/* always 0 from host side */
} __packed;

/*
 * Container creation data
 */
struct aac_container_creation {
	u_int8_t	ViaBuildNumber;
	u_int8_t	MicroSecond;
	u_int8_t	Via;		/* 1 = FSU, 2 = API, etc. */
	u_int8_t	YearsSince1900;
	u_int32_t	Month:4;	/* 1-12 */
	u_int32_t	Day:6;		/* 1-32 */
	u_int32_t	Hour:6;		/* 0-23 */
	u_int32_t	Minute:6;	/* 0-59 */
	u_int32_t	Second:6;	/* 0-59 */
	u_int64_t	ViaAdapterSerialNumber;
} __packed;

/*
 * Revision number handling
 */

typedef enum {
	RevApplication = 1,
	RevDkiCli,
	RevNetService,
	RevApi,
	RevFileSysDriver,
	RevMiniportDriver,
	RevAdapterSW,
	RevMonitor,
	RevRemoteApi
} RevComponent;

struct FsaRevision {
	union {
		struct {
			u_int8_t	dash;
			u_int8_t	type;
			u_int8_t	minor;
			u_int8_t	major;
		} comp;
		u_int32_t	ul;
	} external;
	u_int32_t	buildNumber;
}  __packed;

/*
 * Adapter Information
 */

typedef enum {
	CPU_NTSIM = 1,
	CPU_I960,
	CPU_ARM,
	CPU_SPARC,
	CPU_POWERPC,
	CPU_ALPHA,
	CPU_P7,
	CPU_I960_RX,
	CPU_MIPS,
	CPU_XSCALE,
	CPU__last
} AAC_CpuType;  

typedef enum {
	CPUI960_JX = 1,
	CPUI960_CX,
	CPUI960_HX,
	CPUI960_RX,
	CPUARM_SA110,
	CPUARM_xxx,
	CPUPPC_603e,
	CPUPPC_xxx,
	CPUI960_80303,
	CPU_XSCALE_80321,
	CPU_MIPS_4KC,
	CPU_MIPS_5KC,
	CPUSUBTYPE__last
} AAC_CpuSubType;

typedef enum {
	PLAT_NTSIM = 1,
	PLAT_V3ADU,
	PLAT_CYCLONE,
	PLAT_CYCLONE_HD,
	PLAT_BATBOARD,
	PLAT_BATBOARD_HD,
	PLAT_YOLO,
	PLAT_COBRA,
	PLAT_ANAHEIM,
	PLAT_JALAPENO,
	PLAT_QUEENS,
	PLAT_JALAPENO_DELL,
	PLAT_POBLANO,
	PLAT_POBLANO_OPAL,
	PLAT_POBLANO_SL0,
	PLAT_POBLANO_SL1,
	PLAT_POBLANO_SL2,
	PLAT_POBLANO_XXX,
	PLAT_JALAPENO_P2,
	PLAT_HABANERO,
	PLAT_VULCAN,
	PLAT_CRUSADER,
	PLAT_LANCER,
	PLAT_HARRIER,
	PLAT_TERMINATOR,
	PLAT_SKYHAWK,
	PLAT_CORSAIR,
	PLAT_JAGUAR,
	PLAT_SATAHAWK,
	PLAT_SATANATOR,
	PLAT_PROWLER,
	PLAT_BLACKBIRD,
	PLAT_SABREEXPRESS,
	PLAT_INTRUDER,
	PLAT__last
} AAC_Platform;

typedef enum {
	OEM_FLAVOR_ADAPTEC = 1,
	OEM_FLAVOR_DELL,
	OEM_FLAVOR_HP,
	OEM_FLAVOR_IBM,
	OEM_FLAVOR_CPQ,
	OEM_FLAVOR_FSC,
	OEM_FLAVOR_DWS,
	OEM_FLAVOR_BRAND_Z,
	OEM_FLAVOR_LEGEND,
	OEM_FLAVOR_HITACHI,
	OEM_FLAVOR_ESG,
	OEM_FLAVOR_ICP,
	OEM_FLAVOR_SCM,
	OEM_FLAVOR__last
} AAC_OemFlavor;

/*
 * XXX the aac-2622 with no battery present reports PLATFORM_BAT_OPT_PRESENT
 */
typedef enum
{ 
	PLATFORM_BAT_REQ_PRESENT = 1,	/* BATTERY REQUIRED AND PRESENT */
	PLATFORM_BAT_REQ_NOTPRESENT,	/* BATTERY REQUIRED AND NOT PRESENT */
	PLATFORM_BAT_OPT_PRESENT,	/* BATTERY OPTIONAL AND PRESENT */
	PLATFORM_BAT_OPT_NOTPRESENT,	/* BATTERY OPTIONAL AND NOT PRESENT */
	PLATFORM_BAT_NOT_SUPPORTED	/* BATTERY NOT SUPPORTED */
} AAC_BatteryPlatform;

/* 
 * options supported by this board
 * there has to be a one to one mapping of these defines and the ones in 
 * fsaapi.h, search for FSA_SUPPORT_SNAPSHOT
 */
#define AAC_SUPPORTED_SNAPSHOT		0x01
#define AAC_SUPPORTED_CLUSTERS		0x02
#define AAC_SUPPORTED_WRITE_CACHE	0x04
#define AAC_SUPPORTED_64BIT_DATA	0x08
#define AAC_SUPPORTED_HOST_TIME_FIB	0x10
#define AAC_SUPPORTED_RAID50		0x20
#define AAC_SUPPORTED_4GB_WINDOW	0x40
#define AAC_SUPPORTED_SCSI_UPGRADEABLE	0x80
#define AAC_SUPPORTED_SOFT_ERR_REPORT	0x100
#define AAC_SUPPORTED_NOT_RECONDITION	0x200
#define AAC_SUPPORTED_SGMAP_HOST64	0x400
#define AAC_SUPPORTED_ALARM		0x800
#define AAC_SUPPORTED_NONDASD		0x1000
#define AAC_SUPPORTED_SCSI_MANAGED	0x2000	
#define AAC_SUPPORTED_RAID_SCSI_MODE	0x4000	
#define AAC_SUPPORTED_SUPPLEMENT_ADAPTER_INFO	0x10000
#define AAC_SUPPORTED_NEW_COMM		0x20000
#define AAC_SUPPORTED_64BIT_ARRAYSIZE	0x40000
#define AAC_SUPPORTED_HEAT_SENSOR	0x80000
#define AAC_SUPPORTED_NEW_COMM_TYPE1	0x10000000  /* Tupelo new comm */
#define AAC_SUPPORTED_NEW_COMM_TYPE2	0x20000000  /* Denali new comm */
#define AAC_SUPPORTED_NEW_COMM_TYPE3	0x40000000  /* Series 8 new comm */
#define AAC_SUPPORTED_NEW_COMM_TYPE4	0x80000000  /* Series 9 new comm */

/* 
 * Structure used to respond to a RequestAdapterInfo fib.
 */
struct aac_adapter_info {
	AAC_Platform		PlatformBase;    /* adapter type */
	AAC_CpuType		CpuArchitecture; /* adapter CPU type */
	AAC_CpuSubType		CpuVariant;      /* adapter CPU subtype */
	u_int32_t		ClockSpeed;      /* adapter CPU clockspeed */
	u_int32_t		ExecutionMem;    /* adapter Execution Memory
						  * size */
	u_int32_t		BufferMem;       /* adapter Data Memory */
	u_int32_t		TotalMem;        /* adapter Total Memory */
	struct FsaRevision	KernelRevision;  /* adapter Kernel Software
						  * Revision */
	struct FsaRevision	MonitorRevision; /* adapter Monitor/Diagnostic
						  * Software Revision */
	struct FsaRevision	HardwareRevision;/* TBD */
	struct FsaRevision	BIOSRevision;    /* adapter BIOS Revision */
	u_int32_t		ClusteringEnabled;
	u_int32_t		ClusterChannelMask;
	u_int64_t		SerialNumber;
	AAC_BatteryPlatform	batteryPlatform;
	u_int32_t		SupportedOptions; /* supported features of this
						   * controller */
	AAC_OemFlavor	OemVariant;
} __packed;

/*
 * More options from supplement info - SupportedOptions2
 */
#define AAC_SUPPORTED_MU_RESET			0x01
#define AAC_SUPPORTED_IGNORE_RESET		0x02
#define AAC_SUPPORTED_POWER_MANAGEMENT		0x04
#define AAC_SUPPORTED_ARCIO_PHYDEV		0x08
#define AAC_SUPPORTED_DOORBELL_RESET		0x4000
#define AAC_SUPPORTED_VARIABLE_BLOCK_SIZE	0x40000	/* 4KB sector size */

/*
 * FeatureBits of RequestSupplementAdapterInfo used in the driver
 */
#define AAC_SUPPL_SUPPORTED_JBOD	0x08000000

/* 
 * Structure used to respond to a RequestSupplementAdapterInfo fib.
 */
struct vpd_info {
	u_int8_t		AssemblyPn[8];
	u_int8_t		FruPn[8];
	u_int8_t		BatteryFruPn[8];
	u_int8_t		EcVersionString[8];
	u_int8_t		Tsid[12];
} __packed;

#define	MFG_PCBA_SERIAL_NUMBER_WIDTH	12
#define	MFG_WWN_WIDTH			8

struct aac_supplement_adapter_info {
	/* The assigned Adapter Type Text, extra byte for null termination */
	int8_t		AdapterTypeText[17+1];
	/* Pad for the text above */
	int8_t		Pad[2];
	/* Size in bytes of the memory that is flashed */
	u_int32_t	FlashMemoryByteSize;
	/* The assigned IMAGEID_xxx for this adapter */
	u_int32_t	FlashImageId;
	/*
	 * The maximum number of Phys available on a SATA/SAS
	 * Controller, 0 otherwise
	 */
	u_int32_t	MaxNumberPorts;
	/* Version of expansion area */
	u_int32_t	Version;
	u_int32_t	FeatureBits;
	u_int8_t		SlotNumber;
	u_int8_t		ReservedPad0[3];
	u_int8_t		BuildDate[12];
	/* The current number of Ports on a SAS controller, 0 otherwise */
	u_int32_t	CurrentNumberPorts;

	struct vpd_info VpdInfo;

	/* Firmware Revision (Vmaj.min-dash.) */
	struct FsaRevision	FlashFirmwareRevision;
	u_int32_t	RaidTypeMorphOptions;
	/* Firmware's boot code Revision (Vmaj.min-dash.) */
	struct FsaRevision	FlashFirmwareBootRevision;
	/* PCBA serial no. from th MFG sector */
	u_int8_t		MfgPcbaSerialNo[MFG_PCBA_SERIAL_NUMBER_WIDTH];
	/* WWN from the MFG sector */
	u_int8_t		MfgWWNName[MFG_WWN_WIDTH];
	u_int32_t	SupportedOptions2;		/* more supported features */
	u_int32_t	ExpansionFlag;			/* 1 - following fields are valid */
	u_int32_t	FeatureBits3;
	u_int32_t	SupportedPerformanceMode;
	/* Growth Area for future expansion */
	u_int32_t	ReservedGrowth[80];
} __packed;

/*
 * Monitor/Kernel interface.
 */

/*
 * Synchronous commands to the monitor/kernel.
 */
#define AAC_MONKER_BREAKPOINT	0x04
#define AAC_MONKER_INITSTRUCT	0x05
#define AAC_MONKER_SYNCFIB	0x0c
#define AAC_MONKER_GETKERNVER	0x11
#define AAC_MONKER_POSTRESULTS	0x14
#define AAC_MONKER_GETINFO	0x19
#define AAC_MONKER_GETDRVPROP	0x23
#define AAC_MONKER_RCVTEMP	0x25
#define AAC_MONKER_GETCOMMPREF	0x26
#define AAC_MONKER_REINIT	0xee
#define	AAC_IOP_RESET		0x1000
#define	AAC_IOP_RESET_ALWAYS	0x1001

/*
 *  Adapter Status Register
 *
 *  Phase Staus mailbox is 32bits:
 *  <31:16> = Phase Status
 *  <15:0>  = Phase
 *
 *  The adapter reports its present state through the phase.  Only
 *  a single phase should be ever be set.  Each phase can have multiple
 *  phase status bits to provide more detailed information about the
 *  state of the adapter.
 */
#define AAC_SELF_TEST_FAILED	0x00000004
#define AAC_MONITOR_PANIC	0x00000020
#define AAC_UP_AND_RUNNING	0x00000080
#define AAC_KERNEL_PANIC	0x00000100

/*
 * for dual FW image support
 */
#define AAC_FLASH_UPD_PENDING	0x00002000
#define AAC_FLASH_UPD_SUCCESS	0x00004000
#define AAC_FLASH_UPD_FAILED	0x00008000

/*
 * Data types relating to control and monitoring of the NVRAM/WriteCache 
 * subsystem.
 */

#define AAC_NFILESYS	24	/* maximum number of filesystems */

/*
 * NVRAM/Write Cache subsystem states
 */
typedef enum {
	NVSTATUS_DISABLED = 0,	/* present, clean, not being used */
	NVSTATUS_ENABLED,	/* present, possibly dirty, ready for use */
	NVSTATUS_ERROR,		/* present, dirty, contains dirty data */
	NVSTATUS_BATTERY,	/* present, bad or low battery, may contain
				 * dirty data */
	NVSTATUS_UNKNOWN	/* for bad/missing device */
} AAC_NVSTATUS;

/*
 * NVRAM/Write Cache subsystem battery component states
 *
 */
typedef enum {
	NVBATTSTATUS_NONE = 0,	/* battery has no power or is not present */
	NVBATTSTATUS_LOW,	/* battery is low on power */
	NVBATTSTATUS_OK,	/* battery is okay - normal operation possible
				 * only in this state */
	NVBATTSTATUS_RECONDITIONING	/* no battery present - reconditioning
					 * in process */
} AAC_NVBATTSTATUS;

/*
 * Battery transition type
 */
typedef enum {
	NVBATT_TRANSITION_NONE = 0,	/* battery now has no power or is not
					 * present */
	NVBATT_TRANSITION_LOW,		/* battery is now low on power */
	NVBATT_TRANSITION_OK		/* battery is now okay - normal
					 * operation possible only in this
					 * state */
} AAC_NVBATT_TRANSITION;

/*
 * NVRAM Info structure returned for NVRAM_GetInfo call
 */
struct aac_nvramdevinfo {
	u_int32_t	NV_Enabled;	/* write caching enabled */
	u_int32_t	NV_Error;	/* device in error state */
	u_int32_t	NV_NDirty;	/* count of dirty NVRAM buffers */
	u_int32_t	NV_NActive;	/* count of NVRAM buffers being
					 * written */
} __packed;

struct aac_nvraminfo {
	AAC_NVSTATUS		NV_Status;	/* nvram subsystem status */
	AAC_NVBATTSTATUS	NV_BattStatus;	/* battery status */
	u_int32_t		NV_Size;	/* size of WriteCache NVRAM in
						 * bytes */
	u_int32_t		NV_BufSize;	/* size of NVRAM buffers in
						 * bytes */
	u_int32_t		NV_NBufs;	/* number of NVRAM buffers */
	u_int32_t		NV_NDirty;	/* Num dirty NVRAM buffers */
	u_int32_t		NV_NClean;	/* Num clean NVRAM buffers */
	u_int32_t		NV_NActive;	/* Num NVRAM buffers being
						 * written */
	u_int32_t		NV_NBrokered;	/* Num brokered NVRAM buffers */
	struct aac_nvramdevinfo	NV_DevInfo[AAC_NFILESYS];	/* per device
								 * info */
	u_int32_t		NV_BattNeedsReconditioning;	/* boolean */
	u_int32_t		NV_TotalSize;	/* size of all non-volatile
						 * memories in bytes */
} __packed;

/*
 * Data types relating to adapter-initiated FIBs
 *
 * Based on types and structures in <aifstruc.h>
 */

/*
 * Progress Reports
 */
typedef enum {
	AifJobStsSuccess = 1,
	AifJobStsFinished,
	AifJobStsAborted,
	AifJobStsFailed,
	AifJobStsLastReportMarker = 100,	/* All prior mean last report */
	AifJobStsSuspended,
	AifJobStsRunning
} AAC_AifJobStatus;

typedef enum {
	AifJobScsiMin = 1,		/* Minimum value for Scsi operation */
	AifJobScsiZero,			/* SCSI device clear operation */
	AifJobScsiVerify,		/* SCSI device Verify operation NO
					 * REPAIR */
	AifJobScsiExercise,		/* SCSI device Exercise operation */
	AifJobScsiVerifyRepair,		/* SCSI device Verify operation WITH
					 * repair */
	AifJobScsiWritePattern,		/* write pattern */
	AifJobScsiMax = 99,		/* Max Scsi value */
	AifJobCtrMin,			/* Min Ctr op value */
	AifJobCtrZero,			/* Container clear operation */
	AifJobCtrCopy,			/* Container copy operation */
	AifJobCtrCreateMirror,		/* Container Create Mirror operation */
	AifJobCtrMergeMirror,		/* Container Merge Mirror operation */
	AifJobCtrScrubMirror,		/* Container Scrub Mirror operation */
	AifJobCtrRebuildRaid5,		/* Container Rebuild Raid5 operation */
	AifJobCtrScrubRaid5,		/* Container Scrub Raid5 operation */
	AifJobCtrMorph,			/* Container morph operation */
	AifJobCtrPartCopy,		/* Container Partition copy operation */
	AifJobCtrRebuildMirror,		/* Container Rebuild Mirror operation */
	AifJobCtrCrazyCache,		/* crazy cache */
	AifJobCtrCopyback,		/* Container Copyback operation */
	AifJobCtrCompactRaid5D,		/* Container Compaction operation */
	AifJobCtrExpandRaid5D,		/* Container Expansion operation */
	AifJobCtrRebuildRaid6,		/* Container Rebuild Raid6 operation */
	AifJobCtrScrubRaid6,		/* Container Scrub Raid6 operation */
	AifJobCtrSSBackup,		/* Container snapshot backup task */
	AifJobCtrMax = 199,		/* Max Ctr type operation */
	AifJobFsMin,			/* Min Fs type operation */
	AifJobFsCreate,			/* File System Create operation */
	AifJobFsVerify,			/* File System Verify operation */
	AifJobFsExtend,			/* File System Extend operation */
	AifJobFsMax = 299,		/* Max Fs type operation */
	AifJobApiFormatNTFS,		/* Format a drive to NTFS */
	AifJobApiFormatFAT,		/* Format a drive to FAT */
	AifJobApiUpdateSnapshot,	/* update the read/write half of a
					 * snapshot */
	AifJobApiFormatFAT32,		/* Format a drive to FAT32 */
	AifJobApiMax = 399,		/* Max API type operation */
	AifJobCtlContinuousCtrVerify,	/* Adapter operation */
	AifJobCtlMax = 499		/* Max Adapter type operation */
} AAC_AifJobType;

struct aac_AifContainers {
	u_int32_t	src;		/* from/master */
	u_int32_t	dst;		/* to/slave */
} __packed;

union aac_AifJobClient {
	struct aac_AifContainers	container;	/* For Container and
							 * filesystem progress
							 * ops; */
	int32_t				scsi_dh;	/* For SCSI progress
							 * ops */
};

struct aac_AifJobDesc {
	u_int32_t		jobID;		/* DO NOT FILL IN! Will be
						 * filled in by AIF */
	AAC_AifJobType		type;		/* Operation that is being
						 * performed */
	union aac_AifJobClient	client;		/* Details */
} __packed;

struct aac_AifJobProgressReport {
	struct aac_AifJobDesc	jd;
	AAC_AifJobStatus	status;
	u_int32_t		finalTick;
	u_int32_t		currentTick;
	u_int32_t		jobSpecificData1;
	u_int32_t		jobSpecificData2;
} __packed;

/*
 * Event Notification
 */
typedef enum {
	/* General application notifies start here */
	AifEnGeneric = 1,		/* Generic notification */
	AifEnTaskComplete,		/* Task has completed */
	AifEnConfigChange,		/* Adapter config change occurred */
	AifEnContainerChange,		/* Adapter specific container 
					 * configuration change */
	AifEnDeviceFailure,		/* SCSI device failed */
	AifEnMirrorFailover,		/* Mirror failover started */
	AifEnContainerEvent,		/* Significant container event */
	AifEnFileSystemChange,		/* File system changed */
	AifEnConfigPause,		/* Container pause event */
	AifEnConfigResume,		/* Container resume event */
	AifEnFailoverChange,		/* Failover space assignment changed */
	AifEnRAID5RebuildDone,		/* RAID5 rebuild finished */
	AifEnEnclosureManagement,	/* Enclosure management event */
	AifEnBatteryEvent,		/* Significant NV battery event */
	AifEnAddContainer,		/* A new container was created. */
	AifEnDeleteContainer,		/* A container was deleted. */
	AifEnSMARTEvent, 	       	/* SMART Event */
	AifEnBatteryNeedsRecond,	/* The battery needs reconditioning */
	AifEnClusterEvent,		/* Some cluster event */
	AifEnDiskSetEvent,		/* A disk set event occurred. */
	AifEnContainerScsiEvent,	/* a container event with no. and scsi id */
	AifEnPicBatteryEvent,	/* An event gen. by pic_battery.c for an ABM */
	AifEnExpEvent,		/* Exp. Event Type to replace CTPopUp messages */
	AifEnRAID6RebuildDone,	/* RAID6 rebuild finished */
	AifEnSensorOverHeat,	/* Heat Sensor indicate overheat */
	AifEnSensorCoolDown,	/* Heat Sensor ind. cooled down after overheat */
	AifFeatureKeysModified,	/* notif. of updated feature keys */
	AifApplicationExpirationEvent,	/* notif. on app. expiration status */
	AifEnBackgroundConsistencyCheck,/* BCC notif. for NEC - DDTS 94700 */
	AifEnAddJBOD,		/* A new JBOD type drive was created (30) */
	AifEnDeleteJBOD,	/* A JBOD type drive was deleted (31) */
	AifDriverNotifyStart=199,	/* Notifies for host driver go here */
	/* Host driver notifications start here */
	AifDenMorphComplete, 		/* A morph operation completed */
	AifDenVolumeExtendComplete, 	/* Volume expand operation completed */
	AifDriverNotifyDelay,
	AifRawDeviceRemove			/* Raw device Failure event */
} AAC_AifEventNotifyType;

struct aac_AifEnsGeneric {
	char	text[132];		/* Generic text */
} __packed;

struct aac_AifEnsDeviceFailure {
	u_int32_t	deviceHandle;	/* SCSI device handle */
} __packed;

struct aac_AifEnsMirrorFailover {
	u_int32_t	container;	/* Container with failed element */
	u_int32_t	failedSlice;	/* Old slice which failed */
	u_int32_t	creatingSlice;	/* New slice used for auto-create */
} __packed;

struct aac_AifEnsContainerChange {
	u_int32_t	container[2];	/* container that changed, -1 if no
					 * container */
} __packed;

struct aac_AifEnsContainerEvent {
	u_int32_t	container;	/* container number  */
	u_int32_t	eventType;	/* event type */
} __packed;

struct aac_AifEnsEnclosureEvent {
	u_int32_t	empID;		/* enclosure management proc number  */
	u_int32_t	unitID;		/* unitId, fan id, power supply id,
					 * slot id, tempsensor id.  */
	u_int32_t	eventType;	/* event type */
} __packed;

typedef enum {
	AIF_EM_DRIVE_INSERTION=31,
	AIF_EM_DRIVE_REMOVAL
} aac_AifEMEventType;

struct aac_AifEnsBatteryEvent {
	AAC_NVBATT_TRANSITION	transition_type;	/* eg from low to ok */
	AAC_NVBATTSTATUS	current_state;		/* current batt state */
	AAC_NVBATTSTATUS	prior_state;		/* prev batt state */
} __packed;

struct aac_AifEnsDiskSetEvent {
	u_int32_t	eventType;
	u_int64_t	DsNum;
	u_int64_t	CreatorId;
} __packed;

typedef enum {
	CLUSTER_NULL_EVENT = 0,
	CLUSTER_PARTNER_NAME_EVENT,	/* change in partner hostname or
					 * adaptername from NULL to non-NULL */
	/* (partner's agent may be up) */
	CLUSTER_PARTNER_NULL_NAME_EVENT	/* change in partner hostname or
					 * adaptername from non-null to NULL */
	/* (partner has rebooted) */
} AAC_ClusterAifEvent;

struct aac_AifEnsClusterEvent {
	AAC_ClusterAifEvent	eventType;
} __packed;

struct aac_AifEventNotify {
	AAC_AifEventNotifyType	type;
	union {
		struct aac_AifEnsGeneric		EG;
		struct aac_AifEnsDeviceFailure		EDF;
		struct aac_AifEnsMirrorFailover		EMF;
		struct aac_AifEnsContainerChange	ECC;
		struct aac_AifEnsContainerEvent		ECE;
		struct aac_AifEnsEnclosureEvent		EEE;
		struct aac_AifEnsBatteryEvent		EBE;
		struct aac_AifEnsDiskSetEvent		EDS;
/*		struct aac_AifEnsSMARTEvent		ES;*/
		struct aac_AifEnsClusterEvent		ECLE;
	} data;
} __packed;

/*
 * Adapter Initiated FIB command structures. Start with the adapter
 * initiated FIBs that really come from the adapter, and get responded
 * to by the host. 
 */
#define AAC_AIF_REPORT_MAX_SIZE 64

typedef enum {
	AifCmdEventNotify = 1,	/* Notify of event */
	AifCmdJobProgress,	/* Progress report */
	AifCmdAPIReport,	/* Report from other user of API */
	AifCmdDriverNotify,	/* Notify host driver of event */
	AifReqJobList = 100,	/* Gets back complete job list */
	AifReqJobsForCtr,	/* Gets back jobs for specific container */
	AifReqJobsForScsi,	/* Gets back jobs for specific SCSI device */
	AifReqJobReport,	/* Gets back a specific job report or list */
	AifReqTerminateJob,	/* Terminates job */
	AifReqSuspendJob,	/* Suspends a job */
	AifReqResumeJob,	/* Resumes a job */
	AifReqSendAPIReport,	/* API generic report requests */
	AifReqAPIJobStart,	/* Start a job from the API */
	AifReqAPIJobUpdate,	/* Update a job report from the API */
	AifReqAPIJobFinish,	/* Finish a job from the API */
	AifReqEvent = 200	/* PMC NEW COMM: Request the event data */
} AAC_AifCommand;

struct aac_aif_command {
	AAC_AifCommand	command;	/* Tell host what type of
					 * notify this is */
	u_int32_t	seqNumber;	/* To allow ordering of
					 * reports (if necessary) */
	union {
		struct aac_AifEventNotify	EN;	/* Event notify */
		struct aac_AifJobProgressReport	PR[1];	/* Progress report */
		u_int8_t			AR[AAC_AIF_REPORT_MAX_SIZE];
		u_int8_t			data[AAC_FIB_DATASIZE - 8];
	} data;
} __packed;

/*
 * Filesystem commands/data
 *
 * The adapter has a very complex filesystem interface, most of which we ignore.
 * (And which seems not to be implemented, anyway.)
 */

/*
 * FSA commands
 * (not used?)
 */
typedef enum {
	Null = 0,
	GetAttributes,
	SetAttributes,
	Lookup,
	ReadLink,
	Read,
	Write,
	Create,
	MakeDirectory,
	SymbolicLink,
	MakeNode,
	Removex,
	RemoveDirectory,
	Rename,
	Link,
	ReadDirectory,
	ReadDirectoryPlus,
	FileSystemStatus,
	FileSystemInfo,
	PathConfigure,
	Commit,
	Mount,
	UnMount,
	Newfs,
	FsCheck,
	FsSync,
	SimReadWrite,
	SetFileSystemStatus,
	BlockRead,
	BlockWrite,
	NvramIoctl,
	FsSyncWait,
	ClearArchiveBit,
	SetAcl,
	GetAcl,
	AssignAcl,
	FaultInsertion,
	CrazyCache
} AAC_FSACommand;

/*
 * Command status values
 */
typedef enum {
	ST_OK = 0,
	ST_PERM = 1,
	ST_NOENT = 2,
	ST_IO = 5,
	ST_NXIO = 6,
	ST_E2BIG = 7,
	ST_ACCES = 13,
	ST_EXIST = 17,
	ST_XDEV = 18,
	ST_NODEV = 19,
	ST_NOTDIR = 20,
	ST_ISDIR = 21,
	ST_INVAL = 22,
	ST_FBIG = 27,
	ST_NOSPC = 28,
	ST_ROFS = 30,
	ST_MLINK = 31,
	ST_WOULDBLOCK = 35,
	ST_NAMETOOLONG = 63,
	ST_NOTEMPTY = 66,
	ST_DQUOT = 69,
	ST_STALE = 70,
	ST_REMOTE = 71,
	ST_NOT_READY = 72,
	ST_BADHANDLE = 10001,
	ST_NOT_SYNC = 10002,
	ST_BAD_COOKIE = 10003,
	ST_NOTSUPP = 10004,
	ST_TOOSMALL = 10005,
	ST_SERVERFAULT = 10006,
	ST_BADTYPE = 10007,
	ST_JUKEBOX = 10008,
	ST_NOTMOUNTED = 10009,
	ST_MAINTMODE = 10010,
	ST_STALEACL = 10011,
	ST_BUS_RESET = 20001
} AAC_FSAStatus;

/*
 * Volume manager commands
 */
typedef enum _VM_COMMANDS {
	VM_Null = 0,
	VM_NameServe,        /* query for mountable objects (containers) */
	VM_ContainerConfig,
	VM_Ioctl,
	VM_FilesystemIoctl,
	VM_CloseAll,
	VM_CtBlockRead,
	VM_CtBlockWrite,
	VM_SliceBlockRead,   /* raw access to configured "storage objects" */
	VM_SliceBlockWrite,
	VM_DriveBlockRead,   /* raw access to physical devices */
	VM_DriveBlockWrite,
	VM_EnclosureMgt,     /* enclosure management */
	VM_Unused,           /* used to be diskset management */
	VM_CtBlockVerify,
	VM_CtPerf,           /* performance test */
	VM_CtBlockRead64,
	VM_CtBlockWrite64,
	VM_CtBlockVerify64,
	VM_CtHostRead64,
	VM_CtHostWrite64,
	VM_DrvErrTblLog,     /* drive error table/log type of command */
	VM_NameServe64,      /* query also for containers >2TB */
	VM_SasNvsramAccess,  /* for sas nvsram layout function */
	VM_HandleExpiration, /* handles application expiration, internal use! */
	VM_GetDynAdapProps,  /* retrieves dynamic adapter properties */
	VM_SetDynAdapProps,  /* sets a dynamic adapter property */
	VM_UpdateSSDODM,     /* updates the on-disk metadata for SSD caching */
	VM_GetSPMParameters, /* get SPM parameters for one of the perf. modes */
	VM_SetSPMParameters, /* set SPM parameters for user defined perf. mode */
	VM_NameServeAllBlk,  /* query also for containers with 4KB sector size */
	MAX_VMCOMMAND_NUM    /* used for sizing stats array - leave last */
} AAC_VMCommand;

/* Container Configuration Sub-Commands */
#define CT_GET_SCSI_METHOD	64
#define	CT_PAUSE_IO			65
#define	CT_RELEASE_IO			66
#define	CT_GET_CONFIG_STATUS		147
#define	CT_COMMIT_CONFIG		152
#define	CT_CID_TO_32BITS_UID		165
#define CT_PM_DRIVER_SUPPORT		245

/* General CT_xxx return status */
#define CT_OK		218

/* CT_PM_DRIVER_SUPPORT parameter */
typedef enum {
	AAC_PM_DRIVERSUP_GET_STATUS = 1,
	AAC_PM_DRIVERSUP_START_UNIT,
	AAC_PM_DRIVERSUP_STOP_UNIT
} AAC_CT_PM_DRIVER_SUPPORT_SUB_COM;

/*
 * CT_PAUSE_IO is immediate minimal runtime command that is used
 * to restart the applications and cache.
 */
struct aac_pause_command {
	u_int32_t	Command;
	u_int32_t	Type;
	u_int32_t	Timeout;
	u_int32_t	Min;
	u_int32_t	NoRescan;
	u_int32_t	Parm3;
	u_int32_t	Parm4;
	u_int32_t	Count;
} __packed;

/* Flag values for ContentState */
#define AAC_FSCS_NOTCLEAN	0x1	/* fscheck is necessary before mounting */
#define AAC_FSCS_READONLY	0x2	/* possible result of broken mirror */
#define AAC_FSCS_HIDDEN		0x4	/* container should be ignored by driver */
#define AAC_FSCS_NOT_READY	0x8	/* cnt is in spinn. state, not rdy for IO's */

/*
 * "mountable object"
 */
struct aac_mntobj {
	u_int32_t			ObjectId;
	char				FileSystemName[16];
	struct aac_container_creation	CreateInfo;
	u_int32_t			Capacity;
	u_int32_t			VolType;
	u_int32_t			ObjType;
	u_int32_t			ContentState;
	union {
		u_int32_t	pad[8];
		struct {
			u_int32_t	BlockSize;
			u_int32_t	bdLgclPhysMap;
		} BlockDevice;
	} ObjExtension;
	u_int32_t			AlterEgoId;
	u_int32_t			CapacityHigh;
} __packed;

struct aac_mntinfo {
	u_int32_t		Command;
	u_int32_t		MntType;
	u_int32_t		MntCount;
} __packed;

struct aac_mntinforesp {
	u_int32_t		Status;
	u_int32_t		MntType;
	u_int32_t		MntRespCount;
	struct aac_mntobj	MntTable[1];
} __packed;

/*
 * Container shutdown command.
 */
struct aac_closecommand {
	u_int32_t	Command;
	u_int32_t	ContainerId;
} __packed;

/*
 * Container Config Command
 */
struct aac_ctcfg {
	u_int32_t		Command;
	u_int32_t		cmd;
	u_int32_t		param;
} __packed;

struct aac_ctcfg_resp {
	u_int32_t		Status;
	u_int32_t		resp;
	u_int32_t		param;
} __packed;

/*
 * 'Ioctl' commads
 */
#define AAC_SCSI_MAX_PORTS	10
#define AAC_BUS_NO_EXIST	0
#define AAC_BUS_VALID		1
#define AAC_BUS_FAULTED		2
#define AAC_BUS_DISABLED	3
#define GetBusInfo		0x9

struct aac_getbusinf {
	u_int32_t		ProbeComplete;
	u_int32_t		BusCount;
	u_int32_t		TargetsPerBus;
	u_int8_t		InitiatorBusId[AAC_SCSI_MAX_PORTS];
	u_int8_t		BusValid[AAC_SCSI_MAX_PORTS];
} __packed;

struct aac_vmioctl {
	u_int32_t		Command;
	u_int32_t		ObjType;
	u_int32_t		MethId;
	u_int32_t		ObjId;
	u_int32_t		IoctlCmd;
	u_int32_t		IoctlBuf[1];	/* Placeholder? */
} __packed;

struct aac_vmi_businf_resp {
	u_int32_t		Status;
	u_int32_t		ObjType;
	u_int32_t		MethId;
	u_int32_t		ObjId;
	u_int32_t		IoctlCmd;
	struct aac_getbusinf	BusInf;
} __packed;

struct aac_vmi_devinfo_resp {
	u_int32_t		Status;
	u_int32_t		ObjType;
	u_int32_t		MethId;
	u_int32_t		ObjId;
	u_int32_t		IoctlCmd;
	u_int8_t		VendorId[8];
	u_int8_t		ProductId[16];
	u_int8_t		ProductRev[4];
	u_int32_t		Inquiry7;
	u_int32_t		align1;
	u_int32_t		Inquiry0;
	u_int32_t		align2;
	u_int32_t		Inquiry1;
	u_int32_t		align3;
	u_int32_t		reserved[2];
	u_int8_t		VendorSpecific[20];
	u_int32_t		Smart:1;
	u_int32_t		AAC_Managed:1;
	u_int32_t		align4;
	u_int32_t		reserved2:6;
	u_int32_t		Bus;
	u_int32_t		Target;
	u_int32_t		Lun;
	u_int32_t		ultraEnable:1,
				disconnectEnable:1,
				fast20EnabledW:1,
				scamDevice:1,
				scamTolerant:1,
				setForSync:1,
				setForWide:1,
				syncDevice:1,
				wideDevice:1,
				reserved1:7,
				ScsiRate:8,
				ScsiOffset:8;
}; /* Do not pack */

#define ResetBus 0x16
struct aac_resetbus {
	u_int32_t		BusNumber;
};

/*
 * Write 'stability' options.
 */
typedef enum {
	CSTABLE = 1,
	CUNSTABLE
} AAC_CacheLevel;

/*
 * Commit level response for a write request.
 */
typedef enum {
	CMFILE_SYNC_NVRAM = 1,
	CMDATA_SYNC_NVRAM,
	CMFILE_SYNC,
	CMDATA_SYNC,
	CMUNSTABLE
} AAC_CommitLevel;


#define	CT_FIB_PARAMS			6
#define	MAX_FIB_PARAMS			10
#define	CT_PACKET_SIZE \
	(AAC_FIB_DATASIZE - sizeof (u_int32_t) - \
	((sizeof (u_int32_t)) * (MAX_FIB_PARAMS + 1)))
#define CNT_SIZE			5

struct aac_fsa_ctm {
	u_int32_t	command;
	u_int32_t	param[CT_FIB_PARAMS];
	int8_t		data[CT_PACKET_SIZE];
};

struct aac_cnt_config {
	u_int32_t		Command;
	struct aac_fsa_ctm	CTCommand;
};

/* check config. */
enum {
	CFACT_CONTINUE = 0,	/* continue without pause */
	CFACT_PAUSE,		/* pause, then continue */
	CFACT_ABORT		/* abort */
};

struct aac_cf_status_hdr {
	u_int32_t	action;
	u_int32_t	flags;
	u_int32_t	recordcount;
};

/*
 * Block read/write operations.
 * These structures are packed into the 'data' area in the FIB.
 */

struct aac_blockread {
	u_int32_t		Command;	/* not FSACommand! */
	u_int32_t		ContainerId;
	u_int32_t		BlockNumber;
	u_int32_t		ByteCount;
	struct aac_sg_table	SgMap;		/* variable size */
} __packed;

struct aac_blockread64 {
	u_int32_t		Command;
	u_int16_t		ContainerId;
	u_int16_t		SectorCount;
	u_int32_t		BlockNumber;
	u_int16_t		Pad;
	u_int16_t		Flags;
	struct aac_sg_table64	SgMap64;
} __packed;

struct aac_blockread_response {
	u_int32_t		Status;
	u_int32_t		ByteCount;
} __packed;

struct aac_blockwrite {
	u_int32_t		Command;	/* not FSACommand! */
	u_int32_t		ContainerId;
	u_int32_t		BlockNumber;
	u_int32_t		ByteCount;
	u_int32_t		Stable;
	struct aac_sg_table	SgMap;		/* variable size */
} __packed;

struct aac_blockwrite64 {
	u_int32_t		Command;	/* not FSACommand! */
	u_int16_t		ContainerId;
	u_int16_t		SectorCount;
	u_int32_t		BlockNumber;
	u_int16_t		Pad;
	u_int16_t		Flags;
	struct aac_sg_table64	SgMap64;	/* variable size */
} __packed;

struct aac_blockwrite_response {
	u_int32_t		Status;
	u_int32_t		ByteCount;
	u_int32_t		Committed;
} __packed;

struct aac_raw_io {
	u_int64_t		BlockNumber;
	u_int32_t		ByteCount;
	u_int16_t		ContainerId;
	u_int16_t		Flags;				/* 0: W, 1: R */
	u_int16_t		BpTotal;			/* reserved for FW use */
	u_int16_t		BpComplete;			/* reserved for FW use */
	struct aac_sg_tableraw	SgMapRaw;	/* variable size */
} __packed;

#define RIO2_IO_TYPE		0x0003
#define RIO2_IO_TYPE_WRITE	0x0000
#define RIO2_IO_TYPE_READ	0x0001
#define RIO2_IO_TYPE_VERIFY	0x0002
#define RIO2_IO_ERROR		0x0004
#define RIO2_IO_SUREWRITE	0x0008
#define RIO2_SGL_CONFORMANT	0x0010
#define RIO2_SG_FORMAT		0xF000
#define RIO2_SG_FORMAT_ARC	0x0000
#define RIO2_SG_FORMAT_SRL	0x1000
#define RIO2_SG_FORMAT_IEEE1212	0x2000
struct aac_raw_io2 {
	u_int32_t		strtBlkLow;
	u_int32_t		strtBlkHigh;
	u_int32_t		byteCnt;
	u_int16_t		ldNum;
	u_int16_t		flags;				/* RIO2_xxx */
	u_int32_t		sgeFirstSize;		/* size of first SG element */
	u_int32_t		sgeNominalSize;		/* size of 2nd SG element */
	u_int8_t		sgeCnt;
	u_int8_t		bpTotal;			/* reserved for FW use */
	u_int8_t		bpComplete;			/* reserved for FW use */
	u_int8_t		sgeFirstIndex;		/* reserved for FW use */
	u_int8_t		unused[4];
	struct aac_sge_ieee1212	sge[0];		/* variable size */
} __packed;

/*
 * Container shutdown command.
 */
struct aac_close_command {
	u_int32_t		Command;
	u_int32_t		ContainerId;
} __packed;

/*
 * SCSI Passthrough structures
 */
struct aac_srb {
	u_int32_t		function;
	u_int32_t		bus;
	u_int32_t		target;
	u_int32_t		lun;
	u_int32_t		timeout;
	u_int32_t		flags;
	u_int32_t		data_len;
	u_int32_t		retry_limit;
	u_int32_t		cdb_len;
	u_int8_t		cdb[16];
	struct aac_sg_table	sg_map;
} __packed;

enum {
	AAC_SRB_FUNC_EXECUTE_SCSI	= 0x00,
	AAC_SRB_FUNC_CLAIM_DEVICE,
	AAC_SRB_FUNC_IO_CONTROL,
	AAC_SRB_FUNC_RECEIVE_EVENT,
	AAC_SRB_FUNC_RELEASE_QUEUE,
	AAC_SRB_FUNC_ATTACH_DEVICE,
	AAC_SRB_FUNC_RELEASE_DEVICE,
	AAC_SRB_FUNC_SHUTDOWN,
	AAC_SRB_FUNC_FLUSH,
	AAC_SRB_FUNC_ABORT_COMMAND	= 0x10,
	AAC_SRB_FUNC_RELEASE_RECOVERY,
	AAC_SRB_FUNC_RESET_BUS,
	AAC_SRB_FUNC_RESET_DEVICE,
	AAC_SRB_FUNC_TERMINATE_IO,
	AAC_SRB_FUNC_FLUSH_QUEUE,
	AAC_SRB_FUNC_REMOVE_DEVICE,
	AAC_SRB_FUNC_DOMAIN_VALIDATION
};

#define AAC_SRB_FLAGS_NO_DATA_XFER		0x0000
#define	AAC_SRB_FLAGS_DISABLE_DISCONNECT	0x0004
#define	AAC_SRB_FLAGS_DISABLE_SYNC_TRANSFER	0x0008
#define AAC_SRB_FLAGS_BYPASS_FROZEN_QUEUE	0x0010
#define	AAC_SRB_FLAGS_DISABLE_AUTOSENSE		0x0020
#define	AAC_SRB_FLAGS_DATA_IN			0x0040
#define AAC_SRB_FLAGS_DATA_OUT			0x0080
#define	AAC_SRB_FLAGS_UNSPECIFIED_DIRECTION \
			(AAC_SRB_FLAGS_DATA_IN | AAC_SRB_FLAGS_DATA_OUT)

#define AAC_HOST_SENSE_DATA_MAX			30

struct aac_srb_response {
	u_int32_t	fib_status;
	u_int32_t	srb_status;
	u_int32_t	scsi_status;
	u_int32_t	data_len;
	u_int32_t	sense_len;
	u_int8_t	sense[AAC_HOST_SENSE_DATA_MAX];
} __packed;

/*
 * Status codes for SCSI passthrough commands.  Since they are based on ASPI,
 * they also exactly match CAM status codes in both enumeration and meaning.
 * They seem to also be used as status codes for synchronous FIBs.
 */
enum {
	AAC_SRB_STS_PENDING			= 0x00,
	AAC_SRB_STS_SUCCESS,
	AAC_SRB_STS_ABORTED,
	AAC_SRB_STS_ABORT_FAILED,
	AAC_SRB_STS_ERROR,
	AAC_SRB_STS_BUSY,
	AAC_SRB_STS_INVALID_REQUEST,
	AAC_SRB_STS_INVALID_PATH_ID,
	AAC_SRB_STS_NO_DEVICE,
	AAC_SRB_STS_TIMEOUT,
	AAC_SRB_STS_SELECTION_TIMEOUT,
	AAC_SRB_STS_COMMAND_TIMEOUT,
	AAC_SRB_STS_MESSAGE_REJECTED		= 0x0D,
	AAC_SRB_STS_BUS_RESET,
	AAC_SRB_STS_PARITY_ERROR,
	AAC_SRB_STS_REQUEST_SENSE_FAILED,
	AAC_SRB_STS_NO_HBA,
	AAC_SRB_STS_DATA_OVERRUN,
	AAC_SRB_STS_UNEXPECTED_BUS_FREE,
	AAC_SRB_STS_PHASE_SEQUENCE_FAILURE,
	AAC_SRB_STS_BAD_SRB_BLOCK_LENGTH,
	AAC_SRB_STS_REQUEST_FLUSHED,
	AAC_SRB_STS_INVALID_LUN			= 0x20,
	AAC_SRB_STS_INVALID_TARGET_ID,
	AAC_SRB_STS_BAD_FUNCTION,
	AAC_SRB_STS_ERROR_RECOVERY
};

/*
 * Register definitions for the Adaptec PMC SRC/SRCv adapters.
 */
/* accessible via BAR0 */
#define AAC_SRC_OMR		0xbc	/* outbound message register */
#define AAC_SRC_IOAR		0x18	/* IOA->host interrupt register */
#define AAC_SRC_IDBR		0x20	/* inbound doorbell register */
#define AAC_SRC_IISR		0x24	/* inbound interrupt status register */
#define AAC_SRC_ODBR_R		0x9c	/* outbound doorbell register read */
#define AAC_SRC_ODBR_C		0xa0	/* outbound doorbell register clear */
#define AAC_SRC_OIMR		0x34	/* outbound interrupt mask register */
#define AAC_SRC_IQUE32		0x40	/* inbound queue address 32-bit */
#define AAC_SRC_IQUE64_L	0xc0	/* inbound queue address 64-bit (low) */
#define AAC_SRC_IQUE64_H	0xc4	/* inbound queue address 64-bit (high)*/
#define AAC_SRC_ODBR_MSI	0xc8	/* MSI register for sync./AIF */

#define AAC_SRC_MAILBOX		0x7fc60	/* mailbox (20 bytes) */
#define AAC_SRCV_MAILBOX	0x1000	/* mailbox (20 bytes) */

#define AAC_SRC_ODR_SHIFT 	12		/* outbound doorbell shift */
#define AAC_SRC_IDR_SHIFT 	9		/* inbound doorbell shift */

/* Sunrise Lake dual core reset */
#define AAC_IRCSR		0x38	/* inbound dual cores reset */
#define AAC_IRCSR_CORES_RST	3


/*
 * Common bit definitions for the doorbell registers.
 */

/*
 * Status bits in the doorbell registers.
 */
#define AAC_DB_SYNC_COMMAND	(1<<0)	/* send/completed synchronous FIB */
#define AAC_DB_AIF_PENDING	(1<<6)	/* pending AIF (new comm. type1) */
/* PMC specific outbound doorbell bits */
#define AAC_DB_RESPONSE_SENT_NS		(1<<1)	/* response sent (not shifted)*/

/*
 * The adapter can request the host print a message by setting the
 * DB_PRINTF flag in DOORBELL0.  The driver responds by collecting the
 * message from the printf buffer, clearing the DB_PRINTF flag in 
 * DOORBELL0 and setting it in DOORBELL1.
 * (ODBR and IDBR respectively for the i960Rx adapters)
 */
#define AAC_DB_PRINTF		(1<<5)	/* adapter requests host printf */
#define AAC_PRINTF_DONE		(1<<5)	/* Host completed printf processing */

/*
 * Interrupts
 */
#define AAC_MAX_MSIX		32	/* vectors */
#define AAC_PCI_MSI_ENABLE	0x8000
#define AAC_MSI_SYNC_STATUS	0x1000

enum {
	AAC_ENABLE_INTERRUPT	= 0x0,
	AAC_DISABLE_INTERRUPT,
	AAC_ENABLE_MSIX,
	AAC_DISABLE_MSIX,
	AAC_CLEAR_AIF_BIT,
	AAC_CLEAR_SYNC_BIT,
	AAC_ENABLE_INTX
};

#define AAC_INT_MODE_INTX		(1<<0)
#define AAC_INT_MODE_MSI		(1<<1)
#define AAC_INT_MODE_AIF		(1<<2)
#define AAC_INT_MODE_SYNC		(1<<3)

#define AAC_INT_ENABLE_TYPE1_INTX	0xfffffffb
#define AAC_INT_ENABLE_TYPE1_MSIX	0xfffffffa
#define AAC_INT_DISABLE_ALL		0xffffffff

/* Bit definitions in IOA->Host Interrupt Register */
#define PMC_TRANSITION_TO_OPERATIONAL	(0x80000000 >> 0)
#define PMC_IOARCB_TRANSFER_FAILED	(0x80000000 >> 3)
#define PMC_IOA_UNIT_CHECK		(0x80000000 >> 4)
#define PMC_NO_HOST_RRQ_FOR_CMD_RESPONSE (0x80000000 >> 5)
#define PMC_CRITICAL_IOA_OP_IN_PROGRESS	(0x80000000 >> 6)
#define PMC_IOARRIN_LOST		(0x80000000 >> 27)
#define PMC_SYSTEM_BUS_MMIO_ERROR	(0x80000000 >> 28)
#define PMC_IOA_PROCESSOR_IN_ERROR_STATE (0x80000000 >> 29)
#define PMC_HOST_RRQ_VALID		(0x80000000 >> 30)
#define PMC_OPERATIONAL_STATUS		(0x80000000 >> 0)
#define PMC_ALLOW_MSIX_VECTOR0		(0x80000000 >> 31)

#define PMC_IOA_ERROR_INTERRUPTS	(PMC_IOARCB_TRANSFER_FAILED | \
					 PMC_IOA_UNIT_CHECK | \
					 PMC_NO_HOST_RRQ_FOR_CMD_RESPONSE | \
					 PMC_IOARRIN_LOST | \
					 PMC_SYSTEM_BUS_MMIO_ERROR | \
					 PMC_IOA_PROCESSOR_IN_ERROR_STATE)

#define PMC_ALL_INTERRUPT_BITS		(PMC_IOA_ERROR_INTERRUPTS | \
					 PMC_HOST_RRQ_VALID | \
					 PMC_TRANSITION_TO_OPERATIONAL | \
					 PMC_ALLOW_MSIX_VECTOR0)

#define PMC_GLOBAL_INT_BIT2		0x00000004
#define PMC_GLOBAL_INT_BIT0		0x00000001
