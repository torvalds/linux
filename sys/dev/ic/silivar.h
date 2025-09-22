/*	$OpenBSD: silivar.h,v 1.7 2010/08/05 20:21:36 kettenis Exp $ */

/*
 * Copyright (c) 2007 David Gwynne <dlg@openbsd.org>
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

struct sili_port;

struct sili_softc {
	struct device		sc_dev;

	bus_dma_tag_t		sc_dmat;

	bus_space_tag_t		sc_iot_global;
	bus_space_handle_t	sc_ioh_global;
	bus_size_t		sc_ios_global;

	bus_space_tag_t		sc_iot_port;
	bus_space_handle_t	sc_ioh_port;
	bus_size_t		sc_ios_port;

	u_int			sc_nports;
	struct sili_port	*sc_ports;

	struct atascsi		*sc_atascsi;
};
#define DEVNAME(_sc)	((_sc)->sc_dev.dv_xname)

int	sili_attach(struct sili_softc *);
int	sili_detach(struct sili_softc *, int);

void	sili_resume(struct sili_softc *);

int	sili_intr(void *);
