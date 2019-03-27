/*-
 * FreeBSD/CAM specific routines for LSI '909 FC  adapters.
 * FreeBSD Version.
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-3-Clause
 *
 * Copyright (c)  2000, 2001 by Greg Ansley
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * Copyright (c) 2002, 2006 by Matthew Jacob
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon including
 *    a substantially similar Disclaimer requirement for further binary
 *    redistribution.
 * 3. Neither the names of the above listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF THE COPYRIGHT
 * OWNER OR CONTRIBUTOR IS ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Support from Chris Ellsworth in order to make SAS adapters work
 * is gratefully acknowledged.
 *
 * Support from LSI-Logic has also gone a great deal toward making this a
 * workable subsystem and is gratefully acknowledged.
 */
/*-
 * Copyright (c) 2004, Avid Technology, Inc. and its contributors.
 * Copyright (c) 2005, WHEEL Sp. z o.o.
 * Copyright (c) 2004, 2005 Justin T. Gibbs
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon including
 *    a substantially similar Disclaimer requirement for further binary
 *    redistribution.
 * 3. Neither the names of the above listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF THE COPYRIGHT
 * OWNER OR CONTRIBUTOR IS ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/mpt/mpt.h>
#include <dev/mpt/mpt_cam.h>
#include <dev/mpt/mpt_raid.h>

#include "dev/mpt/mpilib/mpi_ioc.h" /* XXX Fix Event Handling!!! */
#include "dev/mpt/mpilib/mpi_init.h"
#include "dev/mpt/mpilib/mpi_targ.h"
#include "dev/mpt/mpilib/mpi_fc.h"
#include "dev/mpt/mpilib/mpi_sas.h"

#include <sys/callout.h>
#include <sys/kthread.h>
#include <sys/sysctl.h>

static void mpt_poll(struct cam_sim *);
static timeout_t mpt_timeout;
static void mpt_action(struct cam_sim *, union ccb *);
static int
mpt_get_spi_settings(struct mpt_softc *, struct ccb_trans_settings *);
static void mpt_setwidth(struct mpt_softc *, int, int);
static void mpt_setsync(struct mpt_softc *, int, int, int);
static int mpt_update_spi_config(struct mpt_softc *, int);

static mpt_reply_handler_t mpt_scsi_reply_handler;
static mpt_reply_handler_t mpt_scsi_tmf_reply_handler;
static mpt_reply_handler_t mpt_fc_els_reply_handler;
static int mpt_scsi_reply_frame_handler(struct mpt_softc *, request_t *,
					MSG_DEFAULT_REPLY *);
static int mpt_bus_reset(struct mpt_softc *, target_id_t, lun_id_t, int);
static int mpt_fc_reset_link(struct mpt_softc *, int);

static int mpt_spawn_recovery_thread(struct mpt_softc *mpt);
static void mpt_terminate_recovery_thread(struct mpt_softc *mpt);
static void mpt_recovery_thread(void *arg);
static void mpt_recover_commands(struct mpt_softc *mpt);

static int mpt_scsi_send_tmf(struct mpt_softc *, u_int, u_int, u_int,
    target_id_t, lun_id_t, u_int, int);

static void mpt_fc_post_els(struct mpt_softc *mpt, request_t *, int);
static void mpt_post_target_command(struct mpt_softc *, request_t *, int);
static int mpt_add_els_buffers(struct mpt_softc *mpt);
static int mpt_add_target_commands(struct mpt_softc *mpt);
static int mpt_enable_lun(struct mpt_softc *, target_id_t, lun_id_t);
static int mpt_disable_lun(struct mpt_softc *, target_id_t, lun_id_t);
static void mpt_target_start_io(struct mpt_softc *, union ccb *);
static cam_status mpt_abort_target_ccb(struct mpt_softc *, union ccb *);
static int mpt_abort_target_cmd(struct mpt_softc *, request_t *);
static void mpt_scsi_tgt_status(struct mpt_softc *, union ccb *, request_t *,
    uint8_t, uint8_t const *, u_int);
static void
mpt_scsi_tgt_tsk_mgmt(struct mpt_softc *, request_t *, mpt_task_mgmt_t,
    tgt_resource_t *, int);
static void mpt_tgt_dump_tgt_state(struct mpt_softc *, request_t *);
static void mpt_tgt_dump_req_state(struct mpt_softc *, request_t *);
static mpt_reply_handler_t mpt_scsi_tgt_reply_handler;
static mpt_reply_handler_t mpt_sata_pass_reply_handler;

static uint32_t scsi_io_handler_id = MPT_HANDLER_ID_NONE;
static uint32_t scsi_tmf_handler_id = MPT_HANDLER_ID_NONE;
static uint32_t fc_els_handler_id = MPT_HANDLER_ID_NONE;
static uint32_t sata_pass_handler_id = MPT_HANDLER_ID_NONE;

static mpt_probe_handler_t	mpt_cam_probe;
static mpt_attach_handler_t	mpt_cam_attach;
static mpt_enable_handler_t	mpt_cam_enable;
static mpt_ready_handler_t	mpt_cam_ready;
static mpt_event_handler_t	mpt_cam_event;
static mpt_reset_handler_t	mpt_cam_ioc_reset;
static mpt_detach_handler_t	mpt_cam_detach;

static struct mpt_personality mpt_cam_personality =
{
	.name		= "mpt_cam",
	.probe		= mpt_cam_probe,
	.attach		= mpt_cam_attach,
	.enable		= mpt_cam_enable,
	.ready		= mpt_cam_ready,
	.event		= mpt_cam_event,
	.reset		= mpt_cam_ioc_reset,
	.detach		= mpt_cam_detach,
};

DECLARE_MPT_PERSONALITY(mpt_cam, SI_ORDER_SECOND);
MODULE_DEPEND(mpt_cam, cam, 1, 1, 1);

int mpt_enable_sata_wc = -1;
TUNABLE_INT("hw.mpt.enable_sata_wc", &mpt_enable_sata_wc);

static int
mpt_cam_probe(struct mpt_softc *mpt)
{
	int role;

	/*
	 * Only attach to nodes that support the initiator or target role
	 * (or want to) or have RAID physical devices that need CAM pass-thru
	 * support.
	 */
	if (mpt->do_cfg_role) {
		role = mpt->cfg_role;
	} else {
		role = mpt->role;
	}
	if ((role & (MPT_ROLE_TARGET|MPT_ROLE_INITIATOR)) != 0 ||
	    (mpt->ioc_page2 != NULL && mpt->ioc_page2->MaxPhysDisks != 0)) {
		return (0);
	}
	return (ENODEV);
}

static int
mpt_cam_attach(struct mpt_softc *mpt)
{
	struct cam_devq *devq;
	mpt_handler_t	 handler;
	int		 maxq;
	int		 error;

	MPT_LOCK(mpt);
	TAILQ_INIT(&mpt->request_timeout_list);
	maxq = (mpt->ioc_facts.GlobalCredits < MPT_MAX_REQUESTS(mpt))?
	    mpt->ioc_facts.GlobalCredits : MPT_MAX_REQUESTS(mpt);

	handler.reply_handler = mpt_scsi_reply_handler;
	error = mpt_register_handler(mpt, MPT_HANDLER_REPLY, handler,
				     &scsi_io_handler_id);
	if (error != 0) {
		MPT_UNLOCK(mpt);
		goto cleanup;
	}

	handler.reply_handler = mpt_scsi_tmf_reply_handler;
	error = mpt_register_handler(mpt, MPT_HANDLER_REPLY, handler,
				     &scsi_tmf_handler_id);
	if (error != 0) {
		MPT_UNLOCK(mpt);
		goto cleanup;
	}

	/*
	 * If we're fibre channel and could support target mode, we register
	 * an ELS reply handler and give it resources.
	 */
	if (mpt->is_fc && (mpt->role & MPT_ROLE_TARGET) != 0) {
		handler.reply_handler = mpt_fc_els_reply_handler;
		error = mpt_register_handler(mpt, MPT_HANDLER_REPLY, handler,
		    &fc_els_handler_id);
		if (error != 0) {
			MPT_UNLOCK(mpt);
			goto cleanup;
		}
		if (mpt_add_els_buffers(mpt) == FALSE) {
			error = ENOMEM;
			MPT_UNLOCK(mpt);
			goto cleanup;
		}
		maxq -= mpt->els_cmds_allocated;
	}

	/*
	 * If we support target mode, we register a reply handler for it,
	 * but don't add command resources until we actually enable target
	 * mode.
	 */
	if (mpt->is_fc && (mpt->role & MPT_ROLE_TARGET) != 0) {
		handler.reply_handler = mpt_scsi_tgt_reply_handler;
		error = mpt_register_handler(mpt, MPT_HANDLER_REPLY, handler,
		    &mpt->scsi_tgt_handler_id);
		if (error != 0) {
			MPT_UNLOCK(mpt);
			goto cleanup;
		}
	}

	if (mpt->is_sas) {
		handler.reply_handler = mpt_sata_pass_reply_handler;
		error = mpt_register_handler(mpt, MPT_HANDLER_REPLY, handler,
		    &sata_pass_handler_id);
		if (error != 0) {
			MPT_UNLOCK(mpt);
			goto cleanup;
		}
	}

	/*
	 * We keep one request reserved for timeout TMF requests.
	 */
	mpt->tmf_req = mpt_get_request(mpt, FALSE);
	if (mpt->tmf_req == NULL) {
		mpt_prt(mpt, "Unable to allocate dedicated TMF request!\n");
		error = ENOMEM;
		MPT_UNLOCK(mpt);
		goto cleanup;
	}

	/*
	 * Mark the request as free even though not on the free list.
	 * There is only one TMF request allowed to be outstanding at
	 * a time and the TMF routines perform their own allocation
	 * tracking using the standard state flags.
	 */
	mpt->tmf_req->state = REQ_STATE_FREE;
	maxq--;

	/*
	 * The rest of this is CAM foo, for which we need to drop our lock
	 */
	MPT_UNLOCK(mpt);

	if (mpt_spawn_recovery_thread(mpt) != 0) {
		mpt_prt(mpt, "Unable to spawn recovery thread!\n");
		error = ENOMEM;
		goto cleanup;
	}

	/*
	 * Create the device queue for our SIM(s).
	 */
	devq = cam_simq_alloc(maxq);
	if (devq == NULL) {
		mpt_prt(mpt, "Unable to allocate CAM SIMQ!\n");
		error = ENOMEM;
		goto cleanup;
	}

	/*
	 * Construct our SIM entry.
	 */
	mpt->sim =
	    mpt_sim_alloc(mpt_action, mpt_poll, "mpt", mpt, 1, maxq, devq);
	if (mpt->sim == NULL) {
		mpt_prt(mpt, "Unable to allocate CAM SIM!\n");
		cam_simq_free(devq);
		error = ENOMEM;
		goto cleanup;
	}

	/*
	 * Register exactly this bus.
	 */
	MPT_LOCK(mpt);
	if (xpt_bus_register(mpt->sim, mpt->dev, 0) != CAM_SUCCESS) {
		mpt_prt(mpt, "Bus registration Failed!\n");
		error = ENOMEM;
		MPT_UNLOCK(mpt);
		goto cleanup;
	}

	if (xpt_create_path(&mpt->path, NULL, cam_sim_path(mpt->sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		mpt_prt(mpt, "Unable to allocate Path!\n");
		error = ENOMEM;
		MPT_UNLOCK(mpt);
		goto cleanup;
	}
	MPT_UNLOCK(mpt);

	/*
	 * Only register a second bus for RAID physical
	 * devices if the controller supports RAID.
	 */
	if (mpt->ioc_page2 == NULL || mpt->ioc_page2->MaxPhysDisks == 0) {
		return (0);
	}

	/*
	 * Create a "bus" to export all hidden disks to CAM.
	 */
	mpt->phydisk_sim =
	    mpt_sim_alloc(mpt_action, mpt_poll, "mpt", mpt, 1, maxq, devq);
	if (mpt->phydisk_sim == NULL) {
		mpt_prt(mpt, "Unable to allocate Physical Disk CAM SIM!\n");
		error = ENOMEM;
		goto cleanup;
	}

	/*
	 * Register this bus.
	 */
	MPT_LOCK(mpt);
	if (xpt_bus_register(mpt->phydisk_sim, mpt->dev, 1) !=
	    CAM_SUCCESS) {
		mpt_prt(mpt, "Physical Disk Bus registration Failed!\n");
		error = ENOMEM;
		MPT_UNLOCK(mpt);
		goto cleanup;
	}

	if (xpt_create_path(&mpt->phydisk_path, NULL,
	    cam_sim_path(mpt->phydisk_sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		mpt_prt(mpt, "Unable to allocate Physical Disk Path!\n");
		error = ENOMEM;
		MPT_UNLOCK(mpt);
		goto cleanup;
	}
	MPT_UNLOCK(mpt);
	mpt_lprt(mpt, MPT_PRT_DEBUG, "attached cam\n");
	return (0);

cleanup:
	mpt_cam_detach(mpt);
	return (error);
}

/*
 * Read FC configuration information
 */
static int
mpt_read_config_info_fc(struct mpt_softc *mpt)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	char *topology = NULL;
	int rv;

	rv = mpt_read_cfg_header(mpt, MPI_CONFIG_PAGETYPE_FC_PORT, 0,
	    0, &mpt->mpt_fcport_page0.Header, FALSE, 5000);
	if (rv) {
		return (-1);
	}
	mpt_lprt(mpt, MPT_PRT_DEBUG, "FC Port Page 0 Header: %x %x %x %x\n",
		 mpt->mpt_fcport_page0.Header.PageVersion,
		 mpt->mpt_fcport_page0.Header.PageLength,
		 mpt->mpt_fcport_page0.Header.PageNumber,
		 mpt->mpt_fcport_page0.Header.PageType);


	rv = mpt_read_cur_cfg_page(mpt, 0, &mpt->mpt_fcport_page0.Header,
	    sizeof(mpt->mpt_fcport_page0), FALSE, 5000);
	if (rv) {
		mpt_prt(mpt, "failed to read FC Port Page 0\n");
		return (-1);
	}
	mpt2host_config_page_fc_port_0(&mpt->mpt_fcport_page0);

	switch (mpt->mpt_fcport_page0.CurrentSpeed) {
	case MPI_FCPORTPAGE0_CURRENT_SPEED_1GBIT:
		mpt->mpt_fcport_speed = 1;
		break;
	case MPI_FCPORTPAGE0_CURRENT_SPEED_2GBIT:
		mpt->mpt_fcport_speed = 2;
		break;
	case MPI_FCPORTPAGE0_CURRENT_SPEED_10GBIT:
		mpt->mpt_fcport_speed = 10;
		break;
	case MPI_FCPORTPAGE0_CURRENT_SPEED_4GBIT:
		mpt->mpt_fcport_speed = 4;
		break;
	default:
		mpt->mpt_fcport_speed = 0;
		break;
	}

	switch (mpt->mpt_fcport_page0.Flags &
	    MPI_FCPORTPAGE0_FLAGS_ATTACH_TYPE_MASK) {
	case MPI_FCPORTPAGE0_FLAGS_ATTACH_NO_INIT:
		mpt->mpt_fcport_speed = 0;
		topology = "<NO LOOP>";
		break;
	case MPI_FCPORTPAGE0_FLAGS_ATTACH_POINT_TO_POINT:
		topology = "N-Port";
		break;
	case MPI_FCPORTPAGE0_FLAGS_ATTACH_PRIVATE_LOOP:
		topology = "NL-Port";
		break;
	case MPI_FCPORTPAGE0_FLAGS_ATTACH_FABRIC_DIRECT:
		topology = "F-Port";
		break;
	case MPI_FCPORTPAGE0_FLAGS_ATTACH_PUBLIC_LOOP:
		topology = "FL-Port";
		break;
	default:
		mpt->mpt_fcport_speed = 0;
		topology = "?";
		break;
	}

	mpt->scinfo.fc.wwnn = ((uint64_t)mpt->mpt_fcport_page0.WWNN.High << 32)
	    | mpt->mpt_fcport_page0.WWNN.Low;
	mpt->scinfo.fc.wwpn = ((uint64_t)mpt->mpt_fcport_page0.WWPN.High << 32)
	    | mpt->mpt_fcport_page0.WWPN.Low;
	mpt->scinfo.fc.portid = mpt->mpt_fcport_page0.PortIdentifier;

	mpt_lprt(mpt, MPT_PRT_INFO,
	    "FC Port Page 0: Topology <%s> WWNN 0x%16jx WWPN 0x%16jx "
	    "Speed %u-Gbit\n", topology,
	    (uintmax_t)mpt->scinfo.fc.wwnn, (uintmax_t)mpt->scinfo.fc.wwpn,
	    mpt->mpt_fcport_speed);
	MPT_UNLOCK(mpt);
	ctx = device_get_sysctl_ctx(mpt->dev);
	tree = device_get_sysctl_tree(mpt->dev);

	SYSCTL_ADD_QUAD(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "wwnn", CTLFLAG_RD, &mpt->scinfo.fc.wwnn,
	    "World Wide Node Name");

	SYSCTL_ADD_QUAD(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	     "wwpn", CTLFLAG_RD, &mpt->scinfo.fc.wwpn,
	     "World Wide Port Name");

	MPT_LOCK(mpt);
	return (0);
}

/*
 * Set FC configuration information.
 */
static int
mpt_set_initial_config_fc(struct mpt_softc *mpt)
{
	CONFIG_PAGE_FC_PORT_1 fc;
	U32 fl;
	int r, doit = 0;
	int role;

	r = mpt_read_cfg_header(mpt, MPI_CONFIG_PAGETYPE_FC_PORT, 1, 0,
	    &fc.Header, FALSE, 5000);
	if (r) {
		mpt_prt(mpt, "failed to read FC page 1 header\n");
		return (mpt_fc_reset_link(mpt, 1));
	}

	r = mpt_read_cfg_page(mpt, MPI_CONFIG_ACTION_PAGE_READ_NVRAM, 0,
	    &fc.Header, sizeof (fc), FALSE, 5000);
	if (r) {
		mpt_prt(mpt, "failed to read FC page 1\n");
		return (mpt_fc_reset_link(mpt, 1));
	}
	mpt2host_config_page_fc_port_1(&fc);

	/*
	 * Check our flags to make sure we support the role we want.
	 */
	doit = 0;
	role = 0;
	fl = fc.Flags;

	if (fl & MPI_FCPORTPAGE1_FLAGS_PROT_FCP_INIT) {
		role |= MPT_ROLE_INITIATOR;
	}
	if (fl & MPI_FCPORTPAGE1_FLAGS_PROT_FCP_TARG) {
		role |= MPT_ROLE_TARGET;
	}

	fl &= ~MPI_FCPORTPAGE1_FLAGS_PROT_MASK;

	if (mpt->do_cfg_role == 0) {
		role = mpt->cfg_role;
	} else {
		mpt->do_cfg_role = 0;
	}

	if (role != mpt->cfg_role) {
		if (mpt->cfg_role & MPT_ROLE_INITIATOR) {
			if ((role & MPT_ROLE_INITIATOR) == 0) {
				mpt_prt(mpt, "adding initiator role\n");
				fl |= MPI_FCPORTPAGE1_FLAGS_PROT_FCP_INIT;
				doit++;
			} else {
				mpt_prt(mpt, "keeping initiator role\n");
			}
		} else if (role & MPT_ROLE_INITIATOR) {
			mpt_prt(mpt, "removing initiator role\n");
			doit++;
		}
		if (mpt->cfg_role & MPT_ROLE_TARGET) {
			if ((role & MPT_ROLE_TARGET) == 0) {
				mpt_prt(mpt, "adding target role\n");
				fl |= MPI_FCPORTPAGE1_FLAGS_PROT_FCP_TARG;
				doit++;
			} else {
				mpt_prt(mpt, "keeping target role\n");
			}
		} else if (role & MPT_ROLE_TARGET) {
			mpt_prt(mpt, "removing target role\n");
			doit++;
		}
		mpt->role = mpt->cfg_role;
	}

	if (fl & MPI_FCPORTPAGE1_FLAGS_PROT_FCP_TARG) {
		if ((fl & MPI_FCPORTPAGE1_FLAGS_TARGET_MODE_OXID) == 0) {
			mpt_prt(mpt, "adding OXID option\n");
			fl |= MPI_FCPORTPAGE1_FLAGS_TARGET_MODE_OXID;
			doit++;
		}
	}

	if (doit) {
		fc.Flags = fl;
		host2mpt_config_page_fc_port_1(&fc);
		r = mpt_write_cfg_page(mpt,
		    MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM, 0, &fc.Header,
		    sizeof(fc), FALSE, 5000);
		if (r != 0) {
			mpt_prt(mpt, "failed to update NVRAM with changes\n");
			return (0);
		}
		mpt_prt(mpt, "NOTE: NVRAM changes will not take "
		    "effect until next reboot or IOC reset\n");
	}
	return (0);
}

static int
mptsas_sas_io_unit_pg0(struct mpt_softc *mpt, struct mptsas_portinfo *portinfo)
{
	ConfigExtendedPageHeader_t hdr;
	struct mptsas_phyinfo *phyinfo;
	SasIOUnitPage0_t *buffer;
	int error, len, i;

	error = mpt_read_extcfg_header(mpt, MPI_SASIOUNITPAGE0_PAGEVERSION,
				       0, 0, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT,
				       &hdr, 0, 10000);
	if (error)
		goto out;
	if (hdr.ExtPageLength == 0) {
		error = ENXIO;
		goto out;
	}

	len = hdr.ExtPageLength * 4;
	buffer = malloc(len, M_DEVBUF, M_NOWAIT|M_ZERO);
	if (buffer == NULL) {
		error = ENOMEM;
		goto out;
	}

	error = mpt_read_extcfg_page(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
				     0, &hdr, buffer, len, 0, 10000);
	if (error) {
		free(buffer, M_DEVBUF);
		goto out;
	}

	portinfo->num_phys = buffer->NumPhys;
	portinfo->phy_info = malloc(sizeof(*portinfo->phy_info) *
	    portinfo->num_phys, M_DEVBUF, M_NOWAIT|M_ZERO);
	if (portinfo->phy_info == NULL) {
		free(buffer, M_DEVBUF);
		error = ENOMEM;
		goto out;
	}

	for (i = 0; i < portinfo->num_phys; i++) {
		phyinfo = &portinfo->phy_info[i];
		phyinfo->phy_num = i;
		phyinfo->port_id = buffer->PhyData[i].Port;
		phyinfo->negotiated_link_rate =
		    buffer->PhyData[i].NegotiatedLinkRate;
		phyinfo->handle =
		    le16toh(buffer->PhyData[i].ControllerDevHandle);
	}

	free(buffer, M_DEVBUF);
out:
	return (error);
}

static int
mptsas_sas_phy_pg0(struct mpt_softc *mpt, struct mptsas_phyinfo *phy_info,
	uint32_t form, uint32_t form_specific)
{
	ConfigExtendedPageHeader_t hdr;
	SasPhyPage0_t *buffer;
	int error;

	error = mpt_read_extcfg_header(mpt, MPI_SASPHY0_PAGEVERSION, 0, 0,
				       MPI_CONFIG_EXTPAGETYPE_SAS_PHY, &hdr,
				       0, 10000);
	if (error)
		goto out;
	if (hdr.ExtPageLength == 0) {
		error = ENXIO;
		goto out;
	}

	buffer = malloc(sizeof(SasPhyPage0_t), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (buffer == NULL) {
		error = ENOMEM;
		goto out;
	}

	error = mpt_read_extcfg_page(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
				     form + form_specific, &hdr, buffer,
				     sizeof(SasPhyPage0_t), 0, 10000);
	if (error) {
		free(buffer, M_DEVBUF);
		goto out;
	}

	phy_info->hw_link_rate = buffer->HwLinkRate;
	phy_info->programmed_link_rate = buffer->ProgrammedLinkRate;
	phy_info->identify.dev_handle = le16toh(buffer->OwnerDevHandle);
	phy_info->attached.dev_handle = le16toh(buffer->AttachedDevHandle);

	free(buffer, M_DEVBUF);
out:
	return (error);
}

static int
mptsas_sas_device_pg0(struct mpt_softc *mpt, struct mptsas_devinfo *device_info,
	uint32_t form, uint32_t form_specific)
{
	ConfigExtendedPageHeader_t hdr;
	SasDevicePage0_t *buffer;
	uint64_t sas_address;
	int error = 0;

	bzero(device_info, sizeof(*device_info));
	error = mpt_read_extcfg_header(mpt, MPI_SASDEVICE0_PAGEVERSION, 0, 0,
				       MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
				       &hdr, 0, 10000);
	if (error)
		goto out;
	if (hdr.ExtPageLength == 0) {
		error = ENXIO;
		goto out;
	}

	buffer = malloc(sizeof(SasDevicePage0_t), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (buffer == NULL) {
		error = ENOMEM;
		goto out;
	}

	error = mpt_read_extcfg_page(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
				     form + form_specific, &hdr, buffer,
				     sizeof(SasDevicePage0_t), 0, 10000);
	if (error) {
		free(buffer, M_DEVBUF);
		goto out;
	}

	device_info->dev_handle = le16toh(buffer->DevHandle);
	device_info->parent_dev_handle = le16toh(buffer->ParentDevHandle);
	device_info->enclosure_handle = le16toh(buffer->EnclosureHandle);
	device_info->slot = le16toh(buffer->Slot);
	device_info->phy_num = buffer->PhyNum;
	device_info->physical_port = buffer->PhysicalPort;
	device_info->target_id = buffer->TargetID;
	device_info->bus = buffer->Bus;
	bcopy(&buffer->SASAddress, &sas_address, sizeof(uint64_t));
	device_info->sas_address = le64toh(sas_address);
	device_info->device_info = le32toh(buffer->DeviceInfo);

	free(buffer, M_DEVBUF);
out:
	return (error);
}

/*
 * Read SAS configuration information. Nothing to do yet.
 */
static int
mpt_read_config_info_sas(struct mpt_softc *mpt)
{
	struct mptsas_portinfo *portinfo;
	struct mptsas_phyinfo *phyinfo;
	int error, i;

	portinfo = malloc(sizeof(*portinfo), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (portinfo == NULL)
		return (ENOMEM);

	error = mptsas_sas_io_unit_pg0(mpt, portinfo);
	if (error) {
		free(portinfo, M_DEVBUF);
		return (0);
	}

	for (i = 0; i < portinfo->num_phys; i++) {
		phyinfo = &portinfo->phy_info[i];
		error = mptsas_sas_phy_pg0(mpt, phyinfo,
		    (MPI_SAS_PHY_PGAD_FORM_PHY_NUMBER <<
		    MPI_SAS_PHY_PGAD_FORM_SHIFT), i);
		if (error)
			break;
		error = mptsas_sas_device_pg0(mpt, &phyinfo->identify,
		    (MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
		    MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
		    phyinfo->handle);
		if (error)
			break;
		phyinfo->identify.phy_num = phyinfo->phy_num = i;
		if (phyinfo->attached.dev_handle)
			error = mptsas_sas_device_pg0(mpt,
			    &phyinfo->attached,
			    (MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
			    MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
			    phyinfo->attached.dev_handle);
		if (error)
			break;
	}
	mpt->sas_portinfo = portinfo;
	return (0);
}

static void
mptsas_set_sata_wc(struct mpt_softc *mpt, struct mptsas_devinfo *devinfo,
	int enabled)
{
	SataPassthroughRequest_t	*pass;
	request_t *req;
	int error, status;

	req = mpt_get_request(mpt, 0);
	if (req == NULL)
		return;

	pass = req->req_vbuf;
	bzero(pass, sizeof(SataPassthroughRequest_t));
	pass->Function = MPI_FUNCTION_SATA_PASSTHROUGH;
	pass->TargetID = devinfo->target_id;
	pass->Bus = devinfo->bus;
	pass->PassthroughFlags = 0;
	pass->ConnectionRate = MPI_SATA_PT_REQ_CONNECT_RATE_NEGOTIATED;
	pass->DataLength = 0;
	pass->MsgContext = htole32(req->index | sata_pass_handler_id);
	pass->CommandFIS[0] = 0x27;
	pass->CommandFIS[1] = 0x80;
	pass->CommandFIS[2] = 0xef;
	pass->CommandFIS[3] = (enabled) ? 0x02 : 0x82;
	pass->CommandFIS[7] = 0x40;
	pass->CommandFIS[15] = 0x08;

	mpt_check_doorbell(mpt);
	mpt_send_cmd(mpt, req);
	error = mpt_wait_req(mpt, req, REQ_STATE_DONE, REQ_STATE_DONE, 0,
			     10 * 1000);
	if (error) {
		mpt_free_request(mpt, req);
		printf("error %d sending passthrough\n", error);
		return;
	}

	status = le16toh(req->IOCStatus);
	if (status != MPI_IOCSTATUS_SUCCESS) {
		mpt_free_request(mpt, req);
		printf("IOCSTATUS %d\n", status);
		return;
	}

	mpt_free_request(mpt, req);
}

/*
 * Set SAS configuration information. Nothing to do yet.
 */
static int
mpt_set_initial_config_sas(struct mpt_softc *mpt)
{
	struct mptsas_phyinfo *phyinfo;
	int i;

	if ((mpt_enable_sata_wc != -1) && (mpt->sas_portinfo != NULL)) {
		for (i = 0; i < mpt->sas_portinfo->num_phys; i++) {
			phyinfo = &mpt->sas_portinfo->phy_info[i];
			if (phyinfo->attached.dev_handle == 0)
				continue;
			if ((phyinfo->attached.device_info &
			    MPI_SAS_DEVICE_INFO_SATA_DEVICE) == 0)
				continue;
			if (bootverbose)
				device_printf(mpt->dev,
				    "%sabling SATA WC on phy %d\n",
				    (mpt_enable_sata_wc) ? "En" : "Dis", i);
			mptsas_set_sata_wc(mpt, &phyinfo->attached,
					   mpt_enable_sata_wc);
		}
	}

	return (0);
}

static int
mpt_sata_pass_reply_handler(struct mpt_softc *mpt, request_t *req,
 uint32_t reply_desc, MSG_DEFAULT_REPLY *reply_frame)
{

	if (req != NULL) {
		if (reply_frame != NULL) {
			req->IOCStatus = le16toh(reply_frame->IOCStatus);
		}
		req->state &= ~REQ_STATE_QUEUED;
		req->state |= REQ_STATE_DONE;
		TAILQ_REMOVE(&mpt->request_pending_list, req, links);
		if ((req->state & REQ_STATE_NEED_WAKEUP) != 0) {
			wakeup(req);
		} else if ((req->state & REQ_STATE_TIMEDOUT) != 0) {
			/*
			 * Whew- we can free this request (late completion)
			 */
			mpt_free_request(mpt, req);
		}
	}

	return (TRUE);
}

/*
 * Read SCSI configuration information
 */
static int
mpt_read_config_info_spi(struct mpt_softc *mpt)
{
	int rv, i;

	rv = mpt_read_cfg_header(mpt, MPI_CONFIG_PAGETYPE_SCSI_PORT, 0, 0,
	    &mpt->mpt_port_page0.Header, FALSE, 5000);
	if (rv) {
		return (-1);
	}
	mpt_lprt(mpt, MPT_PRT_DEBUG, "SPI Port Page 0 Header: %x %x %x %x\n",
	    mpt->mpt_port_page0.Header.PageVersion,
	    mpt->mpt_port_page0.Header.PageLength,
	    mpt->mpt_port_page0.Header.PageNumber,
	    mpt->mpt_port_page0.Header.PageType);

	rv = mpt_read_cfg_header(mpt, MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
	    &mpt->mpt_port_page1.Header, FALSE, 5000);
	if (rv) {
		return (-1);
	}
	mpt_lprt(mpt, MPT_PRT_DEBUG, "SPI Port Page 1 Header: %x %x %x %x\n",
	    mpt->mpt_port_page1.Header.PageVersion,
	    mpt->mpt_port_page1.Header.PageLength,
	    mpt->mpt_port_page1.Header.PageNumber,
	    mpt->mpt_port_page1.Header.PageType);

	rv = mpt_read_cfg_header(mpt, MPI_CONFIG_PAGETYPE_SCSI_PORT, 2, 0,
	    &mpt->mpt_port_page2.Header, FALSE, 5000);
	if (rv) {
		return (-1);
	}
	mpt_lprt(mpt, MPT_PRT_DEBUG, "SPI Port Page 2 Header: %x %x %x %x\n",
	    mpt->mpt_port_page2.Header.PageVersion,
	    mpt->mpt_port_page2.Header.PageLength,
	    mpt->mpt_port_page2.Header.PageNumber,
	    mpt->mpt_port_page2.Header.PageType);

	for (i = 0; i < 16; i++) {
		rv = mpt_read_cfg_header(mpt, MPI_CONFIG_PAGETYPE_SCSI_DEVICE,
		    0, i, &mpt->mpt_dev_page0[i].Header, FALSE, 5000);
		if (rv) {
			return (-1);
		}
		mpt_lprt(mpt, MPT_PRT_DEBUG,
		    "SPI Target %d Device Page 0 Header: %x %x %x %x\n", i,
		    mpt->mpt_dev_page0[i].Header.PageVersion,
		    mpt->mpt_dev_page0[i].Header.PageLength,
		    mpt->mpt_dev_page0[i].Header.PageNumber,
		    mpt->mpt_dev_page0[i].Header.PageType);
		
		rv = mpt_read_cfg_header(mpt, MPI_CONFIG_PAGETYPE_SCSI_DEVICE,
		    1, i, &mpt->mpt_dev_page1[i].Header, FALSE, 5000);
		if (rv) {
			return (-1);
		}
		mpt_lprt(mpt, MPT_PRT_DEBUG,
		    "SPI Target %d Device Page 1 Header: %x %x %x %x\n", i,
		    mpt->mpt_dev_page1[i].Header.PageVersion,
		    mpt->mpt_dev_page1[i].Header.PageLength,
		    mpt->mpt_dev_page1[i].Header.PageNumber,
		    mpt->mpt_dev_page1[i].Header.PageType);
	}

	/*
	 * At this point, we don't *have* to fail. As long as we have
	 * valid config header information, we can (barely) lurch
	 * along.
	 */

	rv = mpt_read_cur_cfg_page(mpt, 0, &mpt->mpt_port_page0.Header,
	    sizeof(mpt->mpt_port_page0), FALSE, 5000);
	if (rv) {
		mpt_prt(mpt, "failed to read SPI Port Page 0\n");
	} else {
		mpt2host_config_page_scsi_port_0(&mpt->mpt_port_page0);
		mpt_lprt(mpt, MPT_PRT_NEGOTIATION,
		    "SPI Port Page 0: Capabilities %x PhysicalInterface %x\n",
		    mpt->mpt_port_page0.Capabilities,
		    mpt->mpt_port_page0.PhysicalInterface);
	}

	rv = mpt_read_cur_cfg_page(mpt, 0, &mpt->mpt_port_page1.Header,
	    sizeof(mpt->mpt_port_page1), FALSE, 5000);
	if (rv) {
		mpt_prt(mpt, "failed to read SPI Port Page 1\n");
	} else {
		mpt2host_config_page_scsi_port_1(&mpt->mpt_port_page1);
		mpt_lprt(mpt, MPT_PRT_DEBUG,
		    "SPI Port Page 1: Configuration %x OnBusTimerValue %x\n",
		    mpt->mpt_port_page1.Configuration,
		    mpt->mpt_port_page1.OnBusTimerValue);
	}

	rv = mpt_read_cur_cfg_page(mpt, 0, &mpt->mpt_port_page2.Header,
	    sizeof(mpt->mpt_port_page2), FALSE, 5000);
	if (rv) {
		mpt_prt(mpt, "failed to read SPI Port Page 2\n");
	} else {
		mpt_lprt(mpt, MPT_PRT_NEGOTIATION,
		    "Port Page 2: Flags %x Settings %x\n",
		    mpt->mpt_port_page2.PortFlags,
		    mpt->mpt_port_page2.PortSettings);
		mpt2host_config_page_scsi_port_2(&mpt->mpt_port_page2);
		for (i = 0; i < 16; i++) {
			mpt_lprt(mpt, MPT_PRT_NEGOTIATION,
		  	    " Port Page 2 Tgt %d: timo %x SF %x Flags %x\n",
			    i, mpt->mpt_port_page2.DeviceSettings[i].Timeout,
			    mpt->mpt_port_page2.DeviceSettings[i].SyncFactor,
			    mpt->mpt_port_page2.DeviceSettings[i].DeviceFlags);
		}
	}

	for (i = 0; i < 16; i++) {
		rv = mpt_read_cur_cfg_page(mpt, i,
		    &mpt->mpt_dev_page0[i].Header, sizeof(*mpt->mpt_dev_page0),
		    FALSE, 5000);
		if (rv) {
			mpt_prt(mpt,
			    "cannot read SPI Target %d Device Page 0\n", i);
			continue;
		}
		mpt2host_config_page_scsi_device_0(&mpt->mpt_dev_page0[i]);
		mpt_lprt(mpt, MPT_PRT_NEGOTIATION,
		    "target %d page 0: Negotiated Params %x Information %x\n",
		    i, mpt->mpt_dev_page0[i].NegotiatedParameters,
		    mpt->mpt_dev_page0[i].Information);

		rv = mpt_read_cur_cfg_page(mpt, i,
		    &mpt->mpt_dev_page1[i].Header, sizeof(*mpt->mpt_dev_page1),
		    FALSE, 5000);
		if (rv) {
			mpt_prt(mpt,
			    "cannot read SPI Target %d Device Page 1\n", i);
			continue;
		}
		mpt2host_config_page_scsi_device_1(&mpt->mpt_dev_page1[i]);
		mpt_lprt(mpt, MPT_PRT_NEGOTIATION,
		    "target %d page 1: Requested Params %x Configuration %x\n",
		    i, mpt->mpt_dev_page1[i].RequestedParameters,
		    mpt->mpt_dev_page1[i].Configuration);
	}
	return (0);
}

/*
 * Validate SPI configuration information.
 *
 * In particular, validate SPI Port Page 1.
 */
static int
mpt_set_initial_config_spi(struct mpt_softc *mpt)
{
	int error, i, pp1val;

	mpt->mpt_disc_enable = 0xff;
	mpt->mpt_tag_enable = 0;

	pp1val = ((1 << mpt->mpt_ini_id) <<
	    MPI_SCSIPORTPAGE1_CFG_SHIFT_PORT_RESPONSE_ID) | mpt->mpt_ini_id;
	if (mpt->mpt_port_page1.Configuration != pp1val) {
		CONFIG_PAGE_SCSI_PORT_1 tmp;

		mpt_prt(mpt, "SPI Port Page 1 Config value bad (%x)- should "
		    "be %x\n", mpt->mpt_port_page1.Configuration, pp1val);
		tmp = mpt->mpt_port_page1;
		tmp.Configuration = pp1val;
		host2mpt_config_page_scsi_port_1(&tmp);
		error = mpt_write_cur_cfg_page(mpt, 0,
		    &tmp.Header, sizeof(tmp), FALSE, 5000);
		if (error) {
			return (-1);
		}
		error = mpt_read_cur_cfg_page(mpt, 0,
		    &tmp.Header, sizeof(tmp), FALSE, 5000);
		if (error) {
			return (-1);
		}
		mpt2host_config_page_scsi_port_1(&tmp);
		if (tmp.Configuration != pp1val) {
			mpt_prt(mpt,
			    "failed to reset SPI Port Page 1 Config value\n");
			return (-1);
		}
		mpt->mpt_port_page1 = tmp;
	}

	/*
	 * The purpose of this exercise is to get
	 * all targets back to async/narrow.
	 *
	 * We skip this step if the BIOS has already negotiated
	 * speeds with the targets.
	 */
	i = mpt->mpt_port_page2.PortSettings &
	    MPI_SCSIPORTPAGE2_PORT_MASK_NEGO_MASTER_SETTINGS;
	if (i == MPI_SCSIPORTPAGE2_PORT_ALL_MASTER_SETTINGS) {
		mpt_lprt(mpt, MPT_PRT_NEGOTIATION,
		    "honoring BIOS transfer negotiations\n");
	} else {
		for (i = 0; i < 16; i++) {
			mpt->mpt_dev_page1[i].RequestedParameters = 0;
			mpt->mpt_dev_page1[i].Configuration = 0;
			(void) mpt_update_spi_config(mpt, i);
		}
	}
	return (0);
}

static int
mpt_cam_enable(struct mpt_softc *mpt)
{
	int error;

	MPT_LOCK(mpt);

	error = EIO;
	if (mpt->is_fc) {
		if (mpt_read_config_info_fc(mpt)) {
			goto out;
		}
		if (mpt_set_initial_config_fc(mpt)) {
			goto out;
		}
	} else if (mpt->is_sas) {
		if (mpt_read_config_info_sas(mpt)) {
			goto out;
		}
		if (mpt_set_initial_config_sas(mpt)) {
			goto out;
		}
	} else if (mpt->is_spi) {
		if (mpt_read_config_info_spi(mpt)) {
			goto out;
		}
		if (mpt_set_initial_config_spi(mpt)) {
			goto out;
		}
	}
	error = 0;

out:
	MPT_UNLOCK(mpt);
	return (error);
}

static void
mpt_cam_ready(struct mpt_softc *mpt)
{

	/*
	 * If we're in target mode, hang out resources now
	 * so we don't cause the world to hang talking to us.
	 */
	if (mpt->is_fc && (mpt->role & MPT_ROLE_TARGET)) {
		/*
		 * Try to add some target command resources
		 */
		MPT_LOCK(mpt);
		if (mpt_add_target_commands(mpt) == FALSE) {
			mpt_prt(mpt, "failed to add target commands\n");
		}
		MPT_UNLOCK(mpt);
	}
	mpt->ready = 1;
}

static void
mpt_cam_detach(struct mpt_softc *mpt)
{
	mpt_handler_t handler;

	MPT_LOCK(mpt);
	mpt->ready = 0;
	mpt_terminate_recovery_thread(mpt); 

	handler.reply_handler = mpt_scsi_reply_handler;
	mpt_deregister_handler(mpt, MPT_HANDLER_REPLY, handler,
			       scsi_io_handler_id);
	handler.reply_handler = mpt_scsi_tmf_reply_handler;
	mpt_deregister_handler(mpt, MPT_HANDLER_REPLY, handler,
			       scsi_tmf_handler_id);
	handler.reply_handler = mpt_fc_els_reply_handler;
	mpt_deregister_handler(mpt, MPT_HANDLER_REPLY, handler,
			       fc_els_handler_id);
	handler.reply_handler = mpt_scsi_tgt_reply_handler;
	mpt_deregister_handler(mpt, MPT_HANDLER_REPLY, handler,
			       mpt->scsi_tgt_handler_id);
	handler.reply_handler = mpt_sata_pass_reply_handler;
	mpt_deregister_handler(mpt, MPT_HANDLER_REPLY, handler,
			       sata_pass_handler_id);

	if (mpt->tmf_req != NULL) {
		mpt->tmf_req->state = REQ_STATE_ALLOCATED;
		mpt_free_request(mpt, mpt->tmf_req);
		mpt->tmf_req = NULL;
	}
	if (mpt->sas_portinfo != NULL) {
		free(mpt->sas_portinfo, M_DEVBUF);
		mpt->sas_portinfo = NULL;
	}

	if (mpt->sim != NULL) {
		xpt_free_path(mpt->path);
		xpt_bus_deregister(cam_sim_path(mpt->sim));
		cam_sim_free(mpt->sim, TRUE);
		mpt->sim = NULL;
	}

	if (mpt->phydisk_sim != NULL) {
		xpt_free_path(mpt->phydisk_path);
		xpt_bus_deregister(cam_sim_path(mpt->phydisk_sim));
		cam_sim_free(mpt->phydisk_sim, TRUE);
		mpt->phydisk_sim = NULL;
	}
	MPT_UNLOCK(mpt);
}

/* This routine is used after a system crash to dump core onto the swap device.
 */
static void
mpt_poll(struct cam_sim *sim)
{
	struct mpt_softc *mpt;

	mpt = (struct mpt_softc *)cam_sim_softc(sim);
	mpt_intr(mpt);
}

/*
 * Watchdog timeout routine for SCSI requests.
 */
static void
mpt_timeout(void *arg)
{
	union ccb	 *ccb;
	struct mpt_softc *mpt;
	request_t	 *req;

	ccb = (union ccb *)arg;
	mpt = ccb->ccb_h.ccb_mpt_ptr;

	MPT_LOCK_ASSERT(mpt);
	req = ccb->ccb_h.ccb_req_ptr;
	mpt_prt(mpt, "request %p:%u timed out for ccb %p (req->ccb %p)\n", req,
	    req->serno, ccb, req->ccb);
/* XXX: WHAT ARE WE TRYING TO DO HERE? */
	if ((req->state & REQ_STATE_QUEUED) == REQ_STATE_QUEUED) {
		TAILQ_REMOVE(&mpt->request_pending_list, req, links);
		TAILQ_INSERT_TAIL(&mpt->request_timeout_list, req, links);
		req->state |= REQ_STATE_TIMEDOUT;
		mpt_wakeup_recovery_thread(mpt);
	}
}

/*
 * Callback routine from bus_dmamap_load_ccb(9) or, in simple cases, called
 * directly.
 *
 * Takes a list of physical segments and builds the SGL for SCSI IO command
 * and forwards the commard to the IOC after one last check that CAM has not
 * aborted the transaction.
 */
static void
mpt_execute_req_a64(void *arg, bus_dma_segment_t *dm_segs, int nseg, int error)
{
	request_t *req, *trq;
	char *mpt_off;
	union ccb *ccb;
	struct mpt_softc *mpt;
	bus_addr_t chain_list_addr;
	int first_lim, seg, this_seg_lim;
	uint32_t addr, cur_off, flags, nxt_off, tf;
	void *sglp = NULL;
	MSG_REQUEST_HEADER *hdrp;
	SGE_SIMPLE64 *se;
	SGE_CHAIN64 *ce;
	int istgt = 0;

	req = (request_t *)arg;
	ccb = req->ccb;

	mpt = ccb->ccb_h.ccb_mpt_ptr;
	req = ccb->ccb_h.ccb_req_ptr;

	hdrp = req->req_vbuf;
	mpt_off = req->req_vbuf;

	if (error == 0 && ((uint32_t)nseg) >= mpt->max_seg_cnt) {
		error = EFBIG;
	}

	if (error == 0) {
		switch (hdrp->Function) {
		case MPI_FUNCTION_SCSI_IO_REQUEST:
		case MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH:
			istgt = 0;
			sglp = &((PTR_MSG_SCSI_IO_REQUEST)hdrp)->SGL;
			break;
		case MPI_FUNCTION_TARGET_ASSIST:
			istgt = 1;
			sglp = &((PTR_MSG_TARGET_ASSIST_REQUEST)hdrp)->SGL;
			break;
		default:
			mpt_prt(mpt, "bad fct 0x%x in mpt_execute_req_a64\n",
			    hdrp->Function);
			error = EINVAL;
			break;
		}
	}

	if (error == 0 && ((uint32_t)nseg) >= mpt->max_seg_cnt) {
		error = EFBIG;
		mpt_prt(mpt, "segment count %d too large (max %u)\n",
		    nseg, mpt->max_seg_cnt);
	}

bad:
	if (error != 0) {
		if (error != EFBIG && error != ENOMEM) {
			mpt_prt(mpt, "mpt_execute_req_a64: err %d\n", error);
		}
		if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_INPROG) {
			cam_status status;
			mpt_freeze_ccb(ccb);
			if (error == EFBIG) {
				status = CAM_REQ_TOO_BIG;
			} else if (error == ENOMEM) {
				if (mpt->outofbeer == 0) {
					mpt->outofbeer = 1;
					xpt_freeze_simq(mpt->sim, 1);
					mpt_lprt(mpt, MPT_PRT_DEBUG,
					    "FREEZEQ\n");
				}
				status = CAM_REQUEUE_REQ;
			} else {
				status = CAM_REQ_CMP_ERR;
			}
			mpt_set_ccb_status(ccb, status);
		}
		if (hdrp->Function == MPI_FUNCTION_TARGET_ASSIST) {
			request_t *cmd_req =
				MPT_TAG_2_REQ(mpt, ccb->csio.tag_id);
			MPT_TGT_STATE(mpt, cmd_req)->state = TGT_STATE_IN_CAM;
			MPT_TGT_STATE(mpt, cmd_req)->ccb = NULL;
			MPT_TGT_STATE(mpt, cmd_req)->req = NULL;
		}
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		KASSERT(ccb->ccb_h.status, ("zero ccb sts at %d", __LINE__));
		xpt_done(ccb);
		mpt_free_request(mpt, req);
		return;
	}

	/*
	 * No data to transfer?
	 * Just make a single simple SGL with zero length.
	 */

	if (mpt->verbose >= MPT_PRT_DEBUG) {
		int tidx = ((char *)sglp) - mpt_off;
		memset(&mpt_off[tidx], 0xff, MPT_REQUEST_AREA - tidx);
	}

	if (nseg == 0) {
		SGE_SIMPLE32 *se1 = (SGE_SIMPLE32 *) sglp;
		MPI_pSGE_SET_FLAGS(se1,
		    (MPI_SGE_FLAGS_LAST_ELEMENT | MPI_SGE_FLAGS_END_OF_BUFFER |
		    MPI_SGE_FLAGS_SIMPLE_ELEMENT | MPI_SGE_FLAGS_END_OF_LIST));
		se1->FlagsLength = htole32(se1->FlagsLength);
		goto out;
	}


	flags = MPI_SGE_FLAGS_SIMPLE_ELEMENT | MPI_SGE_FLAGS_64_BIT_ADDRESSING;
	if (istgt == 0) {
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT) {
			flags |= MPI_SGE_FLAGS_HOST_TO_IOC;
		}
	} else {
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
			flags |= MPI_SGE_FLAGS_HOST_TO_IOC;
		}
	}

	if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		bus_dmasync_op_t op;
		if (istgt == 0) {
			if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
				op = BUS_DMASYNC_PREREAD;
			} else {
				op = BUS_DMASYNC_PREWRITE;
			}
		} else {
			if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
				op = BUS_DMASYNC_PREWRITE;
			} else {
				op = BUS_DMASYNC_PREREAD;
			}
		}
		bus_dmamap_sync(mpt->buffer_dmat, req->dmap, op);
	}

	/*
	 * Okay, fill in what we can at the end of the command frame.
	 * If we have up to MPT_NSGL_FIRST, we can fit them all into
	 * the command frame.
	 *
	 * Otherwise, we fill up through MPT_NSGL_FIRST less one
	 * SIMPLE64 pointers and start doing CHAIN64 entries after
	 * that.
	 */

	if (nseg < MPT_NSGL_FIRST(mpt)) {
		first_lim = nseg;
	} else {
		/*
		 * Leave room for CHAIN element
		 */
		first_lim = MPT_NSGL_FIRST(mpt) - 1;
	}

	se = (SGE_SIMPLE64 *) sglp;
	for (seg = 0; seg < first_lim; seg++, se++, dm_segs++) {
		tf = flags;
		memset(se, 0, sizeof (*se));
		MPI_pSGE_SET_LENGTH(se, dm_segs->ds_len);
		se->Address.Low = htole32(dm_segs->ds_addr & 0xffffffff);
		if (sizeof(bus_addr_t) > 4) {
			addr = ((uint64_t)dm_segs->ds_addr) >> 32;
			/* SAS1078 36GB limitation WAR */
			if (mpt->is_1078 && (((uint64_t)dm_segs->ds_addr +
			    MPI_SGE_LENGTH(se->FlagsLength)) >> 32) == 9) {
				addr |= (1U << 31);
				tf |= MPI_SGE_FLAGS_LOCAL_ADDRESS;
			}
			se->Address.High = htole32(addr);
		}
		if (seg == first_lim - 1) {
			tf |= MPI_SGE_FLAGS_LAST_ELEMENT;
		}
		if (seg == nseg - 1) {
			tf |=	MPI_SGE_FLAGS_END_OF_LIST |
				MPI_SGE_FLAGS_END_OF_BUFFER;
		}
		MPI_pSGE_SET_FLAGS(se, tf);
		se->FlagsLength = htole32(se->FlagsLength);
	}

	if (seg == nseg) {
		goto out;
	}

	/*
	 * Tell the IOC where to find the first chain element.
	 */
	hdrp->ChainOffset = ((char *)se - (char *)hdrp) >> 2;
	nxt_off = MPT_RQSL(mpt);
	trq = req;

	/*
	 * Make up the rest of the data segments out of a chain element
	 * (contained in the current request frame) which points to
	 * SIMPLE64 elements in the next request frame, possibly ending
	 * with *another* chain element (if there's more).
	 */
	while (seg < nseg) {
		/*
		 * Point to the chain descriptor. Note that the chain
		 * descriptor is at the end of the *previous* list (whether
		 * chain or simple).
		 */
		ce = (SGE_CHAIN64 *) se;

		/*
		 * Before we change our current pointer, make  sure we won't
		 * overflow the request area with this frame. Note that we
		 * test against 'greater than' here as it's okay in this case
		 * to have next offset be just outside the request area.
		 */
		if ((nxt_off + MPT_RQSL(mpt)) > MPT_REQUEST_AREA) {
			nxt_off = MPT_REQUEST_AREA;
			goto next_chain;
		}

		/*
		 * Set our SGE element pointer to the beginning of the chain
		 * list and update our next chain list offset.
		 */
		se = (SGE_SIMPLE64 *) &mpt_off[nxt_off];
		cur_off = nxt_off;
		nxt_off += MPT_RQSL(mpt);

		/*
		 * Now initialize the chain descriptor.
		 */
		memset(ce, 0, sizeof (*ce));

		/*
		 * Get the physical address of the chain list.
		 */
		chain_list_addr = trq->req_pbuf;
		chain_list_addr += cur_off;
		if (sizeof (bus_addr_t) > 4) {
			ce->Address.High =
			    htole32(((uint64_t)chain_list_addr) >> 32);
		}
		ce->Address.Low = htole32(chain_list_addr & 0xffffffff);
		ce->Flags = MPI_SGE_FLAGS_CHAIN_ELEMENT |
			    MPI_SGE_FLAGS_64_BIT_ADDRESSING;

		/*
		 * If we have more than a frame's worth of segments left,
		 * set up the chain list to have the last element be another
		 * chain descriptor.
		 */
		if ((nseg - seg) > MPT_NSGL(mpt)) {
			this_seg_lim = seg + MPT_NSGL(mpt) - 1;
			/*
			 * The length of the chain is the length in bytes of the
			 * number of segments plus the next chain element.
			 *
			 * The next chain descriptor offset is the length,
			 * in words, of the number of segments.
			 */
			ce->Length = (this_seg_lim - seg) *
			    sizeof (SGE_SIMPLE64);
			ce->NextChainOffset = ce->Length >> 2;
			ce->Length += sizeof (SGE_CHAIN64);
		} else {
			this_seg_lim = nseg;
			ce->Length = (this_seg_lim - seg) *
			    sizeof (SGE_SIMPLE64);
		}
		ce->Length = htole16(ce->Length);

		/*
		 * Fill in the chain list SGE elements with our segment data.
		 *
		 * If we're the last element in this chain list, set the last
		 * element flag. If we're the completely last element period,
		 * set the end of list and end of buffer flags.
		 */
		while (seg < this_seg_lim) {
			tf = flags;
			memset(se, 0, sizeof (*se));
			MPI_pSGE_SET_LENGTH(se, dm_segs->ds_len);
			se->Address.Low = htole32(dm_segs->ds_addr &
			    0xffffffff);
			if (sizeof (bus_addr_t) > 4) {
				addr = ((uint64_t)dm_segs->ds_addr) >> 32;
				/* SAS1078 36GB limitation WAR */
				if (mpt->is_1078 &&
				    (((uint64_t)dm_segs->ds_addr +
				    MPI_SGE_LENGTH(se->FlagsLength)) >>
				    32) == 9) {
					addr |= (1U << 31);
					tf |= MPI_SGE_FLAGS_LOCAL_ADDRESS;
				}
				se->Address.High = htole32(addr);
			}
			if (seg == this_seg_lim - 1) {
				tf |=	MPI_SGE_FLAGS_LAST_ELEMENT;
			}
			if (seg == nseg - 1) {
				tf |=	MPI_SGE_FLAGS_END_OF_LIST |
					MPI_SGE_FLAGS_END_OF_BUFFER;
			}
			MPI_pSGE_SET_FLAGS(se, tf);
			se->FlagsLength = htole32(se->FlagsLength);
			se++;
			seg++;
			dm_segs++;
		}

    next_chain:
		/*
		 * If we have more segments to do and we've used up all of
		 * the space in a request area, go allocate another one
		 * and chain to that.
		 */
		if (seg < nseg && nxt_off >= MPT_REQUEST_AREA) {
			request_t *nrq;

			nrq = mpt_get_request(mpt, FALSE);

			if (nrq == NULL) {
				error = ENOMEM;
				goto bad;
			}

			/*
			 * Append the new request area on the tail of our list.
			 */
			if ((trq = req->chain) == NULL) {
				req->chain = nrq;
			} else {
				while (trq->chain != NULL) {
					trq = trq->chain;
				}
				trq->chain = nrq;
			}
			trq = nrq;
			mpt_off = trq->req_vbuf;
			if (mpt->verbose >= MPT_PRT_DEBUG) {
				memset(mpt_off, 0xff, MPT_REQUEST_AREA);
			}
			nxt_off = 0;
		}
	}
out:

	/*
	 * Last time we need to check if this CCB needs to be aborted.
	 */
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_INPROG) {
		if (hdrp->Function == MPI_FUNCTION_TARGET_ASSIST) {
			request_t *cmd_req =
				MPT_TAG_2_REQ(mpt, ccb->csio.tag_id);
			MPT_TGT_STATE(mpt, cmd_req)->state = TGT_STATE_IN_CAM;
			MPT_TGT_STATE(mpt, cmd_req)->ccb = NULL;
			MPT_TGT_STATE(mpt, cmd_req)->req = NULL;
		}
		mpt_prt(mpt,
		    "mpt_execute_req_a64: I/O cancelled (status 0x%x)\n",
		    ccb->ccb_h.status & CAM_STATUS_MASK);
		if (nseg) {
			bus_dmamap_unload(mpt->buffer_dmat, req->dmap);
		}
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		KASSERT(ccb->ccb_h.status, ("zero ccb sts at %d", __LINE__));
		xpt_done(ccb);
		mpt_free_request(mpt, req);
		return;
	}

	ccb->ccb_h.status |= CAM_SIM_QUEUED;
	if (ccb->ccb_h.timeout != CAM_TIME_INFINITY) {
		mpt_req_timeout(req, SBT_1MS * ccb->ccb_h.timeout,
		    mpt_timeout, ccb);
	}
	if (mpt->verbose > MPT_PRT_DEBUG) {
		int nc = 0;
		mpt_print_request(req->req_vbuf);
		for (trq = req->chain; trq; trq = trq->chain) {
			printf("  Additional Chain Area %d\n", nc++);
			mpt_dump_sgl(trq->req_vbuf, 0);
		}
	}

	if (hdrp->Function == MPI_FUNCTION_TARGET_ASSIST) {
		request_t *cmd_req = MPT_TAG_2_REQ(mpt, ccb->csio.tag_id);
		mpt_tgt_state_t *tgt = MPT_TGT_STATE(mpt, cmd_req);
#ifdef	WE_TRUST_AUTO_GOOD_STATUS
		if ((ccb->ccb_h.flags & CAM_SEND_STATUS) &&
		    csio->scsi_status == SCSI_STATUS_OK && tgt->resid == 0) {
			tgt->state = TGT_STATE_MOVING_DATA_AND_STATUS;
		} else {
			tgt->state = TGT_STATE_MOVING_DATA;
		}
#else
		tgt->state = TGT_STATE_MOVING_DATA;
#endif
	}
	mpt_send_cmd(mpt, req);
}

static void
mpt_execute_req(void *arg, bus_dma_segment_t *dm_segs, int nseg, int error)
{
	request_t *req, *trq;
	char *mpt_off;
	union ccb *ccb;
	struct mpt_softc *mpt;
	int seg, first_lim;
	uint32_t flags, nxt_off;
	void *sglp = NULL;
	MSG_REQUEST_HEADER *hdrp;
	SGE_SIMPLE32 *se;
	SGE_CHAIN32 *ce;
	int istgt = 0;

	req = (request_t *)arg;
	ccb = req->ccb;

	mpt = ccb->ccb_h.ccb_mpt_ptr;
	req = ccb->ccb_h.ccb_req_ptr;

	hdrp = req->req_vbuf;
	mpt_off = req->req_vbuf;

	if (error == 0 && ((uint32_t)nseg) >= mpt->max_seg_cnt) {
		error = EFBIG;
	}

	if (error == 0) {
		switch (hdrp->Function) {
		case MPI_FUNCTION_SCSI_IO_REQUEST:
		case MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH:
			sglp = &((PTR_MSG_SCSI_IO_REQUEST)hdrp)->SGL;
			break;
		case MPI_FUNCTION_TARGET_ASSIST:
			istgt = 1;
			sglp = &((PTR_MSG_TARGET_ASSIST_REQUEST)hdrp)->SGL;
			break;
		default:
			mpt_prt(mpt, "bad fct 0x%x in mpt_execute_req\n",
			    hdrp->Function);
			error = EINVAL;
			break;
		}
	}

	if (error == 0 && ((uint32_t)nseg) >= mpt->max_seg_cnt) {
		error = EFBIG;
		mpt_prt(mpt, "segment count %d too large (max %u)\n",
		    nseg, mpt->max_seg_cnt);
	}

bad:
	if (error != 0) {
		if (error != EFBIG && error != ENOMEM) {
			mpt_prt(mpt, "mpt_execute_req: err %d\n", error);
		}
		if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_INPROG) {
			cam_status status;
			mpt_freeze_ccb(ccb);
			if (error == EFBIG) {
				status = CAM_REQ_TOO_BIG;
			} else if (error == ENOMEM) {
				if (mpt->outofbeer == 0) {
					mpt->outofbeer = 1;
					xpt_freeze_simq(mpt->sim, 1);
					mpt_lprt(mpt, MPT_PRT_DEBUG,
					    "FREEZEQ\n");
				}
				status = CAM_REQUEUE_REQ;
			} else {
				status = CAM_REQ_CMP_ERR;
			}
			mpt_set_ccb_status(ccb, status);
		}
		if (hdrp->Function == MPI_FUNCTION_TARGET_ASSIST) {
			request_t *cmd_req =
				MPT_TAG_2_REQ(mpt, ccb->csio.tag_id);
			MPT_TGT_STATE(mpt, cmd_req)->state = TGT_STATE_IN_CAM;
			MPT_TGT_STATE(mpt, cmd_req)->ccb = NULL;
			MPT_TGT_STATE(mpt, cmd_req)->req = NULL;
		}
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		KASSERT(ccb->ccb_h.status, ("zero ccb sts at %d", __LINE__));
		xpt_done(ccb);
		mpt_free_request(mpt, req);
		return;
	}

	/*
	 * No data to transfer?
	 * Just make a single simple SGL with zero length.
	 */

	if (mpt->verbose >= MPT_PRT_DEBUG) {
		int tidx = ((char *)sglp) - mpt_off;
		memset(&mpt_off[tidx], 0xff, MPT_REQUEST_AREA - tidx);
	}

	if (nseg == 0) {
		SGE_SIMPLE32 *se1 = (SGE_SIMPLE32 *) sglp;
		MPI_pSGE_SET_FLAGS(se1,
		    (MPI_SGE_FLAGS_LAST_ELEMENT | MPI_SGE_FLAGS_END_OF_BUFFER |
		    MPI_SGE_FLAGS_SIMPLE_ELEMENT | MPI_SGE_FLAGS_END_OF_LIST));
		se1->FlagsLength = htole32(se1->FlagsLength);
		goto out;
	}


	flags = MPI_SGE_FLAGS_SIMPLE_ELEMENT;
	if (istgt == 0) {
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT) {
			flags |= MPI_SGE_FLAGS_HOST_TO_IOC;
		}
	} else {
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
			flags |= MPI_SGE_FLAGS_HOST_TO_IOC;
		}
	}

	if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		bus_dmasync_op_t op;
		if (istgt) {
			if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
				op = BUS_DMASYNC_PREREAD;
			} else {
				op = BUS_DMASYNC_PREWRITE;
			}
		} else {
			if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
				op = BUS_DMASYNC_PREWRITE;
			} else {
				op = BUS_DMASYNC_PREREAD;
			}
		}
		bus_dmamap_sync(mpt->buffer_dmat, req->dmap, op);
	}

	/*
	 * Okay, fill in what we can at the end of the command frame.
	 * If we have up to MPT_NSGL_FIRST, we can fit them all into
	 * the command frame.
	 *
	 * Otherwise, we fill up through MPT_NSGL_FIRST less one
	 * SIMPLE32 pointers and start doing CHAIN32 entries after
	 * that.
	 */

	if (nseg < MPT_NSGL_FIRST(mpt)) {
		first_lim = nseg;
	} else {
		/*
		 * Leave room for CHAIN element
		 */
		first_lim = MPT_NSGL_FIRST(mpt) - 1;
	}

	se = (SGE_SIMPLE32 *) sglp;
	for (seg = 0; seg < first_lim; seg++, se++, dm_segs++) {
		uint32_t tf;

		memset(se, 0,sizeof (*se));
		se->Address = htole32(dm_segs->ds_addr);

		MPI_pSGE_SET_LENGTH(se, dm_segs->ds_len);
		tf = flags;
		if (seg == first_lim - 1) {
			tf |= MPI_SGE_FLAGS_LAST_ELEMENT;
		}
		if (seg == nseg - 1) {
			tf |=	MPI_SGE_FLAGS_END_OF_LIST |
				MPI_SGE_FLAGS_END_OF_BUFFER;
		}
		MPI_pSGE_SET_FLAGS(se, tf);
		se->FlagsLength = htole32(se->FlagsLength);
	}

	if (seg == nseg) {
		goto out;
	}

	/*
	 * Tell the IOC where to find the first chain element.
	 */
	hdrp->ChainOffset = ((char *)se - (char *)hdrp) >> 2;
	nxt_off = MPT_RQSL(mpt);
	trq = req;

	/*
	 * Make up the rest of the data segments out of a chain element
	 * (contained in the current request frame) which points to
	 * SIMPLE32 elements in the next request frame, possibly ending
	 * with *another* chain element (if there's more).
	 */
	while (seg < nseg) {
		int this_seg_lim;
		uint32_t tf, cur_off;
		bus_addr_t chain_list_addr;

		/*
		 * Point to the chain descriptor. Note that the chain
		 * descriptor is at the end of the *previous* list (whether
		 * chain or simple).
		 */
		ce = (SGE_CHAIN32 *) se;

		/*
		 * Before we change our current pointer, make  sure we won't
		 * overflow the request area with this frame. Note that we
		 * test against 'greater than' here as it's okay in this case
		 * to have next offset be just outside the request area.
		 */
		if ((nxt_off + MPT_RQSL(mpt)) > MPT_REQUEST_AREA) {
			nxt_off = MPT_REQUEST_AREA;
			goto next_chain;
		}

		/*
		 * Set our SGE element pointer to the beginning of the chain
		 * list and update our next chain list offset.
		 */
		se = (SGE_SIMPLE32 *) &mpt_off[nxt_off];
		cur_off = nxt_off;
		nxt_off += MPT_RQSL(mpt);

		/*
		 * Now initialize the chain descriptor.
		 */
		memset(ce, 0, sizeof (*ce));

		/*
		 * Get the physical address of the chain list.
		 */
		chain_list_addr = trq->req_pbuf;
		chain_list_addr += cur_off;



		ce->Address = htole32(chain_list_addr);
		ce->Flags = MPI_SGE_FLAGS_CHAIN_ELEMENT;


		/*
		 * If we have more than a frame's worth of segments left,
		 * set up the chain list to have the last element be another
		 * chain descriptor.
		 */
		if ((nseg - seg) > MPT_NSGL(mpt)) {
			this_seg_lim = seg + MPT_NSGL(mpt) - 1;
			/*
			 * The length of the chain is the length in bytes of the
			 * number of segments plus the next chain element.
			 *
			 * The next chain descriptor offset is the length,
			 * in words, of the number of segments.
			 */
			ce->Length = (this_seg_lim - seg) *
			    sizeof (SGE_SIMPLE32);
			ce->NextChainOffset = ce->Length >> 2;
			ce->Length += sizeof (SGE_CHAIN32);
		} else {
			this_seg_lim = nseg;
			ce->Length = (this_seg_lim - seg) *
			    sizeof (SGE_SIMPLE32);
		}
		ce->Length = htole16(ce->Length);

		/*
		 * Fill in the chain list SGE elements with our segment data.
		 *
		 * If we're the last element in this chain list, set the last
		 * element flag. If we're the completely last element period,
		 * set the end of list and end of buffer flags.
		 */
		while (seg < this_seg_lim) {
			memset(se, 0, sizeof (*se));
			se->Address = htole32(dm_segs->ds_addr);

			MPI_pSGE_SET_LENGTH(se, dm_segs->ds_len);
			tf = flags;
			if (seg == this_seg_lim - 1) {
				tf |=	MPI_SGE_FLAGS_LAST_ELEMENT;
			}
			if (seg == nseg - 1) {
				tf |=	MPI_SGE_FLAGS_END_OF_LIST |
					MPI_SGE_FLAGS_END_OF_BUFFER;
			}
			MPI_pSGE_SET_FLAGS(se, tf);
			se->FlagsLength = htole32(se->FlagsLength);
			se++;
			seg++;
			dm_segs++;
		}

    next_chain:
		/*
		 * If we have more segments to do and we've used up all of
		 * the space in a request area, go allocate another one
		 * and chain to that.
		 */
		if (seg < nseg && nxt_off >= MPT_REQUEST_AREA) {
			request_t *nrq;

			nrq = mpt_get_request(mpt, FALSE);

			if (nrq == NULL) {
				error = ENOMEM;
				goto bad;
			}

			/*
			 * Append the new request area on the tail of our list.
			 */
			if ((trq = req->chain) == NULL) {
				req->chain = nrq;
			} else {
				while (trq->chain != NULL) {
					trq = trq->chain;
				}
				trq->chain = nrq;
			}
			trq = nrq;
			mpt_off = trq->req_vbuf;
			if (mpt->verbose >= MPT_PRT_DEBUG) {
				memset(mpt_off, 0xff, MPT_REQUEST_AREA);
			}
			nxt_off = 0;
		}
	}
out:

	/*
	 * Last time we need to check if this CCB needs to be aborted.
	 */
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_INPROG) {
		if (hdrp->Function == MPI_FUNCTION_TARGET_ASSIST) {
			request_t *cmd_req =
				MPT_TAG_2_REQ(mpt, ccb->csio.tag_id);
			MPT_TGT_STATE(mpt, cmd_req)->state = TGT_STATE_IN_CAM;
			MPT_TGT_STATE(mpt, cmd_req)->ccb = NULL;
			MPT_TGT_STATE(mpt, cmd_req)->req = NULL;
		}
		mpt_prt(mpt,
		    "mpt_execute_req: I/O cancelled (status 0x%x)\n",
		    ccb->ccb_h.status & CAM_STATUS_MASK);
		if (nseg) {
			bus_dmamap_unload(mpt->buffer_dmat, req->dmap);
		}
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		KASSERT(ccb->ccb_h.status, ("zero ccb sts at %d", __LINE__));
		xpt_done(ccb);
		mpt_free_request(mpt, req);
		return;
	}

	ccb->ccb_h.status |= CAM_SIM_QUEUED;
	if (ccb->ccb_h.timeout != CAM_TIME_INFINITY) {
		mpt_req_timeout(req, SBT_1MS * ccb->ccb_h.timeout,
		    mpt_timeout, ccb);
	}
	if (mpt->verbose > MPT_PRT_DEBUG) {
		int nc = 0;
		mpt_print_request(req->req_vbuf);
		for (trq = req->chain; trq; trq = trq->chain) {
			printf("  Additional Chain Area %d\n", nc++);
			mpt_dump_sgl(trq->req_vbuf, 0);
		}
	}

	if (hdrp->Function == MPI_FUNCTION_TARGET_ASSIST) {
		request_t *cmd_req = MPT_TAG_2_REQ(mpt, ccb->csio.tag_id);
		mpt_tgt_state_t *tgt = MPT_TGT_STATE(mpt, cmd_req);
#ifdef	WE_TRUST_AUTO_GOOD_STATUS
		if ((ccb->ccb_h.flags & CAM_SEND_STATUS) &&
		    csio->scsi_status == SCSI_STATUS_OK && tgt->resid == 0) {
			tgt->state = TGT_STATE_MOVING_DATA_AND_STATUS;
		} else {
			tgt->state = TGT_STATE_MOVING_DATA;
		}
#else
		tgt->state = TGT_STATE_MOVING_DATA;
#endif
	}
	mpt_send_cmd(mpt, req);
}

static void
mpt_start(struct cam_sim *sim, union ccb *ccb)
{
	request_t *req;
	struct mpt_softc *mpt;
	MSG_SCSI_IO_REQUEST *mpt_req;
	struct ccb_scsiio *csio = &ccb->csio;
	struct ccb_hdr *ccbh = &ccb->ccb_h;
	bus_dmamap_callback_t *cb;
	target_id_t tgt;
	int raid_passthru;
	int error;

	/* Get the pointer for the physical addapter */
	mpt = ccb->ccb_h.ccb_mpt_ptr;
	raid_passthru = (sim == mpt->phydisk_sim);

	if ((req = mpt_get_request(mpt, FALSE)) == NULL) {
		if (mpt->outofbeer == 0) {
			mpt->outofbeer = 1;
			xpt_freeze_simq(mpt->sim, 1);
			mpt_lprt(mpt, MPT_PRT_DEBUG, "FREEZEQ\n");
		}
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		mpt_set_ccb_status(ccb, CAM_REQUEUE_REQ);
		xpt_done(ccb);
		return;
	}
#ifdef	INVARIANTS
	mpt_req_not_spcl(mpt, req, "mpt_start", __LINE__);
#endif

	if (sizeof (bus_addr_t) > 4) {
		cb = mpt_execute_req_a64;
	} else {
		cb = mpt_execute_req;
	}

	/*
	 * Link the ccb and the request structure so we can find
	 * the other knowing either the request or the ccb
	 */
	req->ccb = ccb;
	ccb->ccb_h.ccb_req_ptr = req;

	/* Now we build the command for the IOC */
	mpt_req = req->req_vbuf;
	memset(mpt_req, 0, sizeof (MSG_SCSI_IO_REQUEST));

	mpt_req->Function = MPI_FUNCTION_SCSI_IO_REQUEST;
	if (raid_passthru) {
		mpt_req->Function = MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH;
		if (mpt_map_physdisk(mpt, ccb, &tgt) != 0) {
			ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
			mpt_set_ccb_status(ccb, CAM_DEV_NOT_THERE);
			xpt_done(ccb);
			return;
		}
		mpt_req->Bus = 0;	/* we never set bus here */
	} else {
		tgt = ccb->ccb_h.target_id;
		mpt_req->Bus = 0;	/* XXX */
		
	}
	mpt_req->SenseBufferLength =
		(csio->sense_len < MPT_SENSE_SIZE) ?
		 csio->sense_len : MPT_SENSE_SIZE;

	/*
	 * We use the message context to find the request structure when we
	 * Get the command completion interrupt from the IOC.
	 */
	mpt_req->MsgContext = htole32(req->index | scsi_io_handler_id);

	/* Which physical device to do the I/O on */
	mpt_req->TargetID = tgt;

	be64enc(mpt_req->LUN, CAM_EXTLUN_BYTE_SWIZZLE(ccb->ccb_h.target_lun));

	/* Set the direction of the transfer */
	if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
		mpt_req->Control = MPI_SCSIIO_CONTROL_READ;
	} else if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT) {
		mpt_req->Control = MPI_SCSIIO_CONTROL_WRITE;
	} else {
		mpt_req->Control = MPI_SCSIIO_CONTROL_NODATATRANSFER;
	}

	if ((ccb->ccb_h.flags & CAM_TAG_ACTION_VALID) != 0) {
		switch(ccb->csio.tag_action) {
		case MSG_HEAD_OF_Q_TAG:
			mpt_req->Control |= MPI_SCSIIO_CONTROL_HEADOFQ;
			break;
		case MSG_ACA_TASK:
			mpt_req->Control |= MPI_SCSIIO_CONTROL_ACAQ;
			break;
		case MSG_ORDERED_Q_TAG:
			mpt_req->Control |= MPI_SCSIIO_CONTROL_ORDEREDQ;
			break;
		case MSG_SIMPLE_Q_TAG:
		default:
			mpt_req->Control |= MPI_SCSIIO_CONTROL_SIMPLEQ;
			break;
		}
	} else {
		if (mpt->is_fc || mpt->is_sas) {
			mpt_req->Control |= MPI_SCSIIO_CONTROL_SIMPLEQ;
		} else {
			/* XXX No such thing for a target doing packetized. */
			mpt_req->Control |= MPI_SCSIIO_CONTROL_UNTAGGED;
		}
	}

	if (mpt->is_spi) {
		if (ccb->ccb_h.flags & CAM_DIS_DISCONNECT) {
			mpt_req->Control |= MPI_SCSIIO_CONTROL_NO_DISCONNECT;
		}
	}
	mpt_req->Control = htole32(mpt_req->Control);

	/* Copy the scsi command block into place */
	if ((ccb->ccb_h.flags & CAM_CDB_POINTER) != 0) {
		bcopy(csio->cdb_io.cdb_ptr, mpt_req->CDB, csio->cdb_len);
	} else {
		bcopy(csio->cdb_io.cdb_bytes, mpt_req->CDB, csio->cdb_len);
	}

	mpt_req->CDBLength = csio->cdb_len;
	mpt_req->DataLength = htole32(csio->dxfer_len);
	mpt_req->SenseBufferLowAddr = htole32(req->sense_pbuf);

	/*
	 * Do a *short* print here if we're set to MPT_PRT_DEBUG
	 */
	if (mpt->verbose == MPT_PRT_DEBUG) {
		U32 df;
		mpt_prt(mpt, "mpt_start: %s op 0x%x ",
		    (mpt_req->Function == MPI_FUNCTION_SCSI_IO_REQUEST)?
		    "SCSI_IO_REQUEST" : "SCSI_IO_PASSTHRU", mpt_req->CDB[0]);
		df = mpt_req->Control & MPI_SCSIIO_CONTROL_DATADIRECTION_MASK;
		if (df != MPI_SCSIIO_CONTROL_NODATATRANSFER) {
			mpt_prtc(mpt, "(%s %u byte%s ",
			    (df == MPI_SCSIIO_CONTROL_READ)?
			    "read" : "write",  csio->dxfer_len,
			    (csio->dxfer_len == 1)? ")" : "s)");
		}
		mpt_prtc(mpt, "tgt %u lun %jx req %p:%u\n", tgt,
		    (uintmax_t)ccb->ccb_h.target_lun, req, req->serno);
	}

	error = bus_dmamap_load_ccb(mpt->buffer_dmat, req->dmap, ccb, cb,
	    req, 0);
	if (error == EINPROGRESS) {
		/*
		 * So as to maintain ordering, freeze the controller queue
		 * until our mapping is returned.
		 */
		xpt_freeze_simq(mpt->sim, 1);
		ccbh->status |= CAM_RELEASE_SIMQ;
	}
}

static int
mpt_bus_reset(struct mpt_softc *mpt, target_id_t tgt, lun_id_t lun,
    int sleep_ok)
{
	int   error;
	uint16_t status;
	uint8_t response;

	error = mpt_scsi_send_tmf(mpt,
	    (tgt != CAM_TARGET_WILDCARD || lun != CAM_LUN_WILDCARD) ?
	    MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET :
	    MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS,
	    mpt->is_fc ? MPI_SCSITASKMGMT_MSGFLAGS_LIP_RESET_OPTION : 0,
	    0,	/* XXX How do I get the channel ID? */
	    tgt != CAM_TARGET_WILDCARD ? tgt : 0,
	    lun != CAM_LUN_WILDCARD ? lun : 0,
	    0, sleep_ok);

	if (error != 0) {
		/*
		 * mpt_scsi_send_tmf hard resets on failure, so no
		 * need to do so here.
		 */
		mpt_prt(mpt,
		    "mpt_bus_reset: mpt_scsi_send_tmf returned %d\n", error);
		return (EIO);
	}

	/* Wait for bus reset to be processed by the IOC. */
	error = mpt_wait_req(mpt, mpt->tmf_req, REQ_STATE_DONE,
	    REQ_STATE_DONE, sleep_ok, 5000);

	status = le16toh(mpt->tmf_req->IOCStatus);
	response = mpt->tmf_req->ResponseCode;
	mpt->tmf_req->state = REQ_STATE_FREE;

	if (error) {
		mpt_prt(mpt, "mpt_bus_reset: Reset timed-out. "
		    "Resetting controller.\n");
		mpt_reset(mpt, TRUE);
		return (ETIMEDOUT);
	}

	if ((status & MPI_IOCSTATUS_MASK) != MPI_IOCSTATUS_SUCCESS) {
		mpt_prt(mpt, "mpt_bus_reset: TMF IOC Status 0x%x. "
		    "Resetting controller.\n", status);
		mpt_reset(mpt, TRUE);
		return (EIO);
	}

	if (response != MPI_SCSITASKMGMT_RSP_TM_SUCCEEDED &&
	    response != MPI_SCSITASKMGMT_RSP_TM_COMPLETE) {
		mpt_prt(mpt, "mpt_bus_reset: TMF Response 0x%x. "
		    "Resetting controller.\n", response);
		mpt_reset(mpt, TRUE);
		return (EIO);
	}
	return (0);
}

static int
mpt_fc_reset_link(struct mpt_softc *mpt, int dowait)
{
	int r = 0;
	request_t *req;
	PTR_MSG_FC_PRIMITIVE_SEND_REQUEST fc;

 	req = mpt_get_request(mpt, FALSE);
	if (req == NULL) {
		return (ENOMEM);
	}
	fc = req->req_vbuf;
	memset(fc, 0, sizeof(*fc));
	fc->SendFlags = MPI_FC_PRIM_SEND_FLAGS_RESET_LINK;
	fc->Function = MPI_FUNCTION_FC_PRIMITIVE_SEND;
	fc->MsgContext = htole32(req->index | fc_els_handler_id);
	mpt_send_cmd(mpt, req);
	if (dowait) {
		r = mpt_wait_req(mpt, req, REQ_STATE_DONE,
		    REQ_STATE_DONE, FALSE, 60 * 1000);
		if (r == 0) {
			mpt_free_request(mpt, req);
		}
	}
	return (r);
}

static int
mpt_cam_event(struct mpt_softc *mpt, request_t *req,
	      MSG_EVENT_NOTIFY_REPLY *msg)
{
	uint32_t data0, data1;

	data0 = le32toh(msg->Data[0]);
	data1 = le32toh(msg->Data[1]);
	switch(msg->Event & 0xFF) {
	case MPI_EVENT_UNIT_ATTENTION:
		mpt_prt(mpt, "UNIT ATTENTION: Bus: 0x%02x TargetID: 0x%02x\n",
		    (data0 >> 8) & 0xff, data0 & 0xff);
		break;

	case MPI_EVENT_IOC_BUS_RESET:
		/* We generated a bus reset */
		mpt_prt(mpt, "IOC Generated Bus Reset Port: %d\n",
		    (data0 >> 8) & 0xff);
		xpt_async(AC_BUS_RESET, mpt->path, NULL);
		break;

	case MPI_EVENT_EXT_BUS_RESET:
		/* Someone else generated a bus reset */
		mpt_prt(mpt, "External Bus Reset Detected\n");
		/*
		 * These replies don't return EventData like the MPI
		 * spec says they do
		 */	
		xpt_async(AC_BUS_RESET, mpt->path, NULL);
		break;

	case MPI_EVENT_RESCAN:
	{
		union ccb *ccb;
		uint32_t pathid;
		/*
		 * In general this means a device has been added to the loop.
		 */
		mpt_prt(mpt, "Rescan Port: %d\n", (data0 >> 8) & 0xff);
		if (mpt->ready == 0) {
			break;
		}
		if (mpt->phydisk_sim) {
			pathid = cam_sim_path(mpt->phydisk_sim);
		} else {
			pathid = cam_sim_path(mpt->sim);
		}
		/*
		 * Allocate a CCB, create a wildcard path for this bus,
		 * and schedule a rescan.
		 */
		ccb = xpt_alloc_ccb_nowait();
		if (ccb == NULL) {
			mpt_prt(mpt, "unable to alloc CCB for rescan\n");
			break;
		}

		if (xpt_create_path(&ccb->ccb_h.path, NULL, pathid,
		    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
			mpt_prt(mpt, "unable to create path for rescan\n");
			xpt_free_ccb(ccb);
			break;
		}
		xpt_rescan(ccb);
		break;
	}

	case MPI_EVENT_LINK_STATUS_CHANGE:
		mpt_prt(mpt, "Port %d: LinkState: %s\n",
		    (data1 >> 8) & 0xff,
		    ((data0 & 0xff) == 0)?  "Failed" : "Active");
		break;

	case MPI_EVENT_LOOP_STATE_CHANGE:
		switch ((data0 >> 16) & 0xff) {
		case 0x01:
			mpt_prt(mpt,
			    "Port 0x%x: FC LinkEvent: LIP(%02x,%02x) "
			    "(Loop Initialization)\n",
			    (data1 >> 8) & 0xff,
			    (data0 >> 8) & 0xff,
			    (data0     ) & 0xff);
			switch ((data0 >> 8) & 0xff) {
			case 0xF7:
				if ((data0 & 0xff) == 0xF7) {
					mpt_prt(mpt, "Device needs AL_PA\n");
				} else {
					mpt_prt(mpt, "Device %02x doesn't like "
					    "FC performance\n",
					    data0 & 0xFF);
				}
				break;
			case 0xF8:
				if ((data0 & 0xff) == 0xF7) {
					mpt_prt(mpt, "Device had loop failure "
					    "at its receiver prior to acquiring"
					    " AL_PA\n");
				} else {
					mpt_prt(mpt, "Device %02x detected loop"
					    " failure at its receiver\n", 
					    data0 & 0xFF);
				}
				break;
			default:
				mpt_prt(mpt, "Device %02x requests that device "
				    "%02x reset itself\n", 
				    data0 & 0xFF,
				    (data0 >> 8) & 0xFF);
				break;
			}
			break;
		case 0x02:
			mpt_prt(mpt, "Port 0x%x: FC LinkEvent: "
			    "LPE(%02x,%02x) (Loop Port Enable)\n",
			    (data1 >> 8) & 0xff, /* Port */
			    (data0 >>  8) & 0xff, /* Character 3 */
			    (data0      ) & 0xff  /* Character 4 */);
			break;
		case 0x03:
			mpt_prt(mpt, "Port 0x%x: FC LinkEvent: "
			    "LPB(%02x,%02x) (Loop Port Bypass)\n",
			    (data1 >> 8) & 0xff, /* Port */
			    (data0 >> 8) & 0xff, /* Character 3 */
			    (data0     ) & 0xff  /* Character 4 */);
			break;
		default:
			mpt_prt(mpt, "Port 0x%x: FC LinkEvent: Unknown "
			    "FC event (%02x %02x %02x)\n",
			    (data1 >> 8) & 0xff, /* Port */
			    (data0 >> 16) & 0xff, /* Event */
			    (data0 >>  8) & 0xff, /* Character 3 */
			    (data0      ) & 0xff  /* Character 4 */);
		}
		break;

	case MPI_EVENT_LOGOUT:
		mpt_prt(mpt, "FC Logout Port: %d N_PortID: %02x\n",
		    (data1 >> 8) & 0xff, data0);
		break;
	case MPI_EVENT_QUEUE_FULL:
	{
		struct cam_sim *sim;
		struct cam_path *tmppath;
		struct ccb_relsim crs;
		PTR_EVENT_DATA_QUEUE_FULL pqf;
		lun_id_t lun_id;

		pqf = (PTR_EVENT_DATA_QUEUE_FULL)msg->Data;
		pqf->CurrentDepth = le16toh(pqf->CurrentDepth);
		if (bootverbose) {
		    mpt_prt(mpt, "QUEUE FULL EVENT: Bus 0x%02x Target 0x%02x "
			"Depth %d\n",
			pqf->Bus, pqf->TargetID, pqf->CurrentDepth);
		}
		if (mpt->phydisk_sim && mpt_is_raid_member(mpt,
		    pqf->TargetID) != 0) {
			sim = mpt->phydisk_sim;
		} else {
			sim = mpt->sim;
		}
		for (lun_id = 0; lun_id < MPT_MAX_LUNS; lun_id++) {
			if (xpt_create_path(&tmppath, NULL, cam_sim_path(sim),
			    pqf->TargetID, lun_id) != CAM_REQ_CMP) {
				mpt_prt(mpt, "unable to create a path to send "
				    "XPT_REL_SIMQ");
				break;
			}
			xpt_setup_ccb(&crs.ccb_h, tmppath, 5);
			crs.ccb_h.func_code = XPT_REL_SIMQ;
			crs.ccb_h.flags = CAM_DEV_QFREEZE;
			crs.release_flags = RELSIM_ADJUST_OPENINGS;
			crs.openings = pqf->CurrentDepth - 1;
			xpt_action((union ccb *)&crs);
			if (crs.ccb_h.status != CAM_REQ_CMP) {
				mpt_prt(mpt, "XPT_REL_SIMQ failed\n");
			}
			xpt_free_path(tmppath);
		}
		break;
	}
	case MPI_EVENT_IR_RESYNC_UPDATE:
		mpt_prt(mpt, "IR resync update %d completed\n",
		    (data0 >> 16) & 0xff);
		break;
	case MPI_EVENT_SAS_DEVICE_STATUS_CHANGE:
	{
		union ccb *ccb;
		struct cam_sim *sim;
		struct cam_path *tmppath;
		PTR_EVENT_DATA_SAS_DEVICE_STATUS_CHANGE psdsc;

		psdsc = (PTR_EVENT_DATA_SAS_DEVICE_STATUS_CHANGE)msg->Data;
		if (mpt->phydisk_sim && mpt_is_raid_member(mpt,
		    psdsc->TargetID) != 0)
			sim = mpt->phydisk_sim;
		else
			sim = mpt->sim;
		switch(psdsc->ReasonCode) {
		case MPI_EVENT_SAS_DEV_STAT_RC_ADDED:
			ccb = xpt_alloc_ccb_nowait();
			if (ccb == NULL) {
				mpt_prt(mpt,
				    "unable to alloc CCB for rescan\n");
				break;
			}
			if (xpt_create_path(&ccb->ccb_h.path, NULL,
			    cam_sim_path(sim), psdsc->TargetID,
			    CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
				mpt_prt(mpt,
				    "unable to create path for rescan\n");
				xpt_free_ccb(ccb);
				break;
			}
			xpt_rescan(ccb);
			break;
		case MPI_EVENT_SAS_DEV_STAT_RC_NOT_RESPONDING:
			if (xpt_create_path(&tmppath, NULL, cam_sim_path(sim),
			    psdsc->TargetID, CAM_LUN_WILDCARD) !=
			    CAM_REQ_CMP) {
				mpt_prt(mpt,
				    "unable to create path for async event");
				break;
			}
			xpt_async(AC_LOST_DEVICE, tmppath, NULL);
			xpt_free_path(tmppath);
			break;
		case MPI_EVENT_SAS_DEV_STAT_RC_CMPL_INTERNAL_DEV_RESET:
		case MPI_EVENT_SAS_DEV_STAT_RC_CMPL_TASK_ABORT_INTERNAL:
		case MPI_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET:
			break;
		default:
			mpt_lprt(mpt, MPT_PRT_WARN,
			    "SAS device status change: Bus: 0x%02x TargetID: "
			    "0x%02x ReasonCode: 0x%02x\n", psdsc->Bus,
			    psdsc->TargetID, psdsc->ReasonCode);
			break;
		}
		break;
	}
	case MPI_EVENT_SAS_DISCOVERY_ERROR:
	{
		PTR_EVENT_DATA_DISCOVERY_ERROR pde;

		pde = (PTR_EVENT_DATA_DISCOVERY_ERROR)msg->Data;
		pde->DiscoveryStatus = le32toh(pde->DiscoveryStatus);
		mpt_lprt(mpt, MPT_PRT_WARN,
		    "SAS discovery error: Port: 0x%02x Status: 0x%08x\n",
		    pde->Port, pde->DiscoveryStatus);
		break;
	}
	case MPI_EVENT_EVENT_CHANGE:
	case MPI_EVENT_INTEGRATED_RAID:
	case MPI_EVENT_IR2:
	case MPI_EVENT_LOG_ENTRY_ADDED:
	case MPI_EVENT_SAS_DISCOVERY:
	case MPI_EVENT_SAS_PHY_LINK_STATUS:
	case MPI_EVENT_SAS_SES:
		break;
	default:
		mpt_lprt(mpt, MPT_PRT_WARN, "mpt_cam_event: 0x%x\n",
		    msg->Event & 0xFF);
		return (0);
	}
	return (1);
}

/*
 * Reply path for all SCSI I/O requests, called from our
 * interrupt handler by extracting our handler index from
 * the MsgContext field of the reply from the IOC.
 *
 * This routine is optimized for the common case of a
 * completion without error.  All exception handling is
 * offloaded to non-inlined helper routines to minimize
 * cache footprint.
 */
static int
mpt_scsi_reply_handler(struct mpt_softc *mpt, request_t *req,
    uint32_t reply_desc, MSG_DEFAULT_REPLY *reply_frame)
{
	MSG_SCSI_IO_REQUEST *scsi_req;
	union ccb *ccb;

	if (req->state == REQ_STATE_FREE) {
		mpt_prt(mpt, "mpt_scsi_reply_handler: req already free\n");
		return (TRUE);
	}

	scsi_req = (MSG_SCSI_IO_REQUEST *)req->req_vbuf;
	ccb = req->ccb;
	if (ccb == NULL) {
		mpt_prt(mpt, "mpt_scsi_reply_handler: req %p:%u with no ccb\n",
		    req, req->serno);
		return (TRUE);
	}

	mpt_req_untimeout(req, mpt_timeout, ccb);
	ccb->ccb_h.status &= ~CAM_SIM_QUEUED;

	if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		bus_dmasync_op_t op;

		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN)
			op = BUS_DMASYNC_POSTREAD;
		else
			op = BUS_DMASYNC_POSTWRITE;
		bus_dmamap_sync(mpt->buffer_dmat, req->dmap, op);
		bus_dmamap_unload(mpt->buffer_dmat, req->dmap);
	}

	if (reply_frame == NULL) {
		/*
		 * Context only reply, completion without error status.
		 */
		ccb->csio.resid = 0;
		mpt_set_ccb_status(ccb, CAM_REQ_CMP);
		ccb->csio.scsi_status = SCSI_STATUS_OK;
	} else {
		mpt_scsi_reply_frame_handler(mpt, req, reply_frame);
	}

	if (mpt->outofbeer) {
		ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
		mpt->outofbeer = 0;
		mpt_lprt(mpt, MPT_PRT_DEBUG, "THAWQ\n");
	}
	if (scsi_req->CDB[0] == INQUIRY && (scsi_req->CDB[1] & SI_EVPD) == 0) {
		struct scsi_inquiry_data *iq = 
		    (struct scsi_inquiry_data *)ccb->csio.data_ptr;
		if (scsi_req->Function ==
		    MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH) {
			/*
			 * Fake out the device type so that only the
			 * pass-thru device will attach.
			 */
			iq->device &= ~0x1F;
			iq->device |= T_NODEVICE;
		}
	}
	if (mpt->verbose == MPT_PRT_DEBUG) {
		mpt_prt(mpt, "mpt_scsi_reply_handler: %p:%u complete\n",
		    req, req->serno);
	}
	KASSERT(ccb->ccb_h.status, ("zero ccb sts at %d", __LINE__));
	xpt_done(ccb);
	if ((req->state & REQ_STATE_TIMEDOUT) == 0) {
		TAILQ_REMOVE(&mpt->request_pending_list, req, links);
	} else {
		mpt_prt(mpt, "completing timedout/aborted req %p:%u\n",
		    req, req->serno);
		TAILQ_REMOVE(&mpt->request_timeout_list, req, links);
	}
	KASSERT((req->state & REQ_STATE_NEED_WAKEUP) == 0,
	    ("CCB req needed wakeup"));
#ifdef	INVARIANTS
	mpt_req_not_spcl(mpt, req, "mpt_scsi_reply_handler", __LINE__);
#endif
	mpt_free_request(mpt, req);
	return (TRUE);
}

static int
mpt_scsi_tmf_reply_handler(struct mpt_softc *mpt, request_t *req,
    uint32_t reply_desc, MSG_DEFAULT_REPLY *reply_frame)
{
	MSG_SCSI_TASK_MGMT_REPLY *tmf_reply;

	KASSERT(req == mpt->tmf_req, ("TMF Reply not using mpt->tmf_req"));
#ifdef	INVARIANTS
	mpt_req_not_spcl(mpt, req, "mpt_scsi_tmf_reply_handler", __LINE__);
#endif
	tmf_reply = (MSG_SCSI_TASK_MGMT_REPLY *)reply_frame;
	/* Record IOC Status and Response Code of TMF for any waiters. */
	req->IOCStatus = le16toh(tmf_reply->IOCStatus);
	req->ResponseCode = tmf_reply->ResponseCode;

	mpt_lprt(mpt, MPT_PRT_DEBUG, "TMF complete: req %p:%u status 0x%x\n",
	    req, req->serno, le16toh(tmf_reply->IOCStatus));
	TAILQ_REMOVE(&mpt->request_pending_list, req, links);
	if ((req->state & REQ_STATE_NEED_WAKEUP) != 0) {
		req->state |= REQ_STATE_DONE;
		wakeup(req);
	} else {
		mpt->tmf_req->state = REQ_STATE_FREE;
	}
	return (TRUE);
}

/*
 * XXX: Move to definitions file
 */
#define	ELS	0x22
#define	FC4LS	0x32
#define	ABTS	0x81
#define	BA_ACC	0x84

#define	LS_RJT	0x01 
#define	LS_ACC	0x02
#define	PLOGI	0x03
#define	LOGO	0x05
#define SRR	0x14
#define PRLI	0x20
#define PRLO	0x21
#define ADISC	0x52
#define RSCN	0x61

static void
mpt_fc_els_send_response(struct mpt_softc *mpt, request_t *req,
    PTR_MSG_LINK_SERVICE_BUFFER_POST_REPLY rp, U8 length)
{
	uint32_t fl;
	MSG_LINK_SERVICE_RSP_REQUEST tmp;
	PTR_MSG_LINK_SERVICE_RSP_REQUEST rsp;

	/*
	 * We are going to reuse the ELS request to send this response back.
	 */
	rsp = &tmp;
	memset(rsp, 0, sizeof(*rsp));

#ifdef	USE_IMMEDIATE_LINK_DATA
	/*
	 * Apparently the IMMEDIATE stuff doesn't seem to work.
	 */
	rsp->RspFlags = LINK_SERVICE_RSP_FLAGS_IMMEDIATE;
#endif
	rsp->RspLength = length;
	rsp->Function = MPI_FUNCTION_FC_LINK_SRVC_RSP;
	rsp->MsgContext = htole32(req->index | fc_els_handler_id);

	/*
	 * Copy over information from the original reply frame to
	 * it's correct place in the response.
	 */
	memcpy((U8 *)rsp + 0x0c, (U8 *)rp + 0x1c, 24);

	/*
	 * And now copy back the temporary area to the original frame.
	 */
	memcpy(req->req_vbuf, rsp, sizeof (MSG_LINK_SERVICE_RSP_REQUEST));
	rsp = req->req_vbuf;

#ifdef	USE_IMMEDIATE_LINK_DATA
	memcpy((U8 *)&rsp->SGL, &((U8 *)req->req_vbuf)[MPT_RQSL(mpt)], length);
#else
{
	PTR_SGE_SIMPLE32 se = (PTR_SGE_SIMPLE32) &rsp->SGL;
	bus_addr_t paddr = req->req_pbuf;
	paddr += MPT_RQSL(mpt);

	fl =
		MPI_SGE_FLAGS_HOST_TO_IOC	|
		MPI_SGE_FLAGS_SIMPLE_ELEMENT	|
		MPI_SGE_FLAGS_LAST_ELEMENT	|
		MPI_SGE_FLAGS_END_OF_LIST	|
		MPI_SGE_FLAGS_END_OF_BUFFER;
	fl <<= MPI_SGE_FLAGS_SHIFT;
	fl |= (length);
	se->FlagsLength = htole32(fl);
	se->Address = htole32((uint32_t) paddr);
}
#endif

	/*
	 * Send it on...
	 */
	mpt_send_cmd(mpt, req);
}

static int
mpt_fc_els_reply_handler(struct mpt_softc *mpt, request_t *req,
    uint32_t reply_desc, MSG_DEFAULT_REPLY *reply_frame)
{
	PTR_MSG_LINK_SERVICE_BUFFER_POST_REPLY rp =
	    (PTR_MSG_LINK_SERVICE_BUFFER_POST_REPLY) reply_frame;
	U8 rctl;
	U8 type;
	U8 cmd;
	U16 status = le16toh(reply_frame->IOCStatus);
	U32 *elsbuf;
	int ioindex;
	int do_refresh = TRUE;

#ifdef	INVARIANTS
	KASSERT(mpt_req_on_free_list(mpt, req) == 0,
	    ("fc_els_reply_handler: req %p:%u for function %x on freelist!",
	    req, req->serno, rp->Function));
	if (rp->Function != MPI_FUNCTION_FC_PRIMITIVE_SEND) {
		mpt_req_spcl(mpt, req, "fc_els_reply_handler", __LINE__);
	} else {
		mpt_req_not_spcl(mpt, req, "fc_els_reply_handler", __LINE__);
	}
#endif
	mpt_lprt(mpt, MPT_PRT_DEBUG,
	    "FC_ELS Complete: req %p:%u, reply %p function %x\n",
	    req, req->serno, reply_frame, reply_frame->Function);

	if  (status != MPI_IOCSTATUS_SUCCESS) {
		mpt_prt(mpt, "ELS REPLY STATUS 0x%x for Function %x\n",
		    status, reply_frame->Function);
		if (status == MPI_IOCSTATUS_INVALID_STATE) {
			/*
			 * XXX: to get around shutdown issue
			 */
			mpt->disabled = 1;
			return (TRUE);
		}
		return (TRUE);
	}

	/*
	 * If the function of a link service response, we recycle the
	 * response to be a refresh for a new link service request.
	 *
	 * The request pointer is bogus in this case and we have to fetch
	 * it based upon the TransactionContext.
	 */
	if (rp->Function == MPI_FUNCTION_FC_LINK_SRVC_RSP) {
		/* Freddie Uncle Charlie Katie */
		/* We don't get the IOINDEX as part of the Link Svc Rsp */
		for (ioindex = 0; ioindex < mpt->els_cmds_allocated; ioindex++)
			if (mpt->els_cmd_ptrs[ioindex] == req) {
				break;
			}

		KASSERT(ioindex < mpt->els_cmds_allocated,
		    ("can't find my mommie!"));

		/* remove from active list as we're going to re-post it */
		TAILQ_REMOVE(&mpt->request_pending_list, req, links);
		req->state &= ~REQ_STATE_QUEUED;
		req->state |= REQ_STATE_DONE;
		mpt_fc_post_els(mpt, req, ioindex);
		return (TRUE);
	}

	if (rp->Function == MPI_FUNCTION_FC_PRIMITIVE_SEND) {
		/* remove from active list as we're done */
		TAILQ_REMOVE(&mpt->request_pending_list, req, links);
		req->state &= ~REQ_STATE_QUEUED;
		req->state |= REQ_STATE_DONE;
		if (req->state & REQ_STATE_TIMEDOUT) {
			mpt_lprt(mpt, MPT_PRT_DEBUG,
			    "Sync Primitive Send Completed After Timeout\n");
			mpt_free_request(mpt, req);
		} else if ((req->state & REQ_STATE_NEED_WAKEUP) == 0) {
			mpt_lprt(mpt, MPT_PRT_DEBUG,
			    "Async Primitive Send Complete\n");
			mpt_free_request(mpt, req);
		} else {
			mpt_lprt(mpt, MPT_PRT_DEBUG,
			    "Sync Primitive Send Complete- Waking Waiter\n");
			wakeup(req);
		}
		return (TRUE);
	}

	if (rp->Function != MPI_FUNCTION_FC_LINK_SRVC_BUF_POST) {
		mpt_prt(mpt, "unexpected ELS_REPLY: Function 0x%x Flags %x "
		    "Length %d Message Flags %x\n", rp->Function, rp->Flags,
		    rp->MsgLength, rp->MsgFlags);
		return (TRUE);
	}

	if (rp->MsgLength <= 5) {
		/*
		 * This is just a ack of an original ELS buffer post
		 */
		mpt_lprt(mpt, MPT_PRT_DEBUG,
		    "RECV'd ACK of FC_ELS buf post %p:%u\n", req, req->serno);
		return (TRUE);
	}


	rctl = (le32toh(rp->Rctl_Did) & MPI_FC_RCTL_MASK) >> MPI_FC_RCTL_SHIFT;
	type = (le32toh(rp->Type_Fctl) & MPI_FC_TYPE_MASK) >> MPI_FC_TYPE_SHIFT;

	elsbuf = &((U32 *)req->req_vbuf)[MPT_RQSL(mpt)/sizeof (U32)];
	cmd = be32toh(elsbuf[0]) >> 24;

	if (rp->Flags & MPI_LS_BUF_POST_REPLY_FLAG_NO_RSP_NEEDED) {
		mpt_lprt(mpt, MPT_PRT_ALWAYS, "ELS_REPLY: response unneeded\n");
		return (TRUE);
	}

	ioindex = le32toh(rp->TransactionContext);
	req = mpt->els_cmd_ptrs[ioindex];

	if (rctl == ELS && type == 1) {
		switch (cmd) {
		case PRLI:
			/*
			 * Send back a PRLI ACC
			 */
			mpt_prt(mpt, "PRLI from 0x%08x%08x\n",
			    le32toh(rp->Wwn.PortNameHigh),
			    le32toh(rp->Wwn.PortNameLow));
			elsbuf[0] = htobe32(0x02100014);
			elsbuf[1] |= htobe32(0x00000100);
			elsbuf[4] = htobe32(0x00000002);
			if (mpt->role & MPT_ROLE_TARGET)
				elsbuf[4] |= htobe32(0x00000010);
			if (mpt->role & MPT_ROLE_INITIATOR)
				elsbuf[4] |= htobe32(0x00000020);
			/* remove from active list as we're done */
			TAILQ_REMOVE(&mpt->request_pending_list, req, links);
			req->state &= ~REQ_STATE_QUEUED;
			req->state |= REQ_STATE_DONE;
			mpt_fc_els_send_response(mpt, req, rp, 20);
			do_refresh = FALSE;
			break;
		case PRLO:
			memset(elsbuf, 0, 5 * (sizeof (U32)));
			elsbuf[0] = htobe32(0x02100014);
			elsbuf[1] = htobe32(0x08000100);
			mpt_prt(mpt, "PRLO from 0x%08x%08x\n",
			    le32toh(rp->Wwn.PortNameHigh),
			    le32toh(rp->Wwn.PortNameLow));
			/* remove from active list as we're done */
			TAILQ_REMOVE(&mpt->request_pending_list, req, links);
			req->state &= ~REQ_STATE_QUEUED;
			req->state |= REQ_STATE_DONE;
			mpt_fc_els_send_response(mpt, req, rp, 20);
			do_refresh = FALSE;
			break;
		default:
			mpt_prt(mpt, "ELS TYPE 1 COMMAND: %x\n", cmd);
			break;
		}
	} else if (rctl == ABTS && type == 0) {
		uint16_t rx_id = le16toh(rp->Rxid);
		uint16_t ox_id = le16toh(rp->Oxid);
		mpt_tgt_state_t *tgt;
		request_t *tgt_req = NULL;
		union ccb *ccb;
		uint32_t ct_id;

		mpt_prt(mpt,
		    "ELS: ABTS OX_ID 0x%x RX_ID 0x%x from 0x%08x%08x\n",
		    ox_id, rx_id, le32toh(rp->Wwn.PortNameHigh),
		    le32toh(rp->Wwn.PortNameLow));
		if (rx_id >= mpt->mpt_max_tgtcmds) {
			mpt_prt(mpt, "Bad RX_ID 0x%x\n", rx_id);
		} else if (mpt->tgt_cmd_ptrs == NULL) {
			mpt_prt(mpt, "No TGT CMD PTRS\n");
		} else {
			tgt_req = mpt->tgt_cmd_ptrs[rx_id];
		}
		if (tgt_req == NULL) {
			mpt_prt(mpt, "no back pointer for RX_ID 0x%x\n", rx_id);
			goto skip;
		}
		tgt = MPT_TGT_STATE(mpt, tgt_req);

		/* Check to make sure we have the correct command. */
		ct_id = GET_IO_INDEX(tgt->reply_desc);
		if (ct_id != rx_id) {
			mpt_lprt(mpt, MPT_PRT_ERROR, "ABORT Mismatch: "
			    "RX_ID received=0x%x, in cmd=0x%x\n", rx_id, ct_id);
			goto skip;
		}
		if (tgt->itag != ox_id) {
			mpt_lprt(mpt, MPT_PRT_ERROR, "ABORT Mismatch: "
			    "OX_ID received=0x%x, in cmd=0x%x\n", ox_id, tgt->itag);
			goto skip;
		}

		if ((ccb = tgt->ccb) != NULL) {
			mpt_prt(mpt, "CCB (%p): lun %jx flags %x status %x\n",
			    ccb, (uintmax_t)ccb->ccb_h.target_lun,
			    ccb->ccb_h.flags, ccb->ccb_h.status);
		}
		mpt_prt(mpt, "target state 0x%x resid %u xfrd %u rpwrd "
		    "%x nxfers %x\n", tgt->state, tgt->resid,
		    tgt->bytes_xfered, tgt->reply_desc, tgt->nxfers);
		if (mpt_abort_target_cmd(mpt, tgt_req))
			mpt_prt(mpt, "unable to start TargetAbort\n");

skip:
		memset(elsbuf, 0, 5 * (sizeof (U32)));
		elsbuf[0] = htobe32(0);
		elsbuf[1] = htobe32((ox_id << 16) | rx_id);
		elsbuf[2] = htobe32(0x000ffff);
		/*
		 * Dork with the reply frame so that the response to it
		 * will be correct.
		 */
		rp->Rctl_Did += ((BA_ACC - ABTS) << MPI_FC_RCTL_SHIFT);
		/* remove from active list as we're done */
		TAILQ_REMOVE(&mpt->request_pending_list, req, links);
		req->state &= ~REQ_STATE_QUEUED;
		req->state |= REQ_STATE_DONE;
		mpt_fc_els_send_response(mpt, req, rp, 12);
		do_refresh = FALSE;
	} else {
		mpt_prt(mpt, "ELS: RCTL %x TYPE %x CMD %x\n", rctl, type, cmd);
	}
	if (do_refresh == TRUE) {
		/* remove from active list as we're done */
		TAILQ_REMOVE(&mpt->request_pending_list, req, links);
		req->state &= ~REQ_STATE_QUEUED;
		req->state |= REQ_STATE_DONE;
		mpt_fc_post_els(mpt, req, ioindex);
	}
	return (TRUE);
}

/*
 * Clean up all SCSI Initiator personality state in response
 * to a controller reset.
 */
static void
mpt_cam_ioc_reset(struct mpt_softc *mpt, int type)
{

	/*
	 * The pending list is already run down by
	 * the generic handler.  Perform the same
	 * operation on the timed out request list.
	 */
	mpt_complete_request_chain(mpt, &mpt->request_timeout_list,
				   MPI_IOCSTATUS_INVALID_STATE);

	/*
	 * XXX: We need to repost ELS and Target Command Buffers?
	 */

	/*
	 * Inform the XPT that a bus reset has occurred.
	 */
	xpt_async(AC_BUS_RESET, mpt->path, NULL);
}

/*
 * Parse additional completion information in the reply
 * frame for SCSI I/O requests.
 */
static int
mpt_scsi_reply_frame_handler(struct mpt_softc *mpt, request_t *req,
			     MSG_DEFAULT_REPLY *reply_frame)
{
	union ccb *ccb;
	MSG_SCSI_IO_REPLY *scsi_io_reply;
	u_int ioc_status;
	u_int sstate;

	MPT_DUMP_REPLY_FRAME(mpt, reply_frame);
	KASSERT(reply_frame->Function == MPI_FUNCTION_SCSI_IO_REQUEST
	     || reply_frame->Function == MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH,
		("MPT SCSI I/O Handler called with incorrect reply type"));
	KASSERT((reply_frame->MsgFlags & MPI_MSGFLAGS_CONTINUATION_REPLY) == 0,
		("MPT SCSI I/O Handler called with continuation reply"));

	scsi_io_reply = (MSG_SCSI_IO_REPLY *)reply_frame;
	ioc_status = le16toh(scsi_io_reply->IOCStatus);
	ioc_status &= MPI_IOCSTATUS_MASK;
	sstate = scsi_io_reply->SCSIState;

	ccb = req->ccb;
	ccb->csio.resid =
	    ccb->csio.dxfer_len - le32toh(scsi_io_reply->TransferCount);

	if ((sstate & MPI_SCSI_STATE_AUTOSENSE_VALID) != 0
	 && (ccb->ccb_h.flags & (CAM_SENSE_PHYS | CAM_SENSE_PTR)) == 0) {
		uint32_t sense_returned;

		ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
		
		sense_returned = le32toh(scsi_io_reply->SenseCount);
		if (sense_returned < ccb->csio.sense_len)
			ccb->csio.sense_resid = ccb->csio.sense_len -
						sense_returned;
		else
			ccb->csio.sense_resid = 0;

		bzero(&ccb->csio.sense_data, sizeof(ccb->csio.sense_data));
		bcopy(req->sense_vbuf, &ccb->csio.sense_data,
		    min(ccb->csio.sense_len, sense_returned));
	}

	if ((sstate & MPI_SCSI_STATE_QUEUE_TAG_REJECTED) != 0) {
		/*
		 * Tag messages rejected, but non-tagged retry
		 * was successful.
XXXX
		mpt_set_tags(mpt, devinfo, MPT_QUEUE_NONE);
		 */
	}

	switch(ioc_status) {
	case MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:
		/*
		 * XXX
		 * Linux driver indicates that a zero
		 * transfer length with this error code
		 * indicates a CRC error.
		 *
		 * No need to swap the bytes for checking
		 * against zero.
		 */
		if (scsi_io_reply->TransferCount == 0) {
			mpt_set_ccb_status(ccb, CAM_UNCOR_PARITY);
			break;
		}
		/* FALLTHROUGH */
	case MPI_IOCSTATUS_SCSI_DATA_UNDERRUN:
	case MPI_IOCSTATUS_SUCCESS:
	case MPI_IOCSTATUS_SCSI_RECOVERED_ERROR:
		if ((sstate & MPI_SCSI_STATE_NO_SCSI_STATUS) != 0) {
			/*
			 * Status was never returned for this transaction.
			 */
			mpt_set_ccb_status(ccb, CAM_UNEXP_BUSFREE);
		} else if (scsi_io_reply->SCSIStatus != SCSI_STATUS_OK) {
			ccb->csio.scsi_status = scsi_io_reply->SCSIStatus;
			mpt_set_ccb_status(ccb, CAM_SCSI_STATUS_ERROR);
			if ((sstate & MPI_SCSI_STATE_AUTOSENSE_FAILED) != 0)
				mpt_set_ccb_status(ccb, CAM_AUTOSENSE_FAIL);
		} else if ((sstate & MPI_SCSI_STATE_RESPONSE_INFO_VALID) != 0) {

			/* XXX Handle SPI-Packet and FCP-2 response info. */
			mpt_set_ccb_status(ccb, CAM_REQ_CMP_ERR);
		} else
			mpt_set_ccb_status(ccb, CAM_REQ_CMP);
		break;
	case MPI_IOCSTATUS_SCSI_DATA_OVERRUN:
		mpt_set_ccb_status(ccb, CAM_DATA_RUN_ERR);
		break;
	case MPI_IOCSTATUS_SCSI_IO_DATA_ERROR:
		mpt_set_ccb_status(ccb, CAM_UNCOR_PARITY);
		break;
	case MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
		/*
		 * Since selection timeouts and "device really not
		 * there" are grouped into this error code, report
		 * selection timeout.  Selection timeouts are
		 * typically retried before giving up on the device
		 * whereas "device not there" errors are considered
		 * unretryable.
		 */
		mpt_set_ccb_status(ccb, CAM_SEL_TIMEOUT);
		break;
	case MPI_IOCSTATUS_SCSI_PROTOCOL_ERROR:
		mpt_set_ccb_status(ccb, CAM_SEQUENCE_FAIL);
		break;
	case MPI_IOCSTATUS_SCSI_INVALID_BUS:
		mpt_set_ccb_status(ccb, CAM_PATH_INVALID);
		break;
	case MPI_IOCSTATUS_SCSI_INVALID_TARGETID:
		mpt_set_ccb_status(ccb, CAM_TID_INVALID);
		break;
	case MPI_IOCSTATUS_SCSI_TASK_MGMT_FAILED:
		ccb->ccb_h.status = CAM_UA_TERMIO;
		break;
	case MPI_IOCSTATUS_INVALID_STATE:
		/*
		 * The IOC has been reset.  Emulate a bus reset.
		 */
		/* FALLTHROUGH */
	case MPI_IOCSTATUS_SCSI_EXT_TERMINATED:
		ccb->ccb_h.status = CAM_SCSI_BUS_RESET; 
		break;
	case MPI_IOCSTATUS_SCSI_TASK_TERMINATED:
	case MPI_IOCSTATUS_SCSI_IOC_TERMINATED:
		/*
		 * Don't clobber any timeout status that has
		 * already been set for this transaction.  We
		 * want the SCSI layer to be able to differentiate
		 * between the command we aborted due to timeout
		 * and any innocent bystanders.
		 */
		if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_INPROG)
			break;
		mpt_set_ccb_status(ccb, CAM_REQ_TERMIO);
		break;

	case MPI_IOCSTATUS_INSUFFICIENT_RESOURCES:
		mpt_set_ccb_status(ccb, CAM_RESRC_UNAVAIL);
		break;
	case MPI_IOCSTATUS_BUSY:
		mpt_set_ccb_status(ccb, CAM_BUSY);
		break;
	case MPI_IOCSTATUS_INVALID_FUNCTION:
	case MPI_IOCSTATUS_INVALID_SGL:
	case MPI_IOCSTATUS_INTERNAL_ERROR:
	case MPI_IOCSTATUS_INVALID_FIELD:
	default:
		/* XXX
		 * Some of the above may need to kick
		 * of a recovery action!!!!
		 */
		ccb->ccb_h.status = CAM_UNREC_HBA_ERROR;
		break;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		mpt_freeze_ccb(ccb);
	}

	return (TRUE);
}

static void
mpt_action(struct cam_sim *sim, union ccb *ccb)
{
	struct mpt_softc *mpt;
	struct ccb_trans_settings *cts;
	target_id_t tgt;
	lun_id_t lun;
	int raid_passthru;

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE, ("mpt_action\n"));

	mpt = (struct mpt_softc *)cam_sim_softc(sim);
	raid_passthru = (sim == mpt->phydisk_sim);
	MPT_LOCK_ASSERT(mpt);

	tgt = ccb->ccb_h.target_id;
	lun = ccb->ccb_h.target_lun;
	if (raid_passthru &&
	    ccb->ccb_h.func_code != XPT_PATH_INQ &&
	    ccb->ccb_h.func_code != XPT_RESET_BUS &&
	    ccb->ccb_h.func_code != XPT_RESET_DEV) {
		if (mpt_map_physdisk(mpt, ccb, &tgt) != 0) {
			ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
			mpt_set_ccb_status(ccb, CAM_DEV_NOT_THERE);
			xpt_done(ccb);
			return;
		}
	}
	ccb->ccb_h.ccb_mpt_ptr = mpt;

	switch (ccb->ccb_h.func_code) {
	case XPT_SCSI_IO:	/* Execute the requested I/O operation */
		/*
		 * Do a couple of preliminary checks...
		 */
		if ((ccb->ccb_h.flags & CAM_CDB_POINTER) != 0) {
			if ((ccb->ccb_h.flags & CAM_CDB_PHYS) != 0) {
				ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
				mpt_set_ccb_status(ccb, CAM_REQ_INVALID);
				break;
			}
		}
		/* Max supported CDB length is 16 bytes */
		/* XXX Unless we implement the new 32byte message type */
		if (ccb->csio.cdb_len >
		    sizeof (((PTR_MSG_SCSI_IO_REQUEST)0)->CDB)) {
			ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
			mpt_set_ccb_status(ccb, CAM_REQ_INVALID);
			break;
		}
#ifdef	MPT_TEST_MULTIPATH
		if (mpt->failure_id == ccb->ccb_h.target_id) {
			ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
			mpt_set_ccb_status(ccb, CAM_SEL_TIMEOUT);
			break;
		}
#endif
		ccb->csio.scsi_status = SCSI_STATUS_OK;
		mpt_start(sim, ccb);
		return;

	case XPT_RESET_BUS:
		if (raid_passthru) {
			ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
			mpt_set_ccb_status(ccb, CAM_REQ_CMP);
			break;
		}
	case XPT_RESET_DEV:
		if (ccb->ccb_h.func_code == XPT_RESET_BUS) {
			if (bootverbose) {
				xpt_print(ccb->ccb_h.path, "reset bus\n");
			}
		} else {
			xpt_print(ccb->ccb_h.path, "reset device\n");
		}
		(void) mpt_bus_reset(mpt, tgt, lun, FALSE);

		/*
		 * mpt_bus_reset is always successful in that it
		 * will fall back to a hard reset should a bus
		 * reset attempt fail.
		 */
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		mpt_set_ccb_status(ccb, CAM_REQ_CMP);
		break;
		
	case XPT_ABORT:
	{
		union ccb *accb = ccb->cab.abort_ccb;
		switch (accb->ccb_h.func_code) {
		case XPT_ACCEPT_TARGET_IO:
		case XPT_IMMEDIATE_NOTIFY:
			ccb->ccb_h.status = mpt_abort_target_ccb(mpt, ccb);
			break;
		case XPT_CONT_TARGET_IO:
			mpt_prt(mpt, "cannot abort active CTIOs yet\n");
			ccb->ccb_h.status = CAM_UA_ABORT;
			break;
		case XPT_SCSI_IO:
			ccb->ccb_h.status = CAM_UA_ABORT;
			break;
		default:
			ccb->ccb_h.status = CAM_REQ_INVALID;
			break;
		}
		break;
	}

#define	IS_CURRENT_SETTINGS(c)	((c)->type == CTS_TYPE_CURRENT_SETTINGS)

#define	DP_DISC_ENABLE	0x1
#define	DP_DISC_DISABL	0x2
#define	DP_DISC		(DP_DISC_ENABLE|DP_DISC_DISABL)

#define	DP_TQING_ENABLE	0x4
#define	DP_TQING_DISABL	0x8
#define	DP_TQING	(DP_TQING_ENABLE|DP_TQING_DISABL)

#define	DP_WIDE		0x10
#define	DP_NARROW	0x20
#define	DP_WIDTH	(DP_WIDE|DP_NARROW)

#define	DP_SYNC		0x40

	case XPT_SET_TRAN_SETTINGS:	/* Nexus Settings */
	{
		struct ccb_trans_settings_scsi *scsi;
		struct ccb_trans_settings_spi *spi;
		uint8_t dval;
		u_int period;
		u_int offset;
		int i, j;

		cts = &ccb->cts;

		if (mpt->is_fc || mpt->is_sas) {
			mpt_set_ccb_status(ccb, CAM_REQ_CMP);
			break;
		}

		scsi = &cts->proto_specific.scsi;
		spi = &cts->xport_specific.spi;

		/*
		 * We can be called just to valid transport and proto versions
		 */
		if (scsi->valid == 0 && spi->valid == 0) {
			mpt_set_ccb_status(ccb, CAM_REQ_CMP);
			break;
		}

		/*
		 * Skip attempting settings on RAID volume disks.
		 * Other devices on the bus get the normal treatment.
		 */
		if (mpt->phydisk_sim && raid_passthru == 0 &&
		    mpt_is_raid_volume(mpt, tgt) != 0) {
			mpt_lprt(mpt, MPT_PRT_NEGOTIATION,
			    "no transfer settings for RAID vols\n");
			mpt_set_ccb_status(ccb, CAM_REQ_CMP);
			break;
		}

		i = mpt->mpt_port_page2.PortSettings &
		    MPI_SCSIPORTPAGE2_PORT_MASK_NEGO_MASTER_SETTINGS;
		j = mpt->mpt_port_page2.PortFlags &
		    MPI_SCSIPORTPAGE2_PORT_FLAGS_DV_MASK;
		if (i == MPI_SCSIPORTPAGE2_PORT_ALL_MASTER_SETTINGS &&
		    j == MPI_SCSIPORTPAGE2_PORT_FLAGS_OFF_DV) {
			mpt_lprt(mpt, MPT_PRT_ALWAYS,
			    "honoring BIOS transfer negotiations\n");
			mpt_set_ccb_status(ccb, CAM_REQ_CMP);
			break;
		}

		dval = 0;
		period = 0;
		offset = 0;

		if ((spi->valid & CTS_SPI_VALID_DISC) != 0) {
			dval |= ((spi->flags & CTS_SPI_FLAGS_DISC_ENB) != 0) ?
			    DP_DISC_ENABLE : DP_DISC_DISABL;
		}

		if ((scsi->valid & CTS_SCSI_VALID_TQ) != 0) {
			dval |= ((scsi->flags & CTS_SCSI_FLAGS_TAG_ENB) != 0) ?
			    DP_TQING_ENABLE : DP_TQING_DISABL;
		}

		if ((spi->valid & CTS_SPI_VALID_BUS_WIDTH) != 0) {
			dval |= (spi->bus_width == MSG_EXT_WDTR_BUS_16_BIT) ?
			    DP_WIDE : DP_NARROW;
		}

		if (spi->valid & CTS_SPI_VALID_SYNC_OFFSET) {
			dval |= DP_SYNC;
			offset = spi->sync_offset;
		} else {
			PTR_CONFIG_PAGE_SCSI_DEVICE_1 ptr =
			    &mpt->mpt_dev_page1[tgt];
			offset = ptr->RequestedParameters;
			offset &= MPI_SCSIDEVPAGE1_RP_MAX_SYNC_OFFSET_MASK;
	    		offset >>= MPI_SCSIDEVPAGE1_RP_SHIFT_MAX_SYNC_OFFSET;
		}
		if (spi->valid & CTS_SPI_VALID_SYNC_RATE) {
			dval |= DP_SYNC;
			period = spi->sync_period;
		} else {
			PTR_CONFIG_PAGE_SCSI_DEVICE_1 ptr =
			    &mpt->mpt_dev_page1[tgt];
			period = ptr->RequestedParameters;
			period &= MPI_SCSIDEVPAGE1_RP_MIN_SYNC_PERIOD_MASK;
	    		period >>= MPI_SCSIDEVPAGE1_RP_SHIFT_MIN_SYNC_PERIOD;
		}

		if (dval & DP_DISC_ENABLE) {
			mpt->mpt_disc_enable |= (1 << tgt);
		} else if (dval & DP_DISC_DISABL) {
			mpt->mpt_disc_enable &= ~(1 << tgt);
		}
		if (dval & DP_TQING_ENABLE) {
			mpt->mpt_tag_enable |= (1 << tgt);
		} else if (dval & DP_TQING_DISABL) {
			mpt->mpt_tag_enable &= ~(1 << tgt);
		}
		if (dval & DP_WIDTH) {
			mpt_setwidth(mpt, tgt, 1);
		}
		if (dval & DP_SYNC) {
			mpt_setsync(mpt, tgt, period, offset);
		}
		if (dval == 0) {
			mpt_set_ccb_status(ccb, CAM_REQ_CMP);
			break;
		}
		mpt_lprt(mpt, MPT_PRT_NEGOTIATION,
		    "set [%d]: 0x%x period 0x%x offset %d\n",
		    tgt, dval, period, offset);
		if (mpt_update_spi_config(mpt, tgt)) {
			mpt_set_ccb_status(ccb, CAM_REQ_CMP_ERR);
		} else {
			mpt_set_ccb_status(ccb, CAM_REQ_CMP);
		}
		break;
	}
	case XPT_GET_TRAN_SETTINGS:
	{
		struct ccb_trans_settings_scsi *scsi;
		cts = &ccb->cts;
		cts->protocol = PROTO_SCSI;
		if (mpt->is_fc) {
			struct ccb_trans_settings_fc *fc =
			    &cts->xport_specific.fc;
			cts->protocol_version = SCSI_REV_SPC;
			cts->transport = XPORT_FC;
			cts->transport_version = 0;
			if (mpt->mpt_fcport_speed != 0) {
				fc->valid = CTS_FC_VALID_SPEED;
				fc->bitrate = 100000 * mpt->mpt_fcport_speed;
			}
		} else if (mpt->is_sas) {
			struct ccb_trans_settings_sas *sas =
			    &cts->xport_specific.sas;
			cts->protocol_version = SCSI_REV_SPC2;
			cts->transport = XPORT_SAS;
			cts->transport_version = 0;
			sas->valid = CTS_SAS_VALID_SPEED;
			sas->bitrate = 300000;
		} else {
			cts->protocol_version = SCSI_REV_2;
			cts->transport = XPORT_SPI;
			cts->transport_version = 2;
			if (mpt_get_spi_settings(mpt, cts) != 0) {
				mpt_set_ccb_status(ccb, CAM_REQ_CMP_ERR);
				break;
			}
		}
		scsi = &cts->proto_specific.scsi;
		scsi->valid = CTS_SCSI_VALID_TQ;
		scsi->flags = CTS_SCSI_FLAGS_TAG_ENB;
		mpt_set_ccb_status(ccb, CAM_REQ_CMP);
		break;
	}
	case XPT_CALC_GEOMETRY:
	{
		struct ccb_calc_geometry *ccg;

		ccg = &ccb->ccg;
		if (ccg->block_size == 0) {
			ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
			mpt_set_ccb_status(ccb, CAM_REQ_INVALID);
			break;
		}
		cam_calc_geometry(ccg, /* extended */ 1);
		KASSERT(ccb->ccb_h.status, ("zero ccb sts at %d", __LINE__));
		break;
	}
	case XPT_GET_SIM_KNOB:
	{
		struct ccb_sim_knob *kp = &ccb->knob;

		if (mpt->is_fc) {
			kp->xport_specific.fc.wwnn = mpt->scinfo.fc.wwnn;
			kp->xport_specific.fc.wwpn = mpt->scinfo.fc.wwpn;
			switch (mpt->role) {
			case MPT_ROLE_NONE:
				kp->xport_specific.fc.role = KNOB_ROLE_NONE;
				break;
			case MPT_ROLE_INITIATOR:
				kp->xport_specific.fc.role = KNOB_ROLE_INITIATOR;
				break;
			case MPT_ROLE_TARGET:
				kp->xport_specific.fc.role = KNOB_ROLE_TARGET;
				break;
			case MPT_ROLE_BOTH:
				kp->xport_specific.fc.role = KNOB_ROLE_BOTH;
				break;
			}
			kp->xport_specific.fc.valid =
			    KNOB_VALID_ADDRESS | KNOB_VALID_ROLE;
			ccb->ccb_h.status = CAM_REQ_CMP;
		} else {
			ccb->ccb_h.status = CAM_REQ_INVALID;
		}
		xpt_done(ccb);
		break;
	}
	case XPT_PATH_INQ:		/* Path routing inquiry */
	{
		struct ccb_pathinq *cpi = &ccb->cpi;

		cpi->version_num = 1;
		cpi->target_sprt = 0;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = mpt->port_facts[0].MaxDevices - 1;
		cpi->maxio = (mpt->max_cam_seg_cnt - 1) * PAGE_SIZE;
		/*
		 * FC cards report MAX_DEVICES of 512, but
		 * the MSG_SCSI_IO_REQUEST target id field
		 * is only 8 bits. Until we fix the driver
		 * to support 'channels' for bus overflow,
		 * just limit it.
		 */
		if (cpi->max_target > 255) {
			cpi->max_target = 255;
		}

		/*
		 * VMware ESX reports > 16 devices and then dies when we probe.
		 */
		if (mpt->is_spi && cpi->max_target > 15) {
			cpi->max_target = 15;
		}
		if (mpt->is_spi)
			cpi->max_lun = 7;
		else
			cpi->max_lun = MPT_MAX_LUNS;
		cpi->initiator_id = mpt->mpt_ini_id;
		cpi->bus_id = cam_sim_bus(sim);

		/*
		 * The base speed is the speed of the underlying connection.
		 */
		cpi->protocol = PROTO_SCSI;
		if (mpt->is_fc) {
			cpi->hba_misc = PIM_NOBUSRESET | PIM_UNMAPPED |
			    PIM_EXTLUNS;
			cpi->base_transfer_speed = 100000;
			cpi->hba_inquiry = PI_TAG_ABLE;
			cpi->transport = XPORT_FC;
			cpi->transport_version = 0;
			cpi->protocol_version = SCSI_REV_SPC;
			cpi->xport_specific.fc.wwnn = mpt->scinfo.fc.wwnn;
			cpi->xport_specific.fc.wwpn = mpt->scinfo.fc.wwpn;
			cpi->xport_specific.fc.port = mpt->scinfo.fc.portid;
			cpi->xport_specific.fc.bitrate =
			    100000 * mpt->mpt_fcport_speed;
		} else if (mpt->is_sas) {
			cpi->hba_misc = PIM_NOBUSRESET | PIM_UNMAPPED |
			    PIM_EXTLUNS;
			cpi->base_transfer_speed = 300000;
			cpi->hba_inquiry = PI_TAG_ABLE;
			cpi->transport = XPORT_SAS;
			cpi->transport_version = 0;
			cpi->protocol_version = SCSI_REV_SPC2;
		} else {
			cpi->hba_misc = PIM_SEQSCAN | PIM_UNMAPPED |
			    PIM_EXTLUNS;
			cpi->base_transfer_speed = 3300;
			cpi->hba_inquiry = PI_SDTR_ABLE|PI_TAG_ABLE|PI_WIDE_16;
			cpi->transport = XPORT_SPI;
			cpi->transport_version = 2;
			cpi->protocol_version = SCSI_REV_2;
		}

		/*
		 * We give our fake RAID passhtru bus a width that is MaxVolumes
		 * wide and restrict it to one lun.
		 */
		if (raid_passthru) {
			cpi->max_target = mpt->ioc_page2->MaxPhysDisks - 1;
			cpi->initiator_id = cpi->max_target + 1;
			cpi->max_lun = 0;
		}

		if ((mpt->role & MPT_ROLE_INITIATOR) == 0) {
			cpi->hba_misc |= PIM_NOINITIATOR;
		}
		if (mpt->is_fc && (mpt->role & MPT_ROLE_TARGET)) {
			cpi->target_sprt =
			    PIT_PROCESSOR | PIT_DISCONNECT | PIT_TERM_IO;
		} else {
			cpi->target_sprt = 0;
		}
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "LSI", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_EN_LUN:		/* Enable LUN as a target */
	{
		int result;

		if (ccb->cel.enable)
			result = mpt_enable_lun(mpt,
			    ccb->ccb_h.target_id, ccb->ccb_h.target_lun);
		else
			result = mpt_disable_lun(mpt,
			    ccb->ccb_h.target_id, ccb->ccb_h.target_lun);
		if (result == 0) {
			mpt_set_ccb_status(ccb, CAM_REQ_CMP);
		} else {
			mpt_set_ccb_status(ccb, CAM_REQ_CMP_ERR);
		}
		break;
	}
	case XPT_IMMEDIATE_NOTIFY:	/* Add Immediate Notify Resource */
	case XPT_ACCEPT_TARGET_IO:	/* Add Accept Target IO Resource */
	{
		tgt_resource_t *trtp;
		lun_id_t lun = ccb->ccb_h.target_lun;
		ccb->ccb_h.sim_priv.entries[0].field = 0;
		ccb->ccb_h.sim_priv.entries[1].ptr = mpt;

		if (lun == CAM_LUN_WILDCARD) {
			if (ccb->ccb_h.target_id != CAM_TARGET_WILDCARD) {
				mpt_set_ccb_status(ccb, CAM_REQ_INVALID);
				break;
			}
			trtp = &mpt->trt_wildcard;
		} else if (lun >= MPT_MAX_LUNS) {
			mpt_set_ccb_status(ccb, CAM_REQ_INVALID);
			break;
		} else {
			trtp = &mpt->trt[lun];
		}
		if (ccb->ccb_h.func_code == XPT_ACCEPT_TARGET_IO) {
			mpt_lprt(mpt, MPT_PRT_DEBUG1,
			    "Put FREE ATIO %p lun %jx\n", ccb, (uintmax_t)lun);
			STAILQ_INSERT_TAIL(&trtp->atios, &ccb->ccb_h,
			    sim_links.stqe);
		} else {
			mpt_lprt(mpt, MPT_PRT_DEBUG1,
			    "Put FREE INOT lun %jx\n", (uintmax_t)lun);
			STAILQ_INSERT_TAIL(&trtp->inots, &ccb->ccb_h,
			    sim_links.stqe);
		}
		mpt_set_ccb_status(ccb, CAM_REQ_INPROG);
		return;
	}
	case XPT_NOTIFY_ACKNOWLEDGE:	/* Task management request done. */
	{
		request_t *req = MPT_TAG_2_REQ(mpt, ccb->cna2.tag_id);

		mpt_lprt(mpt, MPT_PRT_DEBUG, "Got Notify ACK\n");
		mpt_scsi_tgt_status(mpt, NULL, req, 0, NULL, 0);
		mpt_set_ccb_status(ccb, CAM_REQ_CMP);
		break;
	}
	case XPT_CONT_TARGET_IO:
		mpt_target_start_io(mpt, ccb);
		return;

	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}
	xpt_done(ccb);
}

static int
mpt_get_spi_settings(struct mpt_softc *mpt, struct ccb_trans_settings *cts)
{
	struct ccb_trans_settings_scsi *scsi = &cts->proto_specific.scsi;
	struct ccb_trans_settings_spi *spi = &cts->xport_specific.spi;
	target_id_t tgt;
	uint32_t dval, pval, oval;
	int rv;

	if (IS_CURRENT_SETTINGS(cts) == 0) {
		tgt = cts->ccb_h.target_id;
	} else if (xpt_path_sim(cts->ccb_h.path) == mpt->phydisk_sim) {
		if (mpt_map_physdisk(mpt, (union ccb *)cts, &tgt)) {
			return (-1);
		}
	} else {
		tgt = cts->ccb_h.target_id;
	}

	/*
	 * We aren't looking at Port Page 2 BIOS settings here-
	 * sometimes these have been known to be bogus XXX.
	 *
	 * For user settings, we pick the max from port page 0
	 * 
	 * For current settings we read the current settings out from
	 * device page 0 for that target.
	 */
	if (IS_CURRENT_SETTINGS(cts)) {
		CONFIG_PAGE_SCSI_DEVICE_0 tmp;
		dval = 0;

		tmp = mpt->mpt_dev_page0[tgt];
		rv = mpt_read_cur_cfg_page(mpt, tgt, &tmp.Header,
		    sizeof(tmp), FALSE, 5000);
		if (rv) {
			mpt_prt(mpt, "can't get tgt %d config page 0\n", tgt);
			return (rv);
		}
		mpt2host_config_page_scsi_device_0(&tmp);
		
		mpt_lprt(mpt, MPT_PRT_DEBUG,
		    "mpt_get_spi_settings[%d]: current NP %x Info %x\n", tgt,
		    tmp.NegotiatedParameters, tmp.Information);
		dval |= (tmp.NegotiatedParameters & MPI_SCSIDEVPAGE0_NP_WIDE) ?
		    DP_WIDE : DP_NARROW;
		dval |= (mpt->mpt_disc_enable & (1 << tgt)) ?
		    DP_DISC_ENABLE : DP_DISC_DISABL;
		dval |= (mpt->mpt_tag_enable & (1 << tgt)) ?
		    DP_TQING_ENABLE : DP_TQING_DISABL;
		oval = tmp.NegotiatedParameters;
		oval &= MPI_SCSIDEVPAGE0_NP_NEG_SYNC_OFFSET_MASK;
		oval >>= MPI_SCSIDEVPAGE0_NP_SHIFT_SYNC_OFFSET;
		pval = tmp.NegotiatedParameters;
		pval &= MPI_SCSIDEVPAGE0_NP_NEG_SYNC_PERIOD_MASK;
		pval >>= MPI_SCSIDEVPAGE0_NP_SHIFT_SYNC_PERIOD;
		mpt->mpt_dev_page0[tgt] = tmp;
	} else {
		dval = DP_WIDE|DP_DISC_ENABLE|DP_TQING_ENABLE|DP_SYNC;
		oval = mpt->mpt_port_page0.Capabilities;
		oval = MPI_SCSIPORTPAGE0_CAP_GET_MAX_SYNC_OFFSET(oval);
		pval = mpt->mpt_port_page0.Capabilities;
		pval = MPI_SCSIPORTPAGE0_CAP_GET_MIN_SYNC_PERIOD(pval);
	}

	spi->valid = 0;
	scsi->valid = 0;
	spi->flags = 0;
	scsi->flags = 0;
	spi->sync_offset = oval;
	spi->sync_period = pval;
	spi->valid |= CTS_SPI_VALID_SYNC_OFFSET;
	spi->valid |= CTS_SPI_VALID_SYNC_RATE;
	spi->valid |= CTS_SPI_VALID_BUS_WIDTH;
	if (dval & DP_WIDE) {
		spi->bus_width = MSG_EXT_WDTR_BUS_16_BIT;
	} else {
		spi->bus_width = MSG_EXT_WDTR_BUS_8_BIT;
	}
	if (cts->ccb_h.target_lun != CAM_LUN_WILDCARD) {
		scsi->valid = CTS_SCSI_VALID_TQ;
		if (dval & DP_TQING_ENABLE) {
			scsi->flags |= CTS_SCSI_FLAGS_TAG_ENB;
		}
		spi->valid |= CTS_SPI_VALID_DISC;
		if (dval & DP_DISC_ENABLE) {
			spi->flags |= CTS_SPI_FLAGS_DISC_ENB;
		}
	}

	mpt_lprt(mpt, MPT_PRT_NEGOTIATION,
	    "mpt_get_spi_settings[%d]: %s flags 0x%x per 0x%x off=%d\n", tgt,
	    IS_CURRENT_SETTINGS(cts) ? "ACTIVE" : "NVRAM ", dval, pval, oval);
	return (0);
}

static void
mpt_setwidth(struct mpt_softc *mpt, int tgt, int onoff)
{
	PTR_CONFIG_PAGE_SCSI_DEVICE_1 ptr;

	ptr = &mpt->mpt_dev_page1[tgt];
	if (onoff) {
		ptr->RequestedParameters |= MPI_SCSIDEVPAGE1_RP_WIDE;
	} else {
		ptr->RequestedParameters &= ~MPI_SCSIDEVPAGE1_RP_WIDE;
	}
}

static void
mpt_setsync(struct mpt_softc *mpt, int tgt, int period, int offset)
{
	PTR_CONFIG_PAGE_SCSI_DEVICE_1 ptr;

	ptr = &mpt->mpt_dev_page1[tgt];
	ptr->RequestedParameters &= ~MPI_SCSIDEVPAGE1_RP_MIN_SYNC_PERIOD_MASK;
	ptr->RequestedParameters &= ~MPI_SCSIDEVPAGE1_RP_MAX_SYNC_OFFSET_MASK;
	ptr->RequestedParameters &= ~MPI_SCSIDEVPAGE1_RP_DT;
	ptr->RequestedParameters &= ~MPI_SCSIDEVPAGE1_RP_QAS;
	ptr->RequestedParameters &= ~MPI_SCSIDEVPAGE1_RP_IU;
	if (period == 0) {
		return;
	}
	ptr->RequestedParameters |=
	    period << MPI_SCSIDEVPAGE1_RP_SHIFT_MIN_SYNC_PERIOD;
	ptr->RequestedParameters |=
	    offset << MPI_SCSIDEVPAGE1_RP_SHIFT_MAX_SYNC_OFFSET;
	if (period < 0xa) {
		ptr->RequestedParameters |= MPI_SCSIDEVPAGE1_RP_DT;
	}
	if (period < 0x9) {
		ptr->RequestedParameters |= MPI_SCSIDEVPAGE1_RP_QAS;
		ptr->RequestedParameters |= MPI_SCSIDEVPAGE1_RP_IU;
	}
}

static int
mpt_update_spi_config(struct mpt_softc *mpt, int tgt)
{
	CONFIG_PAGE_SCSI_DEVICE_1 tmp;
	int rv;

	mpt_lprt(mpt, MPT_PRT_NEGOTIATION,
	    "mpt_update_spi_config[%d].page1: Requested Params 0x%08x\n",
	    tgt, mpt->mpt_dev_page1[tgt].RequestedParameters);
	tmp = mpt->mpt_dev_page1[tgt];
	host2mpt_config_page_scsi_device_1(&tmp);
	rv = mpt_write_cur_cfg_page(mpt, tgt,
	    &tmp.Header, sizeof(tmp), FALSE, 5000);
	if (rv) {
		mpt_prt(mpt, "mpt_update_spi_config: write cur page failed\n");
		return (-1);
	}
	return (0);
}

/****************************** Timeout Recovery ******************************/
static int
mpt_spawn_recovery_thread(struct mpt_softc *mpt)
{
	int error;

	error = kproc_create(mpt_recovery_thread, mpt,
	    &mpt->recovery_thread, /*flags*/0,
	    /*altstack*/0, "mpt_recovery%d", mpt->unit);
	return (error);
}

static void
mpt_terminate_recovery_thread(struct mpt_softc *mpt)
{

	if (mpt->recovery_thread == NULL) {
		return;
	}
	mpt->shutdwn_recovery = 1;
	wakeup(mpt);
	/*
	 * Sleep on a slightly different location
	 * for this interlock just for added safety.
	 */
	mpt_sleep(mpt, &mpt->recovery_thread, PUSER, "thtrm", 0);
}

static void
mpt_recovery_thread(void *arg)
{
	struct mpt_softc *mpt;

	mpt = (struct mpt_softc *)arg;
	MPT_LOCK(mpt);
	for (;;) {
		if (TAILQ_EMPTY(&mpt->request_timeout_list) != 0) {
			if (mpt->shutdwn_recovery == 0) {
				mpt_sleep(mpt, mpt, PUSER, "idle", 0);
			}
		}
		if (mpt->shutdwn_recovery != 0) {
			break;
		}
		mpt_recover_commands(mpt);
	}
	mpt->recovery_thread = NULL;
	wakeup(&mpt->recovery_thread);
	MPT_UNLOCK(mpt);
	kproc_exit(0);
}

static int
mpt_scsi_send_tmf(struct mpt_softc *mpt, u_int type, u_int flags,
    u_int channel, target_id_t target, lun_id_t lun, u_int abort_ctx,
    int sleep_ok)
{
	MSG_SCSI_TASK_MGMT *tmf_req;
	int		    error;

	/*
	 * Wait for any current TMF request to complete.
	 * We're only allowed to issue one TMF at a time.
	 */
	error = mpt_wait_req(mpt, mpt->tmf_req, REQ_STATE_FREE, REQ_STATE_FREE,
	    sleep_ok, MPT_TMF_MAX_TIMEOUT);
	if (error != 0) {
		mpt_reset(mpt, TRUE);
		return (ETIMEDOUT);
	}

	mpt_assign_serno(mpt, mpt->tmf_req);
	mpt->tmf_req->state = REQ_STATE_ALLOCATED|REQ_STATE_QUEUED;

	tmf_req = (MSG_SCSI_TASK_MGMT *)mpt->tmf_req->req_vbuf;
	memset(tmf_req, 0, sizeof(*tmf_req));
	tmf_req->TargetID = target;
	tmf_req->Bus = channel;
	tmf_req->Function = MPI_FUNCTION_SCSI_TASK_MGMT;
	tmf_req->TaskType = type;
	tmf_req->MsgFlags = flags;
	tmf_req->MsgContext =
	    htole32(mpt->tmf_req->index | scsi_tmf_handler_id);
	be64enc(tmf_req->LUN, CAM_EXTLUN_BYTE_SWIZZLE(lun));
	tmf_req->TaskMsgContext = abort_ctx;

	mpt_lprt(mpt, MPT_PRT_DEBUG,
	    "Issuing TMF %p:%u with MsgContext of 0x%x\n", mpt->tmf_req,
	    mpt->tmf_req->serno, tmf_req->MsgContext);
	if (mpt->verbose > MPT_PRT_DEBUG) {
		mpt_print_request(tmf_req);
	}

	KASSERT(mpt_req_on_pending_list(mpt, mpt->tmf_req) == 0,
	    ("mpt_scsi_send_tmf: tmf_req already on pending list"));
	TAILQ_INSERT_HEAD(&mpt->request_pending_list, mpt->tmf_req, links);
	error = mpt_send_handshake_cmd(mpt, sizeof(*tmf_req), tmf_req);
	if (error != MPT_OK) {
		TAILQ_REMOVE(&mpt->request_pending_list, mpt->tmf_req, links);
		mpt->tmf_req->state = REQ_STATE_FREE;
		mpt_reset(mpt, TRUE);
	}
	return (error);
}

/*
 * When a command times out, it is placed on the requeust_timeout_list
 * and we wake our recovery thread.  The MPT-Fusion architecture supports
 * only a single TMF operation at a time, so we serially abort/bdr, etc,
 * the timedout transactions.  The next TMF is issued either by the
 * completion handler of the current TMF waking our recovery thread,
 * or the TMF timeout handler causing a hard reset sequence.
 */
static void
mpt_recover_commands(struct mpt_softc *mpt)
{
	request_t	   *req;
	union ccb	   *ccb;
	int		    error;

	if (TAILQ_EMPTY(&mpt->request_timeout_list) != 0) {
		/*
		 * No work to do- leave.
		 */
		mpt_prt(mpt, "mpt_recover_commands: no requests.\n");
		return;
	}

	/*
	 * Flush any commands whose completion coincides with their timeout.
	 */
	mpt_intr(mpt);

	if (TAILQ_EMPTY(&mpt->request_timeout_list) != 0) {
		/*
		 * The timedout commands have already
		 * completed.  This typically means
		 * that either the timeout value was on
		 * the hairy edge of what the device
		 * requires or - more likely - interrupts
		 * are not happening.
		 */
		mpt_prt(mpt, "Timedout requests already complete. "
		    "Interrupts may not be functioning.\n");
		mpt_enable_ints(mpt);
		return;
	}

	/*
	 * We have no visibility into the current state of the
	 * controller, so attempt to abort the commands in the
	 * order they timed-out. For initiator commands, we
	 * depend on the reply handler pulling requests off
	 * the timeout list.
	 */
	while ((req = TAILQ_FIRST(&mpt->request_timeout_list)) != NULL) {
		uint16_t status;
		uint8_t response;
		MSG_REQUEST_HEADER *hdrp = req->req_vbuf;

		mpt_prt(mpt, "attempting to abort req %p:%u function %x\n",
		    req, req->serno, hdrp->Function);
		ccb = req->ccb;
		if (ccb == NULL) {
			mpt_prt(mpt, "null ccb in timed out request. "
			    "Resetting Controller.\n");
			mpt_reset(mpt, TRUE);
			continue;
		}
		mpt_set_ccb_status(ccb, CAM_CMD_TIMEOUT);

		/*
		 * Check to see if this is not an initiator command and
		 * deal with it differently if it is.
		 */
		switch (hdrp->Function) {
		case MPI_FUNCTION_SCSI_IO_REQUEST:
		case MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH:
			break;
		default:
			/*
			 * XXX: FIX ME: need to abort target assists...
			 */
			mpt_prt(mpt, "just putting it back on the pend q\n");
			TAILQ_REMOVE(&mpt->request_timeout_list, req, links);
			TAILQ_INSERT_HEAD(&mpt->request_pending_list, req,
			    links);
			continue;
		}

		error = mpt_scsi_send_tmf(mpt,
		    MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK,
		    0, 0, ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
		    htole32(req->index | scsi_io_handler_id), TRUE);

		if (error != 0) {
			/*
			 * mpt_scsi_send_tmf hard resets on failure, so no
			 * need to do so here.  Our queue should be emptied
			 * by the hard reset.
			 */
			continue;
		}

		error = mpt_wait_req(mpt, mpt->tmf_req, REQ_STATE_DONE,
		    REQ_STATE_DONE, TRUE, 500);

		status = le16toh(mpt->tmf_req->IOCStatus);
		response = mpt->tmf_req->ResponseCode;
		mpt->tmf_req->state = REQ_STATE_FREE;

		if (error != 0) {
			/*
			 * If we've errored out,, reset the controller.
			 */
			mpt_prt(mpt, "mpt_recover_commands: abort timed-out. "
			    "Resetting controller\n");
			mpt_reset(mpt, TRUE);
			continue;
		}

		if ((status & MPI_IOCSTATUS_MASK) != MPI_IOCSTATUS_SUCCESS) {
			mpt_prt(mpt, "mpt_recover_commands: IOC Status 0x%x. "
			    "Resetting controller.\n", status);
			mpt_reset(mpt, TRUE);
			continue;
		}

		if (response != MPI_SCSITASKMGMT_RSP_TM_SUCCEEDED &&
		    response != MPI_SCSITASKMGMT_RSP_TM_COMPLETE) {
			mpt_prt(mpt, "mpt_recover_commands: TMF Response 0x%x. "
			    "Resetting controller.\n", response);
			mpt_reset(mpt, TRUE);
			continue;
		}
		mpt_prt(mpt, "abort of req %p:%u completed\n", req, req->serno);
	}
}

/************************ Target Mode Support ****************************/
static void
mpt_fc_post_els(struct mpt_softc *mpt, request_t *req, int ioindex)
{
	MSG_LINK_SERVICE_BUFFER_POST_REQUEST *fc;
	PTR_SGE_TRANSACTION32 tep;
	PTR_SGE_SIMPLE32 se;
	bus_addr_t paddr;
	uint32_t fl;

	paddr = req->req_pbuf;
	paddr += MPT_RQSL(mpt);

	fc = req->req_vbuf;
	memset(fc, 0, MPT_REQUEST_AREA);
	fc->BufferCount = 1;
	fc->Function = MPI_FUNCTION_FC_LINK_SRVC_BUF_POST;
	fc->MsgContext = htole32(req->index | fc_els_handler_id);

	/*
	 * Okay, set up ELS buffer pointers. ELS buffer pointers
	 * consist of a TE SGL element (with details length of zero)
	 * followed by a SIMPLE SGL element which holds the address
	 * of the buffer.
	 */

	tep = (PTR_SGE_TRANSACTION32) &fc->SGL;

	tep->ContextSize = 4;
	tep->Flags = 0;
	tep->TransactionContext[0] = htole32(ioindex);

	se = (PTR_SGE_SIMPLE32) &tep->TransactionDetails[0];
	fl =
		MPI_SGE_FLAGS_HOST_TO_IOC	|
		MPI_SGE_FLAGS_SIMPLE_ELEMENT	|
		MPI_SGE_FLAGS_LAST_ELEMENT	|
		MPI_SGE_FLAGS_END_OF_LIST	|
		MPI_SGE_FLAGS_END_OF_BUFFER;
	fl <<= MPI_SGE_FLAGS_SHIFT;
	fl |= (MPT_NRFM(mpt) - MPT_RQSL(mpt));
	se->FlagsLength = htole32(fl);
	se->Address = htole32((uint32_t) paddr);
	mpt_lprt(mpt, MPT_PRT_DEBUG,
	    "add ELS index %d ioindex %d for %p:%u\n",
	    req->index, ioindex, req, req->serno);
	KASSERT(((req->state & REQ_STATE_LOCKED) != 0),
	    ("mpt_fc_post_els: request not locked"));
	mpt_send_cmd(mpt, req);
}

static void
mpt_post_target_command(struct mpt_softc *mpt, request_t *req, int ioindex)
{
	PTR_MSG_TARGET_CMD_BUFFER_POST_REQUEST fc;
	PTR_CMD_BUFFER_DESCRIPTOR cb;
	bus_addr_t paddr;

	paddr = req->req_pbuf;
	paddr += MPT_RQSL(mpt);
	memset(req->req_vbuf, 0, MPT_REQUEST_AREA);
	MPT_TGT_STATE(mpt, req)->state = TGT_STATE_LOADING;

	fc = req->req_vbuf;
	fc->BufferCount = 1;
	fc->Function = MPI_FUNCTION_TARGET_CMD_BUFFER_POST;
	fc->BufferLength = MIN(MPT_REQUEST_AREA - MPT_RQSL(mpt), UINT8_MAX);
	fc->MsgContext = htole32(req->index | mpt->scsi_tgt_handler_id);

	cb = &fc->Buffer[0];
	cb->IoIndex = htole16(ioindex);
	cb->u.PhysicalAddress32 = htole32((U32) paddr);

	mpt_check_doorbell(mpt);
	mpt_send_cmd(mpt, req);
}

static int
mpt_add_els_buffers(struct mpt_softc *mpt)
{
	int i;

	if (mpt->is_fc == 0) {
		return (TRUE);
	}

	if (mpt->els_cmds_allocated) {
		return (TRUE);
	}

	mpt->els_cmd_ptrs = malloc(MPT_MAX_ELS * sizeof (request_t *),
	    M_DEVBUF, M_NOWAIT | M_ZERO);

	if (mpt->els_cmd_ptrs == NULL) {
		return (FALSE);
	}

	/*
	 * Feed the chip some ELS buffer resources
	 */
	for (i = 0; i < MPT_MAX_ELS; i++) {
		request_t *req = mpt_get_request(mpt, FALSE);
		if (req == NULL) {
			break;
		}
		req->state |= REQ_STATE_LOCKED;
		mpt->els_cmd_ptrs[i] = req;
		mpt_fc_post_els(mpt, req, i);
	}

	if (i == 0) {
		mpt_prt(mpt, "unable to add ELS buffer resources\n");
		free(mpt->els_cmd_ptrs, M_DEVBUF);
		mpt->els_cmd_ptrs = NULL;
		return (FALSE);
	}
	if (i != MPT_MAX_ELS) {
		mpt_lprt(mpt, MPT_PRT_INFO,
		    "only added %d of %d  ELS buffers\n", i, MPT_MAX_ELS);
	}
	mpt->els_cmds_allocated = i;
	return(TRUE);
}

static int
mpt_add_target_commands(struct mpt_softc *mpt)
{
	int i, max;

	if (mpt->tgt_cmd_ptrs) {
		return (TRUE);
	}

	max = MPT_MAX_REQUESTS(mpt) >> 1;
	if (max > mpt->mpt_max_tgtcmds) {
		max = mpt->mpt_max_tgtcmds;
	}
	mpt->tgt_cmd_ptrs =
	    malloc(max * sizeof (request_t *), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (mpt->tgt_cmd_ptrs == NULL) {
		mpt_prt(mpt,
		    "mpt_add_target_commands: could not allocate cmd ptrs\n");
		return (FALSE);
	}

	for (i = 0; i < max; i++) {
		request_t *req;

		req = mpt_get_request(mpt, FALSE);
		if (req == NULL) {
			break;
		}
		req->state |= REQ_STATE_LOCKED;
		mpt->tgt_cmd_ptrs[i] = req;
		mpt_post_target_command(mpt, req, i);
	}


	if (i == 0) {
		mpt_lprt(mpt, MPT_PRT_ERROR, "could not add any target bufs\n");
		free(mpt->tgt_cmd_ptrs, M_DEVBUF);
		mpt->tgt_cmd_ptrs = NULL;
		return (FALSE);
	}

	mpt->tgt_cmds_allocated = i;

	if (i < max) {
		mpt_lprt(mpt, MPT_PRT_INFO,
		    "added %d of %d target bufs\n", i, max);
	}
	return (i);
}

static int
mpt_enable_lun(struct mpt_softc *mpt, target_id_t tgt, lun_id_t lun)
{

	if (tgt == CAM_TARGET_WILDCARD && lun == CAM_LUN_WILDCARD) {
		mpt->twildcard = 1;
	} else if (lun >= MPT_MAX_LUNS) {
		return (EINVAL);
	} else if (tgt != CAM_TARGET_WILDCARD && tgt != 0) {
		return (EINVAL);
	}
	if (mpt->tenabled == 0) {
		if (mpt->is_fc) {
			(void) mpt_fc_reset_link(mpt, 0);
		}
		mpt->tenabled = 1;
	}
	if (lun == CAM_LUN_WILDCARD) {
		mpt->trt_wildcard.enabled = 1;
	} else {
		mpt->trt[lun].enabled = 1;
	}
	return (0);
}

static int
mpt_disable_lun(struct mpt_softc *mpt, target_id_t tgt, lun_id_t lun)
{
	int i;

	if (tgt == CAM_TARGET_WILDCARD && lun == CAM_LUN_WILDCARD) {
		mpt->twildcard = 0;
	} else if (lun >= MPT_MAX_LUNS) {
		return (EINVAL);
	} else if (tgt != CAM_TARGET_WILDCARD && tgt != 0) {
		return (EINVAL);
	}
	if (lun == CAM_LUN_WILDCARD) {
		mpt->trt_wildcard.enabled = 0;
	} else {
		mpt->trt[lun].enabled = 0;
	}
	for (i = 0; i < MPT_MAX_LUNS; i++) {
		if (mpt->trt[i].enabled) {
			break;
		}
	}
	if (i == MPT_MAX_LUNS && mpt->twildcard == 0) {
		if (mpt->is_fc) {
			(void) mpt_fc_reset_link(mpt, 0);
		}
		mpt->tenabled = 0;
	}
	return (0);
}

/*
 * Called with MPT lock held
 */
static void
mpt_target_start_io(struct mpt_softc *mpt, union ccb *ccb)
{
	struct ccb_scsiio *csio = &ccb->csio;
	request_t *cmd_req = MPT_TAG_2_REQ(mpt, csio->tag_id);
	mpt_tgt_state_t *tgt = MPT_TGT_STATE(mpt, cmd_req);

	switch (tgt->state) {
	case TGT_STATE_IN_CAM:
		break;
	case TGT_STATE_MOVING_DATA:
		mpt_set_ccb_status(ccb, CAM_REQUEUE_REQ);
		xpt_freeze_simq(mpt->sim, 1);
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		tgt->ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
		xpt_done(ccb);
		return;
	default:
		mpt_prt(mpt, "ccb %p flags 0x%x tag 0x%08x had bad request "
		    "starting I/O\n", ccb, csio->ccb_h.flags, csio->tag_id);
		mpt_tgt_dump_req_state(mpt, cmd_req);
		mpt_set_ccb_status(ccb, CAM_REQ_CMP_ERR);
		xpt_done(ccb);
		return;
	}

	if (csio->dxfer_len) {
		bus_dmamap_callback_t *cb;
		PTR_MSG_TARGET_ASSIST_REQUEST ta;
		request_t *req;
		int error;

		KASSERT((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE,
		    ("dxfer_len %u but direction is NONE", csio->dxfer_len));

		if ((req = mpt_get_request(mpt, FALSE)) == NULL) {
			if (mpt->outofbeer == 0) {
				mpt->outofbeer = 1;
				xpt_freeze_simq(mpt->sim, 1);
				mpt_lprt(mpt, MPT_PRT_DEBUG, "FREEZEQ\n");
			}
			ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
			mpt_set_ccb_status(ccb, CAM_REQUEUE_REQ);
			xpt_done(ccb);
			return;
		}
		ccb->ccb_h.status = CAM_SIM_QUEUED | CAM_REQ_INPROG;
		if (sizeof (bus_addr_t) > 4) {
			cb = mpt_execute_req_a64;
		} else {
			cb = mpt_execute_req;
		}

		req->ccb = ccb;
		ccb->ccb_h.ccb_req_ptr = req;

		/*
		 * Record the currently active ccb and the
		 * request for it in our target state area.
		 */
		tgt->ccb = ccb;
		tgt->req = req;

		memset(req->req_vbuf, 0, MPT_RQSL(mpt));
		ta = req->req_vbuf;

		if (mpt->is_sas) {
			PTR_MPI_TARGET_SSP_CMD_BUFFER ssp =
			     cmd_req->req_vbuf;
			ta->QueueTag = ssp->InitiatorTag;
		} else if (mpt->is_spi) {
			PTR_MPI_TARGET_SCSI_SPI_CMD_BUFFER sp =
			     cmd_req->req_vbuf;
			ta->QueueTag = sp->Tag;
		}
		ta->Function = MPI_FUNCTION_TARGET_ASSIST;
		ta->MsgContext = htole32(req->index | mpt->scsi_tgt_handler_id);
		ta->ReplyWord = htole32(tgt->reply_desc);
		be64enc(ta->LUN, CAM_EXTLUN_BYTE_SWIZZLE(csio->ccb_h.target_lun));

		ta->RelativeOffset = tgt->bytes_xfered;
		ta->DataLength = ccb->csio.dxfer_len;
		if (ta->DataLength > tgt->resid) {
			ta->DataLength = tgt->resid;
		}

		/*
		 * XXX Should be done after data transfer completes?
		 */
		csio->resid = csio->dxfer_len - ta->DataLength;
		tgt->resid -= csio->dxfer_len;
		tgt->bytes_xfered += csio->dxfer_len;

		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
			ta->TargetAssistFlags |=
			    TARGET_ASSIST_FLAGS_DATA_DIRECTION;
		}

#ifdef	WE_TRUST_AUTO_GOOD_STATUS
		if ((ccb->ccb_h.flags & CAM_SEND_STATUS) &&
		    csio->scsi_status == SCSI_STATUS_OK && tgt->resid == 0) {
			ta->TargetAssistFlags |=
			    TARGET_ASSIST_FLAGS_AUTO_STATUS;
		}
#endif
		tgt->state = TGT_STATE_SETTING_UP_FOR_DATA;

		mpt_lprt(mpt, MPT_PRT_DEBUG, 
		    "DATA_CCB %p tag %x %u bytes %u resid flg %x req %p:%u "
		    "nxtstate=%d\n", csio, csio->tag_id, csio->dxfer_len,
		    tgt->resid, ccb->ccb_h.flags, req, req->serno, tgt->state);

		error = bus_dmamap_load_ccb(mpt->buffer_dmat, req->dmap, ccb,
		    cb, req, 0);
		if (error == EINPROGRESS) {
			xpt_freeze_simq(mpt->sim, 1);
			ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
		}
	} else {
		/*
		 * XXX: I don't know why this seems to happen, but
		 * XXX: completing the CCB seems to make things happy.
		 * XXX: This seems to happen if the initiator requests
		 * XXX: enough data that we have to do multiple CTIOs.
		 */
		if ((ccb->ccb_h.flags & CAM_SEND_STATUS) == 0) {
			mpt_lprt(mpt, MPT_PRT_DEBUG,
			    "Meaningless STATUS CCB (%p): flags %x status %x "
			    "resid %d bytes_xfered %u\n", ccb, ccb->ccb_h.flags,
			    ccb->ccb_h.status, tgt->resid, tgt->bytes_xfered);
			mpt_set_ccb_status(ccb, CAM_REQ_CMP);
			ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
			xpt_done(ccb);
			return;
		}
		mpt_scsi_tgt_status(mpt, ccb, cmd_req, csio->scsi_status,
		    (void *)&csio->sense_data,
		    (ccb->ccb_h.flags & CAM_SEND_SENSE) ?
		     csio->sense_len : 0);
	}
}

static void
mpt_scsi_tgt_local(struct mpt_softc *mpt, request_t *cmd_req,
    lun_id_t lun, int send, uint8_t *data, size_t length)
{
	mpt_tgt_state_t *tgt;
	PTR_MSG_TARGET_ASSIST_REQUEST ta;
	SGE_SIMPLE32 *se;
	uint32_t flags;
	uint8_t *dptr;
	bus_addr_t pptr;
	request_t *req;

	/*
	 * We enter with resid set to the data load for the command.
	 */
	tgt = MPT_TGT_STATE(mpt, cmd_req);
	if (length == 0 || tgt->resid == 0) {
		tgt->resid = 0;
		mpt_scsi_tgt_status(mpt, NULL, cmd_req, 0, NULL, 0);
		return;
	}

	if ((req = mpt_get_request(mpt, FALSE)) == NULL) {
		mpt_prt(mpt, "out of resources- dropping local response\n");
		return;
	}
	tgt->is_local = 1;


	memset(req->req_vbuf, 0, MPT_RQSL(mpt));
	ta = req->req_vbuf;

	if (mpt->is_sas) {
		PTR_MPI_TARGET_SSP_CMD_BUFFER ssp = cmd_req->req_vbuf;
		ta->QueueTag = ssp->InitiatorTag;
	} else if (mpt->is_spi) {
		PTR_MPI_TARGET_SCSI_SPI_CMD_BUFFER sp = cmd_req->req_vbuf;
		ta->QueueTag = sp->Tag;
	}
	ta->Function = MPI_FUNCTION_TARGET_ASSIST;
	ta->MsgContext = htole32(req->index | mpt->scsi_tgt_handler_id);
	ta->ReplyWord = htole32(tgt->reply_desc);
	be64enc(ta->LUN, CAM_EXTLUN_BYTE_SWIZZLE(lun));
	ta->RelativeOffset = 0;
	ta->DataLength = length;

	dptr = req->req_vbuf;
	dptr += MPT_RQSL(mpt);
	pptr = req->req_pbuf;
	pptr += MPT_RQSL(mpt);
	memcpy(dptr, data, min(length, MPT_RQSL(mpt)));

	se = (SGE_SIMPLE32 *) &ta->SGL[0];
	memset(se, 0,sizeof (*se));

	flags = MPI_SGE_FLAGS_SIMPLE_ELEMENT;
	if (send) {
		ta->TargetAssistFlags |= TARGET_ASSIST_FLAGS_DATA_DIRECTION;
		flags |= MPI_SGE_FLAGS_HOST_TO_IOC;
	}
	se->Address = pptr;
	MPI_pSGE_SET_LENGTH(se, length);
	flags |= MPI_SGE_FLAGS_LAST_ELEMENT;
	flags |= MPI_SGE_FLAGS_END_OF_LIST | MPI_SGE_FLAGS_END_OF_BUFFER;
	MPI_pSGE_SET_FLAGS(se, flags);

	tgt->ccb = NULL;
	tgt->req = req;
	tgt->resid -= length;
	tgt->bytes_xfered = length;
#ifdef	WE_TRUST_AUTO_GOOD_STATUS
	tgt->state = TGT_STATE_MOVING_DATA_AND_STATUS;
#else
	tgt->state = TGT_STATE_MOVING_DATA;
#endif
	mpt_send_cmd(mpt, req);
}

/*
 * Abort queued up CCBs
 */
static cam_status
mpt_abort_target_ccb(struct mpt_softc *mpt, union ccb *ccb)
{
	struct mpt_hdr_stailq *lp;
	struct ccb_hdr *srch;
	union ccb *accb = ccb->cab.abort_ccb;
	tgt_resource_t *trtp;
	mpt_tgt_state_t *tgt;
	request_t *req;
	uint32_t tag;

	mpt_lprt(mpt, MPT_PRT_DEBUG, "aborting ccb %p\n", accb);
	if (ccb->ccb_h.target_lun == CAM_LUN_WILDCARD)
		trtp = &mpt->trt_wildcard;
	else
		trtp = &mpt->trt[ccb->ccb_h.target_lun];
	if (accb->ccb_h.func_code == XPT_ACCEPT_TARGET_IO) {
		lp = &trtp->atios;
		tag = accb->atio.tag_id;
	} else {
		lp = &trtp->inots;
		tag = accb->cin1.tag_id;
	}

	/* Search the CCB among queued. */
	STAILQ_FOREACH(srch, lp, sim_links.stqe) {
		if (srch != &accb->ccb_h)
			continue;
		STAILQ_REMOVE(lp, srch, ccb_hdr, sim_links.stqe);
		accb->ccb_h.status = CAM_REQ_ABORTED;
		xpt_done(accb);
		return (CAM_REQ_CMP);
	}

	/* Search the CCB among running. */
	req = MPT_TAG_2_REQ(mpt, tag);
	tgt = MPT_TGT_STATE(mpt, req);
	if (tgt->tag_id == tag) {
		mpt_abort_target_cmd(mpt, req);
		return (CAM_REQ_CMP);
	}

	return (CAM_UA_ABORT);
}

/*
 * Ask the MPT to abort the current target command
 */ 
static int
mpt_abort_target_cmd(struct mpt_softc *mpt, request_t *cmd_req)
{
	int error;
	request_t *req;
	PTR_MSG_TARGET_MODE_ABORT abtp;

	req = mpt_get_request(mpt, FALSE);
	if (req == NULL) {
		return (-1);
	}
	abtp = req->req_vbuf;
	memset(abtp, 0, sizeof (*abtp));

	abtp->MsgContext = htole32(req->index | mpt->scsi_tgt_handler_id);
	abtp->AbortType = TARGET_MODE_ABORT_TYPE_EXACT_IO;
	abtp->Function = MPI_FUNCTION_TARGET_MODE_ABORT;
	abtp->ReplyWord = htole32(MPT_TGT_STATE(mpt, cmd_req)->reply_desc);
	error = 0;
	if (mpt->is_fc || mpt->is_sas) {
		mpt_send_cmd(mpt, req);
	} else {
		error = mpt_send_handshake_cmd(mpt, sizeof(*req), req);
	}
	return (error);
}

/*
 * WE_TRUST_AUTO_GOOD_STATUS- I've found that setting 
 * TARGET_STATUS_SEND_FLAGS_AUTO_GOOD_STATUS leads the
 * FC929 to set bogus FC_RSP fields (nonzero residuals
 * but w/o RESID fields set). This causes QLogic initiators
 * to think maybe that a frame was lost.
 *
 * WE_CAN_USE_AUTO_REPOST- we can't use AUTO_REPOST because
 * we use allocated requests to do TARGET_ASSIST and we
 * need to know when to release them.
 */

static void
mpt_scsi_tgt_status(struct mpt_softc *mpt, union ccb *ccb, request_t *cmd_req,
    uint8_t status, uint8_t const *sense_data, u_int sense_len)
{
	uint8_t *cmd_vbuf;
	mpt_tgt_state_t *tgt;
	PTR_MSG_TARGET_STATUS_SEND_REQUEST tp;
	request_t *req;
	bus_addr_t paddr;
	int resplen = 0;
	uint32_t fl;

	cmd_vbuf = cmd_req->req_vbuf;
	cmd_vbuf += MPT_RQSL(mpt);
	tgt = MPT_TGT_STATE(mpt, cmd_req);

	if ((req = mpt_get_request(mpt, FALSE)) == NULL) {
		if (mpt->outofbeer == 0) {
			mpt->outofbeer = 1;
			xpt_freeze_simq(mpt->sim, 1);
			mpt_lprt(mpt, MPT_PRT_DEBUG, "FREEZEQ\n");
		}
		if (ccb) {
			ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
			mpt_set_ccb_status(ccb, CAM_REQUEUE_REQ);
			xpt_done(ccb);
		} else {
			mpt_prt(mpt,
			    "could not allocate status request- dropping\n");
		}
		return;
	}
	req->ccb = ccb;
	if (ccb) {
		ccb->ccb_h.ccb_mpt_ptr = mpt;
		ccb->ccb_h.ccb_req_ptr = req;
	}

	/*
	 * Record the currently active ccb, if any, and the
	 * request for it in our target state area.
	 */
	tgt->ccb = ccb;
	tgt->req = req;
	tgt->state = TGT_STATE_SENDING_STATUS;

	tp = req->req_vbuf;
	paddr = req->req_pbuf;
	paddr += MPT_RQSL(mpt);

	memset(tp, 0, sizeof (*tp));
	tp->StatusCode = status;
	tp->Function = MPI_FUNCTION_TARGET_STATUS_SEND;
	if (mpt->is_fc) {
		PTR_MPI_TARGET_FCP_CMD_BUFFER fc =
		    (PTR_MPI_TARGET_FCP_CMD_BUFFER) cmd_vbuf;
		uint8_t *sts_vbuf;
		uint32_t *rsp;

		sts_vbuf = req->req_vbuf;
		sts_vbuf += MPT_RQSL(mpt);
		rsp = (uint32_t *) sts_vbuf;
		memcpy(tp->LUN, fc->FcpLun, sizeof (tp->LUN));

		/*
		 * The MPI_TARGET_FCP_RSP_BUFFER define is unfortunate.
		 * It has to be big-endian in memory and is organized
		 * in 32 bit words, which are much easier to deal with
		 * as words which are swizzled as needed.
		 *
		 * All we're filling here is the FC_RSP payload.
		 * We may just have the chip synthesize it if
		 * we have no residual and an OK status.
		 *
		 */
		memset(rsp, 0, sizeof (MPI_TARGET_FCP_RSP_BUFFER));

		rsp[2] = htobe32(status);
#define	MIN_FCP_RESPONSE_SIZE	24
#ifndef	WE_TRUST_AUTO_GOOD_STATUS
		resplen = MIN_FCP_RESPONSE_SIZE;
#endif
		if (tgt->resid < 0) {
			rsp[2] |= htobe32(0x400); /* XXXX NEED MNEMONIC!!!! */
			rsp[3] = htobe32(-tgt->resid);
			resplen = MIN_FCP_RESPONSE_SIZE;
		} else if (tgt->resid > 0) {
			rsp[2] |= htobe32(0x800); /* XXXX NEED MNEMONIC!!!! */
			rsp[3] = htobe32(tgt->resid);
			resplen = MIN_FCP_RESPONSE_SIZE;
		}
		if (sense_len > 0) {
			rsp[2] |= htobe32(0x200); /* XXXX NEED MNEMONIC!!!! */
			rsp[4] = htobe32(sense_len);
			memcpy(&rsp[6], sense_data, sense_len);
			resplen = MIN_FCP_RESPONSE_SIZE + sense_len;
		}
	} else if (mpt->is_sas) {
		PTR_MPI_TARGET_SSP_CMD_BUFFER ssp =
		    (PTR_MPI_TARGET_SSP_CMD_BUFFER) cmd_vbuf;
		memcpy(tp->LUN, ssp->LogicalUnitNumber, sizeof (tp->LUN));
	} else {
		PTR_MPI_TARGET_SCSI_SPI_CMD_BUFFER sp =
		    (PTR_MPI_TARGET_SCSI_SPI_CMD_BUFFER) cmd_vbuf;
		tp->QueueTag = htole16(sp->Tag);
		memcpy(tp->LUN, sp->LogicalUnitNumber, sizeof (tp->LUN));
	}

	tp->ReplyWord = htole32(tgt->reply_desc);
	tp->MsgContext = htole32(req->index | mpt->scsi_tgt_handler_id);

#ifdef	WE_CAN_USE_AUTO_REPOST
	tp->MsgFlags = TARGET_STATUS_SEND_FLAGS_REPOST_CMD_BUFFER;
#endif
	if (status == SCSI_STATUS_OK && resplen == 0) {
		tp->MsgFlags |= TARGET_STATUS_SEND_FLAGS_AUTO_GOOD_STATUS;
	} else {
		tp->StatusDataSGE.u.Address32 = htole32((uint32_t) paddr);
		fl = MPI_SGE_FLAGS_HOST_TO_IOC |
		     MPI_SGE_FLAGS_SIMPLE_ELEMENT |
		     MPI_SGE_FLAGS_LAST_ELEMENT |
		     MPI_SGE_FLAGS_END_OF_LIST |
		     MPI_SGE_FLAGS_END_OF_BUFFER;
		fl <<= MPI_SGE_FLAGS_SHIFT;
		fl |= resplen;
		tp->StatusDataSGE.FlagsLength = htole32(fl);
	}

	mpt_lprt(mpt, MPT_PRT_DEBUG, 
	    "STATUS_CCB %p (with%s sense) tag %x req %p:%u resid %u\n",
	    ccb, sense_len > 0 ? "" : "out", tgt->tag_id,
	    req, req->serno, tgt->resid);
	if (mpt->verbose > MPT_PRT_DEBUG)
		mpt_print_request(req->req_vbuf);
	if (ccb) {
		ccb->ccb_h.status = CAM_SIM_QUEUED | CAM_REQ_INPROG;
		mpt_req_timeout(req, SBT_1S * 60, mpt_timeout, ccb);
	}
	mpt_send_cmd(mpt, req);
}

static void
mpt_scsi_tgt_tsk_mgmt(struct mpt_softc *mpt, request_t *req, mpt_task_mgmt_t fc,
    tgt_resource_t *trtp, int init_id)
{
	struct ccb_immediate_notify *inot;
	mpt_tgt_state_t *tgt;

	tgt = MPT_TGT_STATE(mpt, req);
	inot = (struct ccb_immediate_notify *) STAILQ_FIRST(&trtp->inots);
	if (inot == NULL) {
		mpt_lprt(mpt, MPT_PRT_WARN, "no INOTSs- sending back BSY\n");
		mpt_scsi_tgt_status(mpt, NULL, req, SCSI_STATUS_BUSY, NULL, 0);
		return;
	}
	STAILQ_REMOVE_HEAD(&trtp->inots, sim_links.stqe);
	mpt_lprt(mpt, MPT_PRT_DEBUG1,
	    "Get FREE INOT %p lun %jx\n", inot,
	    (uintmax_t)inot->ccb_h.target_lun);

	inot->initiator_id = init_id;	/* XXX */
	inot->tag_id = tgt->tag_id;
	inot->seq_id = 0;
	/*
	 * This is a somewhat grotesque attempt to map from task management
	 * to old style SCSI messages. God help us all.
	 */
	switch (fc) {
	case MPT_QUERY_TASK_SET:
		inot->arg = MSG_QUERY_TASK_SET;
		break;
	case MPT_ABORT_TASK_SET:
		inot->arg = MSG_ABORT_TASK_SET;
		break;
	case MPT_CLEAR_TASK_SET:
		inot->arg = MSG_CLEAR_TASK_SET;
		break;
	case MPT_QUERY_ASYNC_EVENT:
		inot->arg = MSG_QUERY_ASYNC_EVENT;
		break;
	case MPT_LOGICAL_UNIT_RESET:
		inot->arg = MSG_LOGICAL_UNIT_RESET;
		break;
	case MPT_TARGET_RESET:
		inot->arg = MSG_TARGET_RESET;
		break;
	case MPT_CLEAR_ACA:
		inot->arg = MSG_CLEAR_ACA;
		break;
	default:
		inot->arg = MSG_NOOP;
		break;
	}
	tgt->ccb = (union ccb *) inot;
	inot->ccb_h.status = CAM_MESSAGE_RECV;
	xpt_done((union ccb *)inot);
}

static void
mpt_scsi_tgt_atio(struct mpt_softc *mpt, request_t *req, uint32_t reply_desc)
{
	static uint8_t null_iqd[SHORT_INQUIRY_LENGTH] = {
	    0x7f, 0x00, 0x02, 0x02, 0x20, 0x00, 0x00, 0x32,
	     'F',  'R',  'E',  'E',  'B',  'S',  'D',  ' ',
	     'L',  'S',  'I',  '-',  'L',  'O',  'G',  'I',
	     'C',  ' ',  'N',  'U',  'L',  'D',  'E',  'V',
	     '0',  '0',  '0',  '1'
	};
	struct ccb_accept_tio *atiop;
	lun_id_t lun;
	int tag_action = 0;
	mpt_tgt_state_t *tgt;
	tgt_resource_t *trtp = NULL;
	U8 *lunptr;
	U8 *vbuf;
	U16 ioindex;
	mpt_task_mgmt_t fct = MPT_NIL_TMT_VALUE;
	uint8_t *cdbp;

	/*
	 * Stash info for the current command where we can get at it later.
	 */
	vbuf = req->req_vbuf;
	vbuf += MPT_RQSL(mpt);
	if (mpt->verbose >= MPT_PRT_DEBUG) {
		mpt_dump_data(mpt, "mpt_scsi_tgt_atio response", vbuf,
		    max(sizeof (MPI_TARGET_FCP_CMD_BUFFER),
		    max(sizeof (MPI_TARGET_SSP_CMD_BUFFER),
		    sizeof (MPI_TARGET_SCSI_SPI_CMD_BUFFER))));
	}

	/*
	 * Get our state pointer set up.
	 */
	tgt = MPT_TGT_STATE(mpt, req);
	if (tgt->state != TGT_STATE_LOADED) {
		mpt_tgt_dump_req_state(mpt, req);
		panic("bad target state in mpt_scsi_tgt_atio");
	}
	memset(tgt, 0, sizeof (mpt_tgt_state_t));
	tgt->state = TGT_STATE_IN_CAM;
	tgt->reply_desc = reply_desc;
	ioindex = GET_IO_INDEX(reply_desc);

	/*
	 * The tag we construct here allows us to find the
	 * original request that the command came in with.
	 *
	 * This way we don't have to depend on anything but the
	 * tag to find things when CCBs show back up from CAM.
	 */
	tgt->tag_id = MPT_MAKE_TAGID(mpt, req, ioindex);

	if (mpt->is_fc) {
		PTR_MPI_TARGET_FCP_CMD_BUFFER fc;
		fc = (PTR_MPI_TARGET_FCP_CMD_BUFFER) vbuf;
		if (fc->FcpCntl[2]) {
			/*
			 * Task Management Request
			 */
			switch (fc->FcpCntl[2]) {
			case 0x1:
				fct = MPT_QUERY_TASK_SET;
				break;
			case 0x2:
				fct = MPT_ABORT_TASK_SET;
				break;
			case 0x4:
				fct = MPT_CLEAR_TASK_SET;
				break;
			case 0x8:
				fct = MPT_QUERY_ASYNC_EVENT;
				break;
			case 0x10:
				fct = MPT_LOGICAL_UNIT_RESET;
				break;
			case 0x20:
				fct = MPT_TARGET_RESET;
				break;
			case 0x40:
				fct = MPT_CLEAR_ACA;
				break;
			default:
				mpt_prt(mpt, "CORRUPTED TASK MGMT BITS: 0x%x\n",
				    fc->FcpCntl[2]);
				mpt_scsi_tgt_status(mpt, NULL, req,
				    SCSI_STATUS_OK, NULL, 0);
				return;
			}
		} else {
			switch (fc->FcpCntl[1]) {
			case 0:
				tag_action = MSG_SIMPLE_Q_TAG;
				break;
			case 1:
				tag_action = MSG_HEAD_OF_Q_TAG;
				break;
			case 2:
				tag_action = MSG_ORDERED_Q_TAG;
				break;
			default:
				/*
				 * Bah. Ignore Untagged Queing and ACA
				 */
				tag_action = MSG_SIMPLE_Q_TAG;
				break;
			}
		}
		tgt->resid = be32toh(fc->FcpDl);
		cdbp = fc->FcpCdb;
		lunptr = fc->FcpLun;
		tgt->itag = fc->OptionalOxid;
	} else if (mpt->is_sas) {
		PTR_MPI_TARGET_SSP_CMD_BUFFER ssp;
		ssp = (PTR_MPI_TARGET_SSP_CMD_BUFFER) vbuf;
		cdbp = ssp->CDB;
		lunptr = ssp->LogicalUnitNumber;
		tgt->itag = ssp->InitiatorTag;
	} else {
		PTR_MPI_TARGET_SCSI_SPI_CMD_BUFFER sp;
		sp = (PTR_MPI_TARGET_SCSI_SPI_CMD_BUFFER) vbuf;
		cdbp = sp->CDB;
		lunptr = sp->LogicalUnitNumber;
		tgt->itag = sp->Tag;
	}

	lun = CAM_EXTLUN_BYTE_SWIZZLE(be64dec(lunptr));

	/*
	 * Deal with non-enabled or bad luns here.
	 */
	if (lun >= MPT_MAX_LUNS || mpt->tenabled == 0 ||
	    mpt->trt[lun].enabled == 0) {
		if (mpt->twildcard) {
			trtp = &mpt->trt_wildcard;
		} else if (fct == MPT_NIL_TMT_VALUE) {
			/*
			 * In this case, we haven't got an upstream listener
			 * for either a specific lun or wildcard luns. We
			 * have to make some sensible response. For regular
			 * inquiry, just return some NOT HERE inquiry data.
			 * For VPD inquiry, report illegal field in cdb.
			 * For REQUEST SENSE, just return NO SENSE data.
			 * REPORT LUNS gets illegal command.
			 * All other commands get 'no such device'.
			 */
			uint8_t sense[MPT_SENSE_SIZE];
			size_t len;

			memset(sense, 0, sizeof(sense));
			sense[0] = 0xf0;
			sense[2] = 0x5;
			sense[7] = 0x8;

			switch (cdbp[0]) {
			case INQUIRY:
			{
				if (cdbp[1] != 0) {
					sense[12] = 0x26;
					sense[13] = 0x01;
					break;
				}
				len = min(tgt->resid, cdbp[4]);
				len = min(len, sizeof (null_iqd));
				mpt_lprt(mpt, MPT_PRT_DEBUG,
				    "local inquiry %ld bytes\n", (long) len);
				mpt_scsi_tgt_local(mpt, req, lun, 1,
				    null_iqd, len);
				return;
			}
			case REQUEST_SENSE:
			{
				sense[2] = 0x0;
				len = min(tgt->resid, cdbp[4]);
				len = min(len, sizeof (sense));
				mpt_lprt(mpt, MPT_PRT_DEBUG,
				    "local reqsense %ld bytes\n", (long) len);
				mpt_scsi_tgt_local(mpt, req, lun, 1,
				    sense, len);
				return;
			}
			case REPORT_LUNS:
				mpt_lprt(mpt, MPT_PRT_DEBUG, "REPORT LUNS\n");
				sense[12] = 0x26;
				return;
			default:
				mpt_lprt(mpt, MPT_PRT_DEBUG,
				    "CMD 0x%x to unmanaged lun %jx\n",
				    cdbp[0], (uintmax_t)lun);
				sense[12] = 0x25;
				break;
			}
			mpt_scsi_tgt_status(mpt, NULL, req,
			    SCSI_STATUS_CHECK_COND, sense, sizeof(sense));
			return;
		}
		/* otherwise, leave trtp NULL */
	} else {
		trtp = &mpt->trt[lun];
	}

	/*
	 * Deal with any task management
	 */
	if (fct != MPT_NIL_TMT_VALUE) {
		if (trtp == NULL) {
			mpt_prt(mpt, "task mgmt function %x but no listener\n",
			    fct);
			mpt_scsi_tgt_status(mpt, NULL, req,
			    SCSI_STATUS_OK, NULL, 0);
		} else {
			mpt_scsi_tgt_tsk_mgmt(mpt, req, fct, trtp,
			    GET_INITIATOR_INDEX(reply_desc));
		}
		return;
	}


	atiop = (struct ccb_accept_tio *) STAILQ_FIRST(&trtp->atios);
	if (atiop == NULL) {
		mpt_lprt(mpt, MPT_PRT_WARN,
		    "no ATIOs for lun %jx- sending back %s\n", (uintmax_t)lun,
		    mpt->tenabled? "QUEUE FULL" : "BUSY");
		mpt_scsi_tgt_status(mpt, NULL, req,
		    mpt->tenabled? SCSI_STATUS_QUEUE_FULL : SCSI_STATUS_BUSY,
		    NULL, 0);
		return;
	}
	STAILQ_REMOVE_HEAD(&trtp->atios, sim_links.stqe);
	mpt_lprt(mpt, MPT_PRT_DEBUG1,
	    "Get FREE ATIO %p lun %jx\n", atiop,
	    (uintmax_t)atiop->ccb_h.target_lun);
	atiop->ccb_h.ccb_mpt_ptr = mpt;
	atiop->ccb_h.status = CAM_CDB_RECVD;
	atiop->ccb_h.target_lun = lun;
	atiop->sense_len = 0;
	atiop->tag_id = tgt->tag_id;
	atiop->init_id = GET_INITIATOR_INDEX(reply_desc);
	atiop->cdb_len = 16;
	memcpy(atiop->cdb_io.cdb_bytes, cdbp, atiop->cdb_len);
	if (tag_action) {
		atiop->tag_action = tag_action;
		atiop->ccb_h.flags |= CAM_TAG_ACTION_VALID;
	}
	if (mpt->verbose >= MPT_PRT_DEBUG) {
		int i;
		mpt_prt(mpt, "START_CCB %p for lun %jx CDB=<", atiop,
		    (uintmax_t)atiop->ccb_h.target_lun);
		for (i = 0; i < atiop->cdb_len; i++) {
			mpt_prtc(mpt, "%02x%c", cdbp[i] & 0xff,
			    (i == (atiop->cdb_len - 1))? '>' : ' ');
		}
		mpt_prtc(mpt, " itag %x tag %x rdesc %x dl=%u\n",
		    tgt->itag, tgt->tag_id, tgt->reply_desc, tgt->resid);
	}
	
	xpt_done((union ccb *)atiop);
}

static void
mpt_tgt_dump_tgt_state(struct mpt_softc *mpt, request_t *req)
{
	mpt_tgt_state_t *tgt = MPT_TGT_STATE(mpt, req);

	mpt_prt(mpt, "req %p:%u tgt:rdesc 0x%x resid %u xfrd %u ccb %p treq %p "
	    "nx %d tag 0x%08x itag 0x%04x state=%d\n", req, req->serno,
	    tgt->reply_desc, tgt->resid, tgt->bytes_xfered, tgt->ccb,
	    tgt->req, tgt->nxfers, tgt->tag_id, tgt->itag, tgt->state);
}

static void
mpt_tgt_dump_req_state(struct mpt_softc *mpt, request_t *req)
{

	mpt_prt(mpt, "req %p:%u index %u (%x) state %x\n", req, req->serno,
	    req->index, req->index, req->state);
	mpt_tgt_dump_tgt_state(mpt, req);
}

static int
mpt_scsi_tgt_reply_handler(struct mpt_softc *mpt, request_t *req,
    uint32_t reply_desc, MSG_DEFAULT_REPLY *reply_frame)
{
	int dbg;
	union ccb *ccb;
	U16 status;

	if (reply_frame == NULL) {
		/*
		 * Figure out what the state of the command is.
		 */
		mpt_tgt_state_t *tgt = MPT_TGT_STATE(mpt, req);

#ifdef	INVARIANTS
		mpt_req_spcl(mpt, req, "turbo scsi_tgt_reply", __LINE__);
		if (tgt->req) {
			mpt_req_not_spcl(mpt, tgt->req,
			    "turbo scsi_tgt_reply associated req", __LINE__);
		}
#endif
		switch(tgt->state) {
		case TGT_STATE_LOADED:
			/*
			 * This is a new command starting.
			 */
			mpt_scsi_tgt_atio(mpt, req, reply_desc);
			break;
		case TGT_STATE_MOVING_DATA:
		{
			ccb = tgt->ccb;
			if (tgt->req == NULL) {
				panic("mpt: turbo target reply with null "
				    "associated request moving data");
				/* NOTREACHED */
			}
			if (ccb == NULL) {
				if (tgt->is_local == 0) {
					panic("mpt: turbo target reply with "
					    "null associated ccb moving data");
					/* NOTREACHED */
				}
				mpt_lprt(mpt, MPT_PRT_DEBUG,
				    "TARGET_ASSIST local done\n");
				TAILQ_REMOVE(&mpt->request_pending_list,
				    tgt->req, links);
				mpt_free_request(mpt, tgt->req);
				tgt->req = NULL;
				mpt_scsi_tgt_status(mpt, NULL, req,
				    0, NULL, 0);
				return (TRUE);
			}
			tgt->ccb = NULL;
			tgt->nxfers++;
			mpt_req_untimeout(tgt->req, mpt_timeout, ccb);
			mpt_lprt(mpt, MPT_PRT_DEBUG,
			    "TARGET_ASSIST %p (req %p:%u) done tag 0x%x\n",
			    ccb, tgt->req, tgt->req->serno, ccb->csio.tag_id);
			/*
			 * Free the Target Assist Request
			 */
			KASSERT(tgt->req->ccb == ccb,
			    ("tgt->req %p:%u tgt->req->ccb %p", tgt->req,
			    tgt->req->serno, tgt->req->ccb));
			TAILQ_REMOVE(&mpt->request_pending_list,
			    tgt->req, links);
			mpt_free_request(mpt, tgt->req);
			tgt->req = NULL;

			/*
			 * Do we need to send status now? That is, are
			 * we done with all our data transfers?
			 */
			if ((ccb->ccb_h.flags & CAM_SEND_STATUS) == 0) {
				mpt_set_ccb_status(ccb, CAM_REQ_CMP);
				ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
				KASSERT(ccb->ccb_h.status,
				    ("zero ccb sts at %d", __LINE__));
				tgt->state = TGT_STATE_IN_CAM;
				if (mpt->outofbeer) {
					ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
					mpt->outofbeer = 0;
					mpt_lprt(mpt, MPT_PRT_DEBUG, "THAWQ\n");
				}
				xpt_done(ccb);
				break;
			}
			/*
			 * Otherwise, send status (and sense)
			 */
			mpt_scsi_tgt_status(mpt, ccb, req,
			    ccb->csio.scsi_status,
			    (void *)&ccb->csio.sense_data,
			    (ccb->ccb_h.flags & CAM_SEND_SENSE) ?
			     ccb->csio.sense_len : 0);
			break;
		}
		case TGT_STATE_SENDING_STATUS:
		case TGT_STATE_MOVING_DATA_AND_STATUS:
		{
			int ioindex;
			ccb = tgt->ccb;

			if (tgt->req == NULL) {
				panic("mpt: turbo target reply with null "
				    "associated request sending status");
				/* NOTREACHED */
			}

			if (ccb) {
				tgt->ccb = NULL;
				if (tgt->state ==
				    TGT_STATE_MOVING_DATA_AND_STATUS) {
					tgt->nxfers++;
				}
				mpt_req_untimeout(tgt->req, mpt_timeout, ccb);
				if (ccb->ccb_h.flags & CAM_SEND_SENSE) {
					ccb->ccb_h.status |= CAM_SENT_SENSE;
				}
				mpt_lprt(mpt, MPT_PRT_DEBUG,
				    "TARGET_STATUS tag %x sts %x flgs %x req "
				    "%p\n", ccb->csio.tag_id, ccb->ccb_h.status,
				    ccb->ccb_h.flags, tgt->req);
				/*
				 * Free the Target Send Status Request
				 */
				KASSERT(tgt->req->ccb == ccb,
				    ("tgt->req %p:%u tgt->req->ccb %p",
				    tgt->req, tgt->req->serno, tgt->req->ccb));
				/*
				 * Notify CAM that we're done
				 */
				mpt_set_ccb_status(ccb, CAM_REQ_CMP);
				ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
				KASSERT(ccb->ccb_h.status,
				    ("ZERO ccb sts at %d", __LINE__));
				tgt->ccb = NULL;
			} else {
				mpt_lprt(mpt, MPT_PRT_DEBUG,
				    "TARGET_STATUS non-CAM for req %p:%u\n",
				    tgt->req, tgt->req->serno);
			}
			TAILQ_REMOVE(&mpt->request_pending_list,
			    tgt->req, links);
			mpt_free_request(mpt, tgt->req);
			tgt->req = NULL;

			/*
			 * And re-post the Command Buffer.
			 * This will reset the state.
			 */
			ioindex = GET_IO_INDEX(reply_desc);
			TAILQ_REMOVE(&mpt->request_pending_list, req, links);
			tgt->is_local = 0;
			mpt_post_target_command(mpt, req, ioindex);

			/*
			 * And post a done for anyone who cares
			 */
			if (ccb) {
				if (mpt->outofbeer) {
					ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
					mpt->outofbeer = 0;
					mpt_lprt(mpt, MPT_PRT_DEBUG, "THAWQ\n");
				}
				xpt_done(ccb);
			}
			break;
		}
		case TGT_STATE_NIL:	/* XXX This Never Happens XXX */
			tgt->state = TGT_STATE_LOADED;
			break;
		default:
			mpt_prt(mpt, "Unknown Target State 0x%x in Context "
			    "Reply Function\n", tgt->state);
		}
		return (TRUE);
	}

	status = le16toh(reply_frame->IOCStatus);
	if (status != MPI_IOCSTATUS_SUCCESS) {
		dbg = MPT_PRT_ERROR;
	} else {
		dbg = MPT_PRT_DEBUG1;
	}

	mpt_lprt(mpt, dbg,
	    "SCSI_TGT REPLY: req=%p:%u reply=%p func=%x IOCstatus 0x%x\n",
	     req, req->serno, reply_frame, reply_frame->Function, status);

	switch (reply_frame->Function) {
	case MPI_FUNCTION_TARGET_CMD_BUFFER_POST:
	{
		mpt_tgt_state_t *tgt;
#ifdef	INVARIANTS
		mpt_req_spcl(mpt, req, "tgt reply BUFFER POST", __LINE__);
#endif
		if (status != MPI_IOCSTATUS_SUCCESS) {
			/*
			 * XXX What to do?
			 */
			break;
		}
		tgt = MPT_TGT_STATE(mpt, req);
		KASSERT(tgt->state == TGT_STATE_LOADING,
		    ("bad state 0x%x on reply to buffer post", tgt->state));
		mpt_assign_serno(mpt, req);
		tgt->state = TGT_STATE_LOADED;
		break;
	}
	case MPI_FUNCTION_TARGET_ASSIST:
#ifdef	INVARIANTS
		mpt_req_not_spcl(mpt, req, "tgt reply TARGET ASSIST", __LINE__);
#endif
		mpt_prt(mpt, "target assist completion\n");
		TAILQ_REMOVE(&mpt->request_pending_list, req, links);
		mpt_free_request(mpt, req);
		break;
	case MPI_FUNCTION_TARGET_STATUS_SEND:
#ifdef	INVARIANTS
		mpt_req_not_spcl(mpt, req, "tgt reply STATUS SEND", __LINE__);
#endif
		mpt_prt(mpt, "status send completion\n");
		TAILQ_REMOVE(&mpt->request_pending_list, req, links);
		mpt_free_request(mpt, req);
		break;
	case MPI_FUNCTION_TARGET_MODE_ABORT:
	{
		PTR_MSG_TARGET_MODE_ABORT_REPLY abtrp =
		    (PTR_MSG_TARGET_MODE_ABORT_REPLY) reply_frame;
		PTR_MSG_TARGET_MODE_ABORT abtp =
		    (PTR_MSG_TARGET_MODE_ABORT) req->req_vbuf;
		uint32_t cc = GET_IO_INDEX(le32toh(abtp->ReplyWord));
#ifdef	INVARIANTS
		mpt_req_not_spcl(mpt, req, "tgt reply TMODE ABORT", __LINE__);
#endif
		mpt_prt(mpt, "ABORT RX_ID 0x%x Complete; status 0x%x cnt %u\n",
		    cc, le16toh(abtrp->IOCStatus), le32toh(abtrp->AbortCount));
		TAILQ_REMOVE(&mpt->request_pending_list, req, links);
		mpt_free_request(mpt, req);
		break;
	}
	default:
		mpt_prt(mpt, "Unknown Target Address Reply Function code: "
		    "0x%x\n", reply_frame->Function);
		break;
	}
	return (TRUE);
}
