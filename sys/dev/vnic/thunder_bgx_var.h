/*
 * Copyright (C) 2015 Cavium Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __THUNDER_BGX_VAR_H__
#define	__THUNDER_BGX_VAR_H__

MALLOC_DECLARE(M_BGX);

struct lmac {
	struct bgx		*bgx;
	int			dmac;
	uint8_t			mac[ETHER_ADDR_LEN];
	boolean_t		link_up;
	int			lmacid; /* ID within BGX */
	int			lmacid_bd; /* ID on board */
	device_t		phy_if_dev;
	int			phyaddr;
	unsigned int            last_duplex;
	unsigned int            last_link;
	unsigned int            last_speed;
	boolean_t		is_sgmii;
	struct callout		check_link;
	struct mtx		check_link_mtx;
};

struct bgx {
	device_t		dev;
	struct resource *	reg_base;

	uint8_t			bgx_id;
	enum qlm_mode		qlm_mode;
	struct	lmac		lmac[MAX_LMAC_PER_BGX];
	int			lmac_count;
	int                     lmac_type;
	int                     lane_to_sds;
	int			use_training;
};

#ifdef FDT
extern int bgx_fdt_init_phy(struct bgx *);
#endif

#endif
