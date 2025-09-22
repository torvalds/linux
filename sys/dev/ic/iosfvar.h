/*	$OpenBSD: iosfvar.h,v 1.2 2024/05/11 14:49:56 jsg Exp $ */

/*
 * Copyright (c) 2023 David Gwynne <dlg@openbsd.org>
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

#ifndef _DEV_IC_IOSFVAR_H_
#define _DEV_IC_IOSFVAR_H_

/*
 * iosf provider api
 */

struct iosf_mbi {
	struct device		*mbi_dev;
	int			 mbi_prio;
	int			 mbi_semaddr;

	uint32_t	(*mbi_mdr_rd)(struct iosf_mbi *sc, uint32_t, uint32_t);
	void		(*mbi_mdr_wr)(struct iosf_mbi *sc, uint32_t, uint32_t,
			      uint32_t);
};

void	iosf_mbi_attach(struct iosf_mbi *);

/*
 * iosf consumer apis
 */

int	iosf_mbi_available(void);

/* for i2c */
int	iosf_i2c_acquire(int);
void	iosf_i2c_release(int);

/* for drm to coordinate with the rest of the kernel */
void	iosf_mbi_punit_acquire(void);
void	iosf_mbi_punit_release(void);
void	iosf_mbi_assert_punit_acquired(void);

#ifdef nyetyet
int	iosf_mbi_read(uint8_t, uint8_t, uint32_t, uint32_t *);
int	iosf_mbi_write(uint8_t, uint8_t, uint32_t, uint32_t);
int	iosf_mbi_modify(uint8_t, uint8_t, uint32_t, uint32_t, uint32_t);
#endif

#endif /* _DEV_IC_IOSFVAR_H_ */
