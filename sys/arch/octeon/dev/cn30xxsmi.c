/*	$OpenBSD: cn30xxsmi.c,v 1.13 2024/07/06 06:15:17 landry Exp $	*/

/*
 * Copyright (c) 2007 Internet Initiative Japan, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/stdint.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#include <machine/fdt.h>
#include <machine/octeonvar.h>

#include <octeon/dev/cn30xxsmireg.h>
#include <octeon/dev/cn30xxsmivar.h>

int	cn30xxsmi_match(struct device *, void *, void *);
void	cn30xxsmi_attach(struct device *, struct device *, void *);

const struct cfattach octsmi_ca = {
	sizeof(struct cn30xxsmi_softc), cn30xxsmi_match, cn30xxsmi_attach
};

struct cfdriver octsmi_cd = {
	NULL, "octsmi", DV_DULL
};

static SLIST_HEAD(, cn30xxsmi_softc) smi_list =
    SLIST_HEAD_INITIALIZER(smi_list);

#define	_SMI_RD8(sc, off) \
	bus_space_read_8((sc)->sc_regt, (sc)->sc_regh, (off))
#define	_SMI_WR8(sc, off, v) \
	bus_space_write_8((sc)->sc_regt, (sc)->sc_regh, (off), (v))

int
cn30xxsmi_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "cavium,octeon-3860-mdio");
}

void
cn30xxsmi_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct cn30xxsmi_softc *sc = (struct cn30xxsmi_softc *)self;

	if (faa->fa_nreg != 1)
		return;

	sc->sc_node = faa->fa_node;
	sc->sc_regt = faa->fa_iot;

	if (bus_space_map(sc->sc_regt, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_regh)) {
		printf(": could not map registers\n");
		return;
	}

	SLIST_INSERT_HEAD(&smi_list, sc, sc_link);

	printf("\n");

	_SMI_WR8(sc, SMI_CLK_OFFSET, 0x1464);
	_SMI_WR8(sc, SMI_EN_OFFSET, SMI_EN_EN);
}

int
cn30xxsmi_read(struct cn30xxsmi_softc *sc, int phy_addr, int reg)
{
	uint64_t smi_rd;
	int timo;

	_SMI_WR8(sc, SMI_CMD_OFFSET, SMI_CMD_PHY_OP | 
	    (phy_addr << SMI_CMD_PHY_ADR_SHIFT) |
	    (reg << SMI_CMD_REG_ADR_SHIFT));

	timo = 10000;
	smi_rd = _SMI_RD8(sc, SMI_RD_DAT_OFFSET);
	while (ISSET(smi_rd, SMI_RD_DAT_PENDING)) {
		if (timo-- == 0)
			break;
		delay(10);
		smi_rd = _SMI_RD8(sc, SMI_RD_DAT_OFFSET);
	}
	if (ISSET(smi_rd, SMI_RD_DAT_PENDING)) {
		return -1;
	}

	return ISSET(smi_rd, SMI_RD_DAT_VAL) ? (smi_rd & SMI_RD_DAT_DAT) : 0;
}

void
cn30xxsmi_write(struct cn30xxsmi_softc *sc, int phy_addr, int reg, int value)
{
	uint64_t smi_wr;
	int timo;

	smi_wr = 0;
	SET(smi_wr, value);
	_SMI_WR8(sc, SMI_WR_DAT_OFFSET, smi_wr);

	_SMI_WR8(sc, SMI_CMD_OFFSET, (phy_addr << SMI_CMD_PHY_ADR_SHIFT) |
	    (reg << SMI_CMD_REG_ADR_SHIFT));

	timo = 10000;
	smi_wr = _SMI_RD8(sc, SMI_WR_DAT_OFFSET);
	while (ISSET(smi_wr, SMI_WR_DAT_PENDING)) {
		if (timo-- == 0)
			break;
		delay(10);
		smi_wr = _SMI_RD8(sc, SMI_WR_DAT_OFFSET);
	}
	if (ISSET(smi_wr, SMI_WR_DAT_PENDING)) {
		/* XXX log */
		printf("ERROR: cnmac_mii_writereg(0x%x, 0x%x, 0x%x) timed out.\n",
		    phy_addr, reg, value);
	}
}

int
cn30xxsmi_get_phy(int phandle, int port, struct cn30xxsmi_softc **psmi,
    int *preg)
{
	/* PHY addresses for Portwell CAM-0100 */
	static const int cam0100_phys[] = {
		0x02, 0x03, 0x22
	};
	/* PHY addresses for Check Point UTM-1 EDGE N */
	static const int cpn100_phys[] = {
		0x0c, 0x11, 0x0d
	};
	/* PHY addresses for Netgear ProSecure UTM25 */
	static const int nutm25_phys[] = {
		0x00, 0x04, 0x09
	};

	struct cn30xxsmi_softc *smi;
	int parent, phynode;
	int reg;

	if (phandle != 0) {
		phynode = OF_getnodebyphandle(phandle);
		if (phynode == 0)
			return ENOENT;
		reg = OF_getpropint(phynode, "reg", UINT32_MAX);
		if (reg == UINT32_MAX)
			return ENOENT;

		parent = OF_parent(phynode);
		SLIST_FOREACH(smi, &smi_list, sc_link) {
			if (smi->sc_node == parent)
				goto found;
		}
		return ENOENT;
	} else {
		smi = SLIST_FIRST(&smi_list);
		if (smi == NULL)
			return ENOENT;

		switch (octeon_board) {
		case BOARD_CHECKPOINT_N100:
			if (port >= nitems(cpn100_phys))
				return ENOENT;
			reg = cpn100_phys[port];
			break;
		case BOARD_NETGEAR_UTM25:
			if (port >= nitems(nutm25_phys))
				return ENOENT;
			reg = nutm25_phys[port];
			break;
		case BOARD_UBIQUITI_E100:
			/* XXX Skip the switch port on ERPoe-5.
			 * XXX There is no driver for it. */
			if (port > 1 && octeon_boot_info->board_rev_major == 1)
				return ENOENT;
		case BOARD_UBIQUITI_E120:
			if (port > 2)
				return ENOENT;
			reg = 7 - port;
			break;
		case BOARD_CN3010_EVB_HS5:
			if (port >= nitems(cam0100_phys))
				return ENOENT;
			reg = cam0100_phys[port];
			break;
		default:
			return ENOENT;
		}
	}

found:
	*psmi = smi;
	*preg = reg;
	return 0;
}
