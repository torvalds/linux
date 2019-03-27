/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Katsurajima Naoto <raven@katsurajima.seya.yokohama.jp>
 * Copyright (c) 2001 Cameron Grant <cg@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHERIN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THEPOSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#define PCIR_NAMBAR 0x10
#define PCIR_NABMBAR 0x14

#define PCIR_MMBAR 0x18
#define PCIR_MBBAR 0x1C

#define PCIR_ICH_LEGACY 0x41
#define ICH_LEGACY_ENABLE	0x01

/* Native Audio Bus Master Control Registers */
#define ICH_REG_X_BDBAR 0x00
#define ICH_REG_X_CIV   0x04
#define ICH_REG_X_LVI   0x05
#define ICH_REG_X_SR    0x06
#define ICH_REG_X_PICB  0x08
#define ICH_REG_X_PIV   0x0a
#define ICH_REG_X_CR    0x0b

#define ICH_REG_PI_BASE	0x00
#define ICH_REG_PO_BASE	0x10
#define ICH_REG_MC_BASE	0x20

#define ICH_REG_GLOB_CNT 0x2c
#define ICH_REG_GLOB_STA 0x30
#define ICH_REG_ACC_SEMA 0x34

/* Status Register Values */
#define ICH_X_SR_DCH   0x0001
#define ICH_X_SR_CELV  0x0002
#define ICH_X_SR_LVBCI 0x0004
#define ICH_X_SR_BCIS  0x0008
#define ICH_X_SR_FIFOE 0x0010

/* Control Register Values */
#define ICH_X_CR_RPBM  0x01
#define ICH_X_CR_RR    0x02
#define ICH_X_CR_LVBIE 0x04
#define ICH_X_CR_FEIE  0x08
#define ICH_X_CR_IOCE  0x10

/* Global Control Register Values */
#define ICH_GLOB_CTL_GIE  0x00000001
#define ICH_GLOB_CTL_COLD 0x00000002 /* negate */
#define ICH_GLOB_CTL_WARM 0x00000004
#define ICH_GLOB_CTL_SHUT 0x00000008
#define ICH_GLOB_CTL_PRES 0x00000010
#define ICH_GLOB_CTL_SRES 0x00000020

/* Global Status Register Values */
#define ICH_GLOB_STA_GSCI   0x00000001
#define ICH_GLOB_STA_MIINT  0x00000002
#define ICH_GLOB_STA_MOINT  0x00000004
#define ICH_GLOB_STA_PIINT  0x00000020
#define ICH_GLOB_STA_POINT  0x00000040
#define ICH_GLOB_STA_MINT   0x00000080
#define ICH_GLOB_STA_PCR    0x00000100
#define ICH_GLOB_STA_SCR    0x00000200
#define ICH_GLOB_STA_PRES   0x00000400
#define ICH_GLOB_STA_SRES   0x00000800
#define ICH_GLOB_STA_SLOT12 0x00007000
#define ICH_GLOB_STA_RCODEC 0x00008000
#define ICH_GLOB_STA_AD3    0x00010000
#define ICH_GLOB_STA_MD3    0x00020000
#define ICH_GLOB_STA_IMASK  (ICH_GLOB_STA_MIINT | ICH_GLOB_STA_MOINT | ICH_GLOB_STA_PIINT | ICH_GLOB_STA_POINT | ICH_GLOB_STA_MINT | ICH_GLOB_STA_PRES | ICH_GLOB_STA_SRES)

/* play/record buffer */
#define ICH_BDC_IOC 0x80000000
#define ICH_BDC_BUP 0x40000000

