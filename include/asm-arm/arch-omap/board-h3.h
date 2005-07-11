/*
 * linux/include/asm-arm/arch-omap/board-h3.h
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Copyright (C) 2004 Texas Instruments, Inc.
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
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __ASM_ARCH_OMAP_H3_H
#define __ASM_ARCH_OMAP_H3_H

/* In OMAP1710 H3 the Ethernet is directly connected to CS1 */
#define OMAP1710_ETHR_START		0x04000300

/* Samsung NAND flash at CS2B or CS3(NAND Boot) */
#define OMAP_NAND_FLASH_START1           0x0A000000 /* CS2B */
#define OMAP_NAND_FLASH_START2           0x0C000000 /* CS3 */

#define MAXIRQNUM			(IH_BOARD_BASE)
#define MAXFIQNUM			MAXIRQNUM
#define MAXSWINUM			MAXIRQNUM

#define NR_IRQS				(MAXIRQNUM + 1)


#endif /*  __ASM_ARCH_OMAP_H3_H */
