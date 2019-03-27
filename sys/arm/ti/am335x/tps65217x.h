/*-
 * Copyright (c) 2012 Damjan Marion <dmarion@FreeBSD.org>
 * Copyright (c) 2015 Maksym Sobolyev <sobomax@FreeBSD.org>
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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __TPS65217X_H__
#define __TPS65217X_H__

/*
 * TPS65217 PMIC is a companion chip for AM335x SoC sitting on I2C bus
 */

/* TPS65217 Registers */
#define	TPS65217_CHIPID_REG	0x00
struct tps65217_chipid_reg {
	unsigned int rev:4;
	unsigned int chip:4;
#define TPS65217A		0x7
#define TPS65217B		0xF
#define TPS65217C		0xE
#define TPS65217D		0x6
} __attribute__((__packed__));

#define	TPS65217_INT_REG	0x02
struct tps65217_int_reg {
	unsigned int usbi:1;
	unsigned int aci:1;
	unsigned int pbi:1;
	unsigned int reserved3:1;
	unsigned int usbm:1;
	unsigned int acm:1;
	unsigned int pbm:1;
	unsigned int reserved7:1;
} __attribute__((__packed__));

#define	TPS65217_STATUS_REG	0x0A
struct tps65217_status_reg {
	unsigned int pb:1;
	unsigned int reserved1:1;
	unsigned int usbpwr:1;
	unsigned int acpwr:1;
	unsigned int reserved4:3;
	unsigned int off:1;
} __attribute__((__packed__));

#define	TPS65217_CHGCONFIG0_REG	0x03
struct tps65217_chgconfig0_reg {
	unsigned int battemp:1;
	unsigned int pchgtout:1;
	unsigned int chgtout:1;
	unsigned int active:1;
	unsigned int termi:1;
	unsigned int tsusp:1;
	unsigned int dppm:1;
	unsigned int treg:1;
} __attribute__((__packed__));

#define	TPS65217_CHGCONFIG1_REG	0x04
struct tps65217_chgconfig1_reg {
	unsigned int chg_en:1;
	unsigned int susp:1;
	unsigned int term:1;
	unsigned int reset:1;
	unsigned int ntc_type:1;
	unsigned int tmr_en:1;
	unsigned int timer:2;
} __attribute__((__packed__));

#define	TPS65217_CHGCONFIG2_REG	0x05
struct tps65217_chgconfig2_reg {
	unsigned int reserved:4;
	unsigned int voreg:2;
#define	TPS65217_VO_410V	0b00
#define	TPS65217_VO_415V	0b01
#define	TPS65217_VO_420V	0b10
#define	TPS65217_VO_425V	0b11
	unsigned int vprechg:1;
	unsigned int dyntmr:1;
} __attribute__((__packed__));

#define	TPS65217_CHGCONFIG3_REG	0x06
struct tps65217_chgconfig3_reg {
	unsigned int trange:1;
	unsigned int termif:2;
	unsigned int pchrgt:1;
	unsigned int dppmth:2;
	unsigned int ichrg:2;
} __attribute__((__packed__));

#endif /* __TPS65217X_H__ */
