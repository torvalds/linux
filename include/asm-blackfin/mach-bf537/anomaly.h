
/*
 * File:         include/asm-blackfin/mach-bf537/anomaly.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Rev:
 *
 * Modified:
 *
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* This file shoule be up to date with:
 *  - Revision J, June 1, 2006; ADSP-BF537 Blackfin Processor Anomaly List
 *  - Revision I, June 1, 2006; ADSP-BF536 Blackfin Processor Anomaly List
 *  - Revision J, June 1, 2006; ADSP-BF534 Blackfin Processor Anomaly List
 */

#ifndef _MACH_ANOMALY_H_
#define _MACH_ANOMALY_H_

/* We do not support 0.1 silicon - sorry */
#if (defined(CONFIG_BF_REV_0_1))
#error Kernel will not work on BF537/6/4 Version 0.1
#endif

#if (defined(CONFIG_BF_REV_0_3) || defined(CONFIG_BF_REV_0_2))
#define ANOMALY_05000074 /* A multi issue instruction with dsp32shiftimm in
                            slot1 and store of a P register in slot 2 is not
                            supported */
#define ANOMALY_05000119 /* DMA_RUN bit is not valid after a Peripheral Receive
                            Channel DMA stops */
#define ANOMALY_05000122 /* Rx.H can not be used to access 16-bit System MMR
                            registers. */
#define ANOMALY_05000166 /* PPI Data Lengths Between 8 and 16 do not zero out
                            upper bits*/
#define ANOMALY_05000180 /* PPI_DELAY not functional in PPI modes with 0 frame
                            syncs */
#if (defined(CONFIG_BF537) || defined(CONFIG_BF536))
#define ANOMALY_05000247 /* CLKIN Buffer Output Enable Reset Behavior Is
                            Changed */
#endif
#define ANOMALY_05000265 /* Sensitivity to noise with slow input edge rates on
                            SPORT external receive and transmit clocks. */
#define ANOMALY_05000272 /* Certain data cache write through modes fail for
                            VDDint <=0.9V */
#define ANOMALY_05000273 /* Writes to Synchronous SDRAM memory may be lost */
#define ANOMALY_05000277 /* Writes to a flag data register one SCLK cycle after
                            an edge is detected may clear interrupt */
#define ANOMALY_05000281 /* False Hardware Error Exception when ISR context is
                            not restored */
#define ANOMALY_05000282 /* Memory DMA corruption with 32-bit data and traffic
                            control */
#define ANOMALY_05000283 /* A system MMR write is stalled indefinitely when
                            killed in a particular stage*/
#define ANOMALY_05000312 /* Errors when SSYNC, CSYNC, or loads to LT, LB and LC
			    registers are interrupted */
#endif

#if defined(CONFIG_BF_REV_0_2)
#define ANOMALY_05000244 /* With instruction cache enabled, a CSYNC or SSYNC or
                            IDLE around a Change of Control causes
                            unpredictable results */
#define ANOMALY_05000250 /* Incorrect Bit-Shift of Data Word in Multichannel
                            (TDM) */
#if (defined(CONFIG_BF537) || defined(CONFIG_BF536))
#define ANOMALY_05000252 /* EMAC Tx DMA error after an early frame abort */
#endif
#define ANOMALY_05000253 /* Maximum external clock speed for Timers */
#define ANOMALY_05000255 /* Entering Hibernate Mode with RTC Seconds event
                            interrupt not functional */
#if (defined(CONFIG_BF537) || defined(CONFIG_BF536))
#define ANOMALY_05000256 /* EMAC MDIO input latched on wrong MDC edge */
#endif
#define ANOMALY_05000257 /* An interrupt or exception during short Hardware
                            loops may cause the instruction fetch unit to
                            malfunction */
#define ANOMALY_05000258 /* Instruction Cache is corrupted when bit 9 and 12 of
                            the ICPLB Data registers differ */
#define ANOMALY_05000260 /* ICPLB_STATUS MMR register may be corrupted */
#define ANOMALY_05000261 /* DCPLB_FAULT_ADDR MMR register may be corrupted */
#define ANOMALY_05000262 /* Stores to data cache may be lost */
#define ANOMALY_05000263 /* Hardware loop corrupted when taking an ICPLB exception */
#define ANOMALY_05000264 /* A Sync instruction (CSYNC, SSYNC) or an IDLE
                            instruction will cause an infinite stall in the
                            second to last instruction in a hardware loop */
#define ANOMALY_05000268 /* Memory DMA error when peripheral DMA is running
                            and non-zero DEB_TRAFFIC_PERIOD value */
#define ANOMALY_05000270 /* High I/O activity causes the output voltage of the
                            internal voltage regulator (VDDint) to decrease */
#define ANOMALY_05000277 /* Writes to a flag data register one SCLK cycle after
                            an edge is detected may clear interrupt */
#define ANOMALY_05000278 /* Disabling Peripherals with DMA running may cause
                            DMA system instability */
#define ANOMALY_05000280 /* SPI Master boot mode does not work well with
                            Atmel Dataflash devices */

#endif  /* CONFIG_BF_REV_0_2 */

#endif /* _MACH_ANOMALY_H_ */
