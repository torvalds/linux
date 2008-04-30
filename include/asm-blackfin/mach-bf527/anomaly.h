/*
 * File: include/asm-blackfin/mach-bf527/anomaly.h
 * Bugs: Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2004-2008 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

/* This file shoule be up to date with:
 *  - Revision C, 01/25/2008; ADSP-BF527 Blackfin Processor Anomaly List
 */

#ifndef _MACH_ANOMALY_H_
#define _MACH_ANOMALY_H_

/* Multi-Issue Instruction with dsp32shiftimm in slot1 and P-reg Store in slot2 Not Supported */
#define ANOMALY_05000074 (1)
/* Rx.H Cannot Be Used to Access 16-bit System MMR Registers */
#define ANOMALY_05000122 (1)
/* Spurious Hardware Error from an Access in the Shadow of a Conditional Branch */
#define ANOMALY_05000245 (1)
/* Sensitivity To Noise with Slow Input Edge Rates on External SPORT TX and RX Clocks */
#define ANOMALY_05000265 (1)
/* Incorrect Access of OTP_STATUS During otp_write() Function */
#define ANOMALY_05000328 (1)
/* Disallowed Configuration Prevents Subsequent Allowed Configuration on Host DMA Port */
#define ANOMALY_05000337 (1)
/* Ethernet MAC MDIO Reads Do Not Meet IEEE Specification */
#define ANOMALY_05000341 (1)
/* TWI May Not Operate Correctly Under Certain Signal Termination Conditions */
#define ANOMALY_05000342 (1)
/* USB Calibration Value Is Not Initialized */
#define ANOMALY_05000346 (1)
/* Preboot Routine Incorrectly Alters Reset Value of USB Register */
#define ANOMALY_05000347 (1)
/* Security Features Are Not Functional */
#define ANOMALY_05000348 (__SILICON_REVISION__ < 1)
/* Regulator Programming Blocked when Hibernate Wakeup Source Remains Active */
#define ANOMALY_05000355 (1)
/* Serial Port (SPORT) Multichannel Transmit Failure when Channel 0 Is Disabled */
#define ANOMALY_05000357 (1)
/* Incorrect Revision Number in DSPID Register */
#define ANOMALY_05000364 (__SILICON_REVISION__ > 0)
/* PPI Underflow Error Goes Undetected in ITU-R 656 Mode */
#define ANOMALY_05000366 (1)
/* New Feature: Higher Default CCLK Rate */
#define ANOMALY_05000368 (1)
/* Possible RETS Register Corruption when Subroutine Is under 5 Cycles in Duration */
#define ANOMALY_05000371 (1)
/* Authentication Fails To Initiate */
#define ANOMALY_05000376 (__SILICON_REVISION__ > 0)
/* Data Read From L3 Memory by USB DMA May be Corrupted */
#define ANOMALY_05000380 (1)
/* USB Full-speed Mode not Fully Tested */
#define ANOMALY_05000381 (1)
/* New Feature: Boot from OTP Memory */
#define ANOMALY_05000385 (1)
/* New Feature: bfrom_SysControl() Routine */
#define ANOMALY_05000386 (1)
/* New Feature: Programmable Preboot Settings */
#define ANOMALY_05000387 (1)
/* Reset Vector Must Not Be in SDRAM Memory Space */
#define ANOMALY_05000389 (1)
/* New Feature: pTempCurrent Added to ADI_BOOT_DATA Structure */
#define ANOMALY_05000392 (1)
/* New Feature: dTempByteCount Value Increased in ADI_BOOT_DATA Structure */
#define ANOMALY_05000393 (1)
/* New Feature: Log Buffer Functionality */
#define ANOMALY_05000394 (1)
/* New Feature: Hook Routine Functionality */
#define ANOMALY_05000395 (1)
/* New Feature: Header Indirect Bit */
#define ANOMALY_05000396 (1)
/* New Feature: BK_ONES, BK_ZEROS, and BK_DATECODE Constants */
#define ANOMALY_05000397 (1)
/* New Feature: SWRESET, DFRESET and WDRESET Bits Added to SYSCR Register */
#define ANOMALY_05000398 (1)
/* New Feature: BCODE_NOBOOT Added to BCODE Field of SYSCR Register */
#define ANOMALY_05000399 (1)
/* PPI Data Signals D0 and D8 do not Tristate After Disabling PPI */
#define ANOMALY_05000401 (1)

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
#define ANOMALY_05000312 (0)
#define ANOMALY_05000323 (0)
#define ANOMALY_05000363 (0)

#endif
