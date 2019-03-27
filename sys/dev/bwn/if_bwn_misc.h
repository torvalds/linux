/*-
 * Copyright (c) 2009-2010 Weongyo Jeong <weongyo@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#ifndef	__IF_BWN_MISC_H__
#define	__IF_BWN_MISC_H__

/*
 * These are the functions used by the PHY code.
 *
 * They currently live in the driver itself; at least until they
 * are broken out into smaller pieces.
 */

struct bwn_mac;

extern int	bwn_gpio_control(struct bwn_mac *, uint32_t);

extern uint64_t	bwn_hf_read(struct bwn_mac *);
extern void	bwn_hf_write(struct bwn_mac *, uint64_t);

extern void	bwn_dummy_transmission(struct bwn_mac *mac, int ofdm, int paon);

extern void	bwn_ram_write(struct bwn_mac *, uint16_t, uint32_t);

extern void	bwn_mac_suspend(struct bwn_mac *);
extern void	bwn_mac_enable(struct bwn_mac *);

extern int	bwn_switch_channel(struct bwn_mac *, int);

extern uint16_t	bwn_shm_read_2(struct bwn_mac *, uint16_t, uint16_t);
extern void	bwn_shm_write_2(struct bwn_mac *, uint16_t, uint16_t,
		    uint16_t);
extern uint32_t	bwn_shm_read_4(struct bwn_mac *, uint16_t, uint16_t);
extern void	bwn_shm_write_4(struct bwn_mac *, uint16_t, uint16_t,
		    uint32_t);

extern int	bwn_reset_core(struct bwn_mac *, int g_mode);

extern void	bwn_psctl(struct bwn_mac *, uint32_t);

#endif
