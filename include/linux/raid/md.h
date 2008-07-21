/*
   md.h : Multiple Devices driver for Linux
          Copyright (C) 1996-98 Ingo Molnar, Gadi Oxman
          Copyright (C) 1994-96 Marc ZYNGIER
	  <zyngier@ufr-info-p7.ibp.fr> or
	  <maz@gloups.fdn.fr>
	  
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#ifndef _MD_H
#define _MD_H

#include <linux/blkdev.h>
#include <linux/major.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/hdreg.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/smp_lock.h>
#include <linux/delay.h>
#include <net/checksum.h>
#include <linux/random.h>
#include <linux/kernel_stat.h>
#include <asm/io.h>
#include <linux/completion.h>
#include <linux/mempool.h>
#include <linux/list.h>
#include <linux/reboot.h>
#include <linux/vmalloc.h>
#include <linux/blkpg.h>
#include <linux/bio.h>

/*
 * 'md_p.h' holds the 'physical' layout of RAID devices
 * 'md_u.h' holds the user <=> kernel API
 *
 * 'md_k.h' holds kernel internal definitions
 */

#include <linux/raid/md_p.h>
#include <linux/raid/md_u.h>
#include <linux/raid/md_k.h>

#ifdef CONFIG_MD

/*
 * Different major versions are not compatible.
 * Different minor versions are only downward compatible.
 * Different patchlevel versions are downward and upward compatible.
 */
#define MD_MAJOR_VERSION                0
#define MD_MINOR_VERSION                90
/*
 * MD_PATCHLEVEL_VERSION indicates kernel functionality.
 * >=1 means different superblock formats are selectable using SET_ARRAY_INFO
 *     and major_version/minor_version accordingly
 * >=2 means that Internal bitmaps are supported by setting MD_SB_BITMAP_PRESENT
 *     in the super status byte
 * >=3 means that bitmap superblock version 4 is supported, which uses
 *     little-ending representation rather than host-endian
 */
#define MD_PATCHLEVEL_VERSION           3

extern int mdp_major;

extern int register_md_personality (struct mdk_personality *p);
extern int unregister_md_personality (struct mdk_personality *p);
extern mdk_thread_t * md_register_thread (void (*run) (mddev_t *mddev),
				mddev_t *mddev, const char *name);
extern void md_unregister_thread (mdk_thread_t *thread);
extern void md_wakeup_thread(mdk_thread_t *thread);
extern void md_check_recovery(mddev_t *mddev);
extern void md_write_start(mddev_t *mddev, struct bio *bi);
extern void md_write_end(mddev_t *mddev);
extern void md_handle_safemode(mddev_t *mddev);
extern void md_done_sync(mddev_t *mddev, int blocks, int ok);
extern void md_error (mddev_t *mddev, mdk_rdev_t *rdev);
extern void md_unplug_mddev(mddev_t *mddev);

extern void md_super_write(mddev_t *mddev, mdk_rdev_t *rdev,
			   sector_t sector, int size, struct page *page);
extern void md_super_wait(mddev_t *mddev);
extern int sync_page_io(struct block_device *bdev, sector_t sector, int size,
			struct page *page, int rw);
extern void md_do_sync(mddev_t *mddev);
extern void md_new_event(mddev_t *mddev);
extern void md_allow_write(mddev_t *mddev);
extern void md_wait_for_blocked_rdev(mdk_rdev_t *rdev, mddev_t *mddev);

#endif /* CONFIG_MD */
#endif 

