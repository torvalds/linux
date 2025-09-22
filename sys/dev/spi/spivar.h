/* $OpenBSD: spivar.h,v 1.2 2021/10/31 15:12:00 kettenis Exp $ */
/*
 * Copyright (c) 2018 Patrick Wildt <patrick@blueri.se>
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

struct spi_config {
	int		 sc_cs;
	int		 sc_flags;
#define SPI_CONFIG_CPOL		(1 << 0)
#define SPI_CONFIG_CPHA		(1 << 1)
#define SPI_CONFIG_CS_HIGH	(1 << 2)
	int		 sc_bpw;
	uint32_t	 sc_freq;
	u_int		 sc_cs_delay;
};

#define SPI_KEEP_CS		(1 << 0)

typedef struct spi_controller {
	void		*sc_cookie;
	void		(*sc_config)(void *, struct spi_config *);
	int		(*sc_transfer)(void *, char *, char *, int, int);
	int		(*sc_acquire_bus)(void *, int);
	void		(*sc_release_bus)(void *, int);
} *spi_tag_t;

struct spi_attach_args {
	spi_tag_t	 sa_tag;
	char		*sa_name;
	void		*sa_cookie;
};

#define	spi_config(sc, config)						\
	(*(sc)->sc_config)((sc)->sc_cookie, (config))
#define	spi_read(sc, data, len)						\
	(*(sc)->sc_transfer)((sc)->sc_cookie, NULL, (data), (len), 0)
#define	spi_write(sc, data, len)					\
	(*(sc)->sc_transfer)((sc)->sc_cookie, (data), NULL, (len), 0)
#define	spi_transfer(sc, out, in, len, flags)				\
	(*(sc)->sc_transfer)((sc)->sc_cookie, (out), (in), (len), (flags))
#define	spi_acquire_bus(sc, flags)					\
	(*(sc)->sc_acquire_bus)((sc)->sc_cookie, (flags))
#define	spi_release_bus(sc, flags)					\
	(*(sc)->sc_release_bus)((sc)->sc_cookie, (flags))
