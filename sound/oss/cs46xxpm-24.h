/*******************************************************************************
*
*      "cs46xxpm-24.h" --  Cirrus Logic-Crystal CS46XX linux audio driver.
*
*      Copyright (C) 2000,2001  Cirrus Logic Corp.  
*            -- tom woller (twoller@crystal.cirrus.com) or
*               (pcaudio@crystal.cirrus.com).
*
*      This program is free software; you can redistribute it and/or modify
*      it under the terms of the GNU General Public License as published by
*      the Free Software Foundation; either version 2 of the License, or
*      (at your option) any later version.
*
*      This program is distributed in the hope that it will be useful,
*      but WITHOUT ANY WARRANTY; without even the implied warranty of
*      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*      GNU General Public License for more details.
*
*      You should have received a copy of the GNU General Public License
*      along with this program; if not, write to the Free Software
*      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
* 12/22/00 trw - new file. 
*
*******************************************************************************/
#ifndef __CS46XXPM24_H
#define __CS46XXPM24_H

#include <linux/pm.h>
#include "cs46xxpm.h"


#define CS46XX_ACPI_SUPPORT 1
#ifdef CS46XX_ACPI_SUPPORT
/* 
* for now (12/22/00) only enable the pm_register PM support.
* allow these table entries to be null.
*/
static int cs46xx_suspend_tbl(struct pci_dev *pcidev, pm_message_t state);
static int cs46xx_resume_tbl(struct pci_dev *pcidev);
#define cs_pm_register(a, b, c)  NULL
#define cs_pm_unregister_all(a) 
#define CS46XX_SUSPEND_TBL cs46xx_suspend_tbl
#define CS46XX_RESUME_TBL cs46xx_resume_tbl
#else
#define cs_pm_register(a, b, c) pm_register((a), (b), (c));
#define cs_pm_unregister_all(a) pm_unregister_all((a));
#define CS46XX_SUSPEND_TBL cs46xx_null
#define CS46XX_RESUME_TBL cs46xx_null
#endif

#endif
