/* SPDX-License-Identifier: GPL-2.0 */
/*
  nubus.h: various definitions and prototypes for NuBus drivers to use.

  Originally written by Alan Cox.

  Hacked to death by C. Scott Ananian and David Huggins-Daines.
*/

#ifndef LINUX_NUBUS_H
#define LINUX_NUBUS_H

#include <asm/nubus.h>
#include <uapi/linux/nubus.h>

struct proc_dir_entry;
struct seq_file;

struct nubus_dir {
	unsigned char *base;
	unsigned char *ptr;
	int done;
	int mask;
	struct proc_dir_entry *procdir;
};

struct nubus_dirent {
	unsigned char *base;
	unsigned char type;
	__u32 data;	/* Actually 24 bits used */
	int mask;
};

struct nubus_board {
	struct nubus_board *next;
	struct nubus_rsrc *first_func_rsrc;

	/* Only 9-E actually exist, though 0-8 are also theoretically
	   possible, and 0 is a special case which represents the
	   motherboard and onboard peripherals (Ethernet, video) */
	int slot;
	/* For slot 0, this is bogus. */
	char name[64];

	/* Format block */
	unsigned char *fblock;
	/* Root directory (does *not* always equal fblock + doffset!) */
	unsigned char *directory;

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

	/* Directory entry in /proc/bus/nubus */
	struct proc_dir_entry *procdir;
};

struct nubus_rsrc {
	/* Next link in list */
	struct nubus_rsrc *next;

	/* The functional resource ID */
	unsigned char resid;
	/* These are mostly here for convenience; we could always read
	   them from the ROMs if we wanted to */
	unsigned short category;
	unsigned short type;
	unsigned short dr_sw;
	unsigned short dr_hw;

	/* Functional directory */
	unsigned char *directory;
	/* Much of our info comes from here */
	struct nubus_board *board;
};

/* This is all NuBus functional resources (used to find devices later on) */
extern struct nubus_rsrc *nubus_func_rsrcs;
/* This is all NuBus cards */
extern struct nubus_board *nubus_boards;

/* Generic NuBus interface functions, modelled after the PCI interface */
#ifdef CONFIG_PROC_FS
void nubus_proc_init(void);
struct proc_dir_entry *nubus_proc_add_board(struct nubus_board *board);
struct proc_dir_entry *nubus_proc_add_rsrc_dir(struct proc_dir_entry *procdir,
					       const struct nubus_dirent *ent,
					       struct nubus_board *board);
void nubus_proc_add_rsrc_mem(struct proc_dir_entry *procdir,
			     const struct nubus_dirent *ent,
			     unsigned int size);
void nubus_proc_add_rsrc(struct proc_dir_entry *procdir,
			 const struct nubus_dirent *ent);
#else
static inline void nubus_proc_init(void) {}
static inline
struct proc_dir_entry *nubus_proc_add_board(struct nubus_board *board)
{ return NULL; }
static inline
struct proc_dir_entry *nubus_proc_add_rsrc_dir(struct proc_dir_entry *procdir,
					       const struct nubus_dirent *ent,
					       struct nubus_board *board)
{ return NULL; }
static inline void nubus_proc_add_rsrc_mem(struct proc_dir_entry *procdir,
					   const struct nubus_dirent *ent,
					   unsigned int size) {}
static inline void nubus_proc_add_rsrc(struct proc_dir_entry *procdir,
				       const struct nubus_dirent *ent) {}
#endif

struct nubus_rsrc *nubus_find_type(unsigned short category,
				   unsigned short type,
				   const struct nubus_rsrc *from);

struct nubus_rsrc *nubus_find_slot(unsigned int slot,
				   const struct nubus_rsrc *from);

/* These are somewhat more NuBus-specific.  They all return 0 for
   success and -1 for failure, as you'd expect. */

/* The root directory which contains the board and functional
   directories */
int nubus_get_root_dir(const struct nubus_board *board,
		       struct nubus_dir *dir);
/* The board directory */
int nubus_get_board_dir(const struct nubus_board *board,
			struct nubus_dir *dir);
/* The functional directory */
int nubus_get_func_dir(const struct nubus_rsrc *fres, struct nubus_dir *dir);

/* These work on any directory gotten via the above */
int nubus_readdir(struct nubus_dir *dir,
		  struct nubus_dirent *ent);
int nubus_find_rsrc(struct nubus_dir *dir,
		    unsigned char rsrc_type,
		    struct nubus_dirent *ent);
int nubus_rewinddir(struct nubus_dir *dir);

/* Things to do with directory entries */
int nubus_get_subdir(const struct nubus_dirent *ent,
		     struct nubus_dir *dir);
void nubus_get_rsrc_mem(void *dest, const struct nubus_dirent *dirent,
			unsigned int len);
unsigned int nubus_get_rsrc_str(char *dest, const struct nubus_dirent *dirent,
				unsigned int len);
void nubus_seq_write_rsrc_mem(struct seq_file *m,
			      const struct nubus_dirent *dirent,
			      unsigned int len);
unsigned char *nubus_dirptr(const struct nubus_dirent *nd);

/* Returns a pointer to the "standard" slot space. */
static inline void *nubus_slot_addr(int slot)
{
	return (void *)(0xF0000000 | (slot << 24));
}

#endif /* LINUX_NUBUS_H */
