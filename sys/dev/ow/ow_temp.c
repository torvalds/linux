/*-
 * Copyright (c) 2015 M. Warner Losh <imp@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>

#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/libkern.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <dev/ow/ow.h>
#include <dev/ow/own.h>

#define OWT_DS1820	0x10		/* Also 18S20 */
#define OWT_DS1822	0x22		/* Very close to 18B20 */
#define OWT_DS18B20	0x28		/* Also MAX31820 */
#define	OWT_DS1825	0x3B		/* Just like 18B20 with address bits */

#define	CONVERT_T		0x44
#define COPY_SCRATCHPAD		0x48
#define WRITE_SCRATCHPAD	0x4e
#define READ_POWER_SUPPLY	0xb4
#define	RECALL_EE		0xb8
#define	READ_SCRATCHPAD		0xbe


#define	OW_TEMP_DONE		0x01
#define	OW_TEMP_RUNNING		0x02

struct ow_temp_softc
{
	device_t	dev;
	int		type;
	int		temp;
	int		flags;
	int		bad_crc;
	int		bad_reads;
	int		reading_interval;
	int		parasite;
	struct mtx	temp_lock;
	struct proc	*event_thread;
};

static int
ow_temp_probe(device_t dev)
{

	switch (ow_get_family(dev)) {
	case OWT_DS1820:
		device_set_desc(dev, "One Wire Temperature");
		return BUS_PROBE_DEFAULT;
	case OWT_DS1822:
	case OWT_DS1825:
	case OWT_DS18B20:
		device_set_desc(dev, "Advanced One Wire Temperature");
		return BUS_PROBE_DEFAULT;
	default:
		return ENXIO;
	}
}

static int
ow_temp_read_scratchpad(device_t dev, uint8_t *scratch, int len)
{
	struct ow_cmd cmd;
	
	own_self_command(dev, &cmd, READ_SCRATCHPAD);
	cmd.xpt_read_len = len;
	own_command_wait(dev, &cmd);
	memcpy(scratch, cmd.xpt_read, len);

	return 0;
}

static int
ow_temp_convert_t(device_t dev)
{
	struct ow_cmd cmd;

	own_self_command(dev, &cmd, CONVERT_T);
	own_command_wait(dev, &cmd);

	return 0;
}


static int
ow_temp_read_power_supply(device_t dev, int *parasite)
{
	struct ow_cmd cmd;

	own_self_command(dev, &cmd, READ_POWER_SUPPLY);
	cmd.flags |= OW_FLAG_READ_BIT;
	cmd.xpt_read_len = 1;
	own_command_wait(dev, &cmd);
	*parasite = !cmd.xpt_read[0];	/* parasites pull bus low */

	return 0;
}

static void
ow_temp_event_thread(void *arg)
{
	struct ow_temp_softc *sc;
	uint8_t scratch[8 + 1];
	uint8_t crc;
	int retries, rv, tmp;

	sc = arg;
	pause("owtstart", device_get_unit(sc->dev) * hz / 100);	// 10ms stagger
	mtx_lock(&sc->temp_lock);
	sc->flags |= OW_TEMP_RUNNING;
	ow_temp_read_power_supply(sc->dev, &sc->parasite);
	if (sc->parasite)
		device_printf(sc->dev, "Running in parasitic mode unsupported\n");
	while ((sc->flags & OW_TEMP_DONE) == 0) {
		mtx_unlock(&sc->temp_lock);
		ow_temp_convert_t(sc->dev);
		mtx_lock(&sc->temp_lock);
		msleep(sc, &sc->temp_lock, 0, "owtcvt", hz);
		if (sc->flags & OW_TEMP_DONE)
			break;
		for (retries = 5; retries > 0; retries--) {
			mtx_unlock(&sc->temp_lock);
			rv = ow_temp_read_scratchpad(sc->dev, scratch, sizeof(scratch));
			mtx_lock(&sc->temp_lock);
			if (rv == 0) {
				crc = own_crc(sc->dev, scratch, sizeof(scratch) - 1);
				if (crc == scratch[8]) {
					if (sc->type == OWT_DS1820) {
						if (scratch[7]) {
							/*
							 * Formula from DS18S20 datasheet, page 6
							 * DS18S20 datasheet says count_per_c is 16, DS1820 does not
							 */
							tmp = (int16_t)((scratch[0] & 0xfe) |
							    (scratch[1] << 8)) << 3;
							tmp += 16 - scratch[6] - 4; /* count_per_c == 16 */
						} else
							tmp = (int16_t)(scratch[0] | (scratch[1] << 8)) << 3;
					} else
						tmp = (int16_t)(scratch[0] | (scratch[1] << 8));
					sc->temp = tmp * 1000 / 16 + 273150;
					break;
				}
				sc->bad_crc++;
			} else
				sc->bad_reads++;
		}
		msleep(sc, &sc->temp_lock, 0, "owtcvt", sc->reading_interval);
	}
	sc->flags &= ~OW_TEMP_RUNNING;
	mtx_unlock(&sc->temp_lock);
	kproc_exit(0);
}

static int
ow_temp_attach(device_t dev)
{
	struct ow_temp_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->type = ow_get_family(dev);
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "temperature", CTLFLAG_RD | CTLTYPE_INT,
	    &sc->temp, 0, sysctl_handle_int,
	    "IK3", "Current Temperature");
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "badcrc", CTLFLAG_RD,
	    &sc->bad_crc, 0,
	    "Number of Bad CRC on reading scratchpad");
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "badread", CTLFLAG_RD,
	    &sc->bad_reads, 0,
	    "Number of errors on reading scratchpad");
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "reading_interval", CTLFLAG_RW,
	    &sc->reading_interval, 0,
	    "ticks between reads");
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "parasite", CTLFLAG_RW,
	    &sc->parasite, 0,
	    "In Parasite mode");
	/*
	 * Just do this for unit 0 to avoid locking
	 * the ow bus until that code can be put
	 * into place.
	 */
	sc->temp = -1;
	sc->reading_interval = 10 * hz;
	mtx_init(&sc->temp_lock, "lock for doing temperature", NULL, MTX_DEF);
	/* Start the thread */
	if (kproc_create(ow_temp_event_thread, sc, &sc->event_thread, 0, 0,
	    "%s event thread", device_get_nameunit(dev))) {
		device_printf(dev, "unable to create event thread.\n");
		panic("ow_temp_attach, can't create thread");
	}

	return 0;
}

static int
ow_temp_detach(device_t dev)
{
	struct ow_temp_softc *sc;

	sc = device_get_softc(dev);

	/*
	 * Wait for the thread to die.  kproc_exit will do a wakeup
	 * on the event thread's struct thread * so that we know it is
	 * safe to proceed.  IF the thread is running, set the please
	 * die flag and wait for it to comply.  Since the wakeup on
	 * the event thread happens only in kproc_exit, we don't
	 * need to loop here.
	 */
	mtx_lock(&sc->temp_lock);
	sc->flags |= OW_TEMP_DONE;
	while (sc->flags & OW_TEMP_RUNNING) {
		wakeup(sc);
		msleep(sc->event_thread, &sc->temp_lock, PWAIT, "owtun", 0);
	}
	mtx_destroy(&sc->temp_lock);
	
	return 0;
}

devclass_t ow_temp_devclass;

static device_method_t ow_temp_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ow_temp_probe),
	DEVMETHOD(device_attach,	ow_temp_attach),
	DEVMETHOD(device_detach,	ow_temp_detach),

	{ 0, 0 }
};

static driver_t ow_temp_driver = {
	"ow_temp",
	ow_temp_methods,
	sizeof(struct ow_temp_softc),
};

DRIVER_MODULE(ow_temp, ow, ow_temp_driver, ow_temp_devclass, 0, 0);
MODULE_DEPEND(ow_temp, ow, 1, 1, 1);
