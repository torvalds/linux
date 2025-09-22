/*	$OpenBSD: tea5757.h,v 1.3 2002/01/07 18:32:19 mickey Exp $	*/
/* $RuOBSD: tea5757.h,v 1.2 2001/10/18 16:51:36 pva Exp $ */

/*
 * Copyright (c) 2001 Vladimir Popov <jumbo@narod.ru>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _TEA5757_H_
#define _TEA5757_H_

#include <sys/types.h>

#include <machine/bus.h>

#define TEA5757_REGISTER_LENGTH		25
#define TEA5757_FREQ			0x0007FFF
#define TEA5757_DATA			0x1FF8000

#define TEA5757_SEARCH_START		(1 << 24) /* 0x1000000 */
#define TEA5757_SEARCH_END		(0 << 24) /* 0x0000000 */

#define TEA5757_SEARCH_UP		(1 << 23) /* 0x0800000 */
#define TEA5757_SEARCH_DOWN		(0 << 23) /* 0x0000000 */
#define TEA5757_ACQUISITION_DELAY	100000
#define TEA5757_WAIT_DELAY		1000
#define TEA5757_SEARCH_DELAY		14	  /* 14 microseconds */

#define TEA5757_STEREO			(0 << 22) /* 0x0000000 */
#define TEA5757_MONO			(1 << 22) /* 0x0400000 */

#define TEA5757_BAND_FM			(0 << 20)
#define TEA5757_BAND_MW			(1 << 20)
#define TEA5757_BAND_LW			(2 << 20)
#define TEA5757_BAND_SW			(3 << 20)

#define TEA5757_USER_PORT		(0 << 18)
#define TEA5757_DUMMY			(0 << 15)

#define TEA5757_S005			(0 << 16) /* 0x0000000 * > 5 mkV */
#define TEA5757_S010			(2 << 16) /* 0x0020000 * > 10 mkV */
#define TEA5757_S030			(1 << 16) /* 0x0010000 * > 30 mkV */
#define TEA5757_S150			(3 << 16) /* 0x0030000 * > 150 mkV */

#define TEA5757_TEA5759			(1 << 0)

struct tea5757_t {
	bus_space_tag_t	iot;
	bus_space_handle_t	ioh;
	bus_size_t	offset;
	int	flags;

	void	(*init)(bus_space_tag_t, bus_space_handle_t, bus_size_t,
			u_int32_t); /* init value */
	void	(*rset)(bus_space_tag_t, bus_space_handle_t, bus_size_t,
			u_int32_t); /* reset value */
	void	(*write_bit)(bus_space_tag_t, bus_space_handle_t, bus_size_t,
			int); /* the bit */
	u_int32_t	(*read)(bus_space_tag_t, bus_space_handle_t, bus_size_t);
};

u_int32_t	tea5757_encode_freq(u_int32_t, int);
u_int32_t	tea5757_decode_freq(u_int32_t, int);
u_int32_t	tea5757_encode_lock(u_int8_t);
u_int8_t	tea5757_decode_lock(u_int32_t);

u_int32_t	tea5757_set_freq(struct tea5757_t *, u_int32_t, u_int32_t, u_int32_t);
void	tea5757_search(struct tea5757_t *, u_int32_t, u_int32_t, int);

void	tea5757_hardware_write(struct tea5757_t *, u_int32_t);

#endif /* _TEA5757_H_ */
