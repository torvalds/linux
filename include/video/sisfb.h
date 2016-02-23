/*
 * sisfb.h - definitions for the SiS framebuffer driver
 *
 * Copyright (C) 2001-2005 by Thomas Winischhofer, Vienna, Austria.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 */
#ifndef _LINUX_SISFB_H_
#define _LINUX_SISFB_H_


#include <linux/pci.h>
#include <uapi/video/sisfb.h>

#define	UNKNOWN_VGA  0
#define	SIS_300_VGA  1
#define	SIS_315_VGA  2

#define SISFB_HAVE_MALLOC_NEW
extern void sis_malloc(struct sis_memreq *req);
extern void sis_malloc_new(struct pci_dev *pdev, struct sis_memreq *req);

extern void sis_free(u32 base);
extern void sis_free_new(struct pci_dev *pdev, u32 base);
#endif
