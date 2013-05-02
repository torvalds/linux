/*
  nubus.h: various definitions and prototypes for NuBus drivers to use.

  Originally written by Alan Cox.

  Hacked to death by C. Scott Ananian and David Huggins-Daines.
  
  Some of the constants in here are from the corresponding
  NetBSD/OpenBSD header file, by Allen Briggs.  We figured out the
  rest of them on our own. */
#ifndef LINUX_NUBUS_H
#define LINUX_NUBUS_H

#include <asm/nubus.h>
#include <uapi/linux/nubus.h>

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
#ifdef CONFIG_PROC_FS
extern void nubus_proc_init(void);
#else
static inline void nubus_proc_init(void) {}
#endif
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
#endif /* LINUX_NUBUS_H */
