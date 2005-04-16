/*
 *  linux/include/asm-arm/arch-omap/board-perseus2.h
 *
 *  Copyright 2003 by Texas Instruments Incorporated
 *    OMAP730 / Perseus2 support by Jean Pihet
 *
 * Copyright (C) 2001 RidgeRun, Inc. (http://www.ridgerun.com)
 * Author: RidgeRun, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __ASM_ARCH_OMAP_PERSEUS2_H
#define __ASM_ARCH_OMAP_PERSEUS2_H

#include <asm/arch/fpga.h>

#ifndef OMAP_SDRAM_DEVICE
#define OMAP_SDRAM_DEVICE		D256M_1X16_4B
#endif

/*
 * These definitions define an area of FLASH set aside
 * for the use of MTD/JFFS2. This is the area of flash
 * that a JFFS2 filesystem will reside which is mounted
 * at boot with the "root=/dev/mtdblock/0 rw"
 * command line option.
 */

/* Intel flash_0, partitioned as expected by rrload */
#define OMAP_FLASH_0_BASE	0xD8000000	/* VA */
#define OMAP_FLASH_0_START	0x00000000	/* PA */
#define OMAP_FLASH_0_SIZE	SZ_32M

#define MAXIRQNUM		IH_BOARD_BASE
#define MAXFIQNUM		MAXIRQNUM
#define MAXSWINUM		MAXIRQNUM

#define NR_IRQS			(MAXIRQNUM + 1)

#endif
