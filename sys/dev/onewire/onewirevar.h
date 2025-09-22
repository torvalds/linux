/*	$OpenBSD: onewirevar.h,v 1.8 2025/06/25 20:28:09 miod Exp $	*/

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

#ifndef _DEV_ONEWIRE_ONEWIREVAR_H_
#define _DEV_ONEWIRE_ONEWIREVAR_H_

/*
 * 1-Wire bus interface.
 */

/* Bus master interface */
struct onewire_bus {
	void *	bus_cookie;

	int	(*bus_reset)(void *);
	int	(*bus_bit)(void *, int);
	int	(*bus_read_byte)(void *);
	void	(*bus_write_byte)(void *, int);
	void	(*bus_read_block)(void *, void *, int);
	void	(*bus_write_block)(void *, const void *, int);
	int	(*bus_triplet)(void *, int);
	void	(*bus_matchrom)(void *, u_int64_t);
	int	(*bus_search)(void *, u_int64_t *, int, u_int64_t);
};

/* Bus methods */
int		onewire_lock(void *, int);
void		onewire_unlock(void *);

int		onewire_reset(void *);
int		onewire_read_byte(void *);
void		onewire_write_byte(void *, int);
void		onewire_read_block(void *, void *, int);
int		onewire_triplet(void *, int);
void		onewire_matchrom(void *, u_int64_t);
int		onewire_search(void *, u_int64_t *, int, u_int64_t);

#define ONEWIRE_NOWAIT		0x0001

/* Bus attachment */
struct onewirebus_attach_args {
	struct onewire_bus *	oba_bus;
	int			oba_flags;
#define	ONEWIRE_SCAN_NOW		0x0001
#define	ONEWIRE_NO_PERIODIC_SCAN	0x0002
};

int	onewirebus_print(void *, const char *);

/* Device attachment */
struct onewire_attach_args {
	void *			oa_onewire;
	u_int64_t		oa_rom;
};

/* Family matching */
struct onewire_matchfam {
	int om_type;
};

/* Miscellaneous routines */
int		onewire_crc(const void *, int);
u_int16_t	onewire_crc16(const void *, int);
const char *	onewire_famname(int);
int		onewire_matchbyfam(struct onewire_attach_args *,
		    const struct onewire_matchfam *, int);

/* Bus bit-banging */
struct onewire_bbops {
	void	(*bb_rx)(void *);
	void	(*bb_tx)(void *);
	int	(*bb_get)(void *);
	void	(*bb_set)(void *, int);
};

int		onewire_bb_reset(const struct onewire_bbops *, void *);
int		onewire_bb_bit(const struct onewire_bbops *, void *, int);

#endif	/* !_DEV_ONEWIRE_ONEWIREVAR_H_ */
