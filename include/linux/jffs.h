/*
 * JFFS -- Journalling Flash File System, Linux implementation.
 *
 * Copyright (C) 1999, 2000  Axis Communications AB.
 *
 * Created by Finn Hakansson <finn@axis.com>.
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * $Id: jffs.h,v 1.20 2001/09/18 21:33:37 dwmw2 Exp $
 *
 * Ported to Linux 2.3.x and MTD:
 * Copyright (C) 2000  Alexander Larsson (alex@cendio.se), Cendio Systems AB
 *
 */

#ifndef __LINUX_JFFS_H__
#define __LINUX_JFFS_H__

#include <linux/types.h>
#include <linux/completion.h>

#define JFFS_VERSION_STRING "1.0"

/* This is a magic number that is used as an identification number for
   this file system.  It is written to the super_block structure.  */
#define JFFS_MAGIC_SB_BITMASK 0x07c0  /* 1984 */

/* This is a magic number that every on-flash raw inode begins with.  */
#define JFFS_MAGIC_BITMASK 0x34383931 /* "1984" */

/* These two bitmasks are the valid ones for the flash memories we have
   for the moment.  */
#define JFFS_EMPTY_BITMASK 0xffffffff
#define JFFS_DIRTY_BITMASK 0x00000000

/* This is the inode number of the root node.  */
#define JFFS_MIN_INO 1

/* How many slots in the file hash table should we have?  */
#define JFFS_HASH_SIZE 40

/* Don't use more than 254 bytes as the maximum allowed length of a file's
   name due to errors that could occur during the scanning of the flash
   memory. In fact, a name length of 255 or 0xff, could be the result of
   an uncompleted write.  For instance, if a raw inode is written to the
   flash memory and there is a power lossage just before the length of
   the name is written, the length 255 would be interpreted as an illegal
   value.  */
#define JFFS_MAX_NAME_LEN 254

/* Commands for ioctl().  */
#define JFFS_IOCTL_MAGIC 't'
#define JFFS_PRINT_HASH _IO(JFFS_IOCTL_MAGIC, 90)
#define JFFS_PRINT_TREE _IO(JFFS_IOCTL_MAGIC, 91)
#define JFFS_GET_STATUS _IO(JFFS_IOCTL_MAGIC, 92)

/* XXX: This is something that we should try to get rid of in the future.  */
#define JFFS_MODIFY_INODE 0x01
#define JFFS_MODIFY_NAME  0x02
#define JFFS_MODIFY_DATA  0x04
#define JFFS_MODIFY_EXIST 0x08

struct jffs_control;

/* The JFFS raw inode structure: Used for storage on physical media.  */
/* Perhaps the uid, gid, atime, mtime and ctime members should have
   more space due to future changes in the Linux kernel. Anyhow, since
   a user of this filesystem probably have to fix a large number of
   other things, we have decided to not be forward compatible.  */
struct jffs_raw_inode
{
	__u32 magic;      /* A constant magic number.  */
	__u32 ino;        /* Inode number.  */
	__u32 pino;       /* Parent's inode number.  */
	__u32 version;    /* Version number.  */
	__u32 mode;       /* The file's type or mode.  */
	__u16 uid;        /* The file's owner.  */
	__u16 gid;        /* The file's group.  */
	__u32 atime;      /* Last access time.  */
	__u32 mtime;      /* Last modification time.  */
	__u32 ctime;      /* Creation time.  */
	__u32 offset;     /* Where to begin to write.  */
	__u32 dsize;      /* Size of the node's data.  */
	__u32 rsize;      /* How much are going to be replaced?  */
	__u8 nsize;       /* Name length.  */
	__u8 nlink;       /* Number of links.  */
	__u8 spare : 6;   /* For future use.  */
	__u8 rename : 1;  /* Rename to a name of an already existing file?  */
	__u8 deleted : 1; /* Has this file been deleted?  */
	__u8 accurate;    /* The inode is obsolete if accurate == 0.  */
	__u32 dchksum;    /* Checksum for the data.  */
	__u16 nchksum;    /* Checksum for the name.  */
	__u16 chksum;     /* Checksum for the raw inode.  */
};

/* Define the offset of the accurate byte in struct jffs_raw_inode.  */
#define JFFS_RAW_INODE_ACCURATE_OFFSET (sizeof(struct jffs_raw_inode) \
					- 2 * sizeof(__u32) - sizeof(__u8))

/* Define the offset of the chksum member in struct jffs_raw_inode.  */
#define JFFS_RAW_INODE_CHKSUM_OFFSET (sizeof(struct jffs_raw_inode) \
				      - sizeof(__u16))

/* Define the offset of the dchksum member in struct jffs_raw_inode.  */
#define JFFS_RAW_INODE_DCHKSUM_OFFSET (sizeof(struct jffs_raw_inode)   \
				       - sizeof(__u16) - sizeof(__u16) \
				       - sizeof(__u32))


/* The RAM representation of the node.  The names of pointers to
   jffs_nodes are very often just called `n' in the source code.  */
struct jffs_node
{
	__u32 ino;          /* Inode number.  */
	__u32 version;      /* Version number.  */
	__u32 data_offset;  /* Logic location of the data to insert.  */
	__u32 data_size;    /* The amount of data this node inserts.  */
	__u32 removed_size; /* The amount of data that this node removes.  */
	__u32 fm_offset;    /* Physical location of the data in the actual
			       flash memory data chunk.  */
	__u8 name_size;     /* Size of the name.  */
	struct jffs_fm *fm; /* Physical memory information.  */
	struct jffs_node *version_prev;
	struct jffs_node *version_next;
	struct jffs_node *range_prev;
	struct jffs_node *range_next;
};


/* The RAM representation of a file (plain files, directories,
   links, etc.).  Pointers to jffs_files are normally named `f'
   in the JFFS source code.  */
struct jffs_file
{
	__u32 ino;    /* Inode number.  */
	__u32 pino;   /* Parent's inode number.  */
	__u32 mode;   /* file_type, mode  */
	__u16 uid;    /* owner  */
	__u16 gid;    /* group  */
	__u32 atime;  /* Last access time.  */
	__u32 mtime;  /* Last modification time.  */
	__u32 ctime;  /* Creation time.  */
	__u8 nsize;   /* Name length.  */
	__u8 nlink;   /* Number of links.  */
	__u8 deleted; /* Has this file been deleted?  */
	char *name;   /* The name of this file; NULL-terminated.  */
	__u32 size;   /* The total size of the file's data.  */
	__u32 highest_version; /* The highest version number of this file.  */
	struct jffs_control *c;
	struct jffs_file *parent;   /* Reference to the parent directory.  */
	struct jffs_file *children; /* Always NULL for plain files.  */
	struct jffs_file *sibling_prev; /* Siblings in the same directory.  */
	struct jffs_file *sibling_next;
	struct list_head hash;    /* hash list.  */
	struct jffs_node *range_head;   /* The final data.  */
	struct jffs_node *range_tail;   /* The first data.  */
	struct jffs_node *version_head; /* The youngest node.  */
	struct jffs_node *version_tail; /* The oldest node.  */
};


/* This is just a definition of a simple list used for keeping track of
   files deleted due to a rename.  This list is only used during the
   mounting of the file system and only if there have been rename operations
   earlier.  */
struct jffs_delete_list
{
	__u32 ino;
	struct jffs_delete_list *next;
};


/* A struct for the overall file system control.  Pointers to
   jffs_control structs are named `c' in the source code.  */
struct jffs_control
{
	struct super_block *sb;		/* Reference to the VFS super block.  */
	struct jffs_file *root;		/* The root directory file.  */
	struct list_head *hash;		/* Hash table for finding files by ino.  */
	struct jffs_fmcontrol *fmc;	/* Flash memory control structure.  */
	__u32 hash_len;			/* The size of the hash table.  */
	__u32 next_ino;			/* Next inode number to use for new files.  */
	__u16 building_fs;		/* Is the file system being built right now?  */
	struct jffs_delete_list *delete_list; /* Track deleted files.  */
	pid_t thread_pid;		/* GC thread's PID */
	struct task_struct *gc_task;	/* GC task struct */
	struct completion gc_thread_comp; /* GC thread exit mutex */
	__u32 gc_minfree_threshold;	/* GC trigger thresholds */
	__u32 gc_maxdirty_threshold;
};


/* Used to inform about flash status.  */
struct jffs_flash_status
{
	__u32 size;
	__u32 used;
	__u32 dirty;
	__u32 begin;
	__u32 end;
};

/* This stuff could be used for finding memory leaks.  */
#define JFFS_MEMORY_DEBUG 0

extern long no_jffs_node;
#if defined(JFFS_MEMORY_DEBUG) && JFFS_MEMORY_DEBUG
extern long no_jffs_control;
extern long no_jffs_raw_inode;
extern long no_jffs_node_ref;
extern long no_jffs_fm;
extern long no_jffs_fmcontrol;
extern long no_hash;
extern long no_name;
#define DJM(x) x
#else
#define DJM(x)
#endif

#endif /* __LINUX_JFFS_H__ */
