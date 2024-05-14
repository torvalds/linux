/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_UTIL_MEM_H
#define __SOUND_UTIL_MEM_H

#include <linux/mutex.h>
/*
 *  Copyright (C) 2000 Takashi Iwai <tiwai@suse.de>
 *
 *  Generic memory management routines for soundcard memory allocation
 */

/*
 * memory block
 */
struct snd_util_memblk {
	unsigned int size;		/* size of this block */
	unsigned int offset;		/* zero-offset of this block */
	struct list_head list;		/* link */
};

#define snd_util_memblk_argptr(blk)	(void*)((char*)(blk) + sizeof(struct snd_util_memblk))

/*
 * memory management information
 */
struct snd_util_memhdr {
	unsigned int size;		/* size of whole data */
	struct list_head block;		/* block linked-list header */
	int nblocks;			/* # of allocated blocks */
	unsigned int used;		/* used memory size */
	int block_extra_size;		/* extra data size of chunk */
	struct mutex block_mutex;	/* lock */
};

/*
 * prototypes
 */
struct snd_util_memhdr *snd_util_memhdr_new(int memsize);
void snd_util_memhdr_free(struct snd_util_memhdr *hdr);
struct snd_util_memblk *snd_util_mem_alloc(struct snd_util_memhdr *hdr, int size);
int snd_util_mem_free(struct snd_util_memhdr *hdr, struct snd_util_memblk *blk);
int snd_util_mem_avail(struct snd_util_memhdr *hdr);

/* functions without mutex */
struct snd_util_memblk *__snd_util_mem_alloc(struct snd_util_memhdr *hdr, int size);
void __snd_util_mem_free(struct snd_util_memhdr *hdr, struct snd_util_memblk *blk);
struct snd_util_memblk *__snd_util_memblk_new(struct snd_util_memhdr *hdr,
					      unsigned int units,
					      struct list_head *prev);

#endif /* __SOUND_UTIL_MEM_H */
