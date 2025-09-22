/*	$OpenBSD: itvar.h,v 1.15 2013/03/21 18:20:00 brynet Exp $	*/

/*
 * Copyright (c) 2007-2008 Oleg Safiullin <form@pdp-11.org.ru>
 * Copyright (c) 2006-2007 Juan Romero Pardines <juan@xtrarom.org>
 * Copyright (c) 2003 Julien Bordet <zejames@greyhats.org>
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
 */

#ifndef _DEV_ISA_ITVAR_H_
#define _DEV_ISA_ITVAR_H_

#define IT_EC_INTERVAL		5
#define IT_EC_NUMSENSORS	17
#define IT_EC_VREF		4096

#define IO_IT1			0x2e
#define IO_IT2			0x4e

#define IT_IO_ADDR		0x00
#define IT_IO_DATA		0x01

#define IT_ID_8705		0x8705
#define IT_ID_8712		0x8712
#define IT_ID_8716		0x8716
#define IT_ID_8718		0x8718
#define IT_ID_8720		0x8720
#define IT_ID_8721		0x8721
#define IT_ID_8726		0x8726
#define IT_ID_8728		0x8728
#define IT_ID_8772		0x8772

#define IT_CCR			0x02
#define IT_LDN			0x07
#define IT_CHIPID1		0x20
#define IT_CHIPID2		0x21
#define IT_CHIPREV		0x22

#define IT_EC_LDN		0x04
#define IT_EC_MSB		0x60
#define IT_EC_LSB		0x61

#define IT_EC_ADDR		0x05
#define IT_EC_DATA		0x06

#define IT_EC_CFG		0x00
#define IT_EC_FAN_DIV		0x0b
#define IT_EC_FAN_ECER		0x0c
#define IT_EC_FAN_TAC1		0x0d
#define IT_EC_FAN_TAC2		0x0e
#define IT_EC_FAN_TAC3		0x0f
#define IT_EC_FAN_MCR		0x13
#define IT_EC_FAN_EXT_TAC1	0x18
#define IT_EC_FAN_EXT_TAC2	0x19
#define IT_EC_FAN_EXT_TAC3	0x1a
#define IT_EC_VOLTBASE		0x20
#define IT_EC_TEMPBASE		0x29
#define IT_EC_ADC_VINER		0x50
#define IT_EC_ADC_TEMPER	0x51
#define IT_EC_FAN_TAC4_LSB	0x80
#define IT_EC_FAN_TAC4_MSB	0x81
#define IT_EC_FAN_TAC5_LSB	0x82
#define IT_EC_FAN_TAC5_MSB	0x83

#define IT_EC_CFG_START		0x01
#define IT_EC_CFG_INTCLR	0x08
#define IT_EC_CFG_UPDVBAT	0x40

#define IT_WDT_LDN		0x07

#define IT_WDT_CSR		0x71
#define IT_WDT_TCR		0x72
#define IT_WDT_TMO_LSB		0x73
#define IT_WDT_TMO_MSB		0x74

#define IT_WDT_TCR_SECS		0x80
#define IT_WDT_TCR_KRST		0x40
#define IT_WDT_TCR_PWROK	0x10


struct it_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_iobase;
	int			sc_ec_iobase;
	u_int16_t		sc_chipid;
	u_int8_t		sc_chiprev;

	bus_space_tag_t		sc_ec_iot;
	bus_space_handle_t	sc_ec_ioh;

	struct ksensor		sc_sensors[IT_EC_NUMSENSORS];
	struct ksensordev	sc_sensordev;

	LIST_ENTRY(it_softc)	sc_list;
};

#endif	/* _DEV_ISA_ITVAR_H_ */
