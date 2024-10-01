/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * sisfb.h - definitions for the SiS framebuffer driver
 *
 * Copyright (C) 2001-2005 by Thomas Winischhofer, Vienna, Austria.
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
