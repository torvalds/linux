/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * This is the user-visible SGI GFX interface.
 *
 * This must be used verbatim into the GNU libc.  It does not include
 * any kernel-only bits on it.
 *
 * miguel@nuclecu.unam.mx
 */
#ifndef _ASM_GFX_H
#define _ASM_GFX_H

/* The iocls, yes, they do not make sense, but such is life */
#define GFX_BASE             100
#define GFX_GETNUM_BOARDS    (GFX_BASE + 1)
#define GFX_GETBOARD_INFO    (GFX_BASE + 2)
#define GFX_ATTACH_BOARD     (GFX_BASE + 3)
#define GFX_DETACH_BOARD     (GFX_BASE + 4)
#define GFX_IS_MANAGED       (GFX_BASE + 5)

#define GFX_MAPALL           (GFX_BASE + 10)
#define GFX_LABEL            (GFX_BASE + 11)

#define GFX_INFO_NAME_SIZE  16
#define GFX_INFO_LABEL_SIZE 16

struct gfx_info {
	char name  [GFX_INFO_NAME_SIZE];  /* board name */
	char label [GFX_INFO_LABEL_SIZE]; /* label name */
	unsigned short int xpmax, ypmax;  /* screen resolution */
	unsigned int lenght;	          /* size of a complete gfx_info for this board */
};

struct gfx_getboardinfo_args {
	unsigned int board;     /* board number.  starting from zero */
	void *buf;              /* pointer to gfx_info */
	unsigned int len;       /* buffer size of buf */
};

struct gfx_attach_board_args {
	unsigned int board;	/* board number, starting from zero */
	void        *vaddr;	/* address where the board registers should be mapped */
};

#ifdef __KERNEL__
/* umap.c */
extern void remove_mapping (struct vm_area_struct *vma, struct task_struct *, unsigned long, unsigned long);
extern void *vmalloc_uncached (unsigned long size);
extern int vmap_page_range (struct vm_area_struct *vma, unsigned long from, unsigned long size, unsigned long vaddr);
#endif

#endif /* _ASM_GFX_H */
