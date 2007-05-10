/*
 * This file is part of OMAP DSP driver (DSP Gateway version 3.3.1)
 *
 * Copyright (C) 2004-2006 Nokia Corporation. All rights reserved.
 *
 * Contact: Toshihiro Kobayashi <toshihiro.kobayashi@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef ASM_ARCH_DSP_COMMON_H
#define ASM_ARCH_DSP_COMMON_H

#ifdef CONFIG_ARCH_OMAP1
extern void omap_dsp_request_mpui(void);
extern void omap_dsp_release_mpui(void);
extern int omap_dsp_request_mem(void);
extern int omap_dsp_release_mem(void);
#endif

#endif /* ASM_ARCH_DSP_COMMON_H */
