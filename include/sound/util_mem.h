#ifndef __SOUND_UTIL_MEM_H
#define __SOUND_UTIL_MEM_H
/*
 *  Copyright (C) 2000 Takashi Iwai <tiwai@suse.de>
 *
 *  Generic memory management routines for soundcard memory allocation
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

typedef struct snd_util_memblk snd_util_memblk_t;
typedef struct snd_util_memhdr snd_util_memhdr_t;
typedef unsigned int snd_util_unit_t;

/*
 * memory block
 */
struct snd_util_memblk {
	snd_util_unit_t size;		/* size of this block */
	snd_util_unit_t offset;		/* zero-offset of this block */
	struct list_head list;		/* link */
};

#define snd_util_memblk_argptr(blk)	(void*)((char*)(blk) + sizeof(snd_util_memblk_t))

/*
 * memory management information
 */
struct snd_util_memhdr {
	snd_util_unit_t size;		/* size of whole data */
	struct list_head block;		/* block linked-list header */
	int nblocks;			/* # of allocated blocks */
	snd_util_unit_t used;		/* used memory size */
	int block_extra_size;		/* extra data size of chunk */
	struct semaphore block_mutex;	/* lock */
};

/*
 * prototypes
 */
snd_util_memhdr_t *snd_util_memhdr_new(int memsize);
void snd_util_memhdr_free(snd_util_memhdr_t *hdr);
snd_util_memblk_t *snd_util_mem_alloc(snd_util_memhdr_t *hdr, int size);
int snd_util_mem_free(snd_util_memhdr_t *hdr, snd_util_memblk_t *blk);
int snd_util_mem_avail(snd_util_memhdr_t *hdr);

/* functions without mutex */
snd_util_memblk_t *__snd_util_mem_alloc(snd_util_memhdr_t *hdr, int size);
void __snd_util_mem_free(snd_util_memhdr_t *hdr, snd_util_memblk_t *blk);
snd_util_memblk_t *__snd_util_memblk_new(snd_util_memhdr_t *hdr, snd_util_unit_t units, struct list_head *prev);

#endif /* __SOUND_UTIL_MEM_H */
