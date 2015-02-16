/*
 *	linux/mm/filemap_xip.c
 *
 * Copyright (C) 2005 IBM Corporation
 * Author: Carsten Otte <cotte@de.ibm.com>
 *
 * derived from linux/mm/filemap.c - Copyright (C) Linus Torvalds
 *
 */

#include <linux/fs.h>
#include <linux/backing-dev.h>
#include <linux/pagemap.h>
#include <linux/export.h>
#include <linux/uio.h>
#include <linux/rmap.h>
#include <linux/mmu_notifier.h>
#include <linux/sched.h>
#include <linux/seqlock.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <asm/tlbflush.h>
#include <asm/io.h>

