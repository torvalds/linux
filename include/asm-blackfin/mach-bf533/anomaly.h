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
#if  (defined(CONFIG_BF_REV_0_5) || defined(CONFIG_BF_REV_0_4) \
		|| defined(CONFIG_BF_REV_0_3))
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
#define ANOMALY_05000311 /* Erroneous flag pin operations under specific
			    sequences */
#define ANOMALY_05000312 /* Errors when SSYNC, CSYNC, or loads to LT, LB and LC
			    registers are interrupted */
#define ANOMALY_05000313 /* PPI Is Level-Sensitive on First Transfer  */
#define ANOMALY_05000315 /* Killed System MMR Write Completes Erroneously On
			  *  Next System MMR Access */
#define ANOMALY_05000319 /* Internal Voltage Regulator Values of 1.05V, 1.10V
			  *  and 1.15V Not Allowed for LQFP Packages */
#endif /* Issues that are common to 0.5, 0.4, and 0.3 silicon */

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
#endif /* issues only occur on 0.3 or 0.4 BF533 */

/* These issues are only on 0.4 silicon */
#if (defined(CONFIG_BF_REV_0_4))
#define ANOMALY_05000234 /* Incorrect Revision Number in DSPID Register */
#define ANOMALY_05000250 /* Incorrect Bit-Shift of Data Word in Multichannel
                            (TDM) */
#endif /* issues are only on 0.4 silicon */

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
#endif /* only on 0.3 silicon */

#if defined(CONFIG_BF_REV_0_2)
#define ANOMALY_05000067 /* Watchpoints (Hardware Breakpoints) are not
			  *  supported */
#define ANOMALY_05000109 /* Reserved bits in SYSCFG register not set at
			  *  power on */
#define ANOMALY_05000116 /* Trace Buffers may record discontinuities into
			  *  emulation mode and/or exception, NMI, reset
			  *  handlers */
#define ANOMALY_05000123 /* DTEST_COMMAND initiated memory access may be
			  *  incorrect if data cache or DMA is active */
#define ANOMALY_05000124 /* DMA Lock-up at CCLK to SCLK ratios of 4:1, 2:1,
			  *  or 1:1 */
#define ANOMALY_05000125 /* Erroneous exception when enabling cache */
#define ANOMALY_05000126 /* SPI clock polarity and phase bits incorrect
			  *  during booting */
#define ANOMALY_05000137 /* DMEM_CONTROL is not set on Reset */
#define ANOMALY_05000138 /* SPI boot will not complete if there is a zero fill
			  * block in the loader file */
#define ANOMALY_05000140 /* Allowing the SPORT RX FIFO to fill will cause an
			  *  overflow */
#define ANOMALY_05000141 /* An Infinite Stall occurs with a particular sequence
			  *  of consecutive dual dag events */
#define ANOMALY_05000142 /* Interrupts may be lost when a programmable input
			  *  flag is configured to be edge sensitive */
#define ANOMALY_05000143 /* A read from external memory may return a wrong
			  *  value with data cache enabled */
#define ANOMALY_05000144 /* DMA and TESTSET conflict when both are accessing
			  *  external memory */
#define ANOMALY_05000145 /* In PWM_OUT mode, you must enable the PPI block to
			  *  generate a waveform from PPI_CLK */
#define ANOMALY_05000146 /* MDMA may lose the first few words of a descriptor
			  *  chain */
#define ANOMALY_05000147 /* The source MDMA descriptor may stop with a DMA
			  *  Error */
#define ANOMALY_05000148 /* When booting from a 16-bit asynchronous memory
			  *  device, the upper 8-bits of each word must be
			  *  0x00 */
#define ANOMALY_05000153 /* Frame Delay in SPORT Multichannel Mode */
#define ANOMALY_05000154 /* SPORT TFS signal is active in Multi-channel mode
			  *  outside of valid channels */
#define ANOMALY_05000155 /* Timer1 can not be used for PWMOUT mode when a
			  *  certain PPI mode is in use */
#define ANOMALY_05000157 /* A killed 32-bit System MMR write will lead to
			  *  the next system MMR access thinking it should be
			  *  32-bit. */
#define ANOMALY_05000163 /* SPORT transmit data is not gated by external frame
			  *  sync in certain conditions */
#define ANOMALY_05000168 /* SDRAM auto-refresh and subsequent Power Ups */
#define ANOMALY_05000169 /* DATA CPLB page miss can result in lost
			  *  write-through cache data writes */
#define ANOMALY_05000173 /* DMA vs Core accesses to external memory */
#define ANOMALY_05000174 /* Cache Fill Buffer Data lost */
#define ANOMALY_05000175 /* Overlapping Sequencer and Memory Stalls */
#define ANOMALY_05000176 /* Multiplication of (-1) by (-1) followed by an
			  *  accumulator saturation */
#define ANOMALY_05000181 /* Disabling the PPI resets the PPI configuration
			  *  registers */
#define ANOMALY_05000185 /* PPI TX Mode with 2 External Frame Syncs */
#define ANOMALY_05000191 /* PPI does not invert the Driving PPICLK edge in
			  *  Transmit Modes */
#define ANOMALY_05000192 /* In PPI Transmit Modes with External Frame Syncs
			  *  POLC */
#define ANOMALY_05000206 /* Internal Voltage Regulator may not start up */

#endif

#endif /*  _MACH_ANOMALY_H_ */
