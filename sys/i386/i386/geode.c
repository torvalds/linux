/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2004 Poul-Henning Kamp
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timetc.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/watchdog.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/led/led.h>
#include <machine/pc/bios.h>

static struct bios_oem bios_soekris = {
    { 0xf0000, 0xf1000 },
    {
	{ "Soekris", 0, 8 },		/* Soekris Engineering. */
	{ "net4", 0, 8 },		/* net45xx */
	{ "comBIOS", 0, 54 },		/* comBIOS ver. 1.26a  20040819 ... */
	{ NULL, 0, 0 },
    }
};

static struct bios_oem bios_soekris_55 = {
    { 0xf0000, 0xf1000 },
    {
	{ "Soekris", 0, 8 },		/* Soekris Engineering. */
	{ "net5", 0, 8 },		/* net5xxx */
	{ "comBIOS", 0, 54 },		/* comBIOS ver. 1.26a  20040819 ... */
	{ NULL, 0, 0 },
    }
};

static struct bios_oem bios_pcengines = {
    { 0xf9000, 0xfa000 },
    {
	{ "PC Engines WRAP", 0, 28 },	/* PC Engines WRAP.1C v1.03 */
	{ "tinyBIOS", 0, 28 },		/* tinyBIOS V1.4a (C)1997-2003 */
	{ NULL, 0, 0 },
    }
};

static struct bios_oem bios_pcengines_55 = {
    { 0xf9000, 0xfa000 },
    {
	{ "PC Engines ALIX", 0, 28 },	/* PC Engines ALIX */
	{ "tinyBIOS", 0, 28 },		/* tinyBIOS V1.4a (C)1997-2005 */
	{ NULL, 0, 0 },
    }
};

static struct bios_oem bios_advantech = {
    { 0xfe000, 0xff000 },
    {
	{ "**** PCM-582", 5, 33 },	/* PCM-5823 BIOS V1.12 ... */
	{ "GXm-Cx5530",	-11, 35 },	/* 06/07/2002-GXm-Cx5530... */
	{ NULL, 0, 0 },
    }
};

static unsigned	cba;
static unsigned	gpio;
static unsigned	geode_counter;

static struct cdev *led1, *led2, *led3;
static int 	led1b, led2b, led3b;

static void
led_func(void *ptr, int onoff)
{
	uint32_t u;
	int bit;

	bit = *(int *)ptr;
	if (bit < 0) {
		bit = -bit;
		onoff = !onoff;
	}

	u = inl(gpio + 4);
	if (onoff)
		u |= 1 << bit;
	else
		u &= ~(1 << bit);
	outl(gpio, u);
}

static void
cs5536_led_func(void *ptr, int onoff)
{
	int bit;
	uint16_t a;

	bit = *(int *)ptr;
	if (bit < 0) {
		bit = -bit;
		onoff = !onoff;
	}

	a = rdmsr(0x5140000c);
	if (bit >= 16) {
		a += 0x80;
		bit -= 16;
	}

	if (onoff)
		outl(a, 1 << bit);
	else
		outl(a, 1 << (bit + 16));
}


static unsigned
geode_get_timecount(struct timecounter *tc)
{
	return (inl(geode_counter));
}

static struct timecounter geode_timecounter = {
	geode_get_timecount,
	NULL,
	0xffffffff,
	27000000,
	"Geode",
	1000
};

static uint64_t
geode_cputicks(void)
{
	unsigned c;
	static unsigned last;
	static uint64_t offset;

	c = inl(geode_counter);
	if (c < last)
		offset += (1LL << 32);
	last = c;
	return (offset | c);
}

/*
 * The GEODE watchdog runs from a 32kHz frequency.  One period of that is
 * 31250 nanoseconds which we round down to 2^14 nanoseconds.  The watchdog
 * consists of a power-of-two prescaler and a 16 bit counter, so the math
 * is quite simple.  The max timeout is 14 + 16 + 13 = 2^43 nsec ~= 2h26m.
 */
static void
geode_watchdog(void *foo __unused, u_int cmd, int *error)
{
	u_int u, p, r;

	u = cmd & WD_INTERVAL;
	if (u >= 14 && u <= 43) {
		u -= 14;
		if (u > 16) {
			p = u - 16;
			u -= p;
		} else {
			p = 0;
		}
		if (u == 16)
			u = (1 << u) - 1;
		else
			u = 1 << u;
		r = inw(cba + 2) & 0xff00;
		outw(cba + 2, p | 0xf0 | r);
		outw(cba, u);
		*error = 0;
	} else {
		outw(cba, 0);
	}
}

/*
 * We run MFGPT0 off the 32kHz frequency and prescale by 16384 giving a
 * period of half a second.
 * Range becomes 2^30 (= 1 sec) to 2^44 (almost 5 hours)
 */
static void
cs5536_watchdog(void *foo __unused, u_int cmd, int *error)
{
	u_int u, p, s;
	uint16_t a;
	uint32_t m;

	a = rdmsr(0x5140000d);

	u = cmd & WD_INTERVAL;
	if (u >= 30 && u <= 44) {
		p = 1 << (u - 29);

		/* Set up MFGPT0, 32khz, prescaler 16k, C2 event */
		outw(a + 6, 0x030e);
		/* set comparator 2 */
		outw(a + 2, p);
		/* reset counter */
		outw(a + 4, 0);
		/* Arm reset mechanism */
		m = rdmsr(0x51400029);
		m |= (1 << 24);
		wrmsr(0x51400029, m);
		/* Start counter */
		outw(a + 6, 0x8000);

		*error = 0;
	} else {
		/* 
		 * MFGPT_SETUP is write-once
		 * Check if the counter has been setup
		 */
		s = inw(a + 6);
		if (s & (1 << 12)) {
			/* Stop and reset counter */
			outw(a + 6, 0);
			outw(a + 4, 0);
		}
	}
}

/*
 * The Advantech PCM-582x watchdog expects 0x1 at I/O port 0x0443
 * every 1.6 secs +/- 30%. Writing 0x0 disables the watchdog
 * NB: reading the I/O port enables the timer as well
 */
static void
advantech_watchdog(void *foo __unused, u_int cmd, int *error)
{
	u_int u;

	u = cmd & WD_INTERVAL;
	if (u > 0 && u <= WD_TO_1SEC) {
		outb(0x0443, 1);
		*error = 0;
	} else {
		outb(0x0443, 0);
	}
}

static int
geode_probe(device_t self)
{
#define BIOS_OEM_MAXLEN 80
	static u_char bios_oem[BIOS_OEM_MAXLEN] = "\0";

	switch (pci_get_devid(self)) {
	case 0x0515100b:
		if (geode_counter == 0) {
			/*
			 * The address of the CBA is written to this register
			 * by the bios, see p161 in data sheet.
			 */
			cba = pci_read_config(self, 0x64, 4);
			if (bootverbose)
				printf("Geode CBA@ 0x%x\n", cba);
			geode_counter = cba + 0x08;
			outl(cba + 0x0d, 2);
			if (bootverbose)
				printf("Geode rev: %02x %02x\n",
					inb(cba + 0x3c), inb(cba + 0x3d));
			tc_init(&geode_timecounter);
			EVENTHANDLER_REGISTER(watchdog_list, geode_watchdog,
			    NULL, 0);
			set_cputicker(geode_cputicks, 27000000, 0);
		}
		break;
	case 0x0510100b:
		gpio = pci_read_config(self, PCIR_BAR(0), 4);
		gpio &= ~0x1f;
		if (bootverbose)
			printf("Geode GPIO@ = %x\n", gpio);
		if (bios_oem_strings(&bios_soekris,
		    bios_oem, sizeof bios_oem) > 0 ) {
			led1b = 20;
			led1 = led_create(led_func, &led1b, "error");
		} else if (bios_oem_strings(&bios_pcengines,
		    bios_oem, sizeof bios_oem) > 0 ) {
			led1b = -2;
			led2b = -3;
			led3b = -18;
			led1 = led_create(led_func, &led1b, "led1");
			led2 = led_create(led_func, &led2b, "led2");
			led3 = led_create(led_func, &led3b, "led3");
			/*
		 	* Turn on first LED so we don't make
			* people think their box just died.
		 	*/
			led_func(&led1b, 1);
		}
		if (*bios_oem)
			printf("Geode %s\n", bios_oem);
		break;
	case 0x01011078:
		if (bios_oem_strings(&bios_advantech,
		    bios_oem, sizeof bios_oem) > 0 ) {
			printf("Geode %s\n", bios_oem);
			EVENTHANDLER_REGISTER(watchdog_list, advantech_watchdog,
			    NULL, 0);
		}
		break;
	case 0x20801022:
		if (bios_oem_strings(&bios_soekris_55,
		    bios_oem, sizeof bios_oem) > 0 ) {
			led1b = 6;
			led1 = led_create(cs5536_led_func, &led1b, "error");
		} else if (bios_oem_strings(&bios_pcengines_55,
		    bios_oem, sizeof bios_oem) > 0 ) {
			led1b = -6;
			led2b = -25;
			led3b = -27;
			led1 = led_create(cs5536_led_func, &led1b, "led1");
			led2 = led_create(cs5536_led_func, &led2b, "led2");
			led3 = led_create(cs5536_led_func, &led3b, "led3");
			/*
		 	* Turn on first LED so we don't make
			* people think their box just died.
		 	*/
			cs5536_led_func(&led1b, 1);
		}
		if (*bios_oem)
			printf("Geode LX: %s\n", bios_oem);
		if (bootverbose)
			printf("MFGPT bar: %jx\n", rdmsr(0x5140000d));
		EVENTHANDLER_REGISTER(watchdog_list, cs5536_watchdog, NULL, 0);
		break;
	}
	return (ENXIO);
}

static int
geode_attach(device_t self)
{

	return(ENODEV);
}

static device_method_t geode_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		geode_probe),
	DEVMETHOD(device_attach,	geode_attach),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	{0, 0}
};
 
static driver_t geode_driver = {
	"geode",
	geode_methods,
	0,
};

static devclass_t geode_devclass;

DRIVER_MODULE(geode, pci, geode_driver, geode_devclass, 0, 0);
