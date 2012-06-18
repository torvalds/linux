/*
 * kernel/power/console.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/*
 * drivers/power/process.c - Functions for saving/restoring console.
 *
 * Originally from swsusp.
 */

#include <linux/vt_kern.h>
#include <linux/kbd_kern.h>
#include <linux/vt.h>
#include <linux/module.h>
#include "power.h"

#if defined(CONFIG_VT) && defined(CONFIG_VT_CONSOLE)
#define SUSPEND_CONSOLE	(MAX_NR_CONSOLES-1)

static int orig_fgconsole, orig_kmsg;

int pm_prepare_console(void)
{
#if 0
	orig_fgconsole = vt_move_to_console(SUSPEND_CONSOLE, 1);
	if (orig_fgconsole < 0)
		return 1;

	orig_kmsg = vt_kmsg_redirect(SUSPEND_CONSOLE);
#endif
	return 0;
}

void pm_restore_console(void)
{
#if 0
	if (orig_fgconsole >= 0) {
		vt_move_to_console(orig_fgconsole, 0);
		vt_kmsg_redirect(orig_kmsg);
	}
#endif
}
#endif
