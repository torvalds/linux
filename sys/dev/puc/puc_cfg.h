/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_PUC_CFG_H_
#define	_DEV_PUC_CFG_H_

#define	DEFAULT_RCLK	1843200

#define	PUC_PORT_NONSTANDARD	0
#define	PUC_PORT_1P		1	/* 1 parallel port */
#define	PUC_PORT_1S		2	/* 1 serial port */
#define	PUC_PORT_1S1P		3	/* 1 serial + 1 parallel ports */
#define	PUC_PORT_1S2P		4	/* 1 serial + 2 parallel ports */
#define	PUC_PORT_2P		5	/* 2 parallel ports */
#define	PUC_PORT_2S		6	/* 2 serial ports */
#define	PUC_PORT_2S1P		7	/* 2 serial + 1 parallel ports */
#define	PUC_PORT_3S		8	/* 3 serial ports */
#define	PUC_PORT_4S		9	/* 4 serial ports */
#define	PUC_PORT_4S1P		10	/* 4 serial + 1 parallel ports */
#define	PUC_PORT_6S		11	/* 6 serial ports */
#define	PUC_PORT_8S		12	/* 8 serial ports */
#define	PUC_PORT_12S		13	/* 12 serial ports */
#define	PUC_PORT_16S		14	/* 16 serial ports */

/* Interrupt Latch Register (ILR) types */
#define PUC_ILR_NONE		0
#define PUC_ILR_DIGI		1
#define PUC_ILR_QUATECH		2

/* Configuration queries. */
enum puc_cfg_cmd {
	PUC_CFG_GET_CLOCK,
	PUC_CFG_GET_DESC,
	PUC_CFG_GET_ILR,
	PUC_CFG_GET_LEN,
	PUC_CFG_GET_NPORTS,
	PUC_CFG_GET_OFS,
	PUC_CFG_GET_RID,
	PUC_CFG_GET_TYPE,
	PUC_CFG_SETUP
};

struct puc_softc;

typedef int puc_config_f(struct puc_softc *, enum puc_cfg_cmd, int, intptr_t *);

struct puc_cfg {
	uint16_t	vendor;
	uint16_t	device;
	uint16_t	subvendor;
	uint16_t	subdevice;
	const char	*desc;
	int		clock;
	int8_t		ports;
	int8_t		rid;		/* Rid of first port */
	int8_t		d_rid;		/* Delta rid of next ports */
	int8_t		d_ofs;		/* Delta offset of next ports */
	puc_config_f 	*config_function;
};

puc_config_f puc_config;

#endif /* _DEV_PUC_CFG_H_ */
