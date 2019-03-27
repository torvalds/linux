/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/types.h>
#include <sys/systm.h>

#include <mips/nlm/hal/mips-extns.h>
#include <mips/nlm/hal/haldefs.h>
#include <mips/nlm/hal/iomap.h>
#include <mips/nlm/hal/sys.h>
#include <mips/nlm/hal/nae.h>
#include <mips/nlm/hal/mdio.h>

#include <mips/nlm/xlp.h>

/* Internal MDIO READ/WRITE Routines */
int
nlm_int_gmac_mdio_read(uint64_t nae_base, int bus, int block,
    int intf_type, int phyaddr, int regidx)
{
	uint32_t mdio_ld_cmd;
	uint32_t ctrlval;

	ctrlval = INT_MDIO_CTRL_SMP		|
	    (phyaddr << INT_MDIO_CTRL_PHYADDR_POS) |
	    (regidx << INT_MDIO_CTRL_DEVTYPE_POS) |
	    (2 << INT_MDIO_CTRL_OP_POS)		|
	    (1 << INT_MDIO_CTRL_ST_POS)		|
	    (7 << INT_MDIO_CTRL_XDIV_POS)	|
	    (2 << INT_MDIO_CTRL_TA_POS)		|
	    (2 << INT_MDIO_CTRL_MIIM_POS)	|
	    (1 << INT_MDIO_CTRL_MCDIV_POS);

	mdio_ld_cmd = nlm_read_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (INT_MDIO_CTRL + bus * 4)));
	if (mdio_ld_cmd & INT_MDIO_CTRL_CMD_LOAD) {
		nlm_write_nae_reg(nae_base,
		    NAE_REG(block, intf_type, (INT_MDIO_CTRL + bus*4)),
		    (mdio_ld_cmd & ~INT_MDIO_CTRL_CMD_LOAD));
	}

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (INT_MDIO_CTRL + bus * 4)),
	    ctrlval);

	/* Toggle Load Cmd Bit */
	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (INT_MDIO_CTRL + bus * 4)),
	    ctrlval | (1 << INT_MDIO_CTRL_LOAD_POS));

	/* poll master busy bit until it is not busy */
	while(nlm_read_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (INT_MDIO_RD_STAT + bus * 4))) &
	    INT_MDIO_STAT_MBSY) {
	}

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (INT_MDIO_CTRL + bus * 4)),
	    ctrlval);

	/* Read the data back */
	return nlm_read_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (INT_MDIO_RD_STAT + bus * 4)));
}

/* Internal MDIO WRITE Routines */
int
nlm_int_gmac_mdio_write(uint64_t nae_base, int bus, int block,
    int intf_type, int phyaddr, int regidx, uint16_t val)
{
	uint32_t mdio_ld_cmd;
	uint32_t ctrlval;

	ctrlval = INT_MDIO_CTRL_SMP		|
	    (phyaddr << INT_MDIO_CTRL_PHYADDR_POS) |
	    (regidx << INT_MDIO_CTRL_DEVTYPE_POS) |
	    (1 << INT_MDIO_CTRL_OP_POS)		|
	    (1 << INT_MDIO_CTRL_ST_POS)		|
	    (7 << INT_MDIO_CTRL_XDIV_POS)	|
	    (2 << INT_MDIO_CTRL_TA_POS)		|
	    (1 << INT_MDIO_CTRL_MIIM_POS)	|
	    (1 << INT_MDIO_CTRL_MCDIV_POS);

	mdio_ld_cmd = nlm_read_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (INT_MDIO_CTRL + bus * 4)));
	if (mdio_ld_cmd & INT_MDIO_CTRL_CMD_LOAD) {
		nlm_write_nae_reg(nae_base,
		    NAE_REG(block, intf_type, (INT_MDIO_CTRL + bus*4)),
		    (mdio_ld_cmd & ~INT_MDIO_CTRL_CMD_LOAD));
	}

	/* load data into ctrl data reg */
	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (INT_MDIO_CTRL_DATA + bus * 4)),
	    val);

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (INT_MDIO_CTRL + bus * 4)),
	    ctrlval);

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (INT_MDIO_CTRL + bus * 4)),
	    ctrlval | (1 << INT_MDIO_CTRL_LOAD_POS));

	/* poll master busy bit until it is not busy */
	while(nlm_read_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (INT_MDIO_RD_STAT + bus * 4))) &
	    INT_MDIO_STAT_MBSY) {
	}

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (INT_MDIO_CTRL + bus * 4)),
	    ctrlval);

	return (0);
}

int
nlm_int_gmac_mdio_reset(uint64_t nae_base, int bus, int block,
    int intf_type)
{
	uint32_t val;

	val = (7 << INT_MDIO_CTRL_XDIV_POS) |
	    (1 << INT_MDIO_CTRL_MCDIV_POS) |
	    (INT_MDIO_CTRL_SMP);

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (INT_MDIO_CTRL + bus * 4)),
	    val | INT_MDIO_CTRL_RST);

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (INT_MDIO_CTRL + bus * 4)),
	    val);

        return (0);
}

/*
 *  nae_gmac_mdio_read - Read sgmii phy register
 *
 *  Input parameters:
 *         bus          - bus number, nae has two external gmac bus: 0 and 1
 *         phyaddr      - PHY's address
 *         regidx       - index of register to read
 *
 *  Return value:
 *         value read (16 bits), or 0xffffffff if an error occurred.
 */
int
nlm_gmac_mdio_read(uint64_t nae_base, int bus, int block,
    int intf_type, int phyaddr, int regidx)
{
	uint32_t mdio_ld_cmd;
	uint32_t ctrlval;

	mdio_ld_cmd = nlm_read_nae_reg(nae_base, NAE_REG(block, intf_type,
	    (EXT_G0_MDIO_CTRL + bus * 4)));
	if (mdio_ld_cmd & EXT_G_MDIO_CMD_LCD) {
		nlm_write_nae_reg(nae_base,
		    NAE_REG(block, intf_type, (EXT_G0_MDIO_CTRL+bus*4)),
		    (mdio_ld_cmd & ~EXT_G_MDIO_CMD_LCD));
		while(nlm_read_nae_reg(nae_base,
		    NAE_REG(block, intf_type,
		    (EXT_G0_MDIO_RD_STAT + bus * 4))) &
		    EXT_G_MDIO_STAT_MBSY);
	}

	ctrlval = EXT_G_MDIO_CMD_SP |
	    (phyaddr << EXT_G_MDIO_PHYADDR_POS) |
	    (regidx << EXT_G_MDIO_REGADDR_POS);
	if (nlm_is_xlp8xx_ax() || nlm_is_xlp8xx_b0() || nlm_is_xlp3xx_ax())
		ctrlval |= EXT_G_MDIO_DIV;
	else
		ctrlval |= EXT_G_MDIO_DIV_WITH_HW_DIV64;

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (EXT_G0_MDIO_CTRL+bus*4)),
	    ctrlval);

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (EXT_G0_MDIO_CTRL+bus*4)),
	    ctrlval | (1<<18));
	DELAY(1000);
	/* poll master busy bit until it is not busy */
	while(nlm_read_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (EXT_G0_MDIO_RD_STAT + bus * 4))) &
	    EXT_G_MDIO_STAT_MBSY);

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (EXT_G0_MDIO_CTRL+bus*4)),
	    ctrlval);

	/* Read the data back */
	return nlm_read_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (EXT_G0_MDIO_RD_STAT + bus * 4)));
}

/*
 *  nae_gmac_mdio_write -Write sgmac mii PHY register.
 *
 *  Input parameters:
 *         bus          - bus number, nae has two external gmac bus: 0 and 1
 *         phyaddr      - PHY to use
 *         regidx       - register within the PHY
 *         val          - data to write to register
 *
 *  Return value:
 *         0 - success
 */
int
nlm_gmac_mdio_write(uint64_t nae_base, int bus, int block,
    int intf_type, int phyaddr, int regidx, uint16_t val)
{
	uint32_t mdio_ld_cmd;
	uint32_t ctrlval;

	mdio_ld_cmd = nlm_read_nae_reg(nae_base, NAE_REG(block, intf_type,
	    (EXT_G0_MDIO_CTRL + bus * 4)));
	if (mdio_ld_cmd & EXT_G_MDIO_CMD_LCD) {
		nlm_write_nae_reg(nae_base,
		    NAE_REG(block, intf_type, (EXT_G0_MDIO_CTRL+bus*4)),
		    (mdio_ld_cmd & ~EXT_G_MDIO_CMD_LCD));
		while(nlm_read_nae_reg(nae_base,
		    NAE_REG(block, intf_type,
		    (EXT_G0_MDIO_RD_STAT + bus * 4))) &
		    EXT_G_MDIO_STAT_MBSY);
	}

	/* load data into ctrl data reg */
	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (EXT_G0_MDIO_CTRL_DATA+bus*4)),
	    val);

	ctrlval = EXT_G_MDIO_CMD_SP		|
	    (phyaddr << EXT_G_MDIO_PHYADDR_POS)	|
	    (regidx << EXT_G_MDIO_REGADDR_POS);
	if (nlm_is_xlp8xx_ax() || nlm_is_xlp8xx_b0() || nlm_is_xlp3xx_ax())
		ctrlval |= EXT_G_MDIO_DIV;
	else
		ctrlval |= EXT_G_MDIO_DIV_WITH_HW_DIV64;

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (EXT_G0_MDIO_CTRL+bus*4)),
	    ctrlval);

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (EXT_G0_MDIO_CTRL+bus*4)),
	    ctrlval | EXT_G_MDIO_CMD_LCD);
	DELAY(1000);

	/* poll master busy bit until it is not busy */
	while(nlm_read_nae_reg(nae_base,
	    NAE_REG(block, intf_type,
	    (EXT_G0_MDIO_RD_STAT + bus * 4))) & EXT_G_MDIO_STAT_MBSY);

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (EXT_G0_MDIO_CTRL+bus*4)),
	    ctrlval);

	return (0);
}

/*
 *  nae_gmac_mdio_reset -Reset sgmii mdio module.
 *
 *  Input parameters:
 *         bus - bus number, nae has two external gmac bus: 0 and 1
 *
 *  Return value:
 *        0 - success
 */
int
nlm_gmac_mdio_reset(uint64_t nae_base, int bus, int block,
    int intf_type)
{
	uint32_t ctrlval;

	ctrlval = nlm_read_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (EXT_G0_MDIO_CTRL+bus*4)));

	if (nlm_is_xlp8xx_ax() || nlm_is_xlp8xx_b0() || nlm_is_xlp3xx_ax())
		ctrlval |= EXT_G_MDIO_DIV;
	else
		ctrlval |= EXT_G_MDIO_DIV_WITH_HW_DIV64;

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (EXT_G0_MDIO_CTRL + bus * 4)),
	    EXT_G_MDIO_MMRST | ctrlval);
	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, intf_type, (EXT_G0_MDIO_CTRL + bus * 4)), ctrlval);
	return (0);
}

/*
 * nlm_mdio_reset_all : reset all internal and external MDIO
 */
void
nlm_mdio_reset_all(uint64_t nae_base)
{
	/* reset internal MDIO */
	nlm_int_gmac_mdio_reset(nae_base, 0, BLOCK_7, LANE_CFG);
	/* reset external MDIO */
	nlm_gmac_mdio_reset(nae_base, 0, BLOCK_7, LANE_CFG);
	nlm_gmac_mdio_reset(nae_base, 1, BLOCK_7, LANE_CFG);
}
