/* Copyright (C) 1999,2001
 *
 * Author: J.E.J.Bottomley@HansenPartnership.com
 *
 * Standard include definitions for the NCR Voyager system */

#undef	VOYAGER_DEBUG
#undef	VOYAGER_CAT_DEBUG

#ifdef VOYAGER_DEBUG
#define VDEBUG(x)	printk x
#else
#define VDEBUG(x)
#endif

/* There are three levels of voyager machine: 3,4 and 5. The rule is
 * if it's less than 3435 it's a Level 3 except for a 3360 which is
 * a level 4.  A 3435 or above is a Level 5 */
#define VOYAGER_LEVEL5_AND_ABOVE	0x3435
#define VOYAGER_LEVEL4			0x3360

/* The L4 DINO ASIC */
#define VOYAGER_DINO			0x43

/* voyager ports in standard I/O space */
#define VOYAGER_MC_SETUP	0x96


#define	VOYAGER_CAT_CONFIG_PORT			0x97
#	define VOYAGER_CAT_DESELECT		0xff
#define VOYAGER_SSPB_RELOCATION_PORT		0x98

/* Valid CAT controller commands */
/* start instruction register cycle */
#define VOYAGER_CAT_IRCYC			0x01
/* start data register cycle */
#define VOYAGER_CAT_DRCYC			0x02
/* move to execute state */
#define VOYAGER_CAT_RUN				0x0F
/* end operation */
#define VOYAGER_CAT_END				0x80
/* hold in idle state */
#define VOYAGER_CAT_HOLD			0x90
/* single step an "intest" vector */
#define VOYAGER_CAT_STEP			0xE0
/* return cat controller to CLEMSON mode */
#define VOYAGER_CAT_CLEMSON			0xFF

/* the default cat command header */
#define VOYAGER_CAT_HEADER			0x7F

/* the range of possible CAT module ids in the system */
#define VOYAGER_MIN_MODULE			0x10
#define VOYAGER_MAX_MODULE			0x1f

/* The voyager registers per asic */
#define VOYAGER_ASIC_ID_REG			0x00
#define VOYAGER_ASIC_TYPE_REG			0x01
/* the sub address registers can be made auto incrementing on reads */
#define VOYAGER_AUTO_INC_REG			0x02
#	define VOYAGER_AUTO_INC			0x04
#	define VOYAGER_NO_AUTO_INC		0xfb
#define VOYAGER_SUBADDRDATA			0x03
#define VOYAGER_SCANPATH			0x05
#	define VOYAGER_CONNECT_ASIC		0x01
#	define VOYAGER_DISCONNECT_ASIC		0xfe
#define VOYAGER_SUBADDRLO			0x06
#define VOYAGER_SUBADDRHI			0x07
#define VOYAGER_SUBMODSELECT			0x08
#define VOYAGER_SUBMODPRESENT			0x09

#define VOYAGER_SUBADDR_LO			0xff
#define VOYAGER_SUBADDR_HI			0xffff

/* the maximum size of a scan path -- used to form instructions */
#define VOYAGER_MAX_SCAN_PATH			0x100
/* the biggest possible register size (in bytes) */
#define VOYAGER_MAX_REG_SIZE			4

/* Total number of possible modules (including submodules) */
#define VOYAGER_MAX_MODULES			16
/* Largest number of asics per module */
#define VOYAGER_MAX_ASICS_PER_MODULE		7

/* the CAT asic of each module is always the first one */
#define VOYAGER_CAT_ID				0
#define VOYAGER_PSI				0x1a

/* voyager instruction operations and registers */
#define VOYAGER_READ_CONFIG			0x1
#define VOYAGER_WRITE_CONFIG			0x2
#define VOYAGER_BYPASS				0xff

typedef struct voyager_asic 
{
	__u8	asic_addr;	/* ASIC address; Level 4 */
	__u8	asic_type;      /* ASIC type */
	__u8	asic_id;	/* ASIC id */
	__u8	jtag_id[4];	/* JTAG id */
	__u8	asic_location;	/* Location within scan path; start w/ 0 */
	__u8	bit_location;	/* Location within bit stream; start w/ 0 */
	__u8	ireg_length;	/* Instruction register length */
	__u16	subaddr;	/* Amount of sub address space */
	struct voyager_asic *next;	/* Next asic in linked list */
} voyager_asic_t;

typedef struct voyager_module {
	__u8	module_addr;		/* Module address */
	__u8	scan_path_connected;	/* Scan path connected */
	__u16   ee_size;		/* Size of the EEPROM */
	__u16   num_asics;		/* Number of Asics */
	__u16   inst_bits;		/* Instruction bits in the scan path */
	__u16   largest_reg;		/* Largest register in the scan path */
	__u16   smallest_reg;		/* Smallest register in the scan path */
	voyager_asic_t   *asic;		/* First ASIC in scan path (CAT_I) */
	struct   voyager_module *submodule;	/* Submodule pointer */ 
	struct   voyager_module *next;		/* Next module in linked list */
} voyager_module_t;

typedef struct voyager_eeprom_hdr {
	 __u8  module_id[4] __attribute__((packed)); 
	 __u8  version_id __attribute__((packed));
	 __u8  config_id __attribute__((packed)); 
	 __u16 boundry_id __attribute__((packed));	/* boundary scan id */
	 __u16 ee_size __attribute__((packed));		/* size of EEPROM */
	 __u8  assembly[11] __attribute__((packed));	/* assembly # */
	 __u8  assembly_rev __attribute__((packed));	/* assembly rev */
	 __u8  tracer[4] __attribute__((packed));	/* tracer number */
	 __u16 assembly_cksum __attribute__((packed));	/* asm checksum */
	 __u16 power_consump __attribute__((packed));	/* pwr requirements */
	 __u16 num_asics __attribute__((packed));	/* number of asics */
	 __u16 bist_time __attribute__((packed));	/* min. bist time */
	 __u16 err_log_offset __attribute__((packed));	/* error log offset */
	 __u16 scan_path_offset __attribute__((packed));/* scan path offset */
	 __u16 cct_offset __attribute__((packed));
	 __u16 log_length __attribute__((packed));	/* length of err log */
	 __u16 xsum_end __attribute__((packed));	/* offset to end of
							   checksum */
	 __u8  reserved[4] __attribute__((packed));
	 __u8  sflag __attribute__((packed));		/* starting sentinal */
	 __u8  part_number[13] __attribute__((packed));	/* prom part number */
	 __u8  version[10] __attribute__((packed));	/* version number */
	 __u8  signature[8] __attribute__((packed));
	 __u16 eeprom_chksum __attribute__((packed));
	 __u32  data_stamp_offset __attribute__((packed));
	 __u8  eflag  __attribute__((packed));		 /* ending sentinal */
} voyager_eprom_hdr_t;



#define VOYAGER_EPROM_SIZE_OFFSET   ((__u16)(&(((voyager_eprom_hdr_t *)0)->ee_size)))
#define VOYAGER_XSUM_END_OFFSET		0x2a

/* the following three definitions are for internal table layouts
 * in the module EPROMs.  We really only care about the IDs and
 * offsets */
typedef struct voyager_sp_table {
	__u8 asic_id __attribute__((packed));
	__u8 bypass_flag __attribute__((packed));
	__u16 asic_data_offset __attribute__((packed));
	__u16 config_data_offset __attribute__((packed));
} voyager_sp_table_t;

typedef struct voyager_jtag_table {
	__u8 icode[4] __attribute__((packed));
	__u8 runbist[4] __attribute__((packed));
	__u8 intest[4] __attribute__((packed));
	__u8 samp_preld[4] __attribute__((packed));
	__u8 ireg_len __attribute__((packed));
} voyager_jtt_t;

typedef struct voyager_asic_data_table {
	__u8 jtag_id[4] __attribute__((packed));
	__u16 length_bsr __attribute__((packed));
	__u16 length_bist_reg __attribute__((packed));
	__u32 bist_clk __attribute__((packed));
	__u16 subaddr_bits __attribute__((packed));
	__u16 seed_bits __attribute__((packed));
	__u16 sig_bits __attribute__((packed));
	__u16 jtag_offset __attribute__((packed));
} voyager_at_t;

/* Voyager Interrupt Controller (VIC) registers */

/* Base to add to Cross Processor Interrupts (CPIs) when triggering
 * the CPU IRQ line */
/* register defines for the WCBICs (one per processor) */
#define VOYAGER_WCBIC0	0x41		/* bus A node P1 processor 0 */
#define VOYAGER_WCBIC1	0x49		/* bus A node P1 processor 1 */
#define VOYAGER_WCBIC2	0x51		/* bus A node P2 processor 0 */
#define VOYAGER_WCBIC3	0x59		/* bus A node P2 processor 1 */
#define VOYAGER_WCBIC4	0x61		/* bus B node P1 processor 0 */
#define VOYAGER_WCBIC5	0x69		/* bus B node P1 processor 1 */
#define VOYAGER_WCBIC6	0x71		/* bus B node P2 processor 0 */
#define VOYAGER_WCBIC7	0x79		/* bus B node P2 processor 1 */


/* top of memory registers */
#define VOYAGER_WCBIC_TOM_L	0x4
#define VOYAGER_WCBIC_TOM_H	0x5

/* register defines for Voyager Memory Contol (VMC) 
 * these are present on L4 machines only */
#define	VOYAGER_VMC1		0x81
#define VOYAGER_VMC2		0x91
#define VOYAGER_VMC3		0xa1
#define VOYAGER_VMC4		0xb1

/* VMC Ports */
#define VOYAGER_VMC_MEMORY_SETUP	0x9
#	define VMC_Interleaving		0x01
#	define VMC_4Way			0x02
#	define VMC_EvenCacheLines	0x04
#	define VMC_HighLine		0x08
#	define VMC_Start0_Enable	0x20
#	define VMC_Start1_Enable	0x40
#	define VMC_Vremap		0x80
#define VOYAGER_VMC_BANK_DENSITY	0xa
#	define	VMC_BANK_EMPTY		0
#	define	VMC_BANK_4MB		1
#	define	VMC_BANK_16MB		2
#	define	VMC_BANK_64MB		3
#	define	VMC_BANK0_MASK		0x03
#	define	VMC_BANK1_MASK		0x0C
#	define	VMC_BANK2_MASK		0x30
#	define	VMC_BANK3_MASK		0xC0

/* Magellan Memory Controller (MMC) defines - present on L5 */
#define VOYAGER_MMC_ASIC_ID		1
/* the two memory modules corresponding to memory cards in the system */
#define VOYAGER_MMC_MEMORY0_MODULE	0x14
#define VOYAGER_MMC_MEMORY1_MODULE	0x15
/* the Magellan Memory Address (MMA) defines */
#define VOYAGER_MMA_ASIC_ID		2

/* Submodule number for the Quad Baseboard */
#define VOYAGER_QUAD_BASEBOARD		1

/* ASIC defines for the Quad Baseboard */
#define VOYAGER_QUAD_QDATA0		1
#define VOYAGER_QUAD_QDATA1		2
#define VOYAGER_QUAD_QABC		3

/* Useful areas in extended CMOS */
#define VOYAGER_PROCESSOR_PRESENT_MASK	0x88a
#define VOYAGER_MEMORY_CLICKMAP		0xa23
#define VOYAGER_DUMP_LOCATION		0xb1a

/* SUS In Control bit - used to tell SUS that we don't need to be
 * babysat anymore */
#define VOYAGER_SUS_IN_CONTROL_PORT	0x3ff
#	define VOYAGER_IN_CONTROL_FLAG	0x80

/* Voyager PSI defines */
#define VOYAGER_PSI_STATUS_REG		0x08
#	define PSI_DC_FAIL		0x01
#	define PSI_MON			0x02
#	define PSI_FAULT		0x04
#	define PSI_ALARM		0x08
#	define PSI_CURRENT		0x10
#	define PSI_DVM			0x20
#	define PSI_PSCFAULT		0x40
#	define PSI_STAT_CHG		0x80

#define VOYAGER_PSI_SUPPLY_REG		0x8000
	/* read */
#	define PSI_FAIL_DC		0x01
#	define PSI_FAIL_AC		0x02
#	define PSI_MON_INT		0x04
#	define PSI_SWITCH_OFF		0x08
#	define PSI_HX_OFF		0x10
#	define PSI_SECURITY		0x20
#	define PSI_CMOS_BATT_LOW	0x40
#	define PSI_CMOS_BATT_FAIL	0x80
	/* write */
#	define PSI_CLR_SWITCH_OFF	0x13
#	define PSI_CLR_HX_OFF		0x14
#	define PSI_CLR_CMOS_BATT_FAIL	0x17

#define VOYAGER_PSI_MASK		0x8001
#	define PSI_MASK_MASK		0x10

#define VOYAGER_PSI_AC_FAIL_REG		0x8004
#define	AC_FAIL_STAT_CHANGE		0x80

#define VOYAGER_PSI_GENERAL_REG		0x8007
	/* read */
#	define PSI_SWITCH_ON		0x01
#	define PSI_SWITCH_ENABLED	0x02
#	define PSI_ALARM_ENABLED	0x08
#	define PSI_SECURE_ENABLED	0x10
#	define PSI_COLD_RESET		0x20
#	define PSI_COLD_START		0x80
	/* write */
#	define PSI_POWER_DOWN		0x10
#	define PSI_SWITCH_DISABLE	0x01
#	define PSI_SWITCH_ENABLE	0x11
#	define PSI_CLEAR		0x12
#	define PSI_ALARM_DISABLE	0x03
#	define PSI_ALARM_ENABLE		0x13
#	define PSI_CLEAR_COLD_RESET	0x05
#	define PSI_SET_COLD_RESET	0x15
#	define PSI_CLEAR_COLD_START	0x07
#	define PSI_SET_COLD_START	0x17



struct voyager_bios_info {
	__u8	len;
	__u8	major;
	__u8	minor;
	__u8	debug;
	__u8	num_classes;
	__u8	class_1;
	__u8	class_2;
};

/* The following structures and definitions are for the Kernel/SUS
 * interface these are needed to find out how SUS initialised any Quad
 * boards in the system */

#define	NUMBER_OF_MC_BUSSES	2
#define SLOTS_PER_MC_BUS	8
#define MAX_CPUS                16      /* 16 way CPU system */
#define MAX_PROCESSOR_BOARDS	4	/* 4 processor slot system */
#define MAX_CACHE_LEVELS	4	/* # of cache levels supported */
#define MAX_SHARED_CPUS		4	/* # of CPUs that can share a LARC */
#define NUMBER_OF_POS_REGS	8

typedef struct {
	__u8	MC_Slot __attribute__((packed));
	__u8	POS_Values[NUMBER_OF_POS_REGS] __attribute__((packed));
} MC_SlotInformation_t;

struct QuadDescription {
	__u8  Type __attribute__((packed));	/* for type 0 (DYADIC or MONADIC) all fields
                         * will be zero except for slot */
	__u8 StructureVersion __attribute__((packed));
	__u32 CPI_BaseAddress __attribute__((packed));
	__u32  LARC_BankSize __attribute__((packed));	
	__u32 LocalMemoryStateBits __attribute__((packed));
	__u8  Slot __attribute__((packed)); /* Processor slots 1 - 4 */
}; 

struct ProcBoardInfo { 
	__u8 Type __attribute__((packed));    
	__u8 StructureVersion __attribute__((packed));
	__u8 NumberOfBoards __attribute__((packed));
	struct QuadDescription QuadData[MAX_PROCESSOR_BOARDS] __attribute__((packed));
};

struct CacheDescription {
	__u8 Level __attribute__((packed));
	__u32 TotalSize __attribute__((packed));
	__u16 LineSize __attribute__((packed));
	__u8  Associativity __attribute__((packed));
	__u8  CacheType __attribute__((packed));
	__u8  WriteType __attribute__((packed));
	__u8  Number_CPUs_SharedBy __attribute__((packed));
	__u8  Shared_CPUs_Hardware_IDs[MAX_SHARED_CPUS] __attribute__((packed));

};

struct CPU_Description {
	__u8 CPU_HardwareId __attribute__((packed));
	char *FRU_String __attribute__((packed));
	__u8 NumberOfCacheLevels __attribute__((packed));
	struct CacheDescription CacheLevelData[MAX_CACHE_LEVELS] __attribute__((packed));
};

struct CPU_Info {
	__u8 Type __attribute__((packed));
	__u8 StructureVersion __attribute__((packed));
	__u8 NumberOf_CPUs __attribute__((packed));
	struct CPU_Description CPU_Data[MAX_CPUS] __attribute__((packed));
};


/*
 * This structure will be used by SUS and the OS.
 * The assumption about this structure is that no blank space is
 * packed in it by our friend the compiler.
 */
typedef struct {
	__u8	Mailbox_SUS;		/* Written to by SUS to give commands/response to the OS */
	__u8	Mailbox_OS;		/* Written to by the OS to give commands/response to SUS */
	__u8	SUS_MailboxVersion;	/* Tells the OS which iteration of the interface SUS supports */
	__u8	OS_MailboxVersion;	/* Tells SUS which iteration of the interface the OS supports */
	__u32	OS_Flags;		/* Flags set by the OS as info for SUS */
	__u32	SUS_Flags;		/* Flags set by SUS as info for the OS */
	__u32	WatchDogPeriod;		/* Watchdog period (in seconds) which the DP uses to see if the OS is dead */
	__u32	WatchDogCount;		/* Updated by the OS on every tic. */
	__u32	MemoryFor_SUS_ErrorLog;	/* Flat 32 bit address which tells SUS where to stuff the SUS error log on a dump */
	MC_SlotInformation_t  MC_SlotInfo[NUMBER_OF_MC_BUSSES*SLOTS_PER_MC_BUS];	/* Storage for MCA POS data */
	/* All new SECOND_PASS_INTERFACE fields added from this point */
        struct ProcBoardInfo    *BoardData;
        struct CPU_Info         *CPU_Data;
	/* All new fields must be added from this point */
} Voyager_KernelSUS_Mbox_t;

/* structure for finding the right memory address to send a QIC CPI to */
struct voyager_qic_cpi {
	/* Each cache line (32 bytes) can trigger a cpi.  The cpi
	 * read/write may occur anywhere in the cache line---pick the
	 * middle to be safe */
	struct  {
		__u32 pad1[3];
		__u32 cpi;
		__u32 pad2[4];
	} qic_cpi[8];
};

struct voyager_status {
	__u32	power_fail:1;
	__u32	switch_off:1;
	__u32	request_from_kernel:1;
};

struct voyager_psi_regs {
	__u8 cat_id;
	__u8 cat_dev;
	__u8 cat_control;
	__u8 subaddr;
	__u8 dummy4;
	__u8 checkbit;
	__u8 subaddr_low;
	__u8 subaddr_high;
	__u8 intstatus;
	__u8 stat1;
	__u8 stat3;
	__u8 fault;
	__u8 tms;
	__u8 gen;
	__u8 sysconf;
	__u8 dummy15;
};

struct voyager_psi_subregs {
	__u8 supply;
	__u8 mask;
	__u8 present;
	__u8 DCfail;
	__u8 ACfail;
	__u8 fail;
	__u8 UPSfail;
	__u8 genstatus;
};

struct voyager_psi {
	struct voyager_psi_regs regs;
	struct voyager_psi_subregs subregs;
};

struct voyager_SUS {
#define	VOYAGER_DUMP_BUTTON_NMI		0x1
#define VOYAGER_SUS_VALID		0x2
#define VOYAGER_SYSINT_COMPLETE		0x3
	__u8	SUS_mbox;
#define VOYAGER_NO_COMMAND		0x0
#define VOYAGER_IGNORE_DUMP		0x1
#define VOYAGER_DO_DUMP			0x2
#define VOYAGER_SYSINT_HANDSHAKE	0x3
#define VOYAGER_DO_MEM_DUMP		0x4
#define VOYAGER_SYSINT_WAS_RECOVERED	0x5
	__u8	kernel_mbox;
#define	VOYAGER_MAILBOX_VERSION		0x10
	__u8	SUS_version;
	__u8	kernel_version;
#define VOYAGER_OS_HAS_SYSINT		0x1
#define VOYAGER_OS_IN_PROGRESS		0x2
#define VOYAGER_UPDATING_WDPERIOD	0x4
	__u32	kernel_flags;
#define VOYAGER_SUS_BOOTING		0x1
#define VOYAGER_SUS_IN_PROGRESS		0x2
	__u32	SUS_flags;
	__u32	watchdog_period;
	__u32	watchdog_count;
	__u32	SUS_errorlog;
	/* lots of system configuration stuff under here */
};
	
/* Variables exported by voyager_smp */
extern __u32 voyager_extended_vic_processors;
extern __u32 voyager_allowed_boot_processors;
extern __u32 voyager_quad_processors;
extern struct voyager_qic_cpi *voyager_quad_cpi_addr[NR_CPUS];
extern struct voyager_SUS *voyager_SUS;

/* variables exported always */
extern int voyager_level;
extern int kvoyagerd_running;
extern struct semaphore kvoyagerd_sem;
extern struct voyager_status voyager_status;



/* functions exported by the voyager and voyager_smp modules */

extern int voyager_cat_readb(__u8 module, __u8 asic, int reg);
extern void voyager_cat_init(void);
extern void voyager_detect(struct voyager_bios_info *);
extern void voyager_trap_init(void);
extern void voyager_setup_irqs(void);
extern int voyager_memory_detect(int region, __u32 *addr, __u32 *length);
extern void voyager_smp_intr_init(void);
extern __u8 voyager_extended_cmos_read(__u16 cmos_address);
extern void voyager_smp_dump(void);
extern void voyager_timer_interrupt(struct pt_regs *regs);
extern void smp_local_timer_interrupt(struct pt_regs * regs);
extern void voyager_power_off(void);
extern void smp_voyager_power_off(void *dummy);
extern void voyager_restart(void);
extern void voyager_cat_power_off(void);
extern void voyager_cat_do_common_interrupt(void);
extern void voyager_handle_nmi(void);
/* Commands for the following are */
#define	VOYAGER_PSI_READ	0
#define VOYAGER_PSI_WRITE	1
#define VOYAGER_PSI_SUBREAD	2
#define VOYAGER_PSI_SUBWRITE	3
extern void voyager_cat_psi(__u8, __u16, __u8 *);
