/*
 * File:         include/asm-blackfin/mach-bf533/anomaly.h
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
 *  - Revision U, May 17, 2006; ADSP-BF533 Blackfin Processor Anomaly List
 *  - Revision Y, May 17, 2006; ADSP-BF532 Blackfin Processor Anomaly List
 *  - Revision T, May 17, 2006; ADSP-BF531 Blackfin Processor Anomaly List
 */

#ifndef _MACH_ANOMALY_H_
#define _MACH_ANOMALY_H_

/* We do not support 0.1 or 0.2 silicon - sorry */
#if (defined(CONFIG_BF_REV_0_1) || defined(CONFIG_BF_REV_0_2))
#error Kernel will not work on BF533 Version 0.1 or 0.2
#endif

/* Issues that are common to 0.5, 0.4, and 0.3 silicon */
#if  (defined(CONFIG_BF_REV_0_5) || defined(CONFIG_BF_REV_0_4) || defined(CONFIG_BF_REV_0_3))
#define ANOMALY_05000074 /* A multi issue instruction with dsp32shiftimm in
                            slot1 and store of a P register in slot 2 is not
                            supported */
#define ANOMALY_05000105 /* Watchpoint Status Register (WPSTAT) bits are set on
                            every corresponding match */
#define ANOMALY_05000119 /* DMA_RUN bit is not valid after a Peripheral Receive
                            Channel DMA stops */
#define ANOMALY_05000122 /* Rx.H can not be used to access 16-bit System MMR
                            registers. */
#define ANOMALY_05000166 /* PPI Data Lengths Between 8 and 16 do not zero out
                            upper bits*/
#define ANOMALY_05000167 /* Turning Serial Ports on With External Frame Syncs */
#define ANOMALY_05000180 /* PPI_DELAY not functional in PPI modes with 0 frame
                            syncs */
#define ANOMALY_05000208 /* VSTAT status bit in PLL_STAT register is not
                            functional */
#define ANOMALY_05000219 /* NMI event at boot time results in unpredictable
                            state */
#define ANOMALY_05000229 /* SPI Slave Boot Mode modifies registers */
#define ANOMALY_05000272 /* Certain data cache write through modes fail for
                            VDDint <=0.9V */
#define ANOMALY_05000273 /* Writes to Synchronous SDRAM memory may be lost */
#define ANOMALY_05000277 /* Writes to a flag data register one SCLK cycle after
                            an edge is detected may clear interrupt */
#define ANOMALY_05000278 /* Disabling Peripherals with DMA running may cause
                            DMA system instability */
#define ANOMALY_05000281 /* False Hardware Error Exception when ISR context is
                            not restored */
#define ANOMALY_05000282 /* Memory DMA corruption with 32-bit data and traffic
                            control */
#define ANOMALY_05000283 /* A system MMR write is stalled indefinitely when
                            killed in a particular stage*/
#define ANOMALY_05000312 /* Errors when SSYNC, CSYNC, or loads to LT, LB and LC
			    registers are interrupted */
#define ANOMALY_05000311 /* Erroneous flag pin operations under specific sequences*/

#endif

/* These issues only occur on 0.3 or 0.4 BF533 */
#if (defined(CONFIG_BF_REV_0_4) || defined(CONFIG_BF_REV_0_3))
#define ANOMALY_05000099 /* UART Line Status Register (UART_LSR) bits are not
                            updated at the same time. */
#define ANOMALY_05000158 /* Boot fails when data cache enabled: Data from a Data
        		    Cache Fill can be corrupted after or during
                            Instruction DMA if certain core stalls exist */
#define ANOMALY_05000179 /* PPI_COUNT cannot be programmed to 0 in General
                            Purpose TX or RX modes */
#define ANOMALY_05000198 /* Failing SYSTEM MMR accesses when stalled by
                            preceding memory read */
#define ANOMALY_05000200 /* SPORT TFS and DT are incorrectly driven during
                            inactive channels in certain conditions */
#define ANOMALY_05000202 /* Possible infinite stall with specific dual dag
                            situation */
#define ANOMALY_05000215 /* UART TX Interrupt masked erroneously */
#define ANOMALY_05000225 /* Incorrect pulse-width of UART start-bit */
#define ANOMALY_05000227 /* Scratchpad memory bank reads may return incorrect
                            data*/
#define ANOMALY_05000230 /* UART Receiver is less robust against Baudrate
                            Differences in certain Conditions */
#define ANOMALY_05000231 /* UART STB bit incorrectly affects receiver setting */
#define ANOMALY_05000242 /* DF bit in PLL_CTL register does not respond to
                            hardware reset */
#define ANOMALY_05000244 /* With instruction cache enabled, a CSYNC or SSYNC or
                            IDLE around a Change of Control causes
                            unpredictable results */
#define ANOMALY_05000245 /* Spurious Hardware Error from an access in the
                            shadow of a conditional branch */
#define ANOMALY_05000246 /* Data CPLB's should prevent spurious hardware
                            errors */
#define ANOMALY_05000253 /* Maximum external clock speed for Timers */
#define ANOMALY_05000255 /* Entering Hibernate Mode with RTC Seconds event
                            interrupt not functional */
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
#define ANOMALY_05000265 /* Sensitivity to noise with slow input edge rates on
                            SPORT external receive and transmit clocks. */
#define ANOMALY_05000269 /* High I/O activity causes the output voltage of the
                            internal voltage regulator (VDDint) to increase. */
#define ANOMALY_05000270 /* High I/O activity causes the output voltage of the
                            internal voltage regulator (VDDint) to decrease */
#endif

/* These issues are only on 0.4 silicon */
#if (defined(CONFIG_BF_REV_0_4))
#define ANOMALY_05000234 /* Incorrect Revision Number in DSPID Register */
#define ANOMALY_05000250 /* Incorrect Bit-Shift of Data Word in Multichannel
                            (TDM) */
#endif

/* These issues are only on 0.3 silicon */
#if defined(CONFIG_BF_REV_0_3)
#define ANOMALY_05000183 /* Timer Pin limitations for PPI TX Modes with
                            External Frame Syncs */
#define ANOMALY_05000189 /* False Protection Exceptions caused by Speculative
                            Instruction or Data Fetches, or by Fetches at the
                            boundary of reserved memory space */
#define ANOMALY_05000193 /* False Flag Pin Interrupts on Edge Sensitive Inputs
                            when polarity setting is changed */
#define ANOMALY_05000194 /* Sport Restarting in specific modes may cause data
                            corruption */
#define ANOMALY_05000199 /* DMA current address shows wrong value during carry
                            fix */
#define ANOMALY_05000201 /* Receive frame sync not ignored during active
                            frames in sport MCM */
#define ANOMALY_05000203 /* Specific sequence that can cause DMA error or DMA
                            stopping */
#if defined(CONFIG_BF533)
#define ANOMALY_05000204 /* Incorrect data read with write-through cache and
                            allocate cache lines on reads only mode */
#endif /* CONFIG_BF533 */
#define ANOMALY_05000207 /* Recovery from "brown-out" condition */
#define ANOMALY_05000209 /* Speed-Path in computational unit affects certain
                            instructions */
#define ANOMALY_05000233 /* PPI_FS3 is not driven in 2 or 3 internal Frame
                            Sync Transmit Mode */
#define ANOMALY_05000271 /* Spontaneous reset of Internal Voltage Regulator */
#endif

#endif /*  _MACH_ANOMALY_H_ */
