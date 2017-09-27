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

#ifndef DT_RESET_OXSEMI_OX810SE_H
#define DT_RESET_OXSEMI_OX810SE_H

#define RESET_ARM	0
#define RESET_COPRO	1
/* Reserved		2 */
/* Reserved		3 */
#define RESET_USBHS	4
#define RESET_USBHSPHY	5
#define RESET_MAC	6
#define RESET_PCI	7
#define RESET_DMA	8
#define RESET_DPE	9
#define RESET_DDR	10
#define RESET_SATA	11
#define RESET_SATA_LINK	12
#define RESET_SATA_PHY	13
 /* Reserved		14 */
#define RESET_NAND	15
#define RESET_GPIO	16
#define RESET_UART1	17
#define RESET_UART2	18
#define RESET_MISC	19
#define RESET_I2S	20
#define RESET_AHB_MON	21
#define RESET_UART3	22
#define RESET_UART4	23
#define RESET_SGDMA	24
/* Reserved		25 */
/* Reserved		26 */
/* Reserved		27 */
/* Reserved		28 */
/* Reserved		29 */
/* Reserved		30 */
#define RESET_BUS	31

#endif /* DT_RESET_OXSEMI_OX810SE_H */
