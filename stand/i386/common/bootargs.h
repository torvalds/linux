/*-
 * Copyright (c) 2012 Andriy Gapon <avg@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely
 * permitted provided that the above copyright notice and this
 * paragraph and the following disclaimer are duplicated in all
 * such forms.
 *
 * This software is provided "AS IS" and without any express or
 * implied warranties, including, without limitation, the implied
 * warranties of merchantability and fitness for a particular
 * purpose.
 *
 * $FreeBSD$
 */

#ifndef _BOOT_I386_ARGS_H_
#define	_BOOT_I386_ARGS_H_

#define	KARGS_FLAGS_CD		0x0001	/* .bootdev is a bios CD dev */
#define	KARGS_FLAGS_PXE		0x0002	/* .pxeinfo is valid */
#define	KARGS_FLAGS_ZFS		0x0004	/* .zfspool is valid, EXTARG is zfs_boot_args */
#define	KARGS_FLAGS_EXTARG	0x0008	/* variably sized extended argument */
#define	KARGS_FLAGS_GELI	0x0010	/* EXTARG is geli_boot_args */

#define	BOOTARGS_SIZE	24	/* sizeof(struct bootargs) */
#define	BA_BOOTFLAGS	8	/* offsetof(struct bootargs, bootflags) */
#define	BA_BOOTINFO	20	/* offsetof(struct bootargs, bootinfo) */
#define	BI_SIZE		48	/* offsetof(struct bootinfo, bi_size) */

/*
 * We reserve some space above BTX allocated stack for the arguments
 * and certain data that could hang off them.  Currently only struct bootinfo
 * is supported in that category.  The bootinfo is placed at the top
 * of the arguments area and the actual arguments are placed at ARGOFF offset
 * from the top and grow towards the top.  Hopefully we have enough space
 * for bootinfo and the arguments to not run into each other.
 * Arguments area below ARGOFF is reserved for future use.
 */
#define	ARGSPACE	0x1000	/* total size of the BTX args area */
#define	ARGOFF		0x800	/* actual args offset within the args area */
#define	ARGADJ		(ARGSPACE - ARGOFF)

#ifndef __ASSEMBLER__

/*
 * This struct describes the contents of the stack on entry to btxldr.S.  This
 * is the data that follows the return address, so it begins at 4(%esp).  On
 * the sending side, this data is passed as individual args to __exec().  On the
 * receiving side, code in btxldr.S copies the data from the entry stack to a
 * known fixed location in the new address space.  Then, btxcsu.S sets the
 * global variable __args to point to that known fixed location before calling
 * main(), which casts __args to a struct bootargs pointer to access the data.
 * The btxldr.S code is aware of KARGS_FLAGS_EXTARG, and if it's set, the extra
 * args data is copied along with the other bootargs from the entry stack to the
 * fixed location in the new address space.
 *
 * The bootinfo field is actually a pointer to a bootinfo struct that has been
 * converted to uint32_t using VTOP().  On the receiving side it must be
 * converted back to a pointer using PTOV().  Code in btxldr.S is aware of this
 * field and if it's non-NULL it copies the data it points to into another known
 * fixed location, and adjusts the bootinfo field to point to that new location.
 */
struct bootargs
{
	uint32_t			howto;
	uint32_t			bootdev;
	uint32_t			bootflags;
	union {
		struct {
			uint32_t	pxeinfo;
			uint32_t	reserved;
		};
		uint64_t		zfspool;
	};
	uint32_t			bootinfo;

	/*
	 * If KARGS_FLAGS_EXTARG is set in bootflags, then the above fields
	 * are followed by a uint32_t field that specifies a size of the
	 * extended arguments (including the size field).
	 */
};

#ifdef LOADER_GELI_SUPPORT
#include <crypto/intake.h>
#include "geliboot.h"
#endif

/*
 * geli_boot_data is embedded in geli_boot_args (passed from gptboot to loader)
 * and in zfs_boot_args (passed from zfsboot and gptzfsboot to loader).
 */
struct geli_boot_data
{
    union {
        char            gelipw[256];
        struct {
            char                notapw;	/* 
					 * single null byte to stop keybuf
					 * being interpreted as a password
					 */
            uint32_t            keybuf_sentinel;
#ifdef LOADER_GELI_SUPPORT
            struct keybuf       *keybuf;
#else
            void                *keybuf;
#endif
        };
    };
};

#ifdef LOADER_GELI_SUPPORT

static inline void
export_geli_boot_data(struct geli_boot_data *gbdata)
{

	gbdata->notapw = '\0';
	gbdata->keybuf_sentinel = KEYBUF_SENTINEL;
	gbdata->keybuf = malloc(sizeof(struct keybuf) +
	    (GELI_MAX_KEYS * sizeof(struct keybuf_ent)));
	geli_export_key_buffer(gbdata->keybuf);
}

static inline void
import_geli_boot_data(struct geli_boot_data *gbdata)
{

	if (gbdata->gelipw[0] != '\0') {
	    setenv("kern.geom.eli.passphrase", gbdata->gelipw, 1);
	    explicit_bzero(gbdata->gelipw, sizeof(gbdata->gelipw));
	} else if (gbdata->keybuf_sentinel == KEYBUF_SENTINEL) {
	    geli_import_key_buffer(gbdata->keybuf);
	}
}
#endif /* LOADER_GELI_SUPPORT */

struct geli_boot_args
{
	uint32_t		size;
	struct geli_boot_data	gelidata;
};

struct zfs_boot_args
{
	uint32_t		size;
	uint32_t		reserved;
	uint64_t		pool;
	uint64_t		root;
	uint64_t		primary_pool;
	uint64_t		primary_vdev;
	struct geli_boot_data	gelidata;
};

#endif /*__ASSEMBLER__*/

#endif	/* !_BOOT_I386_ARGS_H_ */
