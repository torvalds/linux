/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2013 Qlogic Corporation
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * File: qla_ioctl.c
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "qla_os.h"
#include "qla_reg.h"
#include "qla_hw.h"
#include "qla_def.h"
#include "qla_reg.h"
#include "qla_inline.h"
#include "qla_glbl.h"
#include "qla_ioctl.h"

static struct cdevsw qla_cdevsw = {
	.d_version = D_VERSION,
	.d_ioctl = qla_eioctl,
	.d_name = "qlcnic",
};

int
qla_make_cdev(qla_host_t *ha)
{
        ha->ioctl_dev = make_dev(&qla_cdevsw,
				ha->ifp->if_dunit,
                                UID_ROOT,
                                GID_WHEEL,
                                0600,
                                "%s",
                                if_name(ha->ifp));

	if (ha->ioctl_dev == NULL)
		return (-1);

        ha->ioctl_dev->si_drv1 = ha;

	return (0);
}

void
qla_del_cdev(qla_host_t *ha)
{
	if (ha->ioctl_dev != NULL)
		destroy_dev(ha->ioctl_dev);
	return;
}

int
qla_eioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
        struct thread *td)
{
        qla_host_t *ha;
        int rval = 0;
        qla_reg_val_t *rv;
        qla_rd_flash_t *rdf;
	qla_wr_flash_t *wrf;
	qla_rd_pci_ids_t *pci_ids;
	device_t pci_dev;

        if ((ha = (qla_host_t *)dev->si_drv1) == NULL)
                return ENXIO;

	pci_dev= ha->pci_dev;

        switch(cmd) {

        case QLA_RDWR_REG:

                rv = (qla_reg_val_t *)data;

                if (rv->direct) {
                        if (rv->rd) {
                                rv->val = READ_OFFSET32(ha, rv->reg);
                        } else {
                                WRITE_OFFSET32(ha, rv->reg, rv->val);
                        }
                } else {
                        if ((rval = qla_rdwr_indreg32(ha, rv->reg, &rv->val,
                                rv->rd)))
                                rval = ENXIO;
                }
                break;

        case QLA_RD_FLASH:
                rdf = (qla_rd_flash_t *)data;
                if ((rval = qla_rd_flash32(ha, rdf->off, &rdf->data)))
                        rval = ENXIO;
                break;

        case QLA_WR_FLASH:
                wrf = (qla_wr_flash_t *)data;
                if ((rval = qla_wr_flash_buffer(ha, wrf->off, wrf->size,
					wrf->buffer, wrf->pattern)))
                        rval = ENXIO;
                break;


	case QLA_ERASE_FLASH:
		if (qla_erase_flash(ha, ((qla_erase_flash_t *)data)->off,
			((qla_erase_flash_t *)data)->size))
			rval = ENXIO;
		break;

	case QLA_RD_PCI_IDS:
		pci_ids = (qla_rd_pci_ids_t *)data;
		pci_ids->ven_id = pci_get_vendor(pci_dev);
		pci_ids->dev_id = pci_get_device(pci_dev);
		pci_ids->subsys_ven_id = pci_get_subvendor(pci_dev);
		pci_ids->subsys_dev_id = pci_get_subdevice(pci_dev);
		pci_ids->rev_id = pci_read_config(pci_dev, PCIR_REVID, 1);
		break;
		
        default:
                break;
        }

        return rval;
}

