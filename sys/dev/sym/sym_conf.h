/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *  Device driver optimized for the Symbios/LSI 53C896/53C895A/53C1010 
 *  PCI-SCSI controllers.
 *
 *  Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 *  This driver also supports the following Symbios/LSI PCI-SCSI chips:
 *	53C810A, 53C825A, 53C860, 53C875, 53C876, 53C885, 53C895,
 *	53C810,  53C815,  53C825 and the 53C1510D is 53C8XX mode.
 *
 *  
 *  This driver for FreeBSD-CAM is derived from the Linux sym53c8xx driver.
 *  Copyright (C) 1998-1999  Gerard Roudier
 *
 *  The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 *  a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 *  The original ncr driver has been written for 386bsd and FreeBSD by
 *          Wolfgang Stanglmeier        <wolf@cologne.de>
 *          Stefan Esser                <se@mi.Uni-Koeln.de>
 *  Copyright (C) 1994  Wolfgang Stanglmeier
 *
 *  The initialisation code, and part of the code that addresses 
 *  FreeBSD-CAM services is based on the aic7xxx driver for FreeBSD-CAM 
 *  written by Justin T. Gibbs.
 *
 *  Other major contributions:
 *
 *  NVRAM detection and reading.
 *  Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $FreeBSD$ */

#ifndef SYM_CONF_H
#define SYM_CONF_H

/*-------------------------------------------------------------------
 *  Static configuration.
 *-------------------------------------------------------------------
 */

/*
 *  Also support early NCR 810, 815 and 825 chips.
 */
#define SYM_CONF_GENERIC_SUPPORT

/*
 *  Use Normal IO instead of MMIO.
 */
/* #define SYM_CONF_IOMAPPED */

/*
 *  Max tags for a device (logical unit)
 * 	We use a power of 2, (7) means 2<<7=128
 *  Maximum is 8 -> 256 tags
 */
#define SYM_CONF_MAX_TAG_ORDER	(6)

/*
 *  DMA boundary
 *  We need to ensure 16 MB boundaries not to be crossed during DMA of
 *  each segment, due to some chips being flawed.
 */
#define SYM_CONF_DMA_BOUNDARY	(1UL << 24)

/*
 *  Max number of scatter/gather entries for an I/O.
 *  Each entry costs 8 bytes in the internal CCB data structure.
 *  We use at most 33 segments but also no more than required for handling
 *  MAXPHYS.
 */
#define	SYM_CONF_MAX_SG		(MIN(33, (MAXPHYS / PAGE_SIZE) + 1))

/*
 *  Max number of targets.
 *  Maximum is 16 and you are advised not to change this value.
 */
#define SYM_CONF_MAX_TARGET	(16)

/*
 *  Max number of logical units.
 *  SPI-2 allows up to 64 logical units, but in real life, target
 *  that implements more that 7 logical units are pretty rare.
 *  Anyway, the cost of accepting up to 64 logical unit is low in 
 *  this driver, thus going with the maximum is acceptable.
 */
#define SYM_CONF_MAX_LUN		(64)

/*
 *  Max number of IO control blocks queued to the controller.
 *  Each entry needs 8 bytes and the queues are allocated contiguously.
 *  Since we donnot want to allocate more than a page, the theorical 
 *  maximum is PAGE_SIZE/8. For safety, we announce a bit less to the 
 *  access method. :)
 *  When not supplied, as it is suggested, the driver compute some 
 *  good value for this parameter.
 */
/* #define SYM_CONF_MAX_START	(PAGE_SIZE/8 - 16) */

/*
 *  Support for NVRAM.
 */
#define SYM_CONF_NVRAM_SUPPORT
/* #define SYM_CONF_NVRAM_SUPPORT */

/*
 *  Support for Immediate Arbitration.
 *  Not advised.
 */
/* #define SYM_CONF_IARB_SUPPORT */

/*-------------------------------------------------------------------
 *  Configuration that could be dynamic if it was possible 
 *  to pass arguments to the driver.
 *-------------------------------------------------------------------
 */

/*
 *  HOST default scsi id.
 */
#define SYM_SETUP_HOST_ID	7

/*
 *  Max synchronous transfers.
 */
#define SYM_SETUP_MIN_SYNC	(9)

/*
 *  Max wide order.
 */
#define SYM_SETUP_MAX_WIDE	(1)

/*
 *  Max SCSI offset.
 */
#define SYM_SETUP_MAX_OFFS	(63)

/*
 *  Default number of tags.
 */
#define SYM_SETUP_MAX_TAG	(1<<SYM_CONF_MAX_TAG_ORDER)

/*
 *  SYMBIOS NVRAM format support.
 */
#define SYM_SETUP_SYMBIOS_NVRAM	(1)

/*
 *  TEKRAM NVRAM format support.
 */
#define SYM_SETUP_TEKRAM_NVRAM	(1)

/*
 *  PCI parity checking.
 *  It should not be an option, but some poor or broken 
 *  PCI-HOST bridges have been reported to make problems 
 *  when this feature is enabled.
 *  Setting this option to 0 tells the driver not to 
 *  enable the checking against PCI parity.
 */
#ifndef SYM_SETUP_PCI_PARITY
#define SYM_SETUP_PCI_PARITY	(1)
#endif

/*
 *  SCSI parity checking.
 */
#define SYM_SETUP_SCSI_PARITY	(1)

/*
 *  SCSI activity LED.
 */
#define SYM_SETUP_SCSI_LED	(0)

/*
 *  SCSI High Voltage Differential support.
 *
 *  HVD/LVD/SE capable controllers (895, 895A, 896, 1010) 
 *  report the actual SCSI BUS mode from the STEST4 IO 
 *  register.
 *
 *  But for HVD/SE only capable chips (825a, 875, 885), 
 *  the driver uses some heuristic to probe against HVD. 
 *  Normally, the chip senses the DIFFSENS signal and 
 *  should switch its BUS tranceivers to high impedance 
 *  in situation of the driver having been wrong about 
 *  the actual BUS mode. May-be, the BUS mode probing of 
 *  the driver is safe, but, given that it may be partially 
 *  based on some previous IO register settings, it 
 *  cannot be stated so. Thus, decision has been taken 
 *  to require a user option to be set for the DIFF probing 
 *  to be applied for the 825a, 875 and 885 chips.
 *  
 *  This setup option works as follows:
 *
 *    0  ->  HVD only supported for 895, 895A, 896, 1010.
 *    1  ->  HVD probed  for 825A, 875, 885.
 *    2  ->  HVD assumed for 825A, 875, 885 (not advised).
 */
#ifndef SYM_SETUP_SCSI_DIFF
#define SYM_SETUP_SCSI_DIFF	(0)
#endif

/*
 *  IRQ mode.
 */
#define SYM_SETUP_IRQ_MODE	(0)

/*
 *  Check SCSI BUS signal on reset.
 */
#define SYM_SETUP_SCSI_BUS_CHECK	(1)

/*
 *  Max burst for PCI (1<<value)
 *  7 means: (1<<7) = 128 DWORDS.
 */
#define SYM_SETUP_BURST_ORDER	(7)

/*
 *  Only relevant if IARB support configured.
 *  - Max number of successive settings of IARB hints.
 *  - Set IARB on arbitration lost.
 */
#define SYM_CONF_IARB_MAX 3
#define SYM_CONF_SET_IARB_ON_ARB_LOST 1

/*
 *  Returning wrong residuals may make problems.
 *  When zero, this define tells the driver to 
 *  always return 0 as transfer residual.
 *  Btw, all my testings of residuals have succeeded.
 */
#define SYM_CONF_RESIDUAL_SUPPORT 1

/*
 *  Supported maximum number of LUNs to announce to 
 *  the access method.
 *  The driver supports up to 64 LUNs per target as 
 *  required by SPI-2/SPI-3. However some SCSI devices  
 *  designed prior to these specifications or not being  
 *  conformant may be highly confused when they are 
 *  asked about a LUN > 7.
 */
#ifndef SYM_SETUP_MAX_LUN
#define SYM_SETUP_MAX_LUN	(8)
#endif

/*
 *  Low priority probe map.
 * 
 *  This option is used as a bitmap to tell the driver 
 *  about chips that are to be claimed with a low priority 
 *  (-2000) by the probe method. This allows any other driver 
 *  that may return some higher priority value for the same 
 *  chips to take precedence over this driver (sym).
 *  This option may be used when both the ncr driver and this 
 *  driver are configured.
 *
 *  Bits are to be coded as follows:
 *    0x01  ->  810a, 860
 *    0x02  ->  825a, 875, 885, 895
 *    0x04  ->  895a, 896, 1510d
 *    0x08  ->  1010
 *    0x40  ->  810, 815, 825
 *
 *  For example, value 5 tells the driver to claim support 
 *  for 810a, 860, 895a, 896 and 1510d with low priority, 
 *  allowing the ncr driver to take precedence if configured.
 */
#ifndef SYM_SETUP_LP_PROBE_MAP
#define SYM_SETUP_LP_PROBE_MAP 0
#endif

#endif /* SYM_CONF_H */
