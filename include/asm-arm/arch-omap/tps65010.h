/* linux/include/asm-arm/arch-omap/tps65010.h
 *
 * Functions to access TPS65010 power management device.
 *
 * Copyright (C) 2004 Dirk Behme <dirk.behme@de.bosch.com>
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

#ifndef __ASM_ARCH_TPS65010_H
#define __ASM_ARCH_TPS65010_H

/*
 * ----------------------------------------------------------------------------
 * Macros used by exported functions
 * ----------------------------------------------------------------------------
 */

#define LED1  1
#define LED2  2
#define OFF   0
#define ON    1
#define BLINK 2
#define GPIO1 1
#define GPIO2 2
#define GPIO3 3
#define GPIO4 4
#define LOW   0
#define HIGH  1

/*
 * ----------------------------------------------------------------------------
 * Exported functions
 * ----------------------------------------------------------------------------
 */

/* Draw from VBUS:
 *   0 mA -- DON'T DRAW (might supply power instead)
 * 100 mA -- usb unit load (slowest charge rate)
 * 500 mA -- usb high power (fast battery charge)
 */
extern int tps65010_set_vbus_draw(unsigned mA);

/* tps65010_set_gpio_out_value parameter:
 * gpio:  GPIO1, GPIO2, GPIO3 or GPIO4
 * value: LOW or HIGH
 */
extern int tps65010_set_gpio_out_value(unsigned gpio, unsigned value);

/* tps65010_set_led parameter:
 * led:  LED1 or LED2
 * mode: ON, OFF or BLINK
 */
extern int tps65010_set_led(unsigned led, unsigned mode);

/* tps65010_set_low_pwr parameter:
 * mode: ON or OFF
 */
extern int tps65010_set_low_pwr(unsigned mode);

#endif /*  __ASM_ARCH_TPS65010_H */

