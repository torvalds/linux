/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under
 * the sponsorship of the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef __THUNDER_MDIO_VAR_H__
#define	__THUNDER_MDIO_VAR_H__

#define	THUNDER_MDIO_DEVSTR	"Cavium ThunderX SMI/MDIO driver"
MALLOC_DECLARE(M_THUNDER_MDIO);
DECLARE_CLASS(thunder_mdio_driver);

enum thunder_mdio_mode {
	MODE_NONE = 0,
	MODE_IEEE_C22,
	MODE_IEEE_C45
};

struct phy_desc {
	device_t		miibus; /* One miibus per LMAC */
	struct ifnet *		ifp;	/* Fake ifp to satisfy miibus */
	int			lmacid;	/* ID number of LMAC connected */
	TAILQ_ENTRY(phy_desc)	phy_desc_list;
};

struct thunder_mdio_softc {
	device_t		dev;
	struct mtx		mtx;
	struct resource *	reg_base;

	enum thunder_mdio_mode	mode;

	TAILQ_HEAD(,phy_desc)	phy_desc_head;
};

int thunder_mdio_attach(device_t);
#endif
