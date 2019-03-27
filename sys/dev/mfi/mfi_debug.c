/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 IronPort Systems
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_mfi.h"

#ifdef MFI_DEBUG

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/bio.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/uio.h>
#include <machine/resource.h>
#include <machine/bus.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/mfi/mfireg.h>
#include <dev/mfi/mfi_ioctl.h>
#include <dev/mfi/mfivar.h>

static void
mfi_print_frame_flags(device_t dev, uint32_t flags)
{
	device_printf(dev, "flags=%b\n", flags, MFI_FRAME_FMT);
}

static void
mfi_print_sgl(struct mfi_frame_header *hdr, union mfi_sgl *sgl, int count)
{
	int i, columns = 0;

	printf("SG List:\n");
	for (i = 0; i < count; i++) {
		if (hdr->flags & MFI_FRAME_IEEE_SGL) {
			printf("0x%lx:%06d ", (u_long)sgl->sg_skinny[i].addr,
			    sgl->sg_skinny[i].len);
			columns += 26;
			if (columns > 77) {
				printf("\n");
				columns = 0;
			}
		} else if (hdr->flags & MFI_FRAME_SGL64) {
			printf("0x%lx:%06d ", (u_long)sgl->sg64[i].addr,
			    sgl->sg64[i].len);
			columns += 26;
			if (columns > 77) {
				printf("\n");
				columns = 0;
			}
		} else {
			printf("0x%x:%06d ", sgl->sg32[i].addr,
			    sgl->sg32[i].len);
			columns += 18;
			if (columns > 71) {
				printf("\n");
				columns = 0;
			}
		}
	}
	if (columns != 0)
		printf("\n");

}

static void
mfi_print_ldio(struct mfi_softc *sc, device_t dev, struct mfi_command *cm)
{
	struct mfi_io_frame *io;
	struct mfi_frame_header *hdr;

	io = &cm->cm_frame->io;
	hdr = &io->header;

	device_printf(dev, "cmd=%s target_id=%d sg_count=%d data_len=%d "
	    "lba=%d\n", (hdr->cmd == MFI_CMD_LD_READ) ? "LD_READ":"LD_WRITE",
	     hdr->target_id, hdr->sg_count, hdr->data_len, io->lba_lo);
	mfi_print_frame_flags(dev, hdr->flags);
	mfi_print_sgl(hdr, &io->sgl, hdr->sg_count);

}

static void
mfi_print_dcmd(struct mfi_softc *sc, device_t dev, struct mfi_command *cm)
{
	struct mfi_dcmd_frame *dcmd;
	struct mfi_frame_header *hdr;
	const char *opcode;

	dcmd = &cm->cm_frame->dcmd;
	hdr = &dcmd->header;

	switch (dcmd->opcode) {
	case MFI_DCMD_CTRL_GETINFO:
		opcode = "CTRL_GETINFO";
		break;
	case MFI_DCMD_CTRL_FLUSHCACHE:
		opcode = "CTRL_FLUSHCACHE";
		break;
	case MFI_DCMD_CTRL_SHUTDOWN:
		opcode = "CTRL_SHUTDOWN";
		break;
	case MFI_DCMD_CTRL_EVENT_GETINFO:
		opcode = "EVENT_GETINFO";
		break;
	case MFI_DCMD_CTRL_EVENT_GET:
		opcode = "EVENT_GET";
		break;
	case MFI_DCMD_CTRL_EVENT_WAIT:
		opcode = "EVENT_WAIT";
		break;
	case MFI_DCMD_LD_GET_LIST:
		opcode = "LD_GET_LIST";
		break;
	case MFI_DCMD_LD_GET_INFO:
		opcode = "LD_GET_INFO";
		break;
	case MFI_DCMD_LD_GET_PROP:
		opcode = "LD_GET_PROP";
		break;
	case MFI_DCMD_LD_SET_PROP:
		opcode = "LD_SET_PROP";
		break;
	case MFI_DCMD_CLUSTER:
		opcode = "CLUSTER";
		break;
	case MFI_DCMD_CLUSTER_RESET_ALL:
		opcode = "CLUSTER_RESET_ALL";
		break;
	case MFI_DCMD_CLUSTER_RESET_LD:
		opcode = "CLUSTER_RESET_LD";
		break;
	case MFI_DCMD_LD_MAP_GET_INFO:
		opcode = "LD_MAP_GET_INFO";
		break;
	case MFI_DCMD_BBU_START_LEARN:
		opcode = "BBU_START_LEARN";
		break;
	case MFI_DCMD_BBU_GET_PROP:
		opcode = "BBU_GET_PROP";
		break;
	case MFI_DCMD_BBU_SET_PROP:
		opcode = "BBU_SET_PROP";
		break;
	default:
		opcode = "UNKNOWN";
		break;
	}

	device_printf(dev, "cmd=MFI_CMD_DCMD opcode=%s data_len=%d\n",
	    opcode, hdr->data_len);
	mfi_print_frame_flags(dev, hdr->flags);
	mfi_print_sgl(hdr, &dcmd->sgl, hdr->sg_count);

}

static void
mfi_print_generic_frame(struct mfi_softc *sc, struct mfi_command *cm)
{
	hexdump(cm->cm_frame, cm->cm_total_frame_size, NULL, HD_OMIT_CHARS);
}

void
mfi_print_cmd(struct mfi_command *cm)
{
	device_t dev;
	struct mfi_softc *sc;

	sc = cm->cm_sc;
	dev = sc->mfi_dev;

	device_printf(dev, "cm=%p index=%d total_frame_size=%d "
	    "extra_frames=%d\n", cm, cm->cm_index, cm->cm_total_frame_size,
	    cm->cm_extra_frames);
	device_printf(dev, "flags=%b\n", cm->cm_flags, MFI_CMD_FLAGS_FMT);

	switch (cm->cm_frame->header.cmd) {
	case MFI_CMD_DCMD:
		mfi_print_dcmd(sc, dev, cm);
		break;
	case MFI_CMD_LD_READ:
	case MFI_CMD_LD_WRITE:
		mfi_print_ldio(sc, dev, cm);
		break;
	default:
		mfi_print_generic_frame(sc, cm);
		break;
	}

	return;
}

void
mfi_dump_cmds(struct mfi_softc *sc)
{
	int i;

	for (i = 0; i < sc->mfi_max_fw_cmds; i++)
		mfi_print_generic_frame(sc, &sc->mfi_commands[i]);
}

void
mfi_validate_sg(struct mfi_softc *sc, struct mfi_command *cm,
	const char *function, int line)
{
	struct mfi_frame_header *hdr;
	int i;
	uint32_t count = 0, data_len;

	hdr = &cm->cm_frame->header;
	count = 0;
	for (i = 0; i < hdr->sg_count; i++) {
		if (hdr->flags & MFI_FRAME_IEEE_SGL)
			count += cm->cm_sg->sg_skinny[i].len;
		else if (hdr->flags & MFI_FRAME_SGL64)
			count += cm->cm_sg->sg64[i].len;
		else
			count += cm->cm_sg->sg32[i].len;
	}
	/*
	count++;
	*/
	data_len = hdr->data_len;
	switch (hdr->cmd) {
	case MFI_CMD_LD_READ:
	case MFI_CMD_LD_WRITE:
		data_len = data_len * 512;
	case MFI_CMD_DCMD:
		if (count != data_len) {
			device_printf(sc->mfi_dev,
			    "%s %d COMMAND %p S/G count bad %d %d %d 0x%jx\n",
			    function, line, cm, count, data_len, cm->cm_len,
			    (intmax_t)pmap_kextract((vm_offset_t)cm->cm_data));
			MFI_PRINT_CMD(cm);
		}
	}
}

#endif
