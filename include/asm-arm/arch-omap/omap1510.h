/* linux/include/asm-arm/arch-omap/omap1510.h
 *
 * Hardware definitions for TI OMAP1510 processor.
 *
 * Cleanup for Linux-2.6 by Dirk Behme <dirk.behme@de.bosch.com>
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

#ifndef __ASM_ARCH_OMAP1510_H
#define __ASM_ARCH_OMAP1510_H

/*
 * ----------------------------------------------------------------------------
 * Base addresses
 * ----------------------------------------------------------------------------
 */

/* Syntax: XX_BASE = Virtual base address, XX_START = Physical base address */

#define OMAP1510_SRAM_BASE	0xD0000000
#define OMAP1510_SRAM_SIZE	(SZ_128K + SZ_64K)
#define OMAP1510_SRAM_START	0x20000000

#define OMAP1510_DSP_BASE	0xE0000000
#define OMAP1510_DSP_SIZE	0x28000
#define OMAP1510_DSP_START	0xE0000000

#define OMAP1510_DSPREG_BASE	0xE1000000
#define OMAP1510_DSPREG_SIZE	SZ_128K
#define OMAP1510_DSPREG_START	0xE1000000

/*
 * ----------------------------------------------------------------------------
 * Memory used by power management
 * ----------------------------------------------------------------------------
 */

#define OMAP1510_SRAM_IDLE_SUSPEND	(OMAP1510_SRAM_BASE + OMAP1510_SRAM_SIZE - 0x200)
#define OMAP1510_SRAM_API_SUSPEND	(OMAP1510_SRAM_IDLE_SUSPEND + 0x100)

#endif /*  __ASM_ARCH_OMAP1510_H */

