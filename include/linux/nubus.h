/*
  nubus.h: various definitions and prototypes for NuBus drivers to use.

  Originally written by Alan Cox.

  Hacked to death by C. Scott Ananian and David Huggins-Daines.
  
  Some of the constants in here are from the corresponding
  NetBSD/OpenBSD header file, by Allen Briggs.  We figured out the
  rest of them on our own. */

#ifndef LINUX_NUBUS_H
#define LINUX_NUBUS_H

#ifdef __KERNEL__
#include <asm/nubus.h>
#endif

enum nubus_category {
	NUBUS_CAT_BOARD          = 0x0001,
	NUBUS_CAT_DISPLAY        = 0x0003,
	NUBUS_CAT_NETWORK        = 0x0004,
	NUBUS_CAT_COMMUNICATIONS = 0x0006,
	NUBUS_CAT_FONT           = 0x0009,
	NUBUS_CAT_CPU            = 0x000A,
	/* For lack of a better name */
	NUBUS_CAT_DUODOCK        = 0x0020
};

enum nubus_type_network {
	NUBUS_TYPE_ETHERNET      = 0x0001,
	NUBUS_TYPE_RS232         = 0x0002
};

enum nubus_type_display {
	NUBUS_TYPE_VIDEO         = 0x0001
};

enum nubus_type_cpu {
	NUBUS_TYPE_68020         = 0x0003,
	NUBUS_TYPE_68030         = 0x0004,
	NUBUS_TYPE_68040         = 0x0005
};

/* Known <Cat,Type,SW,HW> tuples: (according to TattleTech and Slots)
 *  68030 motherboards: <10,4,0,24>
 *  68040 motherboards: <10,5,0,24>
 *  DuoDock Plus: <32,1,1,2>
 *
 *  Toby Frame Buffer card: <3,1,1,1>
 *  RBV built-in video (IIci): <3,1,1,24>
 *  Valkyrie built-in video (Q630): <3,1,1,46>
 *  Macintosh Display Card: <3,1,1,25>
 *  Sonora built-in video (P460): <3,1,1,34>
 *  Jet framebuffer (DuoDock Plus): <3,1,1,41>
 *
 *  SONIC comm-slot/on-board and DuoDock Ethernet: <4,1,1,272>
 *  SONIC LC-PDS Ethernet (Dayna, but like Apple 16-bit, sort of): <4,1,1,271>
 *  Apple SONIC LC-PDS Ethernet ("Apple Ethernet LC Twisted-Pair Card"): <4,1,0,281>
 *  Sonic Systems Ethernet A-Series Card: <4,1,268,256>
 *  Asante MacCon NuBus-A: <4,1,260,256> (alpha-1.0,1.1 revision)
 *   ROM on the above card: <2,1,0,0>
 *  Cabletron ethernet card: <4,1,1,265>
 *  Farallon ethernet card: <4,1,268,256> (identical to Sonic Systems card)
 *  Kinetics EtherPort IIN: <4,1,259,262>
 *  API Engineering EtherRun_LCa PDS enet card: <4,1,282,256>
 *
 *  Add your devices to the list!  You can obtain the "Slots" utility
 *  from Apple's FTP site at:
 *  ftp://dev.apple.com/devworld/Tool_Chest/Devices_-_Hardware/NuBus_Slot_Manager/
 *
 *  Alternately, TattleTech can be found at any Info-Mac mirror site.  
 *  or from its distribution site: ftp://ftp.decismkr.com/dms
 */

/* DrSW: Uniquely identifies the software interface to a board.  This
   is usually the one you want to look at when writing a driver.  It's
   not as useful as you think, though, because as we should know by
   now (duh), "Apple Compatible" can mean a lot of things... */

/* Add known DrSW values here */
enum nubus_drsw {
	/* NUBUS_CAT_DISPLAY */
	NUBUS_DRSW_APPLE        = 0x0001,
	NUBUS_DRSW_APPLE_HIRES  = 0x0013, /* MacII HiRes card driver */
	
	/* NUBUS_CAT_NETWORK */
	NUBUS_DRSW_3COM         = 0x0000,
	NUBUS_DRSW_CABLETRON    = 0x0001,
	NUBUS_DRSW_SONIC_LC     = 0x0001,
	NUBUS_DRSW_KINETICS     = 0x0103,
	NUBUS_DRSW_ASANTE       = 0x0104,
	NUBUS_DRSW_TECHWORKS    = 0x0109,
	NUBUS_DRSW_DAYNA        = 0x010b,
	NUBUS_DRSW_FARALLON     = 0x010c,
	NUBUS_DRSW_APPLE_SN     = 0x010f,
	NUBUS_DRSW_DAYNA2       = 0x0115,
	NUBUS_DRSW_FOCUS        = 0x011a,
	NUBUS_DRSW_ASANTE_CS    = 0x011d, /* use asante SMC9194 driver */
	NUBUS_DRSW_DAYNA_LC     = 0x011e,

	/* NUBUS_CAT_CPU */
	NUBUS_DRSW_NONE         = 0x0000,
};

/* DrHW: Uniquely identifies the hardware interface to a board (or at
   least, it should...  some video cards are known to incorrectly
   identify themselves as Toby cards) */

/* Add known DrHW values here */
enum nubus_drhw {
	/* NUBUS_CAT_DISPLAY */
	NUBUS_DRHW_APPLE_TFB      = 0x0001, /* Toby frame buffer card */
	NUBUS_DRHW_APPLE_WVC      = 0x0006, /* Apple Workstation Video Card */
	NUBUS_DRHW_SIGMA_CLRMAX   = 0x0007, /* Sigma Design ColorMax */
	NUBUS_DRHW_APPLE_SE30     = 0x0009, /* Apple SE/30 video */
	NUBUS_DRHW_APPLE_HRVC     = 0x0013, /* Mac II High-Res Video Card */
	NUBUS_DRHW_APPLE_PVC      = 0x0017, /* Mac II Portrait Video Card */
	NUBUS_DRHW_APPLE_RBV1     = 0x0018, /* IIci RBV video */
	NUBUS_DRHW_APPLE_MDC      = 0x0019, /* Macintosh Display Card */
	NUBUS_DRHW_APPLE_SONORA   = 0x0022, /* Sonora built-in video */
	NUBUS_DRHW_APPLE_24AC     = 0x002b, /* Mac 24AC Video Card */
	NUBUS_DRHW_APPLE_VALKYRIE = 0x002e,
	NUBUS_DRHW_APPLE_JET      = 0x0029, /* Jet framebuffer (DuoDock) */
	NUBUS_DRHW_SMAC_GFX       = 0x0105, /* SuperMac GFX */
	NUBUS_DRHW_RASTER_CB264   = 0x013B, /* RasterOps ColorBoard 264 */
	NUBUS_DRHW_MICRON_XCEED   = 0x0146, /* Micron Exceed color */
	NUBUS_DRHW_RDIUS_GSC      = 0x0153, /* Radius GS/C */
	NUBUS_DRHW_SMAC_SPEC8     = 0x017B, /* SuperMac Spectrum/8 */
	NUBUS_DRHW_SMAC_SPEC24    = 0x017C, /* SuperMac Spectrum/24 */
	NUBUS_DRHW_RASTER_CB364   = 0x026F, /* RasterOps ColorBoard 364 */
	NUBUS_DRHW_RDIUS_DCGX     = 0x027C, /* Radius DirectColor/GX */
	NUBUS_DRHW_RDIUS_PC8      = 0x0291, /* Radius PrecisionColor 8 */
	NUBUS_DRHW_LAPIS_PCS8     = 0x0292, /* Lapis ProColorServer 8 */
	NUBUS_DRHW_RASTER_24XLI   = 0x02A0, /* RasterOps 8/24 XLi */
	NUBUS_DRHW_RASTER_PBPGT   = 0x02A5, /* RasterOps PaintBoard Prism GT */
	NUBUS_DRHW_EMACH_FSX      = 0x02AE, /* E-Machines Futura SX */
	NUBUS_DRHW_RASTER_24XLTV  = 0x02B7, /* RasterOps 24XLTV */
	NUBUS_DRHW_SMAC_THUND24   = 0x02CB, /* SuperMac Thunder/24 */
	NUBUS_DRHW_SMAC_THUNDLGHT = 0x03D9, /* SuperMac ThunderLight */
	NUBUS_DRHW_RDIUS_PC24XP   = 0x0406, /* Radius PrecisionColor 24Xp */
	NUBUS_DRHW_RDIUS_PC24X    = 0x040A, /* Radius PrecisionColor 24X */
	NUBUS_DRHW_RDIUS_PC8XJ    = 0x040B, /* Radius PrecisionColor 8XJ */
	
	/* NUBUS_CAT_NETWORK */
	NUBUS_DRHW_INTERLAN       = 0x0100,
	NUBUS_DRHW_SMC9194        = 0x0101,
	NUBUS_DRHW_KINETICS       = 0x0106,
	NUBUS_DRHW_CABLETRON      = 0x0109,
	NUBUS_DRHW_ASANTE_LC      = 0x010f,
	NUBUS_DRHW_SONIC          = 0x0110,
	NUBUS_DRHW_TECHWORKS      = 0x0112,
	NUBUS_DRHW_APPLE_SONIC_NB = 0x0118,
	NUBUS_DRHW_APPLE_SONIC_LC = 0x0119,
	NUBUS_DRHW_FOCUS          = 0x011c,
	NUBUS_DRHW_SONNET         = 0x011d,
};

/* Resource IDs: These are the identifiers for the various weird and
   wonderful tidbits of information that may or may not reside in the
   NuBus ROM directory. */
enum nubus_res_id {
	NUBUS_RESID_TYPE         = 0x0001,
	NUBUS_RESID_NAME         = 0x0002,
	NUBUS_RESID_ICON         = 0x0003,
	NUBUS_RESID_DRVRDIR      = 0x0004,
	NUBUS_RESID_LOADREC      = 0x0005,
	NUBUS_RESID_BOOTREC      = 0x0006,
	NUBUS_RESID_FLAGS        = 0x0007,
	NUBUS_RESID_HWDEVID      = 0x0008,
	NUBUS_RESID_MINOR_BASEOS = 0x000a,
	NUBUS_RESID_MINOR_LENGTH = 0x000b,
	NUBUS_RESID_MAJOR_BASEOS = 0x000c,
	NUBUS_RESID_MAJOR_LENGTH = 0x000d,
	NUBUS_RESID_CICN         = 0x000f,
	NUBUS_RESID_ICL8         = 0x0010,
	NUBUS_RESID_ICL4         = 0x0011,
};

/* Category-specific resources. */
enum nubus_board_res_id {
	NUBUS_RESID_BOARDID      = 0x0020,
	NUBUS_RESID_PRAMINITDATA = 0x0021,
	NUBUS_RESID_PRIMARYINIT  = 0x0022,
	NUBUS_RESID_TIMEOUTCONST = 0x0023,
	NUBUS_RESID_VENDORINFO   = 0x0024,
	NUBUS_RESID_BOARDFLAGS   = 0x0025,
	NUBUS_RESID_SECONDINIT   = 0x0026,

	/* Not sure why Apple put these next two in here */
	NUBUS_RESID_VIDNAMES     = 0x0041,
	NUBUS_RESID_VIDMODES     = 0x007e
};

/* Fields within the vendor info directory */
enum nubus_vendor_res_id {
	NUBUS_RESID_VEND_ID     = 0x0001,
	NUBUS_RESID_VEND_SERIAL = 0x0002,
	NUBUS_RESID_VEND_REV    = 0x0003,
	NUBUS_RESID_VEND_PART   = 0x0004,
	NUBUS_RESID_VEND_DATE   = 0x0005
};

enum nubus_net_res_id {
	NUBUS_RESID_MAC_ADDRESS  = 0x0080
};

enum nubus_cpu_res_id {
	NUBUS_RESID_MEMINFO      = 0x0081,
	NUBUS_RESID_ROMINFO      = 0x0082
};

enum nubus_display_res_id {
	NUBUS_RESID_GAMMADIR    = 0x0040,
	NUBUS_RESID_FIRSTMODE   = 0x0080,
	NUBUS_RESID_SECONDMODE  = 0x0081,
	NUBUS_RESID_THIRDMODE   = 0x0082,
	NUBUS_RESID_FOURTHMODE  = 0x0083,
	NUBUS_RESID_FIFTHMODE   = 0x0084,
	NUBUS_RESID_SIXTHMODE   = 0x0085
};

struct nubus_dir
{
	unsigned char *base;
	unsigned char *ptr;
	int done;
	int mask;
};

struct nubus_dirent
{
	unsigned char *base;
	unsigned char type;
	__u32 data;	/* Actually 24bits used */
	int mask;
};

struct nubus_board {
	struct nubus_board* next;
	struct nubus_dev* first_dev;
	
	/* Only 9-E actually exist, though 0-8 are also theoretically
	   possible, and 0 is a special case which represents the
	   motherboard and onboard peripherals (Ethernet, video) */
	int slot;
	/* For slot 0, this is bogus. */
	char name[64];

	/* Format block */
	unsigned char* fblock;
	/* Root directory (does *not* always equal fblock + doffset!) */
	unsigned char* directory;
	
	unsigned long slot_addr;
	/* Offset to root directory (sometimes) */
	unsigned long doffset;
	/* Length over which to compute the crc */
	unsigned long rom_length;
	/* Completely useless most of the time */
	unsigned long crc;
	unsigned char rev;
	unsigned char format;
	unsigned char lanes;
};

struct nubus_dev {
	/* Next link in device list */
	struct nubus_dev* next;
	/* Directory entry in /proc/bus/nubus */
	struct proc_dir_entry* procdir;

	/* The functional resource ID of this device */
	unsigned char resid;
	/* These are mostly here for convenience; we could always read
	   them from the ROMs if we wanted to */
	unsigned short category;
	unsigned short type;
	unsigned short dr_sw;
	unsigned short dr_hw;
	/* This is the device's name rather than the board's.
	   Sometimes they are different.  Usually the board name is
	   more correct. */
	char name[64];
	/* MacOS driver (I kid you not) */
	unsigned char* driver;
	/* Actually this is an offset */
	unsigned long iobase;
	unsigned long iosize;
	unsigned char flags, hwdevid;
	
	/* Functional directory */
	unsigned char* directory;
	/* Much of our info comes from here */
	struct nubus_board* board;
};

/* This is all NuBus devices (used to find devices later on) */
extern struct nubus_dev* nubus_devices;
/* This is all NuBus cards */
extern struct nubus_board* nubus_boards;

/* Generic NuBus interface functions, modelled after the PCI interface */
void nubus_scan_bus(void);
extern void nubus_proc_init(void);
int get_nubus_list(char *buf);
int nubus_proc_attach_device(struct nubus_dev *dev);
int nubus_proc_detach_device(struct nubus_dev *dev);
/* If we need more precision we can add some more of these */
struct nubus_dev* nubus_find_device(unsigned short category,
				    unsigned short type,
				    unsigned short dr_hw,
				    unsigned short dr_sw,
				    const struct nubus_dev* from);
struct nubus_dev* nubus_find_type(unsigned short category,
				  unsigned short type,
				  const struct nubus_dev* from);
/* Might have more than one device in a slot, you know... */
struct nubus_dev* nubus_find_slot(unsigned int slot,
				  const struct nubus_dev* from);

/* These are somewhat more NuBus-specific.  They all return 0 for
   success and -1 for failure, as you'd expect. */

/* The root directory which contains the board and functional
   directories */
int nubus_get_root_dir(const struct nubus_board* board,
		       struct nubus_dir* dir);
/* The board directory */
int nubus_get_board_dir(const struct nubus_board* board,
			struct nubus_dir* dir);
/* The functional directory */
int nubus_get_func_dir(const struct nubus_dev* dev,
		       struct nubus_dir* dir);

/* These work on any directory gotten via the above */
int nubus_readdir(struct nubus_dir* dir,
		  struct nubus_dirent* ent);
int nubus_find_rsrc(struct nubus_dir* dir,
		    unsigned char rsrc_type,
		    struct nubus_dirent* ent);
int nubus_rewinddir(struct nubus_dir* dir);

/* Things to do with directory entries */
int nubus_get_subdir(const struct nubus_dirent* ent,
		     struct nubus_dir* dir);
void nubus_get_rsrc_mem(void* dest,
			const struct nubus_dirent *dirent,
			int len);
void nubus_get_rsrc_str(void* dest,
			const struct nubus_dirent *dirent,
			int maxlen);

/* We'd like to get rid of this eventually.  Only daynaport.c uses it now. */
static inline void *nubus_slot_addr(int slot)
{
	return (void *)(0xF0000000|(slot<<24));
}

#endif /* LINUX_NUBUS_H */
