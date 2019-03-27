/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2014 Qlogic Corporation
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
 * File: qls_ioctl.c
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


#include "qls_os.h"
#include "qls_hw.h"
#include "qls_def.h"
#include "qls_inline.h"
#include "qls_glbl.h"
#include "qls_ioctl.h"
#include "qls_dump.h"
extern qls_mpi_coredump_t ql_mpi_coredump;

static int qls_eioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
		struct thread *td);

static struct cdevsw qla_cdevsw = {
	.d_version = D_VERSION,
	.d_ioctl = qls_eioctl,
	.d_name = "qlxge",
};

int
qls_make_cdev(qla_host_t *ha)
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
qls_del_cdev(qla_host_t *ha)
{
	if (ha->ioctl_dev != NULL)
		destroy_dev(ha->ioctl_dev);
	return;
}

static int
qls_eioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
        struct thread *td)
{
        qla_host_t *ha;
        int rval = 0;
	device_t pci_dev;

	qls_mpi_dump_t *mpi_dump;

        if ((ha = (qla_host_t *)dev->si_drv1) == NULL)
                return ENXIO;

	pci_dev= ha->pci_dev;

        switch(cmd) {

	case QLA_MPI_DUMP:
		mpi_dump = (qls_mpi_dump_t *)data;

		if (mpi_dump->size == 0) {
			mpi_dump->size = sizeof (qls_mpi_coredump_t);
		} else {
			if ((mpi_dump->size != sizeof (qls_mpi_coredump_t)) ||
				(mpi_dump->dbuf == NULL))
				rval = EINVAL;
			else {
				if (qls_mpi_core_dump(ha) == 0) {
					rval = copyout(&ql_mpi_coredump,
							mpi_dump->dbuf,
							mpi_dump->size);
				} else 
					rval = ENXIO;

				if (rval) {
					device_printf(ha->pci_dev,
						"%s: mpidump failed[%d]\n",
						__func__, rval);
				}
			}

		}
		
		break;
        default:
                break;
        }

        return rval;
}

