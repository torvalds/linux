/* $OpenBSD: lcd.c,v 1.11 2021/10/24 09:18:51 deraadt Exp $ */
/* $NetBSD: lcd.c,v 1.2 2000/01/07 05:13:08 nisimura Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Tohru Nishimura.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>

#include <machine/autoconf.h>
#include <machine/board.h>
#include <machine/conf.h>
#include <machine/lcd.h>

#define PIO1_MODE_OUTPUT	0x84
#define PIO1_MODE_INPUT		0x94

#define POWER	0x10

#define ENABLE	0x80
#define DISABLE	0x00

#define WRITE_CMD	(0x00 | 0x00)
#define WRITE_DATA	(0x00 | 0x40)
#define READ_BUSY	(0x20 | 0x00)
#define READ_DATA	(0x20 | 0x40)

#define LCD_INIT	0x38
#define LCD_ENTRY	0x06
#define LCD_ON		0x0c
#define LCD_CLS		0x01
#define LCD_HOME	0x02
#define LCD_LOCATE(X, Y)	(((Y) & 1 ? 0xc0 : 0x80) | ((X) & 0x0f))

#define LCD_MAXBUFLEN	80

struct pio {
	volatile u_int8_t portA;
	volatile unsigned : 24;
	volatile u_int8_t portB;
	volatile unsigned : 24;
	volatile u_int8_t portC;
	volatile unsigned : 24;
	volatile u_int8_t cntrl;
	volatile unsigned : 24;
};

/* Autoconf stuff */
int  lcd_match(struct device *, void *, void *);
void lcd_attach(struct device *, struct device *, void *);

struct lcd_softc {
	struct device sc_dev;
	int sc_opened;
};

const struct cfattach lcd_ca = {
	sizeof(struct lcd_softc), lcd_match, lcd_attach
};

struct cfdriver lcd_cd = {
	NULL, "lcd", DV_DULL,
};

/* Internal prototypes */
void lcdbusywait(void);
void lcdput(int);
void lcdctrl(int);
void lcdshow(const char *);
void greeting(void);

/* Internal variables */
				     /* "1234567890123456" */
static const char lcd_boot_message1[] = "OpenBSD/luna88k ";
static const char lcd_boot_message2[] = "   SX-9100/DT   ";

/*
 * Autoconf functions
 */
int
lcd_match(struct device *parent, void *cf, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, lcd_cd.cd_name))
		return 0;
	if (badaddr((vaddr_t)ma->ma_addr, 4))
		return 0;
	return 1;
}

void
lcd_attach(struct device *parent, struct device *self, void *aux)
{
	printf("\n");

	/* Say hello to the world on LCD. */
	greeting();
}

/*
 * open/close/write/ioctl
 */

int
lcdopen(dev_t dev, int flags, int fmt, struct proc *p)
{
	int unit = minor(dev);
	struct lcd_softc *sc;

	if (unit >= lcd_cd.cd_ndevs)
		return ENXIO;
	if ((sc = lcd_cd.cd_devs[unit]) == NULL)
		return ENXIO;
	if (sc->sc_opened)
		return EBUSY;
	sc->sc_opened = 1;

	return 0;
}

int
lcdclose(dev_t dev, int flags, int fmt, struct proc *p)
{
	int unit = minor(dev);
	struct lcd_softc *sc;

	sc = lcd_cd.cd_devs[unit];
	sc->sc_opened = 0;

	return 0;
}

int
lcdwrite(dev_t dev, struct uio *uio, int flag)
{
	int error;
	size_t len, i;
	char buf[LCD_MAXBUFLEN];

	len = uio->uio_resid;

	if (len > LCD_MAXBUFLEN)
		return EIO;

	error = uiomove(buf, len, uio);
	if (error)
		return EIO;

	for (i = 0; i < len; i++) {
		lcdput((int)buf[i]);
	}

	return 0;
}

int
lcdioctl(dev_t dev, u_long cmd, caddr_t addr, int flag, struct proc *p)
{
	int val;

	/* check if the device opened with write mode */
	switch(cmd) {
	case LCDCLS:
	case LCDHOME:
	case LCDMODE:
	case LCDDISP:
	case LCDMOVE:
	case LCDSEEK:
	case LCDRESTORE:
		if ((flag & FWRITE) == 0)
			return EACCES;
		break;
	}

	switch(cmd) {
	case LCDCLS:
		lcdctrl(LCD_CLS);
		break;

	case LCDHOME:
		lcdctrl(LCD_HOME);
		break;

	case LCDMODE:
		val = *(int *)addr;
		switch (val) {
		case LCDMODE_C_LEFT:
		case LCDMODE_C_RIGHT:
		case LCDMODE_D_LEFT:
		case LCDMODE_D_RIGHT:
			lcdctrl(val);
			break;
		default:
			return EINVAL;
		}
		break;

	case LCDDISP:
		val = *(int *)addr;
		if ((val & 0x7) != val)
			return EINVAL;
		lcdctrl(val | 0x8);
		break;

	case LCDMOVE:
		val = *(int *)addr;
		switch (val) {
		case LCDMOVE_C_LEFT:
		case LCDMOVE_C_RIGHT:
		case LCDMOVE_D_LEFT:
		case LCDMOVE_D_RIGHT:
			lcdctrl(val);
			break;
		default:
			return EINVAL;
		}
		break;

	case LCDSEEK:
		val = *(int *)addr & 0x7f;
		lcdctrl(val | 0x80);
		break;

	case LCDRESTORE:
		greeting();
		break;

	default:
		return ENOTTY;
	}
	return 0;
}

/*
 * Internal functions
 */
void
lcdbusywait()
{
	struct pio *p1 = (struct pio *)OBIO_PIO1_BASE;
	int msb, s;

	s = splhigh();
	p1->cntrl = PIO1_MODE_INPUT;
	p1->portC = POWER | READ_BUSY | ENABLE;
	splx(s);

	do {
		msb = p1->portA & ENABLE;
		delay(5);
	} while (msb != 0);

	s = splhigh();
	p1->portC = POWER | READ_BUSY | DISABLE;
	splx(s);
}	

void
lcdput(int cc)
{
	struct pio *p1 = (struct pio *)OBIO_PIO1_BASE;
	int s;

	lcdbusywait();

	s = splhigh();
	p1->cntrl = PIO1_MODE_OUTPUT;

	p1->portC = POWER | WRITE_DATA | ENABLE;
	p1->portA = cc;
	p1->portC = POWER | WRITE_DATA | DISABLE;
	splx(s);
}

void
lcdctrl(int cc)
{
	struct pio *p1 = (struct pio *)OBIO_PIO1_BASE;
	int s;

	lcdbusywait();

	s = splhigh();
	p1->cntrl = PIO1_MODE_OUTPUT;

	p1->portC = POWER | WRITE_CMD | ENABLE;
	p1->portA = cc;
	p1->portC = POWER | WRITE_CMD | DISABLE;
	splx(s);
}

void
lcdshow(const char *s)
{
	int cc;

	while ((cc = *s++) != '\0')
		lcdput(cc);
}

void
greeting()
{
	lcdctrl(LCD_INIT);
	lcdctrl(LCD_ENTRY);
	lcdctrl(LCD_ON);

	lcdctrl(LCD_CLS);
	lcdctrl(LCD_HOME);

	lcdctrl(LCD_LOCATE(0, 0));
	lcdshow(lcd_boot_message1);
	lcdctrl(LCD_LOCATE(0, 1));
	lcdshow(lcd_boot_message2);
}
