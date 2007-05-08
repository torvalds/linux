
/*
 * File:         include/asm-blackfin/mach-bf561/anomaly.h
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
 *  - Revision L, 10Aug2006; ADSP-BF561 Silicon Anomaly List
 */

#ifndef _MACH_ANOMALY_H_
#define _MACH_ANOMALY_H_

/* We do not support 0.1 or 0.4 silicon - sorry */
#if (defined(CONFIG_BF_REV_0_1) || defined(CONFIG_BF_REV_0_2) || defined(CONFIG_BF_REV_0_4))
#error Kernel will not work on BF561 Version 0.1, 0.2, or 0.4
#endif

/* Issues that are common to 0.5 and  0.3 silicon */
#if  (defined(CONFIG_BF_REV_0_5) || defined(CONFIG_BF_REV_0_3))
#define ANOMALY_05000074 /* A multi issue instruction with dsp32shiftimm in
                            slot1 and store of a P register in slot 2 is not
                            supported */
#define ANOMALY_05000099 /* UART Line Status Register (UART_LSR) bits are not
                            updated at the same time. */
#define ANOMALY_05000120 /* Testset instructions restricted to 32-bit aligned
                            memory locations */
#define ANOMALY_05000122 /* Rx.H cannot be used to access 16-bit System MMR
                            registers */
#define ANOMALY_05000127 /* Signbits instruction not functional under certain
                            conditions */
#define ANOMALY_05000149 /* IMDMA S1/D1 channel may stall */
#define ANOMALY_05000166 /* PPI Data Lengths Between 8 and 16 do not zero out
                            upper bits */
#define ANOMALY_05000167 /* Turning Serial Ports on With External Frame Syncs */
#define ANOMALY_05000180 /* PPI_DELAY not functional in PPI modes with 0 frame
                            syncs */
#define ANOMALY_05000182 /* IMDMA does not operate to full speed for 600MHz
                            and higher devices */
#define ANOMALY_05000187 /* IMDMA Corrupted Data after a Halt */
#define ANOMALY_05000190 /* PPI not functional at core voltage < 1Volt */
#define ANOMALY_05000208 /* VSTAT status bit in PLL_STAT register is not
                            functional */
#define ANOMALY_05000245 /* Spurious Hardware Error from an access in the
                            shadow of a conditional branch */
#define ANOMALY_05000257 /* Interrupt/Exception during short hardware loop
                            may cause bad instruction fetches */
#define ANOMALY_05000265 /* Sensitivity to noise with slow input edge rates on
                            external SPORT TX and RX clocks */
#define ANOMALY_05000267 /* IMDMA may corrupt data under certain conditions */
#define ANOMALY_05000269 /* High I/O activity causes output voltage of internal
                            voltage regulator (VDDint) to increase */
#define ANOMALY_05000270 /* High I/O activity causes output voltage of internal
                            voltage regulator (VDDint) to decrease */
#define ANOMALY_05000272 /* Certain data cache write through modes fail for
                            VDDint <=0.9V */
#define ANOMALY_05000274 /* Data cache write back to external synchronous memory
                            may be lost */
#define ANOMALY_05000275 /* PPI Timing and sampling informaton updates */
#define ANOMALY_05000312 /* Errors when SSYNC, CSYNC, or loads to LT, LB and LC
			    registers are interrupted */

#endif /*  (defined(CONFIG_BF_REV_0_5) || defined(CONFIG_BF_REV_0_3)) */

#if  (defined(CONFIG_BF_REV_0_5))
#define ANOMALY_05000254 /* Incorrect Timer Pulse Width in Single-Shot PWM_OUT
                            mode with external clock */
#define ANOMALY_05000266 /* IMDMA destination IRQ status must be read prior to
                            using IMDMA */
#endif

#if  (defined(CONFIG_BF_REV_0_3))
#define ANOMALY_05000156 /* Timers in PWM-Out Mode with PPI GP Receive (Input)
                            Mode with 0 Frame Syncs */
#define ANOMALY_05000168 /* SDRAM auto-refresh and subsequent Power Ups */
#define ANOMALY_05000169 /* DATA CPLB page miss can result in lost write-through
                            cache data writes */
#define ANOMALY_05000171 /* Boot-ROM code modifies SICA_IWRx wakeup registers */
#define ANOMALY_05000174 /* Cache Fill Buffer Data lost */
#define ANOMALY_05000175 /* Overlapping Sequencer and Memory Stalls */
#define ANOMALY_05000176 /* Multiplication of (-1) by (-1) followed by an
                            accumulator saturation */
#define ANOMALY_05000179 /* PPI_COUNT cannot be programmed to 0 in General
                            Purpose TX or RX modes */
#define ANOMALY_05000181 /* Disabling the PPI resets the PPI configuration
                            registers */
#define ANOMALY_05000184 /* Timer Pin limitations for PPI TX Modes with
                            External Frame Syncs */
#define ANOMALY_05000185 /* PPI TX Mode with 2 External Frame Syncs */
#define ANOMALY_05000186 /* PPI packing with Data Length greater than 8 bits
                            (not a meaningful mode) */
#define ANOMALY_05000188 /* IMDMA Restrictions on Descriptor and Buffer
                            Placement in Memory */
#define ANOMALY_05000189 /* False Protection Exception */
#define ANOMALY_05000193 /* False Flag Pin Interrupts on Edge Sensitive Inputs
                            when polarity setting is changed */
#define ANOMALY_05000194 /* Restarting SPORT in specific modes may cause data
                            corruption */
#define ANOMALY_05000198 /* Failing MMR accesses when stalled by preceding
                            memory read */
#define ANOMALY_05000199 /* DMA current address shows wrong value during carry
                            fix */
#define ANOMALY_05000200 /* SPORT TFS and DT are incorrectly driven during
                            inactive channels in certain conditions */
#define ANOMALY_05000202 /* Possible infinite stall with specific dual-DAG
                            situation */
#define ANOMALY_05000204 /* Incorrect data read with write-through cache and
                            allocate cache lines on reads only mode */
#define ANOMALY_05000205 /* Specific sequence that can cause DMA error or DMA
                            stopping */
#define ANOMALY_05000207 /* Recovery from "brown-out" condition */
#define ANOMALY_05000209 /* Speed-Path in computational unit affects certain
                            instructions */
#define ANOMALY_05000215 /* UART TX Interrupt masked erroneously */
#define ANOMALY_05000219 /* NMI event at boot time results in unpredictable
                            state */
#define ANOMALY_05000220 /* Data Corruption with Cached External Memory and
                            Non-Cached On-Chip L2 Memory */
#define ANOMALY_05000225 /* Incorrect pulse-width of UART start-bit */
#define ANOMALY_05000227 /* Scratchpad memory bank reads may return incorrect
                            data */
#define ANOMALY_05000230 /* UART Receiver is less robust against Baudrate
                            Differences in certain Conditions */
#define ANOMALY_05000231 /* UART STB bit incorrectly affects receiver setting */
#define ANOMALY_05000232 /* SPORT data transmit lines are incorrectly driven in
                            multichannel mode */
#define ANOMALY_05000242 /* DF bit in PLL_CTL register does not respond to
                            hardware reset */
#define ANOMALY_05000244 /* If i-cache is on, CSYNC/SSYNC/IDLE around Change of
                            Control causes failures */
#define ANOMALY_05000248 /* TESTSET operation forces stall on the other core */
#define ANOMALY_05000250 /* Incorrect Bit-Shift of Data Word in Multichannel
                            (TDM) mode in certain conditions */
#define ANOMALY_05000251 /* Exception not generated for MMR accesses in
                            reserved region */
#define ANOMALY_05000253 /* Maximum external clock speed for Timers */
#define ANOMALY_05000258 /* Instruction Cache is corrupted when bits 9 and 12
                            of the ICPLB Data registers differ */
#define ANOMALY_05000260 /* ICPLB_STATUS MMR register may be corrupted */
#define ANOMALY_05000261 /* DCPLB_FAULT_ADDR MMR register may be corrupted */
#define ANOMALY_05000262 /* Stores to data cache may be lost */
#define ANOMALY_05000263 /* Hardware loop corrupted when taking an ICPLB
                            exception */
#define ANOMALY_05000264 /* CSYNC/SSYNC/IDLE causes infinite stall in second
                            to last instruction in hardware loop */
#define ANOMALY_05000276 /* Timing requirements change for External Frame
                            Sync PPI Modes with non-zero PPI_DELAY */
#define ANOMALY_05000278 /* Disabling Peripherals with DMA running may cause
                            DMA system instability */
#define ANOMALY_05000281 /* False Hardware Error Exception when ISR context is
                            not restored */
#define ANOMALY_05000283 /* An MMR write is stalled indefinitely when killed
                            in a particular stage */
#define ANOMALY_05000287 /* A read will receive incorrect data under certain
                            conditions */
#define ANOMALY_05000288 /* SPORTs may receive bad data if FIFOs fill up */
#endif

#endif /* _MACH_ANOMALY_H_ */
