/*
 * Linux WiMAX Stack
 * Debug levels control file for the wimax module
 *
 *
 * Copyright (C) 2007-2008 Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
#ifndef __debug_levels__h__
#define __debug_levels__h__

/* Maximum compile and run time debug level for all submodules */
#define D_MODULENAME wimax
#define D_MASTER CONFIG_WIMAX_DEBUG_LEVEL

#include <linux/wimax/debug.h>

/* List of all the enabled modules */
enum d_module {
	D_SUBMODULE_DECLARE(debugfs),
	D_SUBMODULE_DECLARE(id_table),
	D_SUBMODULE_DECLARE(op_msg),
	D_SUBMODULE_DECLARE(op_reset),
	D_SUBMODULE_DECLARE(op_rfkill),
	D_SUBMODULE_DECLARE(stack),
};

#endif /* #ifndef __debug_levels__h__ */
