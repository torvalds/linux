/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Netflix, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#ifdef _KERNEL
#include "opt_scsi.h"

#include <sys/systm.h>
#include <sys/libkern.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#else
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef min
#define min(a,b) (((a)<(b))?(a):(b))
#endif
#endif

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_queue.h>
#include <cam/cam_xpt.h>
#include <cam/nvme/nvme_all.h>
#include <sys/sbuf.h>
#include <sys/endian.h>

#ifdef _KERNEL
#include <cam/cam_periph.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_xpt_internal.h>
#endif

void
nvme_ns_cmd(struct ccb_nvmeio *nvmeio, uint8_t cmd, uint32_t nsid,
    uint32_t cdw10, uint32_t cdw11, uint32_t cdw12, uint32_t cdw13,
    uint32_t cdw14, uint32_t cdw15)
{
	bzero(&nvmeio->cmd, sizeof(struct nvme_command));
	nvmeio->cmd.opc = cmd;
	nvmeio->cmd.nsid = htole32(nsid);
	nvmeio->cmd.cdw10 = htole32(cdw10);
	nvmeio->cmd.cdw11 = htole32(cdw11);
	nvmeio->cmd.cdw12 = htole32(cdw12);
	nvmeio->cmd.cdw13 = htole32(cdw13);
	nvmeio->cmd.cdw14 = htole32(cdw14);
	nvmeio->cmd.cdw15 = htole32(cdw15);
}

int
nvme_identify_match(caddr_t identbuffer, caddr_t table_entry)
{
	return 0;
}


void
nvme_print_ident(const struct nvme_controller_data *cdata,
    const struct nvme_namespace_data *data, struct sbuf *sb)
{

	sbuf_printf(sb, "<");
	cam_strvis_sbuf(sb, cdata->mn, sizeof(cdata->mn), 0);
	sbuf_printf(sb, " ");
	cam_strvis_sbuf(sb, cdata->fr, sizeof(cdata->fr), 0);
	sbuf_printf(sb, " ");
	cam_strvis_sbuf(sb, cdata->sn, sizeof(cdata->sn), 0);
	sbuf_printf(sb, ">\n");
}

/* XXX need to do nvme admin opcodes too, but those aren't used yet by nda */
static const char *
nvme_opc2str[] = {
	"FLUSH",
	"WRITE",
	"READ",
	"RSVD-3",
	"WRITE_UNCORRECTABLE",
	"COMPARE",
	"RSVD-6",
	"RSVD-7",
	"DATASET_MANAGEMENT"
};

const char *
nvme_op_string(const struct nvme_command *cmd)
{

	if (cmd->opc >= nitems(nvme_opc2str))
		return "UNKNOWN";

	return nvme_opc2str[cmd->opc];
}

const char *
nvme_cmd_string(const struct nvme_command *cmd, char *cmd_string, size_t len)
{

	/*
	 * cid, rsvd areas and mptr not printed, since they are used
	 * only internally by the SIM.
	 */
	snprintf(cmd_string, len,
	    "opc=%x fuse=%x nsid=%x prp1=%llx prp2=%llx cdw=%x %x %x %x %x %x",
	    cmd->opc, cmd->fuse, cmd->nsid,
	    (unsigned long long)cmd->prp1, (unsigned long long)cmd->prp2,
	    cmd->cdw10, cmd->cdw11, cmd->cdw12,
	    cmd->cdw13, cmd->cdw14, cmd->cdw15);

	return cmd_string;
}

const void *
nvme_get_identify_cntrl(struct cam_periph *periph)
{
	struct cam_ed *device;

	device = periph->path->device;

	return device->nvme_cdata;
}

const void *
nvme_get_identify_ns(struct cam_periph *periph)
{
	struct cam_ed *device;

	device = periph->path->device;

	return device->nvme_data;
}
