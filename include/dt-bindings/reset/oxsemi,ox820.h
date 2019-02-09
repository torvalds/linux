/*
 * Copyright (C) 2016 Neil Armstrong <narmstrong@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DT_RESET_OXSEMI_OX820_H
#define DT_RESET_OXSEMI_OX820_H

#define RESET_SCU	0
#define RESET_LEON	1
#define RESET_ARM0	2
#define RESET_ARM1	3
#define RESET_USBHS	4
#define RESET_USBPHYA	5
#define RESET_MAC	6
#define RESET_PCIEA	7
#define RESET_SGDMA	8
#define RESET_CIPHER	9
#define RESET_DDR	10
#define RESET_SATA	11
#define RESET_SATA_LINK	12
#define RESET_SATA_PHY	13
#define RESET_PCIEPHY	14
#define RESET_NAND	15
#define RESET_GPIO	16
#define RESET_UART1	17
#define RESET_UART2	18
#define RESET_MISC	19
#define RESET_I2S	20
#define RESET_SD	21
#define RESET_MAC_2	22
#define RESET_PCIEB	23
#define RESET_VIDEO	24
#define RESET_DDR_PHY	25
#define RESET_USBPHYB	26
#define RESET_USBDEV	27
/* Reserved		29 */
#define RESET_ARMDBG	29
#define RESET_PLLA	30
#define RESET_PLLB	31

#endif /* DT_RESET_OXSEMI_OX820_H */
