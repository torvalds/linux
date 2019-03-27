/*************************************************************************
SPDX-License-Identifier: BSD-3-Clause

Copyright (c) 2003-2007  Cavium Networks (support@cavium.com). All rights
reserved.


Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Cavium Networks nor the names of
      its contributors may be used to endorse or promote products
      derived from this software without specific prior written
      permission.

This Software, including technical data, may be subject to U.S. export  control laws, including the U.S. Export Administration Act and its  associated regulations, and may be subject to export or import  regulations in other countries.

TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
AND WITH ALL FAULTS AND CAVIUM  NETWORKS MAKES NO PROMISES, REPRESENTATIONS OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES WITH YOU.

*************************************************************************/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>

#include "wrapper-cvmx-includes.h"
#include "ethernet-headers.h"

struct mtx cvm_oct_mdio_mtx;
MTX_SYSINIT(cvm_oct_mdio, &cvm_oct_mdio_mtx, "MDIO", MTX_DEF);

/**
 * Perform an MII read. Called by the generic MII routines
 *
 * @param dev      Device to perform read for
 * @param phy_id   The MII phy id
 * @param location Register location to read
 * @return Result from the read or zero on failure
 */
int cvm_oct_mdio_read(struct ifnet *ifp, int phy_id, int location)
{
	cvmx_smi_cmd_t          smi_cmd;
	cvmx_smi_rd_dat_t       smi_rd;

	MDIO_LOCK();
	smi_cmd.u64 = 0;
	smi_cmd.s.phy_op = 1;
	smi_cmd.s.phy_adr = phy_id;
	smi_cmd.s.reg_adr = location;
	cvmx_write_csr(CVMX_SMI_CMD, smi_cmd.u64);

	do {
		cvmx_wait(1000);
		smi_rd.u64 = cvmx_read_csr(CVMX_SMI_RD_DAT);
	} while (smi_rd.s.pending);

	MDIO_UNLOCK();

	if (smi_rd.s.val)
		return smi_rd.s.dat;
	else
		return 0;
}


/**
 * Perform an MII write. Called by the generic MII routines
 *
 * @param dev      Device to perform write for
 * @param phy_id   The MII phy id
 * @param location Register location to write
 * @param val      Value to write
 */
void cvm_oct_mdio_write(struct ifnet *ifp, int phy_id, int location, int val)
{
	cvmx_smi_cmd_t          smi_cmd;
	cvmx_smi_wr_dat_t       smi_wr;

	MDIO_LOCK();
	smi_wr.u64 = 0;
	smi_wr.s.dat = val;
	cvmx_write_csr(CVMX_SMI_WR_DAT, smi_wr.u64);

	smi_cmd.u64 = 0;
	smi_cmd.s.phy_op = 0;
	smi_cmd.s.phy_adr = phy_id;
	smi_cmd.s.reg_adr = location;
	cvmx_write_csr(CVMX_SMI_CMD, smi_cmd.u64);

	do {
		cvmx_wait(1000);
		smi_wr.u64 = cvmx_read_csr(CVMX_SMI_WR_DAT);
	} while (smi_wr.s.pending);
	MDIO_UNLOCK();
}

/**
 * Setup the MDIO device structures
 *
 * @param dev    Device to setup
 *
 * @return Zero on success, negative on failure
 */
int cvm_oct_mdio_setup_device(struct ifnet *ifp)
{
	cvm_oct_private_t *priv = (cvm_oct_private_t *)ifp->if_softc;

	priv->phy_id = cvmx_helper_board_get_mii_address(priv->port);
	priv->phy_device = NULL;
	priv->mdio_read = NULL;
	priv->mdio_write = NULL;

	return 0;
}

