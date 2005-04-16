/*
 *  
 *  This file contains the register definitions for the Excalibur
 *  Timer TIMER00.
 *
 *  Copyright (C) 2001 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __TIMER00_H
#define __TIMER00_H

/*
 * Register definitions for the timers
 */


#define TIMER0_CR(BASE_ADDR) (TIMER00_TYPE (BASE_ADDR  + 0x00 ))
#define TIMER0_CR_B_MSK (0x20)
#define TIMER0_CR_B_OFST (0x5)
#define TIMER0_CR_S_MSK  (0x10)
#define TIMER0_CR_S_OFST (0x4)
#define TIMER0_CR_CI_MSK (0x08)
#define TIMER0_CR_CI_OFST (0x3)
#define TIMER0_CR_IE_MSK (0x04)
#define TIMER0_CR_IE_OFST (0x2)
#define TIMER0_CR_MODE_MSK (0x3)
#define TIMER0_CR_MODE_OFST (0)
#define TIMER0_CR_MODE_FREE (0)
#define TIMER0_CR_MODE_ONE  (1)
#define TIMER0_CR_MODE_INTVL (2)

#define TIMER0_SR(BASE_ADDR) (TIMER00_TYPE (BASE_ADDR  + 0x00 ))
#define TIMER0_SR_B_MSK (0x20)
#define TIMER0_SR_B_OFST (0x5)
#define TIMER0_SR_S_MSK  (0x10)
#define TIMER0_SR_S_OFST (0x4)
#define TIMER0_SR_CI_MSK (0x08)
#define TIMER0_SR_CI_OFST (0x3)
#define TIMER0_SR_IE_MSK (0x04)
#define TIMER0_SR_IE_OFST (0x2)
#define TIMER0_SR_MODE_MSK (0x3)
#define TIMER0_SR_MODE_OFST (0)
#define TIMER0_SR_MODE_FREE (0)
#define TIMER0_SR_MODE_ONE  (1)
#define TIMER0_SR_MODE_INTVL (2)

#define TIMER0_PRESCALE(BASE_ADDR) (TIMER00_TYPE (BASE_ADDR  + 0x010 ))
#define TIMER0_LIMIT(BASE_ADDR) (TIMER00_TYPE (BASE_ADDR  + 0x020 ))
#define TIMER0_READ(BASE_ADDR) (TIMER00_TYPE (BASE_ADDR  + 0x030 ))

#define TIMER1_CR(BASE_ADDR) (TIMER00_TYPE (BASE_ADDR  + 0x40 ))
#define TIMER1_CR_B_MSK (0x20)
#define TIMER1_CR_B_OFST (0x5)
#define TIMER1_CR_S_MSK  (0x10)
#define TIMER1_CR_S_OFST (0x4)
#define TIMER1_CR_CI_MSK (0x08)
#define TIMER1_CR_CI_OFST (0x3)
#define TIMER1_CR_IE_MSK (0x04)
#define TIMER1_CR_IE_OFST (0x2)
#define TIMER1_CR_MODE_MSK (0x3)
#define TIMER1_CR_MODE_OFST (0)
#define TIMER1_CR_MODE_FREE (0)
#define TIMER1_CR_MODE_ONE  (1)
#define TIMER1_CR_MODE_INTVL (2)

#define TIMER1_SR(BASE_ADDR) (TIMER00_TYPE (BASE_ADDR  + 0x40 ))
#define TIMER1_SR_B_MSK (0x20)
#define TIMER1_SR_B_OFST (0x5)
#define TIMER1_SR_S_MSK  (0x10)
#define TIMER1_SR_S_OFST (0x4)
#define TIMER1_SR_CI_MSK (0x08)
#define TIMER1_SR_CI_OFST (0x3)
#define TIMER1_SR_IE_MSK (0x04)
#define TIMER1_SR_IE_OFST (0x2)
#define TIMER1_SR_MODE_MSK (0x3)
#define TIMER1_SR_MODE_OFST (0)
#define TIMER1_SR_MODE_FREE (0)
#define TIMER1_SR_MODE_ONE  (1)
#define TIMER1_SR_MODE_INTVL (2)

#define TIMER1_PRESCALE(BASE_ADDR) (TIMER00_TYPE (BASE_ADDR  + 0x050 ))
#define TIMER1_LIMIT(BASE_ADDR) (TIMER00_TYPE (BASE_ADDR  + 0x060 ))
#define TIMER1_READ(BASE_ADDR) (TIMER00_TYPE (BASE_ADDR  + 0x070 ))

#endif /* __TIMER00_H */
