/*
 * linux/include/asm-arm/arch-omap/dsp_common.h
 *
 * Header for OMAP DSP subsystem control
 *
 * Copyright (C) 2004,2005 Nokia Corporation
 *
 * Written by Toshihiro Kobayashi <toshihiro.kobayashi@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * 2005/06/03:  DSP Gateway version 3.3
 */

#ifndef ASM_ARCH_DSP_COMMON_H
#define ASM_ARCH_DSP_COMMON_H

void omap_dsp_pm_suspend(void);
void omap_dsp_pm_resume(void);
void omap_dsp_request_mpui(void);
void omap_dsp_release_mpui(void);
int omap_dsp_request_mem(void);
int omap_dsp_release_mem(void);

#endif /* ASM_ARCH_DSP_COMMON_H */
