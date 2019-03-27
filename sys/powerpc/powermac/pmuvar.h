/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Michael Lorenz
 * Copyright (c) 2008 Nathan Whitehorn
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef PMUVAR_H
#define PMUVAR_H

/* PMU commands */
#define PMU_POWER_CTRL0		0x10	/* control power of some devices */
#define PMU_POWER_CTRL		0x11	/* control power of some devices */

#define PMU_POWER_OFF		0x7e	/* Turn Power off */
#define PMU_RESET_CPU		0xd0	/* Reset CPU */

#define PMU_SET_RTC		0x30	/* Set realtime clock */
#define PMU_READ_RTC		0x38	/* Read realtime clock */

#define PMU_WRITE_PRAM		0x32	/* Write PRAM */
#define PMU_READ_PRAM		0x3a	/* Read PRAM */

#define PMU_WRITE_NVRAM		0x33	/* Write NVRAM */
#define PMU_READ_NVRAM		0x3b	/* Read NVRAM */

#define PMU_EJECT_PCMCIA	0x4c	/* Eject PCMCIA slot */

#define PMU_SET_BRIGHTNESS	0x41	/* Set backlight brightness */
#define PMU_READ_BRIGHTNESS	0xd9	/* Read brightness button position */

#define PMU_POWER_EVENTS        0x8f    /* Send power-event commands to PMU */
#define PMU_SYSTEM_READY        0xdf    /* tell PMU we are awake */

#define PMU_BATTERY_STATE	0x6b	/* Read old battery state */
#define PMU_SMART_BATTERY_STATE	0x6f	/* Read battery state */

#define PMU_ADB_CMD		0x20	/* Send ADB packet */
#define PMU_ADB_POLL_OFF	0x21	/* Disable ADB auto-poll */
#define PMU_SET_VOL		0x40	/* Set volume button position */
#define PMU_GET_VOL		0x48	/* Get volume button position */
#define PMU_SET_IMASK		0x70	/* Set interrupt mask */
#define PMU_INT_ACK		0x78	/* Read interrupt bits */
#define PMU_CPU_SPEED		0x7d	/* Control CPU speed on some models */
#define PMU_SLEEP		0x7f	/* Put CPU to sleep */
#define PMU_SET_POLL_MASK	0x86	/*
					 * 16bit mask enables autopolling per
					 * device
					 */
#define PMU_I2C_CMD		0x9a	/* i2c commands */
#define PMU_GET_LID_STATE	0xdc	/* Report lid state */
#define PMU_GET_VERSION		0xea	/* Identify thyself */
#define	PMU_SET_SLEEPLED	0xee	/* Set sleep LED on/off */

/* Bits in PMU interrupt and interrupt mask bytes */
#define PMU_INT_ADB_AUTO	0x04	/* ADB autopoll, when PMU_INT_ADB */
#define PMU_INT_PCEJECT		0x04	/* PC-card eject buttons */
#define PMU_INT_SNDBRT		0x08	/* sound/brightness up/down buttons */
#define PMU_INT_ADB		0x10	/* ADB autopoll or reply data */
#define PMU_INT_BATTERY		0x20
#define PMU_INT_ENVIRONMENT	0x40
#define PMU_INT_TICK		0x80	/* 1-second tick interrupt */

/* Bits to use with the PMU_POWER_CTRL0 command */
#define PMU_POW0_ON		0x80	/* OR this to power ON the device */
#define PMU_POW0_OFF		0x00	/* leave bit 7 to 0 to power it OFF */
#define PMU_POW0_HARD_DRIVE	0x04	/* wallstreet/lombard? */

/* Bits to use with the PMU_POWER_CTRL command */
#define PMU_POW_ON		0x80	/* OR this to power ON the device */
#define PMU_POW_OFF		0x00	/* leave bit 7 to 0 to power it OFF */
#define PMU_POW_BACKLIGHT	0x01	/* backlight power */
#define PMU_POW_CHARGER		0x02	/* battery charger power */
#define PMU_POW_IRLED		0x04	/* IR led power (on wallstreet) */
#define PMU_POW_MEDIABAY	0x08	/* media bay power (wallstreet/lombard ?) */

/* Bits from PMU_GET_LID_STATE or PMU_INT_ENVIRONMENT on core99 */
#define PMU_ENV_LID_CLOSED	0x01	/* The lid is closed */
#define PMU_ENV_POWER		0x08	/* Power Button pressed */

/* PMU PMU_POWER_EVENTS commands */
enum {
	PMU_PWR_GET_POWERUP_EVENTS      = 0x00,
	PMU_PWR_SET_POWERUP_EVENTS      = 0x01,
	PMU_PWR_CLR_POWERUP_EVENTS      = 0x02,
	PMU_PWR_GET_WAKEUP_EVENTS       = 0x03,
	PMU_PWR_SET_WAKEUP_EVENTS       = 0x04,
	PMU_PWR_CLR_WAKEUP_EVENTS       = 0x05,
};

/* PMU Power Information */

#define PMU_PWR_AC_PRESENT	(1 << 0)
#define PMU_PWR_BATT_CHARGING	(1 << 1)
#define PMU_PWR_BATT_PRESENT	(1 << 2)
#define PMU_PWR_BATT_FULL	(1 << 5)
#define PMU_PWR_PCHARGE_RESET	(1 << 6)
#define PMU_PWR_BATT_EXIST	(1 << 7)


/* I2C related definitions */
#define PMU_I2C_MODE_SIMPLE	0
#define PMU_I2C_MODE_STDSUB	1
#define PMU_I2C_MODE_COMBINED	2

#define PMU_I2C_BUS_STATUS	0
#define PMU_I2C_BUS_SYSCLK	1
#define PMU_I2C_BUS_POWER	2

#define PMU_I2C_STATUS_OK	0
#define PMU_I2C_STATUS_DATAREAD	1
#define PMU_I2C_STATUS_BUSY	0xfe

/* Power events wakeup bits */
enum {
	PMU_PWR_WAKEUP_KEY		= 0x01, /* Wake on key press */
	PMU_PWR_WAKEUP_AC_INSERT	= 0x02, /* Wake on AC adapter plug */
	PMU_PWR_WAKEUP_AC_CHANGE 	= 0x04,
	PMU_PWR_WAKEUP_LID_OPEN		= 0x08,
	PMU_PWR_WAKEUP_RING		= 0x10,
};

#define PMU_NOTREADY	0x1	/* has not been initialized yet */
#define PMU_IDLE	0x2	/* the bus is currently idle */
#define PMU_OUT		0x3	/* sending out a command */
#define PMU_IN		0x4	/* receiving data */

struct pmu_softc {
	device_t	sc_dev;
	int		sc_memrid;
	struct resource	*sc_memr;
	int     	sc_irqrid;
        struct resource *sc_irq;
        void    	*sc_ih;

	struct mtx	sc_mutex;
	device_t	adb_bus;
	volatile int	sc_autopoll;
	int		sc_batteries;
	struct cdev	*sc_leddev;
	int		lid_closed;
	uint8_t		saved_regs[9];
};

struct pmu_battstate {
	int state;

	int charge;
	int maxcharge;
	int current;
	int voltage;
};

int pmu_set_speed(int low_speed);

#endif /* PMUVAR_H */
