/*
 * linux/include/asm-arm/arch-omap/board-osk.h
 *
 * Hardware definitions for TI OMAP5912 OSK board.
 *
 * Written by Dirk Behme <dirk.behme@de.bosch.com>
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

#ifndef __ASM_ARCH_OMAP_OSK_H
#define __ASM_ARCH_OMAP_OSK_H

/* At OMAP5912 OSK the Ethernet is directly connected to CS1 */
#define OMAP_OSK_ETHR_START		0x04800300

/* TPS65010 has four GPIOs.  nPG and LED2 can be treated like GPIOs with
 * alternate pin configurations for hardware-controlled blinking.
 */
#define OSK_TPS_GPIO_BASE		(OMAP_MAX_GPIO_LINES + 16 /* MPUIO */)
#	define OSK_TPS_GPIO_USB_PWR_EN	(OSK_TPS_GPIO_BASE + 0)
#	define OSK_TPS_GPIO_LED_D3	(OSK_TPS_GPIO_BASE + 1)
#	define OSK_TPS_GPIO_LAN_RESET	(OSK_TPS_GPIO_BASE + 2)
#	define OSK_TPS_GPIO_DSP_PWR_EN	(OSK_TPS_GPIO_BASE + 3)
#	define OSK_TPS_GPIO_LED_D9	(OSK_TPS_GPIO_BASE + 4)
#	define OSK_TPS_GPIO_LED_D2	(OSK_TPS_GPIO_BASE + 5)

#endif /*  __ASM_ARCH_OMAP_OSK_H */

