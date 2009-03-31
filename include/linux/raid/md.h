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
#include <linux/seq_file.h>

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

#endif /* CONFIG_MD */
#endif 

