/*
 * AGPGART module version 0.99
 * Copyright (C) 1999 Jeff Hartmann
 * Copyright (C) 1999 Precision Insight, Inc.
 * Copyright (C) 1999 Xi Graphics, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * JEFF HARTMANN, OR ANY OTHER CONTRIBUTORS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE 
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _UAPI_AGP_H
#define _UAPI_AGP_H

#define AGPIOC_BASE       'A'
#define AGPIOC_INFO       _IOR (AGPIOC_BASE, 0, struct agp_info*)
#define AGPIOC_ACQUIRE    _IO  (AGPIOC_BASE, 1)
#define AGPIOC_RELEASE    _IO  (AGPIOC_BASE, 2)
#define AGPIOC_SETUP      _IOW (AGPIOC_BASE, 3, struct agp_setup*)
#define AGPIOC_RESERVE    _IOW (AGPIOC_BASE, 4, struct agp_region*)
#define AGPIOC_PROTECT    _IOW (AGPIOC_BASE, 5, struct agp_region*)
#define AGPIOC_ALLOCATE   _IOWR(AGPIOC_BASE, 6, struct agp_allocate*)
#define AGPIOC_DEALLOCATE _IOW (AGPIOC_BASE, 7, int)
#define AGPIOC_BIND       _IOW (AGPIOC_BASE, 8, struct agp_bind*)
#define AGPIOC_UNBIND     _IOW (AGPIOC_BASE, 9, struct agp_unbind*)
#define AGPIOC_CHIPSET_FLUSH _IO (AGPIOC_BASE, 10)

#define AGP_DEVICE      "/dev/agpgart"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef __KERNEL__
#include <linux/types.h>
#include <stdlib.h>

struct agp_version {
	__u16 major;
	__u16 minor;
};

typedef struct _agp_info {
	struct agp_version version;	/* version of the driver        */
	__u32 bridge_id;	/* bridge vendor/device         */
	__u32 agp_mode;		/* mode info of bridge          */
	unsigned long aper_base;/* base of aperture             */
	size_t aper_size;	/* size of aperture             */
	size_t pg_total;	/* max pages (swap + system)    */
	size_t pg_system;	/* max pages (system)           */
	size_t pg_used;		/* current pages used           */
} agp_info;

typedef struct _agp_setup {
	__u32 agp_mode;		/* mode info of bridge          */
} agp_setup;

/*
 * The "prot" down below needs still a "sleep" flag somehow ...
 */
typedef struct _agp_segment {
	__kernel_off_t pg_start;	/* starting page to populate    */
	__kernel_size_t pg_count;	/* number of pages              */
	int prot;			/* prot flags for mmap          */
} agp_segment;

typedef struct _agp_region {
	__kernel_pid_t pid;		/* pid of process       */
	__kernel_size_t seg_count;	/* number of segments   */
	struct _agp_segment *seg_list;
} agp_region;

typedef struct _agp_allocate {
	int key;		/* tag of allocation            */
	__kernel_size_t pg_count;/* number of pages             */
	__u32 type;		/* 0 == normal, other devspec   */
   	__u32 physical;         /* device specific (some devices  
				 * need a phys address of the     
				 * actual page behind the gatt    
				 * table)                        */
} agp_allocate;

typedef struct _agp_bind {
	int key;		/* tag of allocation            */
	__kernel_off_t pg_start;/* starting page to populate    */
} agp_bind;

typedef struct _agp_unbind {
	int key;		/* tag of allocation            */
	__u32 priority;		/* priority for paging out      */
} agp_unbind;

#endif				/* __KERNEL__ */

#endif /* _UAPI_AGP_H */
