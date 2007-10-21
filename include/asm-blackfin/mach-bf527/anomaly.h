/*
 * File: include/asm-blackfin/mach-bf527/anomaly.h
 * Bugs: Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2004-2007 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

/* This file shoule be up to date with:
 *  - Revision A, May 30, 2007; ADSP-BF527 Blackfin Processor Anomaly List
 */

#ifndef _MACH_ANOMALY_H_
#define _MACH_ANOMALY_H_

/* Multi-Issue Instruction with dsp32shiftimm in slot1 and P-reg Store in slot2 Not Supported */
#define ANOMALY_05000074 (1)
/* DMA_RUN Bit Is Not Valid after a Peripheral Receive Channel DMA Stops */
#define ANOMALY_05000119 (1)
/* Rx.H Cannot Be Used to Access 16-bit System MMR Registers */
#define ANOMALY_05000122 (1)
/* Spurious Hardware Error from an Access in the Shadow of a Conditional Branch */
#define ANOMALY_05000245 (1)
/* Sensitivity To Noise with Slow Input Edge Rates on External SPORT TX and RX Clocks */
#define ANOMALY_05000265 (1)
/* Memory-To-Memory DMA Source/Destination Descriptors Must Be in Same Memory Space */
#define ANOMALY_05000301 (1)
/* Errors When SSYNC, CSYNC, or Loads to LT, LB and LC Registers Are Interrupted */
#define ANOMALY_05000312 (1)
/* Incorrect Access of OTP_STATUS During otp_write() Function */
#define ANOMALY_05000328 (1)
/* Disallowed Configuration Prevents Subsequent Allowed Configuration on Host DMA Port */
#define ANOMALY_05000337 (1)
/* TWI Does Not Operate Correctly Under Certain Signal Termination Conditions */
#define ANOMALY_05000342 (1)
/* Boot ROM Kernel Incorrectly Alters Reset Value of USB Register */
#define ANOMALY_05000347 (1)

/* Anomalies that don't exist on this proc */
#define ANOMALY_05000323 (0)
#define ANOMALY_05000244 (0)
#define ANOMALY_05000198 (0)
#define ANOMALY_05000125 (0)
#define ANOMALY_05000158 (0)
#define ANOMALY_05000273 (0)
#define ANOMALY_05000263 (0)
#define ANOMALY_05000311 (0)
#define ANOMALY_05000230 (0)
#endif
