/*
 * File: include/asm-blackfin/mach-bf548/anomaly.h
 * Bugs: Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2004-2007 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

/* This file shoule be up to date with:
 *  - Revision E, 11/28/2007; ADSP-BF542/BF544/BF547/BF548/BF549 Blackfin Processor Anomaly List
 */

#ifndef _MACH_ANOMALY_H_
#define _MACH_ANOMALY_H_

/* Multi-Issue Instruction with dsp32shiftimm in slot1 and P-reg Store in slot 2 Not Supported */
#define ANOMALY_05000074 (1)
/* DMA_RUN Bit Is Not Valid after a Peripheral Receive Channel DMA Stops */
#define ANOMALY_05000119 (1)
/* Rx.H Cannot Be Used to Access 16-bit System MMR Registers */
#define ANOMALY_05000122 (1)
/* Spurious Hardware Error from an Access in the Shadow of a Conditional Branch */
#define ANOMALY_05000245 (1)
/* Sensitivity To Noise with Slow Input Edge Rates on External SPORT TX and RX Clocks */
#define ANOMALY_05000265 (1)
/* Certain Data Cache Writethrough Modes Fail for Vddint <= 0.9V */
#define ANOMALY_05000272 (1)
/* False Hardware Error Exception when ISR context is not restored */
#define ANOMALY_05000281 (__SILICON_REVISION__ < 1)
/* SSYNCs After Writes To CAN/DMA MMR Registers Are Not Always Handled Correctly */
#define ANOMALY_05000304 (__SILICON_REVISION__ < 1)
/* False Hardware Errors Caused by Fetches at the Boundary of Reserved Memory */
#define ANOMALY_05000310 (1)
/* Errors When SSYNC, CSYNC, or Loads to LT, LB and LC Registers Are Interrupted */
#define ANOMALY_05000312 (__SILICON_REVISION__ < 1)
/* TWI Slave Boot Mode Is Not Functional */
#define ANOMALY_05000324 (__SILICON_REVISION__ < 1)
/* External FIFO Boot Mode Is Not Functional */
#define ANOMALY_05000325 (__SILICON_REVISION__ < 1)
/* Data Lost When Core and DMA Accesses Are Made to the USB FIFO Simultaneously */
#define ANOMALY_05000327 (__SILICON_REVISION__ < 1)
/* Incorrect Access of OTP_STATUS During otp_write() Function */
#define ANOMALY_05000328 (__SILICON_REVISION__ < 1)
/* Synchronous Burst Flash Boot Mode Is Not Functional */
#define ANOMALY_05000329 (__SILICON_REVISION__ < 1)
/* Host DMA Boot Mode Is Not Functional */
#define ANOMALY_05000330 (__SILICON_REVISION__ < 1)
/* Inadequate Timing Margins on DDR DQS to DQ and DQM Skew */
#define ANOMALY_05000334 (__SILICON_REVISION__ < 1)
/* Inadequate Rotary Debounce Logic Duration */
#define ANOMALY_05000335 (__SILICON_REVISION__ < 1)
/* Phantom Interrupt Occurs After First Configuration of Host DMA Port */
#define ANOMALY_05000336 (__SILICON_REVISION__ < 1)
/* Disallowed Configuration Prevents Subsequent Allowed Configuration on Host DMA Port */
#define ANOMALY_05000337 (__SILICON_REVISION__ < 1)
/* Slave-Mode SPI0 MISO Failure With CPHA = 0 */
#define ANOMALY_05000338 (__SILICON_REVISION__ < 1)
/* If Memory Reads Are Enabled on SDH or HOSTDP, Other DMAC1 Peripherals Cannot Read */
#define ANOMALY_05000340 (__SILICON_REVISION__ < 1)
/* Boot Host Wait (HWAIT) and Boot Host Wait Alternate (HWAITA) Signals Are Swapped */
#define ANOMALY_05000344 (__SILICON_REVISION__ < 1)
/* USB Calibration Value Is Not Intialized */
#define ANOMALY_05000346 (__SILICON_REVISION__ < 1)
/* Boot ROM Kernel Incorrectly Alters Reset Value of USB Register */
#define ANOMALY_05000347 (__SILICON_REVISION__ < 1)
/* Data Lost when Core Reads SDH Data FIFO */
#define ANOMALY_05000349 (__SILICON_REVISION__ < 1)
/* PLL Status Register Is Inaccurate */
#define ANOMALY_05000351 (__SILICON_REVISION__ < 1)
/* Serial Port (SPORT) Multichannel Transmit Failure when Channel 0 Is Disabled */
#define ANOMALY_05000357 (1)
/* External Memory Read Access Hangs Core With PLL Bypass */
#define ANOMALY_05000360 (1)
/* DMAs that Go Urgent during Tight Core Writes to External Memory Are Blocked */
#define ANOMALY_05000365 (1)
/* Addressing Conflict between Boot ROM and Asynchronous Memory */
#define ANOMALY_05000369 (1)
/* Possible RETS Register Corruption when Subroutine Is under 5 Cycles in Duration */
#define ANOMALY_05000371 (1)
/* Mobile DDR Operation Not Functional */
#define ANOMALY_05000377 (1)
/* Security/Authentication Speedpath Causes Authentication To Fail To Initiate */
#define ANOMALY_05000378 (1)

/* Anomalies that don't exist on this proc */
#define ANOMALY_05000125 (0)
#define ANOMALY_05000158 (0)
#define ANOMALY_05000183 (0)
#define ANOMALY_05000198 (0)
#define ANOMALY_05000230 (0)
#define ANOMALY_05000244 (0)
#define ANOMALY_05000261 (0)
#define ANOMALY_05000263 (0)
#define ANOMALY_05000266 (0)
#define ANOMALY_05000273 (0)
#define ANOMALY_05000311 (0)
#define ANOMALY_05000323 (0)
#define ANOMALY_05000363 (0)

#endif
