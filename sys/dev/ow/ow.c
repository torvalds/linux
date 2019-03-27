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
#include <sys/malloc.h>
#include <sys/module.h>

#include <dev/ow/ow.h>
#include <dev/ow/owll.h>
#include <dev/ow/own.h>

/*
 * lldev - link level device
 * ndev - network / transport device (this module)
 * pdev - presentation device (children of this module)
 */

typedef int ow_enum_fn(device_t, device_t);
typedef int ow_found_fn(device_t, romid_t);

struct ow_softc 
{
	device_t	dev;		/* Newbus driver back pointer */
	struct mtx	mtx;		/* bus mutex */
	device_t	owner;		/* bus owner, if != NULL */
};

struct ow_devinfo
{
	romid_t	romid;
};

static int ow_acquire_bus(device_t ndev, device_t pdev, int how);
static void ow_release_bus(device_t ndev, device_t pdev);

#define	OW_LOCK(_sc) mtx_lock(&(_sc)->mtx)
#define	OW_UNLOCK(_sc) mtx_unlock(&(_sc)->mtx)
#define	OW_LOCK_DESTROY(_sc) mtx_destroy(&_sc->mtx)
#define	OW_ASSERT_LOCKED(_sc) mtx_assert(&_sc->mtx, MA_OWNED)
#define	OW_ASSERT_UNLOCKED(_sc) mtx_assert(&_sc->mtx, MA_NOTOWNED)

static MALLOC_DEFINE(M_OW, "ow", "House keeping data for 1wire bus");

static struct ow_timing timing_regular = {
	.t_slot = 60,		/* 60 to 120 */
	.t_low0 = 60,		/* really 60 to 120 */
	.t_low1 = 1,		/* really 1 to 15 */
	.t_release = 45,	/* <= 45us */
	.t_rec = 1,		/* at least 1us */
	.t_rdv = 15,		/* 15us */
	.t_rstl = 480,		/* 480us or more */
	.t_rsth = 480,		/* 480us or more */
	.t_pdl = 60,		/* 60us to 240us */
 	.t_pdh = 60,		/* 15us to 60us */
	.t_lowr = 1,		/* 1us */
};

/* NB: Untested */
static struct ow_timing timing_overdrive = {
	.t_slot = 11,		/* 6us to 16us */
	.t_low0 = 6,		/* really 6 to 16 */
	.t_low1 = 1,		/* really 1 to 2 */
	.t_release = 4,		/* <= 4us */
	.t_rec = 1,		/* at least 1us */
	.t_rdv = 2,		/* 2us */
	.t_rstl = 48,		/* 48us to 80us */
	.t_rsth = 48,		/* 48us or more  */
	.t_pdl = 8,		/* 8us to 24us */
	.t_pdh = 2,		/* 2us to 6us */
	.t_lowr = 1,		/* 1us */
};

static void
ow_send_byte(device_t lldev, struct ow_timing *t, uint8_t byte)
{
	int i;
	
	for (i = 0; i < 8; i++)
		if (byte & (1 << i))
			OWLL_WRITE_ONE(lldev, t);
		else
			OWLL_WRITE_ZERO(lldev, t);
}

static void
ow_read_byte(device_t lldev, struct ow_timing *t, uint8_t *bytep)
{
	int i;
	uint8_t byte = 0;
	int bit;
	
	for (i = 0; i < 8; i++) {
		OWLL_READ_DATA(lldev, t, &bit);
		byte |= bit << i;
	}
	*bytep = byte;
}

static int
ow_send_command(device_t ndev, device_t pdev, struct ow_cmd *cmd)
{
	int present, i, bit, tries;
	device_t lldev;
	struct ow_timing *t;

	lldev = device_get_parent(ndev);

	/*
	 * Retry the reset a couple of times before giving up.
	 */
	tries = 4;
	do {
		OWLL_RESET_AND_PRESENCE(lldev, &timing_regular, &present);
		if (present == 1)
			device_printf(ndev, "Reset said no device on bus?.\n");
	} while (present == 1 && tries-- > 0);
	if (present == 1) {
		device_printf(ndev, "Reset said the device wasn't there.\n");
		return ENOENT;		/* No devices acked the RESET */
	}
	if (present == -1) {
		device_printf(ndev, "Reset discovered bus wired wrong.\n");
		return ENOENT;
	}

	for (i = 0; i < cmd->rom_len; i++)
		ow_send_byte(lldev, &timing_regular, cmd->rom_cmd[i]);
	for (i = 0; i < cmd->rom_read_len; i++)
		ow_read_byte(lldev, &timing_regular, cmd->rom_read + i);
	if (cmd->xpt_len) {
		/*
		 * Per AN937, the reset pulse and ROM level are always
		 * done with the regular timings. Certain ROM commands
		 * put the device into overdrive mode for the remainder
		 * of the data transfer, which is why we have to pass the
		 * timings here. Commands that need to be handled like this
		 * are expected to be flagged by the client.
		 */
		t = (cmd->flags & OW_FLAG_OVERDRIVE) ?
		    &timing_overdrive : &timing_regular;
		for (i = 0; i < cmd->xpt_len; i++)
			ow_send_byte(lldev, t, cmd->xpt_cmd[i]);
		if (cmd->flags & OW_FLAG_READ_BIT) {
			memset(cmd->xpt_read, 0, (cmd->xpt_read_len + 7) / 8);
			for (i = 0; i < cmd->xpt_read_len; i++) {
				OWLL_READ_DATA(lldev, t, &bit);
				cmd->xpt_read[i / 8] |= bit << (i % 8);
			}
		} else {
			for (i = 0; i < cmd->xpt_read_len; i++)
				ow_read_byte(lldev, t, cmd->xpt_read + i);
		}
	}
	return 0;
}

static int
ow_search_rom(device_t lldev, device_t dev)
{
	struct ow_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.rom_cmd[0] = SEARCH_ROM;
	cmd.rom_len = 1;
	return ow_send_command(lldev, dev, &cmd);
}

#if 0
static int
ow_alarm_search(device_t lldev, device_t dev)
{
	struct ow_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.rom_cmd[0] = ALARM_SEARCH;
	cmd.rom_len = 1;
	return ow_send_command(lldev, dev, &cmd);
}
#endif

static int
ow_add_child(device_t dev, romid_t romid)
{
	struct ow_devinfo *di;
	device_t child;

	di = malloc(sizeof(*di), M_OW, M_WAITOK);
	di->romid = romid;
	child = device_add_child(dev, NULL, -1);
	if (child == NULL) {
		free(di, M_OW);
		return ENOMEM;
	}
	device_set_ivars(child, di);
	return (0);
}

static device_t
ow_child_by_romid(device_t dev, romid_t romid)
{
	device_t *children, retval, child;
	int nkid, i;
	struct ow_devinfo *di;

	if (device_get_children(dev, &children, &nkid) != 0)
		return (NULL);
	retval = NULL;
	for (i = 0; i < nkid; i++) {
		child = children[i];
		di = device_get_ivars(child);
		if (di->romid == romid) {
			retval = child;
			break;
		}
	}
	free(children, M_TEMP);

	return (retval);
}

/*
 * CRC generator table -- taken from AN937 DOW CRC LOOKUP FUNCTION Table 2
 */
const uint8_t ow_crc_table[] = {
	0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32, 163, 253, 31, 65,
	157, 195, 33, 127, 252, 162, 64, 30, 95, 1, 227, 189, 62, 96, 130, 220,
	35, 125, 159, 193, 66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
	190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158, 29, 67, 161, 255,
	70, 24, 250, 164, 39, 121, 155, 197, 132, 218, 56, 102, 229, 187, 89, 7,
	219, 133,103, 57, 186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
	101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69, 198, 152, 122, 36,
	248, 166, 68, 26, 153, 199, 37, 123, 58, 100, 134, 216, 91, 5, 231, 185,
	140,210, 48, 110, 237, 179, 81, 15, 78, 16, 242,  172, 47, 113,147, 205,
	17, 79, 173, 243, 112, 46, 204, 146, 211,141, 111, 49, 178, 236, 14, 80,
	175, 241, 19, 77, 206, 144, 114, 44, 109, 51, 209, 143, 12, 82,176, 238,
	50, 108, 142, 208, 83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
	202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234, 105, 55, 213, 139,
	87, 9, 235, 181, 54, 104, 138, 212, 149, 203, 41, 119, 244, 170, 72, 22,
	233, 183, 85, 11, 136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
	116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84, 215, 137, 107, 53
};

/*
 * Converted from DO_CRC page 131 ANN937
 */
static uint8_t
ow_crc(device_t ndev, device_t pdev, uint8_t *buffer, size_t len)
{
	uint8_t crc = 0;
	int i;

	for (i = 0; i < len; i++)
		crc = ow_crc_table[crc ^ buffer[i]];
	return crc;
}

static int
ow_check_crc(romid_t romid)
{
	return ow_crc(NULL, NULL, (uint8_t *)&romid, sizeof(romid)) == 0;
}

static int
ow_device_found(device_t dev, romid_t romid)
{

	/* XXX Move this up into enumerate? */
	/*
	 * All valid ROM IDs have a valid CRC. Check that first.
	 */
	if (!ow_check_crc(romid)) {
		device_printf(dev, "Device romid %8D failed CRC.\n",
		    &romid, ":");
		return EINVAL;
	}

	/*
	 * If we've seen this child before, don't add a new one for it.
	 */
	if (ow_child_by_romid(dev, romid) != NULL)
		return 0;

	return ow_add_child(dev, romid);
}

static int
ow_enumerate(device_t dev, ow_enum_fn *enumfp, ow_found_fn *foundfp)
{
	device_t lldev = device_get_parent(dev);
	int first, second, i, dir, prior, last, err, retries;
	uint64_t probed, last_mask;
	int sanity = 10;

	prior = -1;
	last_mask = 0;
	retries = 0;
	last = -2;
	err = ow_acquire_bus(dev, dev, OWN_DONTWAIT);
	if (err != 0)
		return err;
	while (last != -1) {
		if (sanity-- < 0) {
			printf("Reached the sanity limit\n");
			return EIO;
		}
again:
		probed = 0;
		last = -1;

		/*
		 * See AN397 section 5.II.C.3 for the algorithm (though a bit
		 * poorly stated). The search command forces each device to
		 * send ROM ID bits one at a time (first the bit, then the
		 * complement) the master (us) sends back a bit. If the
		 * device's bit doesn't match what we send back, that device
		 * stops sending bits back. So each time through we remember
		 * where we made the last decision (always 0). If there's a
		 * conflict there this time (and there will be in the absence
		 * of a hardware failure) we go with 1. This way, we prune the
		 * devices on the bus and wind up with a unique ROM. We know
		 * we're done when we detect no new conflicts. The same
		 * algorithm is used for devices in alarm state as well.
		 *
		 * In addition, experience has shown that sometimes devices
		 * stop responding in the middle of enumeration, so try this
		 * step again a few times when that happens. It is unclear if
		 * this is due to a nosiy electrical environment or some odd
		 * timing issue.
		 */

		/*
		 * The enumeration command should be successfully sent, if not,
		 * we have big issues on the bus so punt. Lower layers report
		 * any unusual errors, so we don't need to here.
		 */
		err = enumfp(dev, dev);
		if (err != 0)
			return (err);

		for (i = 0; i < 64; i++) {
			OWLL_READ_DATA(lldev, &timing_regular, &first);
			OWLL_READ_DATA(lldev, &timing_regular, &second);
			switch (first | second << 1) {
			case 0: /* Conflict */
				if (i < prior)
					dir = (last_mask >> i) & 1;
				else
					dir = i == prior;

				if (dir == 0)
					last = i;
				break;
			case 1: /* 1 then 0 -> 1 for all */
				dir = 1;
				break;
			case 2: /* 0 then 1 -> 0 for all */
				dir = 0;
				break;
			case 3:
				/*
				 * No device responded. This is unexpected, but
				 * experience has shown that on some platforms
				 * we miss a timing window, or otherwise have
				 * an issue. Start this step over. Since we've
				 * not updated prior yet, we can just jump to
				 * the top of the loop for a re-do of this step.
				 */
				printf("oops, starting over\n");
				if (++retries > 5)
					return (EIO);
				goto again;
			default: /* NOTREACHED */
				__unreachable();
			}
			if (dir) {
				OWLL_WRITE_ONE(lldev, &timing_regular);
				probed |= 1ull << i;
			} else {
				OWLL_WRITE_ZERO(lldev, &timing_regular);
			}
		}
		retries = 0;
		foundfp(dev, probed);
		last_mask = probed;
		prior = last;
	}
	ow_release_bus(dev, dev);

	return (0);
}

static int
ow_probe(device_t dev)
{

	device_set_desc(dev, "1 Wire Bus");
	return (BUS_PROBE_GENERIC);
}

static int
ow_attach(device_t ndev)
{
	struct ow_softc *sc;

	/*
	 * Find all the devices on the bus. We don't probe / attach them in the
	 * enumeration phase. We do this because we want to allow the probe /
	 * attach routines of the child drivers to have as full an access to the
	 * bus as possible. While we reset things before the next step of the
	 * search (so it would likely be OK to allow access by the clients to
	 * the bus), it is more conservative to find them all, then to do the
	 * attach of the devices. This also allows the child devices to have
	 * more knowledge of the bus. We also ignore errors from the enumeration
	 * because they might happen after we've found a few devices.
	 */
	sc = device_get_softc(ndev);
	sc->dev = ndev;
	mtx_init(&sc->mtx, device_get_nameunit(sc->dev), "ow", MTX_DEF);
	ow_enumerate(ndev, ow_search_rom, ow_device_found);
	return bus_generic_attach(ndev);
}

static int
ow_detach(device_t ndev)
{
	device_t *children, child;
	int nkid, i;
	struct ow_devinfo *di;
	struct ow_softc *sc;

	sc = device_get_softc(ndev);
	/*
	 * detach all the children first. This is blocking until any threads
	 * have stopped, etc.
	 */
	bus_generic_detach(ndev);

	/*
	 * We delete all the children, and free up the ivars 
	 */
	if (device_get_children(ndev, &children, &nkid) != 0)
		return ENOMEM;
	for (i = 0; i < nkid; i++) {
		child = children[i];
		di = device_get_ivars(child);
		free(di, M_OW);
		device_delete_child(ndev, child);
	}
	free(children, M_TEMP);

	OW_LOCK_DESTROY(sc);
	return 0;
}

/*
 * Not sure this is really needed. I'm having trouble figuring out what
 * location means in the context of the one wire bus.
 */
static int
ow_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{

	*buf = '\0';
	return (0);
}

static int
ow_child_pnpinfo_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	struct ow_devinfo *di;

	di = device_get_ivars(child);
	snprintf(buf, buflen, "romid=%8D", &di->romid, ":");
	return (0);
}

static int
ow_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
	struct ow_devinfo *di;
	romid_t **ptr;

	di = device_get_ivars(child);
	switch (which) {
	case OW_IVAR_FAMILY:
		*result = di->romid & 0xff;
		break;
	case OW_IVAR_ROMID:
		ptr = (romid_t **)result;
		*ptr = &di->romid;
		break;
	default:
		return EINVAL;
	}

	return 0;
}

static int
ow_write_ivar(device_t dev, device_t child, int which, uintptr_t value)
{

	return EINVAL;
}

static int
ow_print_child(device_t ndev, device_t pdev)
{
	int retval = 0;
	struct ow_devinfo *di;

	di = device_get_ivars(pdev);

	retval += bus_print_child_header(ndev, pdev);
	retval += printf(" romid %8D", &di->romid, ":");
	retval += bus_print_child_footer(ndev, pdev);

	return retval;
}

static void
ow_probe_nomatch(device_t ndev, device_t pdev)
{
	struct ow_devinfo *di;

	di = device_get_ivars(pdev);
	device_printf(ndev, "romid %8D: no driver\n", &di->romid, ":");
}

static int
ow_acquire_bus(device_t ndev, device_t pdev, int how)
{
	struct ow_softc *sc;

	sc = device_get_softc(ndev);
	OW_ASSERT_UNLOCKED(sc);
	OW_LOCK(sc);
	if (sc->owner != NULL) {
		if (sc->owner == pdev)
			panic("%s: %s recursively acquiring the bus.\n",
			    device_get_nameunit(ndev),
			    device_get_nameunit(pdev));
		if (how == OWN_DONTWAIT) {
			OW_UNLOCK(sc);
			return EWOULDBLOCK;
		}
		while (sc->owner != NULL)
			mtx_sleep(sc, &sc->mtx, 0, "owbuswait", 0);
	}
	sc->owner = pdev;
	OW_UNLOCK(sc);

	return 0;
}

static void
ow_release_bus(device_t ndev, device_t pdev)
{
	struct ow_softc *sc;

	sc = device_get_softc(ndev);
	OW_ASSERT_UNLOCKED(sc);
	OW_LOCK(sc);
	if (sc->owner == NULL)
		panic("%s: %s releasing unowned bus.", device_get_nameunit(ndev),
		    device_get_nameunit(pdev));
	if (sc->owner != pdev)
		panic("%s: %s don't own the bus. %s does. game over.",
		    device_get_nameunit(ndev), device_get_nameunit(pdev),
		    device_get_nameunit(sc->owner));
	sc->owner = NULL;
	wakeup(sc);
	OW_UNLOCK(sc);
}

devclass_t ow_devclass;

static device_method_t ow_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ow_probe),
	DEVMETHOD(device_attach,	ow_attach),
	DEVMETHOD(device_detach,	ow_detach),

	/* Bus interface */
	DEVMETHOD(bus_child_pnpinfo_str, ow_child_pnpinfo_str),
	DEVMETHOD(bus_child_location_str, ow_child_location_str),
	DEVMETHOD(bus_read_ivar,	ow_read_ivar),
	DEVMETHOD(bus_write_ivar,	ow_write_ivar),
	DEVMETHOD(bus_print_child,	ow_print_child),
	DEVMETHOD(bus_probe_nomatch,	ow_probe_nomatch),

	/* One Wire Network/Transport layer interface */
	DEVMETHOD(own_send_command,	ow_send_command),
	DEVMETHOD(own_acquire_bus,	ow_acquire_bus),
	DEVMETHOD(own_release_bus,	ow_release_bus),
	DEVMETHOD(own_crc,		ow_crc),
	{ 0, 0 }
};

static driver_t ow_driver = {
	"ow",
	ow_methods,
	sizeof(struct ow_softc),
};

DRIVER_MODULE(ow, owc, ow_driver, ow_devclass, 0, 0);
MODULE_VERSION(ow, 1);
