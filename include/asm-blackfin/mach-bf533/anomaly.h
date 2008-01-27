/*
 * File: include/asm-blackfin/mach-bf533/anomaly.h
 * Bugs: Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2004-2007 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

/* This file shoule be up to date with:
 *  - Revision B, 12/10/2007; ADSP-BF531/BF532/BF533 Blackfin Processor Anomaly List
 */

#ifndef _MACH_ANOMALY_H_
#define _MACH_ANOMALY_H_

/* We do not support 0.1 or 0.2 silicon - sorry */
#if __SILICON_REVISION__ < 3
# error will not work on BF533 silicon version 0.0, 0.1, or 0.2
#endif

#if defined(__ADSPBF531__)
# define ANOMALY_BF531 1
#else
# define ANOMALY_BF531 0
#endif
#if defined(__ADSPBF532__)
# define ANOMALY_BF532 1
#else
# define ANOMALY_BF532 0
#endif
#if defined(__ADSPBF533__)
# define ANOMALY_BF533 1
#else
# define ANOMALY_BF533 0
#endif

/* Multi-Issue Instruction with dsp32shiftimm in slot1 and P-reg Store in slot 2 Not Supported */
#define ANOMALY_05000074 (1)
/* UART Line Status Register (UART_LSR) Bits Are Not Updated at the Same Time */
#define ANOMALY_05000099 (__SILICON_REVISION__ < 5)
/* Watchpoint Status Register (WPSTAT) Bits Are Set on Every Corresponding Match */
#define ANOMALY_05000105 (1)
/* DMA_RUN Bit Is Not Valid after a Peripheral Receive Channel DMA Stops */
#define ANOMALY_05000119 (1)
/* Rx.H Cannot Be Used to Access 16-bit System MMR Registers */
#define ANOMALY_05000122 (1)
/* Instruction DMA Can Cause Data Cache Fills to Fail (Boot Implications) */
#define ANOMALY_05000158 (__SILICON_REVISION__ < 5)
/* PPI Data Lengths Between 8 and 16 Do Not Zero Out Upper Bits */
#define ANOMALY_05000166 (1)
/* Turning Serial Ports on with External Frame Syncs */
#define ANOMALY_05000167 (1)
/* PPI_COUNT Cannot Be Programmed to 0 in General Purpose TX or RX Modes */
#define ANOMALY_05000179 (__SILICON_REVISION__ < 5)
/* PPI_DELAY Not Functional in PPI Modes with 0 Frame Syncs */
#define ANOMALY_05000180 (1)
/* Timer Pin Limitations for PPI TX Modes with External Frame Syncs */
#define ANOMALY_05000183 (__SILICON_REVISION__ < 4)
/* False Protection Exceptions */
#define ANOMALY_05000189 (__SILICON_REVISION__ < 4)
/* False I/O Pin Interrupts on Edge-Sensitive Inputs When Polarity Setting Is Changed */
#define ANOMALY_05000193 (__SILICON_REVISION__ < 4)
/* Restarting SPORT in Specific Modes May Cause Data Corruption */
#define ANOMALY_05000194 (__SILICON_REVISION__ < 4)
/* Failing MMR Accesses When Stalled by Preceding Memory Read */
#define ANOMALY_05000198 (__SILICON_REVISION__ < 5)
/* Current DMA Address Shows Wrong Value During Carry Fix */
#define ANOMALY_05000199 (__SILICON_REVISION__ < 4)
/* SPORT TFS and DT Are Incorrectly Driven During Inactive Channels in Certain Conditions */
#define ANOMALY_05000200 (__SILICON_REVISION__ < 5)
/* Receive Frame Sync Not Ignored During Active Frames in SPORT Multi-Channel Mode */
#define ANOMALY_05000201 (__SILICON_REVISION__ < 4)
/* Possible Infinite Stall with Specific Dual-DAG Situation */
#define ANOMALY_05000202 (__SILICON_REVISION__ < 5)
/* Specific Sequence That Can Cause DMA Error or DMA Stopping */
#define ANOMALY_05000203 (__SILICON_REVISION__ < 4)
/* Incorrect data read with write-through cache and allocate cache lines on reads only mode */
#define ANOMALY_05000204 (__SILICON_REVISION__ < 4 && ANOMALY_BF533)
/* Recovery from "Brown-Out" Condition */
#define ANOMALY_05000207 (__SILICON_REVISION__ < 4)
/* VSTAT Status Bit in PLL_STAT Register Is Not Functional */
#define ANOMALY_05000208 (1)
/* Speed Path in Computational Unit Affects Certain Instructions */
#define ANOMALY_05000209 (__SILICON_REVISION__ < 4)
/* UART TX Interrupt Masked Erroneously */
#define ANOMALY_05000215 (__SILICON_REVISION__ < 5)
/* NMI Event at Boot Time Results in Unpredictable State */
#define ANOMALY_05000219 (1)
/* Incorrect Pulse-Width of UART Start Bit */
#define ANOMALY_05000225 (__SILICON_REVISION__ < 5)
/* Scratchpad Memory Bank Reads May Return Incorrect Data */
#define ANOMALY_05000227 (__SILICON_REVISION__ < 5)
/* SPI Slave Boot Mode Modifies Registers from Reset Value */
#define ANOMALY_05000229 (1)
/* UART Receiver is Less Robust Against Baudrate Differences in Certain Conditions */
#define ANOMALY_05000230 (__SILICON_REVISION__ < 5)
/* UART STB Bit Incorrectly Affects Receiver Setting */
#define ANOMALY_05000231 (__SILICON_REVISION__ < 5)
/* PPI_FS3 Is Not Driven in 2 or 3 Internal Frame Sync Transmit Modes */
#define ANOMALY_05000233 (__SILICON_REVISION__ < 4)
/* Incorrect Revision Number in DSPID Register */
#define ANOMALY_05000234 (__SILICON_REVISION__ == 4)
/* DF Bit in PLL_CTL Register Does Not Respond to Hardware Reset */
#define ANOMALY_05000242 (__SILICON_REVISION__ < 4)
/* If I-Cache Is On, CSYNC/SSYNC/IDLE Around Change of Control Causes Failures */
#define ANOMALY_05000244 (__SILICON_REVISION__ < 5)
/* Spurious Hardware Error from an Access in the Shadow of a Conditional Branch */
#define ANOMALY_05000245 (1)
/* Data CPLBs Should Prevent Spurious Hardware Errors */
#define ANOMALY_05000246 (__SILICON_REVISION__ < 5)
/* Incorrect Bit Shift of Data Word in Multichannel (TDM) Mode in Certain Conditions */
#define ANOMALY_05000250 (__SILICON_REVISION__ == 4)
/* Maximum External Clock Speed for Timers */
#define ANOMALY_05000253 (__SILICON_REVISION__ < 5)
/* Incorrect Timer Pulse Width in Single-Shot PWM_OUT Mode with External Clock */
#define ANOMALY_05000254 (__SILICON_REVISION__ > 4)
/* Entering Hibernate State with RTC Seconds Interrupt Not Functional */
#define ANOMALY_05000255 (__SILICON_REVISION__ < 5)
/* Interrupt/Exception During Short Hardware Loop May Cause Bad Instruction Fetches */
#define ANOMALY_05000257 (__SILICON_REVISION__ < 5)
/* Instruction Cache Is Corrupted When Bits 9 and 12 of the ICPLB Data Registers Differ */
#define ANOMALY_05000258 (__SILICON_REVISION__ < 5)
/* ICPLB_STATUS MMR Register May Be Corrupted */
#define ANOMALY_05000260 (__SILICON_REVISION__ < 5)
/* DCPLB_FAULT_ADDR MMR Register May Be Corrupted */
#define ANOMALY_05000261 (__SILICON_REVISION__ < 5)
/* Stores To Data Cache May Be Lost */
#define ANOMALY_05000262 (__SILICON_REVISION__ < 5)
/* Hardware Loop Corrupted When Taking an ICPLB Exception */
#define ANOMALY_05000263 (__SILICON_REVISION__ < 5)
/* CSYNC/SSYNC/IDLE Causes Infinite Stall in Penultimate Instruction in Hardware Loop */
#define ANOMALY_05000264 (__SILICON_REVISION__ < 5)
/* Sensitivity To Noise with Slow Input Edge Rates on External SPORT TX and RX Clocks */
#define ANOMALY_05000265 (__SILICON_REVISION__ < 5)
/* High I/O Activity Causes Output Voltage of Internal Voltage Regulator (Vddint) to Increase */
#define ANOMALY_05000269 (__SILICON_REVISION__ < 5)
/* High I/O Activity Causes Output Voltage of Internal Voltage Regulator (Vddint) to Decrease */
#define ANOMALY_05000270 (__SILICON_REVISION__ < 5)
/* Spontaneous Reset of Internal Voltage Regulator */
#define ANOMALY_05000271 (__SILICON_REVISION__ < 4)
/* Certain Data Cache Writethrough Modes Fail for Vddint <= 0.9V */
#define ANOMALY_05000272 (1)
/* Writes to Synchronous SDRAM Memory May Be Lost */
#define ANOMALY_05000273 (1)
/* Timing Requirements Change for External Frame Sync PPI Modes with Non-Zero PPI_DELAY */
#define ANOMALY_05000276 (1)
/* Writes to an I/O Data Register One SCLK Cycle after an Edge Is Detected May Clear Interrupt */
#define ANOMALY_05000277 (1)
/* Disabling Peripherals with DMA Running May Cause DMA System Instability */
#define ANOMALY_05000278 (1)
/* False Hardware Error Exception When ISR Context Is Not Restored */
#define ANOMALY_05000281 (1)
/* Memory DMA Corruption with 32-Bit Data and Traffic Control */
#define ANOMALY_05000282 (1)
/* System MMR Write Is Stalled Indefinitely When Killed in a Particular Stage */
#define ANOMALY_05000283 (1)
/* SPORTs May Receive Bad Data If FIFOs Fill Up */
#define ANOMALY_05000288 (1)
/* Memory-To-Memory DMA Source/Destination Descriptors Must Be in Same Memory Space */
#define ANOMALY_05000301 (1)
/* SSYNCs After Writes To DMA MMR Registers May Not Be Handled Correctly */
#define ANOMALY_05000302 (__SILICON_REVISION__ < 5)
/* New Feature: Additional Hysteresis on SPORT Input Pins (Not Available On Older Silicon) */
#define ANOMALY_05000305 (__SILICON_REVISION__ < 5)
/* New Feature: Additional PPI Frame Sync Sampling Options (Not Available On Older Silicon) */
#define ANOMALY_05000306 (__SILICON_REVISION__ < 5)
/* False Hardware Errors Caused by Fetches at the Boundary of Reserved Memory */
#define ANOMALY_05000310 (1)
/* Erroneous Flag (GPIO) Pin Operations under Specific Sequences */
#define ANOMALY_05000311 (1)
/* Errors When SSYNC, CSYNC, or Loads to LT, LB and LC Registers Are Interrupted */
#define ANOMALY_05000312 (1)
/* PPI Is Level-Sensitive on First Transfer */
#define ANOMALY_05000313 (1)
/* Killed System MMR Write Completes Erroneously On Next System MMR Access */
#define ANOMALY_05000315 (1)
/* Internal Voltage Regulator Values of 1.05V, 1.10V and 1.15V Not Allowed for LQFP Packages */
#define ANOMALY_05000319 (ANOMALY_BF531 || ANOMALY_BF532)

/* These anomalies have been "phased" out of analog.com anomaly sheets and are
 * here to show running on older silicon just isn't feasible.
 */

/* Watchpoints (Hardware Breakpoints) are not supported */
#define ANOMALY_05000067 (__SILICON_REVISION__ < 3)
/* Reserved bits in SYSCFG register not set at power on */
#define ANOMALY_05000109 (__SILICON_REVISION__ < 3)
/* Trace Buffers may record discontinuities into emulation mode and/or exception, NMI, reset handlers */
#define ANOMALY_05000116 (__SILICON_REVISION__ < 3)
/* DTEST_COMMAND initiated memory access may be incorrect if data cache or DMA is active */
#define ANOMALY_05000123 (__SILICON_REVISION__ < 3)
/* DMA Lock-up at CCLK to SCLK ratios of 4:1, 2:1, or 1:1 */
#define ANOMALY_05000124 (__SILICON_REVISION__ < 3)
/* Erroneous exception when enabling cache */
#define ANOMALY_05000125 (__SILICON_REVISION__ < 3)
/* SPI clock polarity and phase bits incorrect during booting */
#define ANOMALY_05000126 (__SILICON_REVISION__ < 3)
/* DMEM_CONTROL is not set on Reset */
#define ANOMALY_05000137 (__SILICON_REVISION__ < 3)
/* SPI boot will not complete if there is a zero fill block in the loader file */
#define ANOMALY_05000138 (__SILICON_REVISION__ < 3)
/* Allowing the SPORT RX FIFO to fill will cause an overflow */
#define ANOMALY_05000140 (__SILICON_REVISION__ < 3)
/* An Infinite Stall occurs with a particular sequence of consecutive dual dag events */
#define ANOMALY_05000141 (__SILICON_REVISION__ < 3)
/* Interrupts may be lost when a programmable input flag is configured to be edge sensitive */
#define ANOMALY_05000142 (__SILICON_REVISION__ < 3)
/* A read from external memory may return a wrong value with data cache enabled */
#define ANOMALY_05000143 (__SILICON_REVISION__ < 3)
/* DMA and TESTSET conflict when both are accessing external memory */
#define ANOMALY_05000144 (__SILICON_REVISION__ < 3)
/* In PWM_OUT mode, you must enable the PPI block to generate a waveform from PPI_CLK */
#define ANOMALY_05000145 (__SILICON_REVISION__ < 3)
/* MDMA may lose the first few words of a descriptor chain */
#define ANOMALY_05000146 (__SILICON_REVISION__ < 3)
/* The source MDMA descriptor may stop with a DMA Error */
#define ANOMALY_05000147 (__SILICON_REVISION__ < 3)
/* When booting from a 16-bit asynchronous memory device, the upper 8-bits of each word must be 0x00 */
#define ANOMALY_05000148 (__SILICON_REVISION__ < 3)
/* Frame Delay in SPORT Multichannel Mode */
#define ANOMALY_05000153 (__SILICON_REVISION__ < 3)
/* SPORT TFS signal is active in Multi-channel mode outside of valid channels */
#define ANOMALY_05000154 (__SILICON_REVISION__ < 3)
/* Timer1 can not be used for PWMOUT mode when a certain PPI mode is in use */
#define ANOMALY_05000155 (__SILICON_REVISION__ < 3)
/* A killed 32-bit System MMR write will lead to the next system MMR access thinking it should be 32-bit. */
#define ANOMALY_05000157 (__SILICON_REVISION__ < 3)
/* SPORT transmit data is not gated by external frame sync in certain conditions */
#define ANOMALY_05000163 (__SILICON_REVISION__ < 3)
/* SDRAM auto-refresh and subsequent Power Ups */
#define ANOMALY_05000168 (__SILICON_REVISION__ < 3)
/* DATA CPLB page miss can result in lost write-through cache data writes */
#define ANOMALY_05000169 (__SILICON_REVISION__ < 3)
/* DMA vs Core accesses to external memory */
#define ANOMALY_05000173 (__SILICON_REVISION__ < 3)
/* Cache Fill Buffer Data lost */
#define ANOMALY_05000174 (__SILICON_REVISION__ < 3)
/* Overlapping Sequencer and Memory Stalls */
#define ANOMALY_05000175 (__SILICON_REVISION__ < 3)
/* Multiplication of (-1) by (-1) followed by an accumulator saturation */
#define ANOMALY_05000176 (__SILICON_REVISION__ < 3)
/* Disabling the PPI resets the PPI configuration registers */
#define ANOMALY_05000181 (__SILICON_REVISION__ < 3)
/* PPI TX Mode with 2 External Frame Syncs */
#define ANOMALY_05000185 (__SILICON_REVISION__ < 3)
/* PPI does not invert the Driving PPICLK edge in Transmit Modes */
#define ANOMALY_05000191 (__SILICON_REVISION__ < 3)
/* In PPI Transmit Modes with External Frame Syncs POLC */
#define ANOMALY_05000192 (__SILICON_REVISION__ < 3)
/* Internal Voltage Regulator may not start up */
#define ANOMALY_05000206 (__SILICON_REVISION__ < 3)
/* Serial Port (SPORT) Multichannel Transmit Failure when Channel 0 Is Disabled */
#define ANOMALY_05000357 (1)
/* PPI Underflow Error Goes Undetected in ITU-R 656 Mode */
#define ANOMALY_05000366 (1)
/* Possible RETS Register Corruption when Subroutine Is under 5 Cycles in Duration */
#define ANOMALY_05000371 (1)

/* Anomalies that don't exist on this proc */
#define ANOMALY_05000266 (0)
#define ANOMALY_05000323 (0)

#endif
