/*
 * Copyright (c) 2008, 2009 Michael Shalayeff
 * Copyright (c) 2009, 2010 Hans-Joerg Hoexer
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */

#ifndef _TPMVAR_H
#define _TPMVAR_H

struct tpm_softc {
#ifndef __FreeBSD__
	struct device sc_dev;
#endif
	void *sc_ih;

	int	(*sc_init)(struct tpm_softc *, int, const char *);
	int	(*sc_start)(struct tpm_softc *, int);
	int	(*sc_read)(struct tpm_softc *, void *, int, size_t *, int);
	int	(*sc_write)(struct tpm_softc *, void *, int);
	int	(*sc_end)(struct tpm_softc *, int, int);

	bus_space_tag_t sc_bt, sc_batm;
	bus_space_handle_t sc_bh, sc_bahm;

	u_int32_t sc_devid;
	u_int32_t sc_rev;
	u_int32_t sc_stat;
	u_int32_t sc_capabilities;

	int sc_flags;
#define	TPM_OPEN	0x0001

	int	 sc_vector;
#ifdef __FreeBSD__
	void	*intr_cookie;
	int mem_rid, irq_rid;
	struct resource *mem_res, *irq_res;
	struct cdev *sc_cdev;
#endif

#ifndef __FreeBSD__
	void	*sc_powerhook;
#endif
	int	 sc_suspend;
};

int tpm_tis12_probe(bus_space_tag_t iot, bus_space_handle_t ioh);
int tpm_attach(device_t dev);
int tpm_detach(device_t dev);
int tpm_suspend(device_t dev);
int tpm_resume(device_t dev);
#endif
