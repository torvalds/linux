/*
 * File: include/asm-blackfin/mach-bf537/anomaly.h
 * Bugs: Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2004-2007 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

/* This file shoule be up to date with:
 *  - Revision C, 02/08/2008; ADSP-BF534/ADSP-BF536/ADSP-BF537 Blackfin Processor Anomaly List
 */

#ifndef _MACH_ANOMALY_H_
#define _MACH_ANOMALY_H_

/* We do not support 0.1 silicon - sorry */
#if __SILICON_REVISION__ < 2
# error will not work on BF537 silicon version 0.0 or 0.1
#endif

#if defined(__ADSPBF534__)
# define ANOMALY_BF534 1
#else
# define ANOMALY_BF534 0
#endif
#if defined(__ADSPBF536__)
# define ANOMALY_BF536 1
#else
# define ANOMALY_BF536 0
#endif
#if defined(__ADSPBF537__)
# define ANOMALY_BF537 1
#else
# define ANOMALY_BF537 0
#endif

/* Multi-issue instruction with dsp32shiftimm in slot1 and P-reg store in slot 2 not supported */
#define ANOMALY_05000074 (1)
/* DMA_RUN bit is not valid after a Peripheral Receive Channel DMA stops */
#define ANOMALY_05000119 (1)
/* Rx.H cannot be used to access 16-bit System MMR registers */
#define ANOMALY_05000122 (1)
/* Killed 32-bit MMR write leads to next system MMR access thinking it should be 32-bit */
#define ANOMALY_05000157 (__SILICON_REVISION__ < 2)
/* Turning SPORTs on while External Frame Sync Is Active May Corrupt Data */
#define ANOMALY_05000167 (1)
/* PPI_DELAY not functional in PPI modes with 0 frame syncs */
#define ANOMALY_05000180 (1)
/* Instruction Cache Is Not Functional */
#define ANOMALY_05000237 (__SILICON_REVISION__ < 2)
/* If i-cache is on, CSYNC/SSYNC/IDLE around Change of Control causes failures */
#define ANOMALY_05000244 (__SILICON_REVISION__ < 3)
/* Spurious Hardware Error from an access in the shadow of a conditional branch */
#define ANOMALY_05000245 (1)
/* CLKIN Buffer Output Enable Reset Behavior Is Changed */
#define ANOMALY_05000247 (1)
/* Incorrect Bit-Shift of Data Word in Multichannel (TDM) mode in certain conditions */
#define ANOMALY_05000250 (__SILICON_REVISION__ < 3)
/* EMAC Tx DMA error after an early frame abort */
#define ANOMALY_05000252 (__SILICON_REVISION__ < 3)
/* Maximum external clock speed for Timers */
#define ANOMALY_05000253 (__SILICON_REVISION__ < 3)
/* Incorrect Timer Pulse Width in Single-Shot PWM_OUT mode with external clock */
#define ANOMALY_05000254 (__SILICON_REVISION__ > 2)
/* Entering Hibernate Mode with RTC Seconds event interrupt not functional */
#define ANOMALY_05000255 (__SILICON_REVISION__ < 3)
/* EMAC MDIO input latched on wrong MDC edge */
#define ANOMALY_05000256 (__SILICON_REVISION__ < 3)
/* Interrupt/Exception during short hardware loop may cause bad instruction fetches */
#define ANOMALY_05000257 (__SILICON_REVISION__ < 3)
/* Instruction Cache is corrupted when bits 9 and 12 of the ICPLB Data registers differ */
#define ANOMALY_05000258 (((ANOMALY_BF536 || ANOMALY_BF537) && __SILICON_REVISION__ == 1) || __SILICON_REVISION__ == 2)
/* ICPLB_STATUS MMR register may be corrupted */
#define ANOMALY_05000260 (__SILICON_REVISION__ == 2)
/* DCPLB_FAULT_ADDR MMR register may be corrupted */
#define ANOMALY_05000261 (__SILICON_REVISION__ < 3)
/* Stores to data cache may be lost */
#define ANOMALY_05000262 (__SILICON_REVISION__ < 3)
/* Hardware loop corrupted when taking an ICPLB exception */
#define ANOMALY_05000263 (__SILICON_REVISION__ == 2)
/* CSYNC/SSYNC/IDLE causes infinite stall in second to last instruction in hardware loop */
#define ANOMALY_05000264 (__SILICON_REVISION__ < 3)
/* Sensitivity to noise with slow input edge rates on external SPORT TX and RX clocks */
#define ANOMALY_05000265 (1)
/* Memory DMA error when peripheral DMA is running with non-zero DEB_TRAFFIC_PERIOD */
#define ANOMALY_05000268 (__SILICON_REVISION__ < 3)
/* High I/O activity causes output voltage of internal voltage regulator (VDDint) to decrease */
#define ANOMALY_05000270 (__SILICON_REVISION__ < 3)
/* Certain data cache write through modes fail for VDDint <=0.9V */
#define ANOMALY_05000272 (1)
/* Writes to Synchronous SDRAM memory may be lost */
#define ANOMALY_05000273 (__SILICON_REVISION__ < 3)
/* Writes to an I/O data register one SCLK cycle after an edge is detected may clear interrupt */
#define ANOMALY_05000277 (__SILICON_REVISION__ < 3)
/* Disabling Peripherals with DMA running may cause DMA system instability */
#define ANOMALY_05000278 (((ANOMALY_BF536 || ANOMALY_BF537) && __SILICON_REVISION__ < 3) || (ANOMALY_BF534 && __SILICON_REVISION__ < 2))
/* SPI Master boot mode does not work well with Atmel Data flash devices */
#define ANOMALY_05000280 (1)
/* False Hardware Error Exception when ISR context is not restored */
#define ANOMALY_05000281 (__SILICON_REVISION__ < 3)
/* Memory DMA corruption with 32-bit data and traffic control */
#define ANOMALY_05000282 (__SILICON_REVISION__ < 3)
/* System MMR Write Is Stalled Indefinitely When Killed in a Particular Stage */
#define ANOMALY_05000283 (__SILICON_REVISION__ < 3)
/* New Feature: EMAC TX DMA Word Alignment (Not Available On Older Silicon) */
#define ANOMALY_05000285 (__SILICON_REVISION__ < 3)
/* SPORTs may receive bad data if FIFOs fill up */
#define ANOMALY_05000288 (__SILICON_REVISION__ < 3)
/* Memory to memory DMA source/destination descriptors must be in same memory space */
#define ANOMALY_05000301 (1)
/* SSYNCs After Writes To CAN/DMA MMR Registers Are Not Always Handled Correctly */
#define ANOMALY_05000304 (__SILICON_REVISION__ < 3)
/* New Feature: Additional Hysteresis on SPORT Input Pins (Not Available On Older Silicon) */
#define ANOMALY_05000305 (__SILICON_REVISION__ < 3)
/* SCKELOW Bit Does Not Maintain State Through Hibernate */
#define ANOMALY_05000307 (__SILICON_REVISION__ < 3)
/* Writing UART_THR while UART clock is disabled sends erroneous start bit */
#define ANOMALY_05000309 (__SILICON_REVISION__ < 3)
/* False hardware errors caused by fetches at the boundary of reserved memory */
#define ANOMALY_05000310 (1)
/* Errors when SSYNC, CSYNC, or loads to LT, LB and LC registers are interrupted */
#define ANOMALY_05000312 (1)
/* PPI is level sensitive on first transfer */
#define ANOMALY_05000313 (1)
/* Killed System MMR Write Completes Erroneously On Next System MMR Access */
#define ANOMALY_05000315 (__SILICON_REVISION__ < 3)
/* EMAC RMII mode: collisions occur in Full Duplex mode */
#define ANOMALY_05000316 (__SILICON_REVISION__ < 3)
/* EMAC RMII mode: TX frames in half duplex fail with status No Carrier */
#define ANOMALY_05000321 (__SILICON_REVISION__ < 3)
/* EMAC RMII mode at 10-Base-T speed: RX frames not received properly */
#define ANOMALY_05000322 (1)
/* Ethernet MAC MDIO Reads Do Not Meet IEEE Specification */
#define ANOMALY_05000341 (__SILICON_REVISION__ >= 3)
/* New Feature: UART Remains Enabled after UART Boot (Not Available on Older Silicon) */
#define ANOMALY_05000350 (__SILICON_REVISION__ < 3)
/* Regulator Programming Blocked when Hibernate Wakeup Source Remains Active */
#define ANOMALY_05000355 (1)
/* Serial Port (SPORT) Multichannel Transmit Failure when Channel 0 Is Disabled */
#define ANOMALY_05000357 (1)
/* DMAs that Go Urgent during Tight Core Writes to External Memory Are Blocked */
#define ANOMALY_05000359 (1)
/* PPI Underflow Error Goes Undetected in ITU-R 656 Mode */
#define ANOMALY_05000366 (1)
/* Possible RETS Register Corruption when Subroutine Is under 5 Cycles in Duration */
#define ANOMALY_05000371 (1)
/* SSYNC Stalls Processor when Executed from Non-Cacheable Memory */
#define ANOMALY_05000402 (__SILICON_REVISION__ >= 3)
/* Level-Sensitive External GPIO Wakeups May Cause Indefinite Stall */
#define ANOMALY_05000403 (1)



/* Anomalies that don't exist on this proc */
#define ANOMALY_05000125 (0)
#define ANOMALY_05000158 (0)
#define ANOMALY_05000183 (0)
#define ANOMALY_05000198 (0)
#define ANOMALY_05000230 (0)
#define ANOMALY_05000266 (0)
#define ANOMALY_05000311 (0)
#define ANOMALY_05000323 (0)
#define ANOMALY_05000363 (0)

#endif
