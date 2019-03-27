/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_debug.h>
#include <dev/rtwn/if_rtwn_efuse.h>

#include <dev/rtwn/rtl8192c/r92c_reg.h>


static int
rtwn_efuse_switch_power(struct rtwn_softc *sc)
{
	uint32_t reg;
	int error;

	error = rtwn_write_1(sc, R92C_EFUSE_ACCESS, R92C_EFUSE_ACCESS_ON);
	if (error != 0)
		return (error);

	reg = rtwn_read_2(sc, R92C_SYS_FUNC_EN);
	if (!(reg & R92C_SYS_FUNC_EN_ELDR)) {
		error = rtwn_write_2(sc, R92C_SYS_FUNC_EN,
		    reg | R92C_SYS_FUNC_EN_ELDR);
		if (error != 0)
			return (error);
	}
	reg = rtwn_read_2(sc, R92C_SYS_CLKR);
	if ((reg & (R92C_SYS_CLKR_LOADER_EN | R92C_SYS_CLKR_ANA8M)) !=
	    (R92C_SYS_CLKR_LOADER_EN | R92C_SYS_CLKR_ANA8M)) {
		error = rtwn_write_2(sc, R92C_SYS_CLKR,
		    reg | R92C_SYS_CLKR_LOADER_EN | R92C_SYS_CLKR_ANA8M);
		if (error != 0)
			return (error);
	}

	return (0);
}

int
rtwn_efuse_read_next(struct rtwn_softc *sc, uint8_t *val)
{
	uint32_t reg;
	int ntries, error;

	if (sc->next_rom_addr >= sc->efuse_maxlen)
		return (EFAULT);

	reg = rtwn_read_4(sc, R92C_EFUSE_CTRL);
	reg = RW(reg, R92C_EFUSE_CTRL_ADDR, sc->next_rom_addr);
	reg &= ~R92C_EFUSE_CTRL_VALID;

	error = rtwn_write_4(sc, R92C_EFUSE_CTRL, reg);
	if (error != 0)
		return (error);
	/* Wait for read operation to complete. */
	for (ntries = 0; ntries < 100; ntries++) {
		reg = rtwn_read_4(sc, R92C_EFUSE_CTRL);
		if (reg & R92C_EFUSE_CTRL_VALID)
			break;
		rtwn_delay(sc, 10);
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev,
		    "could not read efuse byte at address 0x%x\n",
		    sc->next_rom_addr);
		return (ETIMEDOUT);
	}

	*val = MS(reg, R92C_EFUSE_CTRL_DATA);
	sc->next_rom_addr++;

	return (0);
}

static int
rtwn_efuse_read_data(struct rtwn_softc *sc, uint8_t *rom, uint8_t off,
    uint8_t msk)
{
	uint8_t reg;
	int addr, i, error;

	for (i = 0; i < 4; i++) {
		if (msk & (1 << i))
			continue;

		addr = off * 8 + i * 2;
		if (addr + 1 >= sc->efuse_maplen)
			return (EFAULT);

		error = rtwn_efuse_read_next(sc, &reg);
		if (error != 0)
			return (error);
		RTWN_DPRINTF(sc, RTWN_DEBUG_ROM, "rom[0x%03X] == 0x%02X\n",
		    addr, reg);
		rom[addr] = reg;

		error = rtwn_efuse_read_next(sc, &reg);
		if (error != 0)
			return (error);
		RTWN_DPRINTF(sc, RTWN_DEBUG_ROM, "rom[0x%03X] == 0x%02X\n",
		    addr + 1, reg);
		rom[addr + 1] = reg;
	}

	return (0);
}

#ifdef RTWN_DEBUG
static void
rtwn_dump_rom_contents(struct rtwn_softc *sc, uint8_t *rom, uint16_t size)
{
	int i;

	/* Dump ROM contents. */
	device_printf(sc->sc_dev, "%s:", __func__);
	for (i = 0; i < size; i++) {
		if (i % 32 == 0)
			printf("\n%03X: ", i);
		else if (i % 4 == 0)
			printf(" ");

		printf("%02X", rom[i]);
	}
	printf("\n");
}
#endif

static int
rtwn_efuse_read(struct rtwn_softc *sc, uint8_t *rom, uint16_t size)
{
#define RTWN_CHK(res) do {	\
	if ((error = res) != 0)	\
		goto end;	\
} while(0)
	uint8_t msk, off, reg;
	int error;

	/* Read full ROM image. */
	sc->next_rom_addr = 0;
	memset(rom, 0xff, size);

	RTWN_CHK(rtwn_efuse_read_next(sc, &reg));
	while (reg != 0xff) {
		/* check for extended header */
		if ((sc->sc_flags & RTWN_FLAG_EXT_HDR) &&
		    (reg & 0x1f) == 0x0f) {
			off = reg >> 5;
			RTWN_CHK(rtwn_efuse_read_next(sc, &reg));

			if ((reg & 0x0f) != 0x0f)
				off = ((reg & 0xf0) >> 1) | off;
			else
				continue;
		} else
			off = reg >> 4;
		msk = reg & 0xf;

		RTWN_CHK(rtwn_efuse_read_data(sc, rom, off, msk));
		RTWN_CHK(rtwn_efuse_read_next(sc, &reg));
	}

end:

#ifdef RTWN_DEBUG
	if (sc->sc_debug & RTWN_DEBUG_ROM)
		rtwn_dump_rom_contents(sc, rom, size);
#endif

	/* Device-specific. */
	rtwn_efuse_postread(sc);

	if (error != 0) {
		device_printf(sc->sc_dev, "%s: error while reading ROM\n",
		    __func__);
	}

	return (error);
#undef RTWN_CHK
}

static int
rtwn_efuse_read_prepare(struct rtwn_softc *sc, uint8_t *rom, uint16_t size)
{
	int error;

	error = rtwn_efuse_switch_power(sc);
	if (error != 0)
		goto fail;

	error = rtwn_efuse_read(sc, rom, size);

fail:
	rtwn_write_1(sc, R92C_EFUSE_ACCESS, R92C_EFUSE_ACCESS_OFF);

	return (error);
}

int
rtwn_read_rom(struct rtwn_softc *sc)
{
	uint8_t *rom;
	int error;

	rom = malloc(sc->efuse_maplen, M_TEMP, M_WAITOK);

	/* Read full ROM image. */
	RTWN_LOCK(sc);
	error = rtwn_efuse_read_prepare(sc, rom, sc->efuse_maplen);
	RTWN_UNLOCK(sc);
	if (error != 0)
		goto fail;

	/* Parse & save data in softc. */
	rtwn_parse_rom(sc, rom);

fail:
	free(rom, M_TEMP);

	return (error);
}
