/*	$OpenBSD: sdmmc_cis.c,v 1.8 2020/04/29 09:44:49 patrick Exp $	*/

/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
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

/* Routines to decode the Card Information Structure of SD I/O cards */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/sdmmc/sdmmc_ioreg.h>
#include <dev/sdmmc/sdmmcdevs.h>
#include <dev/sdmmc/sdmmcvar.h>

u_int32_t sdmmc_cisptr(struct sdmmc_function *);

#ifdef SDMMC_DEBUG
#define DPRINTF(s)	printf s
#else
#define DPRINTF(s)	/**/
#endif

u_int32_t
sdmmc_cisptr(struct sdmmc_function *sf)
{
	struct sdmmc_function *sf0 = sf->sc->sc_fn0;
	u_int32_t cisptr = 0;
	int reg;

	rw_assert_wrlock(&sf->sc->sc_lock);

	reg = SD_IO_CCCR_CISPTR + (sf->number * SD_IO_CCCR_SIZE);
	cisptr |= sdmmc_io_read_1(sf0, reg + 0) << 0;
	cisptr |= sdmmc_io_read_1(sf0, reg + 1) << 8;
	cisptr |= sdmmc_io_read_1(sf0, reg + 2) << 16;

	return cisptr;
}

int
sdmmc_read_cis(struct sdmmc_function *sf, struct sdmmc_cis *cis)
{
	struct sdmmc_function *sf0 = sf->sc->sc_fn0;
	int reg;
	u_int8_t tplcode;
	u_int8_t tpllen;

	rw_assert_wrlock(&sf->sc->sc_lock);

	reg = (int)sdmmc_cisptr(sf);
	if (reg < SD_IO_CIS_START ||
	    reg >= (SD_IO_CIS_START+SD_IO_CIS_SIZE-16)) {
		printf("%s: bad CIS ptr %#x\n", DEVNAME(sf->sc), reg);
		return 1;
	}

	for (;;) {
		tplcode = sdmmc_io_read_1(sf0, reg++);
		if (tplcode == SD_IO_CISTPL_END)
			break;
		if (tplcode == SD_IO_CISTPL_NULL)
			continue;

		tpllen = sdmmc_io_read_1(sf0, reg++);

		switch (tplcode) {
		case SD_IO_CISTPL_FUNCID:
			if (tpllen < 2) {
				printf("%s: bad CISTPL_FUNCID length\n",
				    DEVNAME(sf->sc));
				reg += tpllen;
				break;
			}
			cis->function = sdmmc_io_read_1(sf0, reg);
			reg += tpllen;
			break;
		case SD_IO_CISTPL_MANFID:
			if (tpllen < 4) {
				printf("%s: bad CISTPL_MANFID length\n",
				    DEVNAME(sf->sc));
				reg += tpllen;
				break;
			}
			cis->manufacturer = sdmmc_io_read_1(sf0, reg++);
			cis->manufacturer |= sdmmc_io_read_1(sf0, reg++) << 8;
			cis->product = sdmmc_io_read_1(sf0, reg++);
			cis->product |= sdmmc_io_read_1(sf0, reg++) << 8;
			break;
		case SD_IO_CISTPL_VERS_1:
			if (tpllen < 2) {
				printf("%s: CISTPL_VERS_1 too short\n",
				    DEVNAME(sf->sc));
				reg += tpllen;
				break;
			}
			{
				int start, i, ch, count;

				cis->cis1_major = sdmmc_io_read_1(sf0, reg++);
				cis->cis1_minor = sdmmc_io_read_1(sf0, reg++);

				for (count = 0, start = 0, i = 0;
				     (count < 4) && ((i + 4) < 256); i++) {
					ch = sdmmc_io_read_1(sf0, reg + i);
					if (ch == 0xff)
						break;
					cis->cis1_info_buf[i] = ch;
					if (ch == 0) {
						cis->cis1_info[count] =
						    cis->cis1_info_buf + start;
						start = i + 1;
						count++;
					}
				}

				reg += tpllen - 2;
			}
			break;
		default:
			DPRINTF(("%s: unknown tuple code %#x, length %d\n",
			    DEVNAME(sf->sc), tplcode, tpllen));
			reg += tpllen;
			break;
		}
	}

	return 0;
}

void
sdmmc_print_cis(struct sdmmc_function *sf)
{
	struct sdmmc_cis *cis = &sf->cis;
	int i;

	printf("%s: CIS version %d.%d\n", DEVNAME(sf->sc),
	    cis->cis1_major, cis->cis1_minor);

	printf("%s: CIS info: ", DEVNAME(sf->sc));
	for (i = 0; i < 4; i++) {
		if (cis->cis1_info[i] == NULL)
			break;
		if (i)
			printf(", ");
		printf("%s", cis->cis1_info[i]);
	}
	printf("\n");

	printf("%s: Manufacturer code 0x%x, product 0x%x\n",
	    DEVNAME(sf->sc), cis->manufacturer, cis->product);

	printf("%s: function %d: ", DEVNAME(sf->sc), sf->number);
	switch (sf->cis.function) {
	case TPLFID_FUNCTION_SDIO:
		printf("SDIO");
		break;
	default:
		printf("unknown (%d)", sf->cis.function);
		break;
	}
	printf("\n");
}

void
sdmmc_check_cis_quirks(struct sdmmc_function *sf)
{
	if (sf->cis.manufacturer == SDMMC_VENDOR_SPECTEC &&
	    sf->cis.product == SDMMC_PRODUCT_SPECTEC_SDW820) {
		/* This card lacks the VERS_1 tuple. */
		sf->cis.cis1_major = 0x01;
		sf->cis.cis1_minor = 0x00;
		sf->cis.cis1_info[0] = "Spectec";
		sf->cis.cis1_info[1] = "SDIO WLAN Card";
		sf->cis.cis1_info[2] = "SDW-820";
		sf->cis.cis1_info[3] = "";
	}
}
