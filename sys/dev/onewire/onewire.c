/*	$OpenBSD: onewire.c,v 1.20 2025/06/25 20:28:09 miod Exp $	*/

/*
 * Copyright (c) 2006 Alexander Yurchenko <grange@openbsd.org>
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
 * 1-Wire bus driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/rwlock.h>

#include <dev/onewire/onewirereg.h>
#include <dev/onewire/onewirevar.h>

#ifdef ONEWIRE_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

#define ONEWIRE_MAXDEVS		16
#define ONEWIRE_SCANTIME	SEC_TO_NSEC(3)

struct onewire_softc {
	struct device			sc_dev;

	struct onewire_bus *		sc_bus;
	struct rwlock			sc_lock;
	struct proc *			sc_thread;
	TAILQ_HEAD(, onewire_device)	sc_devs;

	int				sc_dying;
	int				sc_flags;
	u_int64_t			sc_rombuf[ONEWIRE_MAXDEVS];
};

struct onewire_device {
	TAILQ_ENTRY(onewire_device)	d_list;
	struct device *			d_dev;	/* may be NULL */
	u_int64_t			d_rom;
	int				d_present;
};

int	onewire_match(struct device *, void *, void *);
void	onewire_attach(struct device *, struct device *, void *);
int	onewire_detach(struct device *, int);
int	onewire_activate(struct device *, int);
int	onewire_print(void *, const char *);

void	onewire_thread(void *);
void	onewire_createthread(void *);
void	onewire_scan(struct onewire_softc *);

const struct cfattach onewire_ca = {
	sizeof(struct onewire_softc),
	onewire_match,
	onewire_attach,
	onewire_detach,
	onewire_activate
};

struct cfdriver onewire_cd = {
	NULL, "onewire", DV_DULL
};

int
onewire_match(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;

	return (strcmp(cf->cf_driver->cd_name, "onewire") == 0);
}

void
onewire_attach(struct device *parent, struct device *self, void *aux)
{
	struct onewire_softc *sc = (struct onewire_softc *)self;
	struct onewirebus_attach_args *oba = aux;

	sc->sc_bus = oba->oba_bus;
	sc->sc_flags = oba->oba_flags;
	rw_init(&sc->sc_lock, sc->sc_dev.dv_xname);
	TAILQ_INIT(&sc->sc_devs);

	printf("\n");

	if (sc->sc_flags & ONEWIRE_SCAN_NOW) {
		onewire_scan(sc);
		if (sc->sc_flags & ONEWIRE_NO_PERIODIC_SCAN)
			return;
	}

	kthread_create_deferred(onewire_createthread, sc);
}

int
onewire_detach(struct device *self, int flags)
{
	struct onewire_softc *sc = (struct onewire_softc *)self;

	sc->sc_dying = 1;
	if (sc->sc_thread != NULL) {
		wakeup(sc->sc_thread);
		tsleep_nsec(&sc->sc_dying, PWAIT, "owdt", INFSLP);
	}

	return (config_detach_children(self, flags));
}

int
onewire_activate(struct device *self, int act)
{
	struct onewire_softc *sc = (struct onewire_softc *)self;

	switch (act) {
	case DVACT_DEACTIVATE:
		sc->sc_dying = 1;
		break;
	}

	return (config_activate_children(self, act));
}

int
onewire_print(void *aux, const char *pnp)
{
	struct onewire_attach_args *oa = aux;
	const char *famname;

	if (pnp == NULL)
		printf(" ");

	famname = onewire_famname(ONEWIRE_ROM_FAMILY_TYPE(oa->oa_rom));
	if (famname == NULL)
		printf("family 0x%02x", ONEWIRE_ROM_FAMILY_TYPE(oa->oa_rom));
	else
		printf("\"%s\"", famname);
	printf(" sn %012llx", ONEWIRE_ROM_SN(oa->oa_rom));

	if (pnp != NULL)
		printf(" at %s", pnp);

	return (UNCONF);
}

int
onewirebus_print(void *aux, const char *pnp)
{
	if (pnp != NULL)
		printf("onewire at %s", pnp);

	return (UNCONF);
}

int
onewire_lock(void *arg, int flags)
{
	struct onewire_softc *sc = arg;
	int lflags = RW_WRITE;

	if (flags & ONEWIRE_NOWAIT)
		lflags |= RW_NOSLEEP;

	return (rw_enter(&sc->sc_lock, lflags));
}

void
onewire_unlock(void *arg)
{
	struct onewire_softc *sc = arg;

	rw_exit(&sc->sc_lock);
}

int
onewire_reset(void *arg)
{
	struct onewire_softc *sc = arg;
	struct onewire_bus *bus = sc->sc_bus;

	return (bus->bus_reset(bus->bus_cookie));
}

int
onewire_read_byte(void *arg)
{
	struct onewire_softc *sc = arg;
	struct onewire_bus *bus = sc->sc_bus;
	u_int8_t value = 0;
	int i;

	if (bus->bus_read_byte != NULL)
		return (bus->bus_read_byte(bus->bus_cookie));

	for (i = 0; i < 8; i++)
		value |= (bus->bus_bit(bus->bus_cookie, 1) << i);

	return (value);
}

void
onewire_write_byte(void *arg, int value)
{
	struct onewire_softc *sc = arg;
	struct onewire_bus *bus = sc->sc_bus;
	int i;

	if (bus->bus_write_byte != NULL)
		return (bus->bus_write_byte(bus->bus_cookie, value));

	for (i = 0; i < 8; i++)
		bus->bus_bit(bus->bus_cookie, (value >> i) & 0x1);
}

void
onewire_read_block(void *arg, void *buf, int len)
{
	struct onewire_softc *sc = arg;
	struct onewire_bus *bus = sc->sc_bus;
	u_int8_t *p = buf;

	if (bus->bus_read_block != NULL)
		return (bus->bus_read_block(bus->bus_cookie, buf, len));

	while (len--)
		*p++ = onewire_read_byte(arg);
}

int
onewire_triplet(void *arg, int dir)
{
	struct onewire_softc *sc = arg;
	struct onewire_bus *bus = sc->sc_bus;
	int rv;

	if (bus->bus_triplet != NULL)
		return (bus->bus_triplet(bus->bus_cookie, dir));

	rv = bus->bus_bit(bus->bus_cookie, 1);
	rv <<= 1;
	rv |= bus->bus_bit(bus->bus_cookie, 1);

	switch (rv) {
	case 0x0:
		bus->bus_bit(bus->bus_cookie, dir);
		break;
	case 0x1:
		bus->bus_bit(bus->bus_cookie, 0);
		break;
	default:
		bus->bus_bit(bus->bus_cookie, 1);
	}

	return (rv);
}

void
onewire_matchrom(void *arg, u_int64_t rom)
{
	struct onewire_softc *sc = arg;
	struct onewire_bus *bus = sc->sc_bus;
	int i;

	if (bus->bus_matchrom != NULL)
		return (bus->bus_matchrom(bus->bus_cookie, rom));

	onewire_write_byte(arg, ONEWIRE_CMD_MATCH_ROM);
	for (i = 0; i < 8; i++)
		onewire_write_byte(arg, (rom >> (i * 8)) & 0xff);
}

int
onewire_search(void *arg, u_int64_t *buf, int size, u_int64_t startrom)
{
	struct onewire_softc *sc = arg;
	struct onewire_bus *bus = sc->sc_bus;
	int search = 1, count = 0, lastd = -1, dir, rv, i, i0;
	u_int64_t mask, rom = startrom, lastrom;
	u_int8_t data[8];

	if (bus->bus_search != NULL)
		return (bus->bus_search(bus->bus_cookie, buf, size, rom));

	while (search && count < size) {
		/* XXX: yield processor */
		tsleep_nsec(sc, PWAIT, "owscan", MSEC_TO_NSEC(100));

		/*
		 * Start new search. Go through the previous path to
		 * the point we made a decision last time and make an
		 * opposite decision. If we didn't make any decision
		 * stop searching.
		 */
		lastrom = rom;
		rom = 0;
		onewire_lock(sc, 0);
		onewire_reset(sc);
		onewire_write_byte(sc, ONEWIRE_CMD_SEARCH_ROM);
		for (i = 0, i0 = -1; i < 64; i++) {
			dir = (lastrom >> i) & 0x1;
			if (i == lastd)
				dir = 1;
			else if (i > lastd)
				dir = 0;
			rv = onewire_triplet(sc, dir);
			switch (rv) {
			case 0x0:
				if (i != lastd && dir == 0)
					i0 = i;
				mask = dir;
				break;
			case 0x1:
				mask = 0;
				break;
			case 0x2:
				mask = 1;
				break;
			default:
				DPRINTF(("%s: search triplet error 0x%x, "
				    "step %d\n",
				    sc->sc_dev.dv_xname, rv, i));
				onewire_unlock(sc);
				return (-1);
			}
			rom |= (mask << i);
		}
		onewire_unlock(sc);

		if ((lastd = i0) == -1)
			search = 0;

		if (rom == 0)
			continue;

		/*
		 * The last byte of the ROM code contains a CRC calculated
		 * from the first 7 bytes. Re-calculate it to make sure
		 * we found a valid device.
		 */
		for (i = 0; i < 8; i++)
			data[i] = (rom >> (i * 8)) & 0xff;
		if (onewire_crc(data, 7) != data[7])
			continue;

		buf[count++] = rom;
	}

	return (count);
}

void
onewire_thread(void *arg)
{
	struct onewire_softc *sc = arg;

	while (!sc->sc_dying) {
		onewire_scan(sc);
		if (sc->sc_flags & ONEWIRE_NO_PERIODIC_SCAN)
			break;
		tsleep_nsec(sc->sc_thread, PWAIT, "owidle", ONEWIRE_SCANTIME);
	}

	sc->sc_thread = NULL;
	wakeup(&sc->sc_dying);
	kthread_exit(0);
}

void
onewire_createthread(void *arg)
{
	struct onewire_softc *sc = arg;

	if (kthread_create(onewire_thread, sc, &sc->sc_thread,
	    sc->sc_dev.dv_xname) != 0)
		printf("%s: can't create kernel thread\n",
		    sc->sc_dev.dv_xname);
}

void
onewire_scan(struct onewire_softc *sc)
{
	struct onewire_device *d, *next, *nd;
	struct onewire_attach_args oa;
	struct device *dev;
	int present;
	u_int64_t rom;
	int i, rv;

	/*
	 * Mark all currently present devices as absent before
	 * scanning. This allows to find out later which devices
	 * have been disappeared.
	 */
	TAILQ_FOREACH(d, &sc->sc_devs, d_list)
		d->d_present = 0;

	/*
	 * Reset the bus. If there's no presence pulse don't search
	 * for any devices.
	 */
	onewire_lock(sc, 0);
	rv = onewire_reset(sc);
	onewire_unlock(sc);
	if (rv != 0) {
		DPRINTF(("%s: no presence pulse\n", sc->sc_dev.dv_xname));
		goto out;
	}

	/* Scan the bus */
	if ((rv = onewire_search(sc, sc->sc_rombuf, ONEWIRE_MAXDEVS, 0)) == -1)
		return;

	for (i = 0; i < rv; i++) {
		rom = sc->sc_rombuf[i];

		/*
		 * Go through the list of attached devices to see if we
		 * found a new one.
		 */
		present = 0;
		TAILQ_FOREACH(d, &sc->sc_devs, d_list) {
			if (d->d_rom == rom) {
				d->d_present = 1;
				present = 1;
				break;
			}
		}
		if (!present) {
			nd = malloc(sizeof(struct onewire_device),
			    M_DEVBUF, M_NOWAIT);
			if (nd == NULL)
				continue;

			bzero(&oa, sizeof(oa));
			oa.oa_onewire = sc;
			oa.oa_rom = rom;
			dev = config_found(&sc->sc_dev, &oa, onewire_print);

			nd->d_dev = dev;
			nd->d_rom = rom;
			nd->d_present = 1;
			TAILQ_INSERT_TAIL(&sc->sc_devs, nd, d_list);
		}
	}

out:
	/* Detach disappeared devices */
	TAILQ_FOREACH_SAFE(d, &sc->sc_devs, d_list, next) {
		if (!d->d_present) {
			if (d->d_dev != NULL)
				config_detach(d->d_dev, DETACH_FORCE);
			TAILQ_REMOVE(&sc->sc_devs, d, d_list);
			free(d, M_DEVBUF, sizeof *d);
		}
	}
}
