/*
 * linux/include/asm-arm/arch-omap/board-apollon.h
 *
 * Hardware definitions for Samsung OMAP24XX Apollon board.
 *
 * Initial creation by Kyungmin Park <kyungmin.park@samsung.com>
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

#ifndef __ASM_ARCH_OMAP_APOLLON_H
#define __ASM_ARCH_OMAP_APOLLON_H

/* Placeholder for APOLLON specific defines */
/* GPMC CS0 */
#define APOLLON_CS0_BASE		0x00000000
/* GPMC CS1 */
#define APOLLON_CS1_BASE		0x08000000
#define APOLLON_ETHR_START		(APOLLON_CS1_BASE + 0x300)
#define APOLLON_ETHR_GPIO_IRQ		74
/* GPMC CS2 - reserved for OneNAND */
#define APOLLON_CS2_BASE		0x10000000
/* GPMC CS3 - reserved for NOR or NAND */
#define APOLLON_CS3_BASE		0x18000000

#endif /*  __ASM_ARCH_OMAP_APOLLON_H */

