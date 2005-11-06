/*
 * Copyright (C) 2002 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#ifndef __ASM_SIBYTE_CARMEL_H
#define __ASM_SIBYTE_CARMEL_H

#include <linux/config.h>

#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_int.h>

#define SIBYTE_BOARD_NAME "Carmel"

#define GPIO_PHY_INTERRUPT      2
#define GPIO_NONMASKABLE_INT    3
#define GPIO_CF_INSERTED        6
#define GPIO_MONTEREY_RESET     7
#define GPIO_QUADUART_INT       8
#define GPIO_CF_INT             9
#define GPIO_FPGA_CCLK          10
#define GPIO_FPGA_DOUT          11
#define GPIO_FPGA_DIN           12
#define GPIO_FPGA_PGM           13
#define GPIO_FPGA_DONE          14
#define GPIO_FPGA_INIT          15

#define LEDS_CS                 2
#define LEDS_PHYS               0x100C0000
#define MLEDS_CS                3
#define MLEDS_PHYS              0x100A0000
#define UART_CS                 4
#define UART_PHYS               0x100D0000
#define ARAVALI_CS              5
#define ARAVALI_PHYS            0x11000000
#define IDE_CS                  6
#define IDE_PHYS                0x100B0000
#define ARAVALI2_CS             7
#define ARAVALI2_PHYS           0x100E0000

#if defined(CONFIG_SIBYTE_CARMEL)
#define K_GPIO_GB_IDE   9
#define K_INT_GB_IDE    (K_INT_GPIO_0 + K_GPIO_GB_IDE)
#endif


#endif /* __ASM_SIBYTE_CARMEL_H */
