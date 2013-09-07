/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/************************************************************************/
/*                                                                      */
/*  PROJECT : exFAT & FAT12/16/32 File System                           */
/*  FILE    : exfat_cache.c                                             */
/*  PURPOSE : exFAT Cache Manager                                       */
/*            (FAT Cache & Buffer Cache)                                */
/*                                                                      */
/*----------------------------------------------------------------------*/
/*  NOTES                                                               */
/*                                                                      */
/*----------------------------------------------------------------------*/
/*  REVISION HISTORY (Ver 0.9)                                          */
/*                                                                      */
/*  - 2010.11.15 [Sung-Kwan Kim] : first writing                        */
/*                                                                      */
/************************************************************************/

#include "exfat_config.h"
#include "exfat_global.h"
#include "exfat_data.h"

#include "exfat_cache.h"
#include "exfat_super.h"
#include "exfat.h"

/*----------------------------------------------------------------------*/
/*  Global Variable Definitions                                         */
/*----------------------------------------------------------------------*/

extern FS_STRUCT_T      fs_struct[];

#define sm_P(s)
#define sm_V(s)

static INT32 __FAT_read(struct super_block *sb, UINT32 loc, UINT32 *content);
static INT32 __FAT_write(struct super_block *sb, UINT32 loc, UINT32 content);

static BUF_CACHE_T *FAT_cache_find(struct super_block *sb, UINT32 sec);
static BUF_CACHE_T *FAT_cache_get(struct super_block *sb, UINT32 sec);
static void FAT_cache_insert_hash(struct super_block *sb, BUF_CACHE_T *bp);
static void FAT_cache_remove_hash(BUF_CACHE_T *bp);

static UINT8 *__buf_getblk(struct super_block *sb, UINT32 sec);

static BUF_CACHE_T *buf_cache_find(struct super_block *sb, UINT32 sec);
static BUF_CACHE_T *buf_cache_get(struct super_block *sb, UINT32 sec);
static void buf_cache_insert_hash(struct super_block *sb, BUF_CACHE_T *bp);
static void buf_cache_remove_hash(BUF_CACHE_T *bp);

static void push_to_mru(BUF_CACHE_T *bp, BUF_CACHE_T *list);
static void push_to_lru(BUF_CACHE_T *bp, BUF_CACHE_T *list);
static void move_to_mru(BUF_CACHE_T *bp, BUF_CACHE_T *list);
static void move_to_lru(BUF_CACHE_T *bp, BUF_CACHE_T *list);

/*======================================================================*/
/*  Cache Initialization Functions                                      */
/*======================================================================*/

INT32 buf_init(struct super_block *sb)
{
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	INT32 i;

	/* LRU list */
	p_fs->FAT_cache_lru_list.next = p_fs->FAT_cache_lru_list.prev = &p_fs->FAT_cache_lru_list;

	for (i = 0; i < FAT_CACHE_SIZE; i++) {
		p_fs->FAT_cache_array[i].drv = -1;
		p_fs->FAT_cache_array[i].sec = ~0;
		p_fs->FAT_cache_array[i].flag = 0;
		p_fs->FAT_cache_array[i].buf_bh = NULL;
		p_fs->FAT_cache_array[i].prev = p_fs->FAT_cache_array[i].next = NULL;
		push_to_mru(&(p_fs->FAT_cache_array[i]), &p_fs->FAT_cache_lru_list);
	}

	p_fs->buf_cache_lru_list.next = p_fs->buf_cache_lru_list.prev = &p_fs->buf_cache_lru_list;

	for (i = 0; i < BUF_CACHE_SIZE; i++) {
		p_fs->buf_cache_array[i].drv = -1;
		p_fs->buf_cache_array[i].sec = ~0;
		p_fs->buf_cache_array[i].flag = 0;
		p_fs->buf_cache_array[i].buf_bh = NULL;
		p_fs->buf_cache_array[i].prev = p_fs->buf_cache_array[i].next = NULL;
		push_to_mru(&(p_fs->buf_cache_array[i]), &p_fs->buf_cache_lru_list);
	}

	/* HASH list */
	for (i = 0; i < FAT_CACHE_HASH_SIZE; i++) {
		p_fs->FAT_cache_hash_list[i].drv = -1;
		p_fs->FAT_cache_hash_list[i].sec = ~0;
		p_fs->FAT_cache_hash_list[i].hash_next = p_fs->FAT_cache_hash_list[i].hash_prev = &(p_fs->FAT_cache_hash_list[i]);
	}

	for (i = 0; i < FAT_CACHE_SIZE; i++) {
		FAT_cache_insert_hash(sb, &(p_fs->FAT_cache_array[i]));
	}

	for (i = 0; i < BUF_CACHE_HASH_SIZE; i++) {
		p_fs->buf_cache_hash_list[i].drv = -1;
		p_fs->buf_cache_hash_list[i].sec = ~0;
		p_fs->buf_cache_hash_list[i].hash_next = p_fs->buf_cache_hash_list[i].hash_prev = &(p_fs->buf_cache_hash_list[i]);
	}

	for (i = 0; i < BUF_CACHE_SIZE; i++) {
		buf_cache_insert_hash(sb, &(p_fs->buf_cache_array[i]));
	}

	return(FFS_SUCCESS);
} /* end of buf_init */

INT32 buf_shutdown(struct super_block *sb)
{
	return(FFS_SUCCESS);
} /* end of buf_shutdown */

/*======================================================================*/
/*  FAT Read/Write Functions                                            */
/*======================================================================*/

/* in : sb, loc
  * out: content
  * returns 0 on success
  *            -1 on error
  */
INT32 FAT_read(struct super_block *sb, UINT32 loc, UINT32 *content)
{
	INT32 ret;

	sm_P(&f_sem);

	ret = __FAT_read(sb, loc, content);

	sm_V(&f_sem);

	return(ret);
} /* end of FAT_read */

INT32 FAT_write(struct super_block *sb, UINT32 loc, UINT32 content)
{
	INT32 ret;

	sm_P(&f_sem);

	ret = __FAT_write(sb, loc, content);

	sm_V(&f_sem);

	return(ret);
} /* end of FAT_write */

static INT32 __FAT_read(struct super_block *sb, UINT32 loc, UINT32 *content)
{
	INT32 off;
	UINT32 sec, _content;
	UINT8 *fat_sector, *fat_entry;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	if (p_fs->vol_type == FAT12) {
		sec = p_fs->FAT1_start_sector + ((loc + (loc >> 1)) >> p_bd->sector_size_bits);
		off = (loc + (loc >> 1)) & p_bd->sector_size_mask;

		if (off == (p_bd->sector_size-1)) {
			fat_sector = FAT_getblk(sb, sec);
			if (!fat_sector)
				return -1;

			_content  = (UINT32) fat_sector[off];

			fat_sector = FAT_getblk(sb, ++sec);
			if (!fat_sector)
				return -1;

			_content |= (UINT32) fat_sector[0] << 8;
		} else {
			fat_sector = FAT_getblk(sb, sec);
			if (!fat_sector)
				return -1;

			fat_entry = &(fat_sector[off]);
			_content = GET16(fat_entry);
		}

		if (loc & 1) _content >>= 4;

		_content &= 0x00000FFF;

		if (_content >= CLUSTER_16(0x0FF8)) {
			*content = CLUSTER_32(~0);
			return 0;
		} else {
			*content = CLUSTER_32(_content);
			return 0;
		}
	} else if (p_fs->vol_type == FAT16) {
		sec = p_fs->FAT1_start_sector + (loc >> (p_bd->sector_size_bits-1));
		off = (loc << 1) & p_bd->sector_size_mask;

		fat_sector = FAT_getblk(sb, sec);
		if (!fat_sector)
			return -1;

		fat_entry = &(fat_sector[off]);

		_content = GET16_A(fat_entry);

		_content &= 0x0000FFFF;

		if (_content >= CLUSTER_16(0xFFF8)) {
			*content = CLUSTER_32(~0);
			return 0;
		} else {
			*content = CLUSTER_32(_content);
			return 0;
		}
	} else if (p_fs->vol_type == FAT32) {
		sec = p_fs->FAT1_start_sector + (loc >> (p_bd->sector_size_bits-2));
		off = (loc << 2) & p_bd->sector_size_mask;

		fat_sector = FAT_getblk(sb, sec);
		if (!fat_sector)
			return -1;

		fat_entry = &(fat_sector[off]);

		_content = GET32_A(fat_entry);

		_content &= 0x0FFFFFFF;

		if (_content >= CLUSTER_32(0x0FFFFFF8)) {
			*content = CLUSTER_32(~0);
			return 0;
		} else {
			*content = CLUSTER_32(_content);
			return 0;
		}
	} else {
		sec = p_fs->FAT1_start_sector + (loc >> (p_bd->sector_size_bits-2));
		off = (loc << 2) & p_bd->sector_size_mask;

		fat_sector = FAT_getblk(sb, sec);
		if (!fat_sector)
			return -1;

		fat_entry = &(fat_sector[off]);
		_content = GET32_A(fat_entry);

		if (_content >= CLUSTER_32(0xFFFFFFF8)) {
			*content = CLUSTER_32(~0);
			return 0;
		} else {
			*content = CLUSTER_32(_content);
			return 0;
		}
	}

	*content = CLUSTER_32(~0);
	return 0;
} /* end of __FAT_read */

static INT32 __FAT_write(struct super_block *sb, UINT32 loc, UINT32 content)
{
	INT32 off;
	UINT32 sec;
	UINT8 *fat_sector, *fat_entry;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	if (p_fs->vol_type == FAT12) {

		content &= 0x00000FFF;

		sec = p_fs->FAT1_start_sector + ((loc + (loc >> 1)) >> p_bd->sector_size_bits);
		off = (loc + (loc >> 1)) & p_bd->sector_size_mask;

		fat_sector = FAT_getblk(sb, sec);
		if (!fat_sector)
			return -1;

		if (loc & 1) { /* odd */

			content <<= 4;

			if (off == (p_bd->sector_size-1)) {
				fat_sector[off] = (UINT8)(content | (fat_sector[off] & 0x0F));
				FAT_modify(sb, sec);

				fat_sector = FAT_getblk(sb, ++sec);
				if (!fat_sector)
					return -1;

				fat_sector[0] = (UINT8)(content >> 8);
			} else {
				fat_entry = &(fat_sector[off]);
				content |= GET16(fat_entry) & 0x000F;

				SET16(fat_entry, content);
			}
		} else { /* even */
			fat_sector[off] = (UINT8)(content);

			if (off == (p_bd->sector_size-1)) {
				fat_sector[off] = (UINT8)(content);
				FAT_modify(sb, sec);

				fat_sector = FAT_getblk(sb, ++sec);
				fat_sector[0] = (UINT8)((fat_sector[0] & 0xF0) | (content >> 8));
			} else {
				fat_entry = &(fat_sector[off]);
				content |= GET16(fat_entry) & 0xF000;

				SET16(fat_entry, content);
			}
		}
	}

	else if (p_fs->vol_type == FAT16) {

		content &= 0x0000FFFF;

		sec = p_fs->FAT1_start_sector + (loc >> (p_bd->sector_size_bits-1));
		off = (loc << 1) & p_bd->sector_size_mask;

		fat_sector = FAT_getblk(sb, sec);
		if (!fat_sector)
			return -1;

		fat_entry = &(fat_sector[off]);

		SET16_A(fat_entry, content);
	}

	else if (p_fs->vol_type == FAT32) {

		content &= 0x0FFFFFFF;

		sec = p_fs->FAT1_start_sector + (loc >> (p_bd->sector_size_bits-2));
		off = (loc << 2) & p_bd->sector_size_mask;

		fat_sector = FAT_getblk(sb, sec);
		if (!fat_sector)
			return -1;

		fat_entry = &(fat_sector[off]);

		content |= GET32_A(fat_entry) & 0xF0000000;

		SET32_A(fat_entry, content);
	}

	else { /* p_fs->vol_type == EXFAT */

		sec = p_fs->FAT1_start_sector + (loc >> (p_bd->sector_size_bits-2));
		off = (loc << 2) & p_bd->sector_size_mask;

		fat_sector = FAT_getblk(sb, sec);
		if (!fat_sector)
			return -1;

		fat_entry = &(fat_sector[off]);

		SET32_A(fat_entry, content);
	}

	FAT_modify(sb, sec);
	return 0;
} /* end of __FAT_write */

UINT8 *FAT_getblk(struct super_block *sb, UINT32 sec)
{
	BUF_CACHE_T *bp;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	bp = FAT_cache_find(sb, sec);
	if (bp != NULL) {
		move_to_mru(bp, &p_fs->FAT_cache_lru_list);
		return(bp->buf_bh->b_data);
	}

	bp = FAT_cache_get(sb, sec);

	FAT_cache_remove_hash(bp);

	bp->drv = p_fs->drv;
	bp->sec = sec;
	bp->flag = 0;

	FAT_cache_insert_hash(sb, bp);

	if (sector_read(sb, sec, &(bp->buf_bh), 1) != FFS_SUCCESS) {
		FAT_cache_remove_hash(bp);
		bp->drv = -1;
		bp->sec = ~0;
		bp->flag = 0;
		bp->buf_bh = NULL;

		move_to_lru(bp, &p_fs->FAT_cache_lru_list);
		return NULL;
	}

	return(bp->buf_bh->b_data);
} /* end of FAT_getblk */

void FAT_modify(struct super_block *sb, UINT32 sec)
{
	BUF_CACHE_T *bp;

	bp = FAT_cache_find(sb, sec);
	if (bp != NULL) {
		sector_write(sb, sec, bp->buf_bh, 0);
	}
} /* end of FAT_modify */

void FAT_release_all(struct super_block *sb)
{
	BUF_CACHE_T *bp;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	sm_P(&f_sem);

	bp = p_fs->FAT_cache_lru_list.next;
	while (bp != &p_fs->FAT_cache_lru_list) {
		if (bp->drv == p_fs->drv) {
			bp->drv = -1;
			bp->sec = ~0;
			bp->flag = 0;

			if(bp->buf_bh) {
				__brelse(bp->buf_bh);
				bp->buf_bh = NULL;
			}
		}
		bp = bp->next;
	}

	sm_V(&f_sem);
} /* end of FAT_release_all */

void FAT_sync(struct super_block *sb)
{
	BUF_CACHE_T *bp;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	sm_P(&f_sem);

	bp = p_fs->FAT_cache_lru_list.next;
	while (bp != &p_fs->FAT_cache_lru_list) {
		if ((bp->drv == p_fs->drv) && (bp->flag & DIRTYBIT)) {
			sync_dirty_buffer(bp->buf_bh);
			bp->flag &= ~(DIRTYBIT);
		}
		bp = bp->next;
	}

	sm_V(&f_sem);
} /* end of FAT_sync */

static BUF_CACHE_T *FAT_cache_find(struct super_block *sb, UINT32 sec)
{
	INT32 off;
	BUF_CACHE_T *bp, *hp;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	off = (sec + (sec >> p_fs->sectors_per_clu_bits)) & (FAT_CACHE_HASH_SIZE - 1);

	hp = &(p_fs->FAT_cache_hash_list[off]);
	for (bp = hp->hash_next; bp != hp; bp = bp->hash_next) {
		if ((bp->drv == p_fs->drv) && (bp->sec == sec)) {

			WARN(!bp->buf_bh, "[EXFAT] FAT_cache has no bh. "
					  "It will make system panic.\n");

			touch_buffer(bp->buf_bh);
			return(bp);
		}
	}
	return(NULL);
} /* end of FAT_cache_find */

static BUF_CACHE_T *FAT_cache_get(struct super_block *sb, UINT32 sec)
{
	BUF_CACHE_T *bp;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	bp = p_fs->FAT_cache_lru_list.prev;


	move_to_mru(bp, &p_fs->FAT_cache_lru_list);
	return(bp);
} /* end of FAT_cache_get */

static void FAT_cache_insert_hash(struct super_block *sb, BUF_CACHE_T *bp)
{
	INT32 off;
	BUF_CACHE_T *hp;
	FS_INFO_T *p_fs;

	p_fs = &(EXFAT_SB(sb)->fs_info);
	off = (bp->sec + (bp->sec >> p_fs->sectors_per_clu_bits)) & (FAT_CACHE_HASH_SIZE-1);

	hp = &(p_fs->FAT_cache_hash_list[off]);
	bp->hash_next = hp->hash_next;
	bp->hash_prev = hp;
	hp->hash_next->hash_prev = bp;
	hp->hash_next = bp;
} /* end of FAT_cache_insert_hash */

static void FAT_cache_remove_hash(BUF_CACHE_T *bp)
{
	(bp->hash_prev)->hash_next = bp->hash_next;
	(bp->hash_next)->hash_prev = bp->hash_prev;
} /* end of FAT_cache_remove_hash */

/*======================================================================*/
/*  Buffer Read/Write Functions                                         */
/*======================================================================*/

UINT8 *buf_getblk(struct super_block *sb, UINT32 sec)
{
	UINT8 *buf;

	sm_P(&b_sem);

	buf = __buf_getblk(sb, sec);

	sm_V(&b_sem);

	return(buf);
} /* end of buf_getblk */

static UINT8 *__buf_getblk(struct super_block *sb, UINT32 sec)
{
	BUF_CACHE_T *bp;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	bp = buf_cache_find(sb, sec);
	if (bp != NULL) {
		move_to_mru(bp, &p_fs->buf_cache_lru_list);
		return(bp->buf_bh->b_data);
	}

	bp = buf_cache_get(sb, sec);

	buf_cache_remove_hash(bp);

	bp->drv = p_fs->drv;
	bp->sec = sec;
	bp->flag = 0;

	buf_cache_insert_hash(sb, bp);

	if (sector_read(sb, sec, &(bp->buf_bh), 1) != FFS_SUCCESS) {
		buf_cache_remove_hash(bp);
		bp->drv = -1;
		bp->sec = ~0;
		bp->flag = 0;
		bp->buf_bh = NULL;

		move_to_lru(bp, &p_fs->buf_cache_lru_list);
		return NULL;
	}

	return(bp->buf_bh->b_data);

} /* end of __buf_getblk */

void buf_modify(struct super_block *sb, UINT32 sec)
{
	BUF_CACHE_T *bp;

	sm_P(&b_sem);

	bp = buf_cache_find(sb, sec);
	if (likely(bp != NULL)) {
		sector_write(sb, sec, bp->buf_bh, 0);
	}

	WARN(!bp, "[EXFAT] failed to find buffer_cache(sector:%u).\n", sec);

	sm_V(&b_sem);
} /* end of buf_modify */

void buf_lock(struct super_block *sb, UINT32 sec)
{
	BUF_CACHE_T *bp;

	sm_P(&b_sem);

	bp = buf_cache_find(sb, sec);
	if (likely(bp != NULL)) bp->flag |= LOCKBIT;

	WARN(!bp, "[EXFAT] failed to find buffer_cache(sector:%u).\n", sec);

	sm_V(&b_sem);
} /* end of buf_lock */

void buf_unlock(struct super_block *sb, UINT32 sec)
{
	BUF_CACHE_T *bp;

	sm_P(&b_sem);

	bp = buf_cache_find(sb, sec);
	if (likely(bp != NULL)) bp->flag &= ~(LOCKBIT);

	WARN(!bp, "[EXFAT] failed to find buffer_cache(sector:%u).\n", sec);

	sm_V(&b_sem);
} /* end of buf_unlock */

void buf_release(struct super_block *sb, UINT32 sec)
{
	BUF_CACHE_T *bp;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	sm_P(&b_sem);

	bp = buf_cache_find(sb, sec);
	if (likely(bp != NULL)) {
		bp->drv = -1;
		bp->sec = ~0;
		bp->flag = 0;

		if(bp->buf_bh) {
			__brelse(bp->buf_bh);
			bp->buf_bh = NULL;
		}

		move_to_lru(bp, &p_fs->buf_cache_lru_list);
	}

	sm_V(&b_sem);
} /* end of buf_release */

void buf_release_all(struct super_block *sb)
{
	BUF_CACHE_T *bp;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	sm_P(&b_sem);

	bp = p_fs->buf_cache_lru_list.next;
	while (bp != &p_fs->buf_cache_lru_list) {
		if (bp->drv == p_fs->drv) {
			bp->drv = -1;
			bp->sec = ~0;
			bp->flag = 0;

			if(bp->buf_bh) {
				__brelse(bp->buf_bh);
				bp->buf_bh = NULL;
			}
		}
		bp = bp->next;
	}

	sm_V(&b_sem);
} /* end of buf_release_all */

void buf_sync(struct super_block *sb)
{
	BUF_CACHE_T *bp;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	sm_P(&b_sem);

	bp = p_fs->buf_cache_lru_list.next;
	while (bp != &p_fs->buf_cache_lru_list) {
		if ((bp->drv == p_fs->drv) && (bp->flag & DIRTYBIT)) {
			sync_dirty_buffer(bp->buf_bh);
			bp->flag &= ~(DIRTYBIT);
		}
		bp = bp->next;
	}

	sm_V(&b_sem);
} /* end of buf_sync */

static BUF_CACHE_T *buf_cache_find(struct super_block *sb, UINT32 sec)
{
	INT32 off;
	BUF_CACHE_T *bp, *hp;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	off = (sec + (sec >> p_fs->sectors_per_clu_bits)) & (BUF_CACHE_HASH_SIZE - 1);

	hp = &(p_fs->buf_cache_hash_list[off]);
	for (bp = hp->hash_next; bp != hp; bp = bp->hash_next) {
		if ((bp->drv == p_fs->drv) && (bp->sec == sec)) {
			touch_buffer(bp->buf_bh);
			return(bp);
		}
	}
	return(NULL);
} /* end of buf_cache_find */

static BUF_CACHE_T *buf_cache_get(struct super_block *sb, UINT32 sec)
{
	BUF_CACHE_T *bp;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	bp = p_fs->buf_cache_lru_list.prev;
	while (bp->flag & LOCKBIT) bp = bp->prev;


	move_to_mru(bp, &p_fs->buf_cache_lru_list);
	return(bp);
} /* end of buf_cache_get */

static void buf_cache_insert_hash(struct super_block *sb, BUF_CACHE_T *bp)
{
	INT32 off;
	BUF_CACHE_T *hp;
	FS_INFO_T *p_fs;

	p_fs = &(EXFAT_SB(sb)->fs_info);
	off = (bp->sec + (bp->sec >> p_fs->sectors_per_clu_bits)) & (BUF_CACHE_HASH_SIZE-1);

	hp = &(p_fs->buf_cache_hash_list[off]);
	bp->hash_next = hp->hash_next;
	bp->hash_prev = hp;
	hp->hash_next->hash_prev = bp;
	hp->hash_next = bp;
} /* end of buf_cache_insert_hash */

static void buf_cache_remove_hash(BUF_CACHE_T *bp)
{
	(bp->hash_prev)->hash_next = bp->hash_next;
	(bp->hash_next)->hash_prev = bp->hash_prev;
} /* end of buf_cache_remove_hash */

/*======================================================================*/
/*  Local Function Definitions                                          */
/*======================================================================*/

static void push_to_mru(BUF_CACHE_T *bp, BUF_CACHE_T *list)
{
	bp->next = list->next;
	bp->prev = list;
	list->next->prev = bp;
	list->next = bp;
} /* end of buf_cache_push_to_mru */

static void push_to_lru(BUF_CACHE_T *bp, BUF_CACHE_T *list)
{
	bp->prev = list->prev;
	bp->next = list;
	list->prev->next = bp;
	list->prev = bp;
} /* end of buf_cache_push_to_lru */

static void move_to_mru(BUF_CACHE_T *bp, BUF_CACHE_T *list)
{
	bp->prev->next = bp->next;
	bp->next->prev = bp->prev;
	push_to_mru(bp, list);
} /* end of buf_cache_move_to_mru */

static void move_to_lru(BUF_CACHE_T *bp, BUF_CACHE_T *list)
{
	bp->prev->next = bp->next;
	bp->next->prev = bp->prev;
	push_to_lru(bp, list);
} /* end of buf_cache_move_to_lru */

/* end of exfat_cache.c */
