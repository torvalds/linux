/*	$OpenBSD: lm78var.h,v 1.20 2022/04/08 15:02:28 naddy Exp $	*/

/*
 * Copyright (c) 2005, 2006 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * National Semiconductor LM78/79/81 registers
 */

#define LM_POST_RAM	0x00	/* POST RAM occupies 0x00 -- 0x1f */
#define LM_VALUE_RAM	0x20	/* Value RAM occupies 0x20 -- 0x3f */
#define LM_FAN1		0x28	/* FAN1 reading */
#define LM_FAN2		0x29	/* FAN2 reading */
#define LM_FAN3		0x2a	/* FAN3 reading */

#define LM_CONFIG	0x40	/* Configuration */ 
#define LM_ISR1		0x41	/* Interrupt Status 1 */
#define LM_ISR2		0x42	/* Interrupt Status 2 */
#define LM_SMI1		0x43	/* SMI# Mask 1 */
#define LM_SMI2		0x44	/* SMI# Mask 2 */
#define LM_NMI1		0x45	/* NMI Mask 1 */
#define LM_NMI2		0x46	/* NMI Mask 2 */
#define LM_VIDFAN	0x47	/* VID/Fan Divisor */
#define LM_SBUSADDR	0x48	/* Serial Bus Address */
#define LM_CHIPID	0x49	/* Chip Reset/ID */

/* Chip IDs */

#define LM_CHIPID_LM78	0x00
#define LM_CHIPID_LM78J	0x40
#define LM_CHIPID_LM79	0xC0
#define LM_CHIPID_LM81	0x80
#define LM_CHIPID_MASK	0xfe

/*
 * Winbond registers
 *
 * Several models exists.  The W83781D is mostly compatible with the
 * LM78, but has two extra temperatures.  Later models add extra
 * voltage sensors, fans and bigger fan divisors to accommodate slow
 * running fans.  To accommodate the extra sensors some models have
 * different memory banks.
 */

#define WB_T23ADDR	0x4a	/* Temperature 2 and 3 Serial Bus Address */
#define WB_PIN		0x4b	/* Pin Control */
#define WB_BANKSEL	0x4e	/* Bank Select */
#define WB_VENDID	0x4f	/* Vendor ID */

/* Bank 0 regs */
#define WB_BANK0_CHIPID	0x58	/* Chip ID */
#define WB_BANK0_FAN45	0x5c	/* Fan 4/5 Divisor Control (W83791D only) */
#define WB_BANK0_VBAT	0x5d	/* VBAT Monitor Control */
#define WB_BANK0_FAN4	0xba	/* Fan 4 reading (W83791D only) */
#define WB_BANK0_FAN5	0xbb	/* Fan 5 reading (W83791D only) */

#define WB_BANK0_CONFIG	0x18	/* VRM & OVT Config (W83627THF/W83637HF) */

/* Bank 1 registers */
#define WB_BANK1_T2H	0x50	/* Temperature 2 High Byte */
#define WB_BANK1_T2L	0x51	/* Temperature 2 Low Byte */

/* Bank 2 registers */
#define WB_BANK2_T3H	0x50	/* Temperature 3 High Byte */
#define WB_BANK2_T3L	0x51	/* Temperature 3 Low Byte */

/* Bank 4 registers (W83782D/W83627HF and later models only) */
#define WB_BANK4_T1OFF	0x54	/* Temperature 1 Offset */
#define WB_BANK4_T2OFF	0x55	/* Temperature 2 Offset */
#define WB_BANK4_T3OFF	0x56	/* Temperature 3 Offset */

/* Bank 5 registers (W83782D/W83627HF and later models only) */
#define WB_BANK5_5VSB	0x50	/* 5VSB reading */
#define WB_BANK5_VBAT	0x51	/* VBAT reading */

/* Bank selection */
#define WB_BANKSEL_B0	0x00	/* Bank 0 */
#define WB_BANKSEL_B1	0x01	/* Bank 1 */
#define WB_BANKSEL_B2	0x02	/* Bank 2 */
#define WB_BANKSEL_B3	0x03	/* Bank 3 */
#define WB_BANKSEL_B4	0x04	/* Bank 4 */
#define WB_BANKSEL_B5	0x05	/* Bank 5 */
#define WB_BANKSEL_HBAC	0x80	/* Register 0x4f High Byte Access */

/* Vendor IDs */
#define WB_VENDID_WINBOND	0x5ca3	/* Winbond */
#define WB_VENDID_ASUS		0x12c3	/* ASUS */

/* Chip IDs */
#define WB_CHIPID_W83781D	0x10
#define WB_CHIPID_W83781D_2	0x11
#define WB_CHIPID_W83627HF	0x21
#define WB_CHIPID_AS99127F	0x31 /* Asus W83781D clone */
#define WB_CHIPID_W83782D	0x30
#define WB_CHIPID_W83783S	0x40
#define WB_CHIPID_W83697HF	0x60
#define WB_CHIPID_W83791D	0x71
#define WB_CHIPID_W83791SD	0x72
#define WB_CHIPID_W83792D	0x7a
#define WB_CHIPID_W83637HF	0x80
#define WB_CHIPID_W83627EHF_A	0x88 /* early version, only for ASUS MBs */
#define WB_CHIPID_W83627THF	0x90
#define WB_CHIPID_W83627EHF	0xa1
#define WB_CHIPID_W83627DHG	0xc1 /* also used in WBSIO_ID_NCT6776F */

/* Config bits */
#define WB_CONFIG_VMR9		0x01

/* Reference voltage (mV) */
#define WB_VREF			3600
#define WB_W83627EHF_VREF	2048

#define WB_MAX_SENSORS  36

struct lm_softc;

struct lm_sensor {
	char *desc;
	enum sensor_type type;
	u_int8_t bank;
	u_int8_t reg;
	void (*refresh)(struct lm_softc *, int);
	int rfact;
};

struct lm_softc {
	struct device sc_dev;

	struct ksensor sensors[WB_MAX_SENSORS];
	struct ksensordev sensordev;
	struct sensor_task *sensortask;
	const struct lm_sensor *lm_sensors;
	u_int numsensors;
	void (*refresh_sensor_data) (struct lm_softc *);

	u_int8_t (*lm_readreg)(struct lm_softc *, int);
	void (*lm_writereg)(struct lm_softc *, int, int);

	u_int8_t sbusaddr;
	u_int8_t chipid;
	u_int8_t sioid;
	u_int8_t vrm9;
};

void lm_attach(struct lm_softc *);
