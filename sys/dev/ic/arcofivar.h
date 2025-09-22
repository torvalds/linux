/*	$OpenBSD: arcofivar.h,v 1.3 2016/09/19 06:46:44 ratchov Exp $	*/

/*
 * Copyright (c) 2011 Miodrag Vallat.
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

#define	ARCOFI_NREGS		6

struct arcofi_softc {
	struct device		sc_dev;
	bus_addr_t		sc_reg[ARCOFI_NREGS];
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	void			*sc_sih;

	int			sc_open;
	int			sc_mode;

	struct {
		uint8_t	cr3, cr4;
		uint	gr_idx, gx_idx;
		int	output_mute;
	}			sc_active,
				sc_shadow;

	struct {
		uint8_t	*buf;
		uint8_t	*past;
		void	(*cb)(void *);
		void	*cbarg;
	}			sc_recv,
				sc_xmit;
};

void	arcofi_attach(struct arcofi_softc *, const char *);
int	arcofi_hwintr(void *);
void	arcofi_swintr(void *);
