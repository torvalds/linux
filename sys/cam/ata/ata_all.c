/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
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
#include <sys/ata.h>
#include <cam/ata/ata_all.h>
#include <sys/sbuf.h>
#include <sys/endian.h>

int
ata_version(int ver)
{
	int bit;

	if (ver == 0xffff)
		return 0;
	for (bit = 15; bit >= 0; bit--)
		if (ver & (1<<bit))
			return bit;
	return 0;
}

char *
ata_op_string(struct ata_cmd *cmd)
{

	if (cmd->control & 0x04)
		return ("SOFT_RESET");
	switch (cmd->command) {
	case 0x00:
		switch (cmd->features) {
		case 0x00: return ("NOP FLUSHQUEUE");
		case 0x01: return ("NOP AUTOPOLL");
		}
		return ("NOP");
	case 0x03: return ("CFA_REQUEST_EXTENDED_ERROR");
	case 0x06:
		switch (cmd->features) {
		case 0x01: return ("DSM TRIM");
		}
		return "DSM";
	case 0x08: return ("DEVICE_RESET");
	case 0x0b: return ("REQUEST_SENSE_DATA_EXT");
	case 0x20: return ("READ");
	case 0x24: return ("READ48");
	case 0x25: return ("READ_DMA48");
	case 0x26: return ("READ_DMA_QUEUED48");
	case 0x27: return ("READ_NATIVE_MAX_ADDRESS48");
	case 0x29: return ("READ_MUL48");
	case 0x2a: return ("READ_STREAM_DMA48");
	case 0x2b: return ("READ_STREAM48");
	case 0x2f: return ("READ_LOG_EXT");
	case 0x30: return ("WRITE");
	case 0x34: return ("WRITE48");
	case 0x35: return ("WRITE_DMA48");
	case 0x36: return ("WRITE_DMA_QUEUED48");
	case 0x37: return ("SET_MAX_ADDRESS48");
	case 0x39: return ("WRITE_MUL48");
	case 0x3a: return ("WRITE_STREAM_DMA48");
	case 0x3b: return ("WRITE_STREAM48");
	case 0x3d: return ("WRITE_DMA_FUA48");
	case 0x3e: return ("WRITE_DMA_QUEUED_FUA48");
	case 0x3f: return ("WRITE_LOG_EXT");
	case 0x40: return ("READ_VERIFY");
	case 0x42: return ("READ_VERIFY48");
	case 0x44: return ("ZERO_EXT");
	case 0x45:
		switch (cmd->features) {
		case 0x55: return ("WRITE_UNCORRECTABLE48 PSEUDO");
		case 0xaa: return ("WRITE_UNCORRECTABLE48 FLAGGED");
		}
		return "WRITE_UNCORRECTABLE48";
	case 0x47: return ("READ_LOG_DMA_EXT");
	case 0x4a: return ("ZAC_MANAGEMENT_IN");
	case 0x51: return ("CONFIGURE_STREAM");
	case 0x57: return ("WRITE_LOG_DMA_EXT");
	case 0x5b: return ("TRUSTED_NON_DATA");
	case 0x5c: return ("TRUSTED_RECEIVE");
	case 0x5d: return ("TRUSTED_RECEIVE_DMA");
	case 0x5e: return ("TRUSTED_SEND");
	case 0x5f: return ("TRUSTED_SEND_DMA");
	case 0x60: return ("READ_FPDMA_QUEUED");
	case 0x61: return ("WRITE_FPDMA_QUEUED");
	case 0x63:
		switch (cmd->features & 0xf) {
		case 0x00: return ("NCQ_NON_DATA ABORT NCQ QUEUE");
		case 0x01: return ("NCQ_NON_DATA DEADLINE HANDLING");
		case 0x05: return ("NCQ_NON_DATA SET FEATURES");
		/*
		 * XXX KDM need common decoding between NCQ and non-NCQ
		 * versions of SET FEATURES.
		 */
		case 0x06: return ("NCQ_NON_DATA ZERO EXT");
		case 0x07: return ("NCQ_NON_DATA ZAC MANAGEMENT OUT");
		}
		return ("NCQ_NON_DATA");
	case 0x64:
		switch (cmd->sector_count_exp & 0xf) {
		case 0x00: return ("SEND_FPDMA_QUEUED DATA SET MANAGEMENT");
		case 0x02: return ("SEND_FPDMA_QUEUED WRITE LOG DMA EXT");
		case 0x03: return ("SEND_FPDMA_QUEUED ZAC MANAGEMENT OUT");
		case 0x04: return ("SEND_FPDMA_QUEUED DATA SET MANAGEMENT XL");
		}
		return ("SEND_FPDMA_QUEUED");
	case 0x65:
		switch (cmd->sector_count_exp & 0xf) {
		case 0x01: return ("RECEIVE_FPDMA_QUEUED READ LOG DMA EXT");
		case 0x02: return ("RECEIVE_FPDMA_QUEUED ZAC MANAGEMENT IN");
		}
		return ("RECEIVE_FPDMA_QUEUED");
	case 0x67:
		if (cmd->features == 0xec)
			return ("SEP_ATTN IDENTIFY");
		switch (cmd->lba_low) {
		case 0x00: return ("SEP_ATTN READ BUFFER");
		case 0x02: return ("SEP_ATTN RECEIVE DIAGNOSTIC RESULTS");
		case 0x80: return ("SEP_ATTN WRITE BUFFER");
		case 0x82: return ("SEP_ATTN SEND DIAGNOSTIC");
		}
		return ("SEP_ATTN");
	case 0x70: return ("SEEK");
	case 0x77: return ("SET_DATE_TIME_EXT");
	case 0x78: return ("ACCESSIBLE_MAX_ADDRESS_CONFIGURATION");
	case 0x87: return ("CFA_TRANSLATE_SECTOR");
	case 0x90: return ("EXECUTE_DEVICE_DIAGNOSTIC");
	case 0x92: return ("DOWNLOAD_MICROCODE");
	case 0x93: return ("DOWNLOAD_MICROCODE_DMA");
	case 0x9a: return ("ZAC_MANAGEMENT_OUT");
	case 0xa0: return ("PACKET");
	case 0xa1: return ("ATAPI_IDENTIFY");
	case 0xa2: return ("SERVICE");
	case 0xb0:
		switch(cmd->features) {
		case 0xd0: return ("SMART READ ATTR VALUES");
		case 0xd1: return ("SMART READ ATTR THRESHOLDS");
		case 0xd3: return ("SMART SAVE ATTR VALUES");
		case 0xd4: return ("SMART EXECUTE OFFLINE IMMEDIATE");
		case 0xd5: return ("SMART READ LOG DATA");
		case 0xd8: return ("SMART ENABLE OPERATION");
		case 0xd9: return ("SMART DISABLE OPERATION");
		case 0xda: return ("SMART RETURN STATUS");
		}
		return ("SMART");
	case 0xb1: return ("DEVICE CONFIGURATION");
	case 0xb4: return ("SANITIZE_DEVICE");
	case 0xc0: return ("CFA_ERASE");
	case 0xc4: return ("READ_MUL");
	case 0xc5: return ("WRITE_MUL");
	case 0xc6: return ("SET_MULTI");
	case 0xc7: return ("READ_DMA_QUEUED");
	case 0xc8: return ("READ_DMA");
	case 0xca: return ("WRITE_DMA");
	case 0xcc: return ("WRITE_DMA_QUEUED");
	case 0xcd: return ("CFA_WRITE_MULTIPLE_WITHOUT_ERASE");
	case 0xce: return ("WRITE_MUL_FUA48");
	case 0xd1: return ("CHECK_MEDIA_CARD_TYPE");
	case 0xda: return ("GET_MEDIA_STATUS");
	case 0xde: return ("MEDIA_LOCK");
	case 0xdf: return ("MEDIA_UNLOCK");
	case 0xe0: return ("STANDBY_IMMEDIATE");
	case 0xe1: return ("IDLE_IMMEDIATE");
	case 0xe2: return ("STANDBY");
	case 0xe3: return ("IDLE");
	case 0xe4: return ("READ_BUFFER/PM");
	case 0xe5: return ("CHECK_POWER_MODE");
	case 0xe6: return ("SLEEP");
	case 0xe7: return ("FLUSHCACHE");
	case 0xe8: return ("WRITE_PM");
	case 0xea: return ("FLUSHCACHE48");
	case 0xec: return ("ATA_IDENTIFY");
	case 0xed: return ("MEDIA_EJECT");
	case 0xef:
		/*
		 * XXX KDM need common decoding between NCQ and non-NCQ
		 * versions of SET FEATURES.
		 */
		switch (cmd->features) {
	        case 0x02: return ("SETFEATURES ENABLE WCACHE");
	        case 0x03: return ("SETFEATURES SET TRANSFER MODE");
		case 0x04: return ("SETFEATURES ENABLE APM");
	        case 0x06: return ("SETFEATURES ENABLE PUIS");
	        case 0x07: return ("SETFEATURES SPIN-UP");
		case 0x0b: return ("SETFEATURES ENABLE WRITE READ VERIFY");
		case 0x0c: return ("SETFEATURES ENABLE DEVICE LIFE CONTROL");
	        case 0x10: return ("SETFEATURES ENABLE SATA FEATURE");
		case 0x41: return ("SETFEATURES ENABLE FREEFALL CONTROL");
		case 0x43: return ("SETFEATURES SET MAX HOST INT SECT TIMES");
		case 0x45: return ("SETFEATURES SET RATE BASIS");
		case 0x4a: return ("SETFEATURES EXTENDED POWER CONDITIONS");
	        case 0x55: return ("SETFEATURES DISABLE RCACHE");
		case 0x5d: return ("SETFEATURES ENABLE RELIRQ");
		case 0x5e: return ("SETFEATURES ENABLE SRVIRQ");
		case 0x62: return ("SETFEATURES LONG PHYS SECT ALIGN ERC");
		case 0x63: return ("SETFEATURES DSN");
		case 0x66: return ("SETFEATURES DISABLE DEFAULTS");
	        case 0x82: return ("SETFEATURES DISABLE WCACHE");
	        case 0x85: return ("SETFEATURES DISABLE APM");
	        case 0x86: return ("SETFEATURES DISABLE PUIS");
		case 0x8b: return ("SETFEATURES DISABLE WRITE READ VERIFY");
		case 0x8c: return ("SETFEATURES DISABLE DEVICE LIFE CONTROL");
	        case 0x90: return ("SETFEATURES DISABLE SATA FEATURE");
	        case 0xaa: return ("SETFEATURES ENABLE RCACHE");
		case 0xC1: return ("SETFEATURES DISABLE FREEFALL CONTROL");
		case 0xC3: return ("SETFEATURES SENSE DATA REPORTING");
		case 0xC4: return ("SETFEATURES NCQ SENSE DATA RETURN");
		case 0xCC: return ("SETFEATURES ENABLE DEFAULTS");
		case 0xdd: return ("SETFEATURES DISABLE RELIRQ");
		case 0xde: return ("SETFEATURES DISABLE SRVIRQ");
	        }
	        return "SETFEATURES";
	case 0xf1: return ("SECURITY_SET_PASSWORD");
	case 0xf2: return ("SECURITY_UNLOCK");
	case 0xf3: return ("SECURITY_ERASE_PREPARE");
	case 0xf4: return ("SECURITY_ERASE_UNIT");
	case 0xf5: return ("SECURITY_FREEZE_LOCK");
	case 0xf6: return ("SECURITY_DISABLE_PASSWORD");
	case 0xf8: return ("READ_NATIVE_MAX_ADDRESS");
	case 0xf9: return ("SET_MAX_ADDRESS");
	}
	return "UNKNOWN";
}

char *
ata_cmd_string(struct ata_cmd *cmd, char *cmd_string, size_t len)
{
	struct sbuf sb;
	int error;

	if (len == 0)
		return ("");

	sbuf_new(&sb, cmd_string, len, SBUF_FIXEDLEN);
	ata_cmd_sbuf(cmd, &sb);

	error = sbuf_finish(&sb);
	if (error != 0 && error != ENOMEM)
		return ("");

	return(sbuf_data(&sb));
}

void
ata_cmd_sbuf(struct ata_cmd *cmd, struct sbuf *sb)
{
	sbuf_printf(sb, "%02x %02x %02x %02x "
	    "%02x %02x %02x %02x %02x %02x %02x %02x",
	    cmd->command, cmd->features,
	    cmd->lba_low, cmd->lba_mid, cmd->lba_high, cmd->device,
	    cmd->lba_low_exp, cmd->lba_mid_exp, cmd->lba_high_exp,
	    cmd->features_exp, cmd->sector_count, cmd->sector_count_exp);
}

char *
ata_res_string(struct ata_res *res, char *res_string, size_t len)
{
	struct sbuf sb;
	int error;

	if (len == 0)
		return ("");

	sbuf_new(&sb, res_string, len, SBUF_FIXEDLEN);
	ata_res_sbuf(res, &sb);

	error = sbuf_finish(&sb);
	if (error != 0 && error != ENOMEM)
		return ("");

	return(sbuf_data(&sb));
}

int
ata_res_sbuf(struct ata_res *res, struct sbuf *sb)
{

	sbuf_printf(sb, "%02x %02x %02x %02x "
	    "%02x %02x %02x %02x %02x %02x %02x",
	    res->status, res->error,
	    res->lba_low, res->lba_mid, res->lba_high, res->device,
	    res->lba_low_exp, res->lba_mid_exp, res->lba_high_exp,
	    res->sector_count, res->sector_count_exp);

	return (0);
}

/*
 * ata_command_sbuf() returns 0 for success and -1 for failure.
 */
int
ata_command_sbuf(struct ccb_ataio *ataio, struct sbuf *sb)
{

	sbuf_printf(sb, "%s. ACB: ",
	    ata_op_string(&ataio->cmd));
	ata_cmd_sbuf(&ataio->cmd, sb);

	return(0);
}

/*
 * ata_status_abuf() returns 0 for success and -1 for failure.
 */
int
ata_status_sbuf(struct ccb_ataio *ataio, struct sbuf *sb)
{

	sbuf_printf(sb, "ATA status: %02x (%s%s%s%s%s%s%s%s)",
	    ataio->res.status,
	    (ataio->res.status & 0x80) ? "BSY " : "",
	    (ataio->res.status & 0x40) ? "DRDY " : "",
	    (ataio->res.status & 0x20) ? "DF " : "",
	    (ataio->res.status & 0x10) ? "SERV " : "",
	    (ataio->res.status & 0x08) ? "DRQ " : "",
	    (ataio->res.status & 0x04) ? "CORR " : "",
	    (ataio->res.status & 0x02) ? "IDX " : "",
	    (ataio->res.status & 0x01) ? "ERR" : "");
	if (ataio->res.status & 1) {
	    sbuf_printf(sb, ", error: %02x (%s%s%s%s%s%s%s%s)",
		ataio->res.error,
		(ataio->res.error & 0x80) ? "ICRC " : "",
		(ataio->res.error & 0x40) ? "UNC " : "",
		(ataio->res.error & 0x20) ? "MC " : "",
		(ataio->res.error & 0x10) ? "IDNF " : "",
		(ataio->res.error & 0x08) ? "MCR " : "",
		(ataio->res.error & 0x04) ? "ABRT " : "",
		(ataio->res.error & 0x02) ? "NM " : "",
		(ataio->res.error & 0x01) ? "ILI" : "");
	}

	return(0);
}

void
ata_print_ident(struct ata_params *ident_data)
{
	const char *proto;
	char ata[12], sata[12];

	ata_print_ident_short(ident_data);

	proto = (ident_data->config == ATA_PROTO_CFA) ? "CFA" :
		(ident_data->config & ATA_PROTO_ATAPI) ? "ATAPI" : "ATA";
	if (ata_version(ident_data->version_major) == 0) {
		snprintf(ata, sizeof(ata), "%s", proto);
	} else if (ata_version(ident_data->version_major) <= 7) {
		snprintf(ata, sizeof(ata), "%s-%d", proto,
		    ata_version(ident_data->version_major));
	} else if (ata_version(ident_data->version_major) == 8) {
		snprintf(ata, sizeof(ata), "%s8-ACS", proto);
	} else {
		snprintf(ata, sizeof(ata), "ACS-%d %s",
		    ata_version(ident_data->version_major) - 7, proto);
	}
	if (ident_data->satacapabilities && ident_data->satacapabilities != 0xffff) {
		if (ident_data->satacapabilities & ATA_SATA_GEN3)
			snprintf(sata, sizeof(sata), " SATA 3.x");
		else if (ident_data->satacapabilities & ATA_SATA_GEN2)
			snprintf(sata, sizeof(sata), " SATA 2.x");
		else if (ident_data->satacapabilities & ATA_SATA_GEN1)
			snprintf(sata, sizeof(sata), " SATA 1.x");
		else
			snprintf(sata, sizeof(sata), " SATA");
	} else
		sata[0] = 0;
	printf(" %s%s device\n", ata, sata);
}

void
ata_print_ident_sbuf(struct ata_params *ident_data, struct sbuf *sb)
{
	const char *proto, *sata;
	int version;

	ata_print_ident_short_sbuf(ident_data, sb);
	sbuf_printf(sb, " ");

	proto = (ident_data->config == ATA_PROTO_CFA) ? "CFA" :
		(ident_data->config & ATA_PROTO_ATAPI) ? "ATAPI" : "ATA";
	version = ata_version(ident_data->version_major);

	switch (version) {
	case 0:
		sbuf_printf(sb, "%s", proto);
		break;
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		sbuf_printf(sb, "%s-%d", proto, version);
		break;
	case 8:
		sbuf_printf(sb, "%s8-ACS", proto);
		break;
	default:
		sbuf_printf(sb, "ACS-%d %s", version - 7, proto);
		break;
	}

	if (ident_data->satacapabilities && ident_data->satacapabilities != 0xffff) {
		if (ident_data->satacapabilities & ATA_SATA_GEN3)
			sata = " SATA 3.x";
		else if (ident_data->satacapabilities & ATA_SATA_GEN2)
			sata = " SATA 2.x";
		else if (ident_data->satacapabilities & ATA_SATA_GEN1)
			sata = " SATA 1.x";
		else
			sata = " SATA";
	} else
		sata = "";
	sbuf_printf(sb, "%s device\n", sata);
}

void
ata_print_ident_short(struct ata_params *ident_data)
{
	char product[48], revision[16];

	cam_strvis(product, ident_data->model, sizeof(ident_data->model),
		   sizeof(product));
	cam_strvis(revision, ident_data->revision, sizeof(ident_data->revision),
		   sizeof(revision));
	printf("<%s %s>", product, revision);
}

void
ata_print_ident_short_sbuf(struct ata_params *ident_data, struct sbuf *sb)
{

	sbuf_printf(sb, "<");
	cam_strvis_sbuf(sb, ident_data->model, sizeof(ident_data->model), 0);
	sbuf_printf(sb, " ");
	cam_strvis_sbuf(sb, ident_data->revision, sizeof(ident_data->revision), 0);
	sbuf_printf(sb, ">");
}

void
semb_print_ident(struct sep_identify_data *ident_data)
{
	char in[7], ins[5];

	semb_print_ident_short(ident_data);
	cam_strvis(in, ident_data->interface_id, 6, sizeof(in));
	cam_strvis(ins, ident_data->interface_rev, 4, sizeof(ins));
	printf(" SEMB %s %s device\n", in, ins);
}

void
semb_print_ident_sbuf(struct sep_identify_data *ident_data, struct sbuf *sb)
{

	semb_print_ident_short_sbuf(ident_data, sb);

	sbuf_printf(sb, " SEMB ");
	cam_strvis_sbuf(sb, ident_data->interface_id, 6, 0);
	sbuf_printf(sb, " ");
	cam_strvis_sbuf(sb, ident_data->interface_rev, 4, 0);
	sbuf_printf(sb, " device\n");
}

void
semb_print_ident_short(struct sep_identify_data *ident_data)
{
	char vendor[9], product[17], revision[5], fw[5];

	cam_strvis(vendor, ident_data->vendor_id, 8, sizeof(vendor));
	cam_strvis(product, ident_data->product_id, 16, sizeof(product));
	cam_strvis(revision, ident_data->product_rev, 4, sizeof(revision));
	cam_strvis(fw, ident_data->firmware_rev, 4, sizeof(fw));
	printf("<%s %s %s %s>", vendor, product, revision, fw);
}

void
semb_print_ident_short_sbuf(struct sep_identify_data *ident_data, struct sbuf *sb)
{

	sbuf_printf(sb, "<");
	cam_strvis_sbuf(sb, ident_data->vendor_id, 8, 0);
	sbuf_printf(sb, " ");
	cam_strvis_sbuf(sb, ident_data->product_id, 16, 0);
	sbuf_printf(sb, " ");
	cam_strvis_sbuf(sb, ident_data->product_rev, 4, 0);
	sbuf_printf(sb, " ");
	cam_strvis_sbuf(sb, ident_data->firmware_rev, 4, 0);
	sbuf_printf(sb, ">");
}

uint32_t
ata_logical_sector_size(struct ata_params *ident_data)
{
	if ((ident_data->pss & ATA_PSS_VALID_MASK) == ATA_PSS_VALID_VALUE &&
	    (ident_data->pss & ATA_PSS_LSSABOVE512)) {
		return (((u_int32_t)ident_data->lss_1 |
		    ((u_int32_t)ident_data->lss_2 << 16)) * 2);
	}
	return (512);
}

uint64_t
ata_physical_sector_size(struct ata_params *ident_data)
{
	if ((ident_data->pss & ATA_PSS_VALID_MASK) == ATA_PSS_VALID_VALUE) {
		if (ident_data->pss & ATA_PSS_MULTLS) {
			return ((uint64_t)ata_logical_sector_size(ident_data) *
			    (1 << (ident_data->pss & ATA_PSS_LSPPS)));
		} else {
			return (uint64_t)ata_logical_sector_size(ident_data);
		}
	}
	return (512);
}

uint64_t
ata_logical_sector_offset(struct ata_params *ident_data)
{
	if ((ident_data->lsalign & 0xc000) == 0x4000) {
		return ((uint64_t)ata_logical_sector_size(ident_data) *
		    (ident_data->lsalign & 0x3fff));
	}
	return (0);
}

void
ata_28bit_cmd(struct ccb_ataio *ataio, uint8_t cmd, uint8_t features,
    uint32_t lba, uint8_t sector_count)
{
	bzero(&ataio->cmd, sizeof(ataio->cmd));
	ataio->cmd.flags = 0;
	if (cmd == ATA_READ_DMA ||
	    cmd == ATA_READ_DMA_QUEUED ||
	    cmd == ATA_WRITE_DMA ||
	    cmd == ATA_WRITE_DMA_QUEUED)
		ataio->cmd.flags |= CAM_ATAIO_DMA;
	ataio->cmd.command = cmd;
	ataio->cmd.features = features;
	ataio->cmd.lba_low = lba;
	ataio->cmd.lba_mid = lba >> 8;
	ataio->cmd.lba_high = lba >> 16;
	ataio->cmd.device = ATA_DEV_LBA | ((lba >> 24) & 0x0f);
	ataio->cmd.sector_count = sector_count;
}

void
ata_48bit_cmd(struct ccb_ataio *ataio, uint8_t cmd, uint16_t features,
    uint64_t lba, uint16_t sector_count)
{

	ataio->cmd.flags = CAM_ATAIO_48BIT;
	if (cmd == ATA_READ_DMA48 ||
	    cmd == ATA_READ_DMA_QUEUED48 ||
	    cmd == ATA_READ_STREAM_DMA48 ||
	    cmd == ATA_WRITE_DMA48 ||
	    cmd == ATA_WRITE_DMA_FUA48 ||
	    cmd == ATA_WRITE_DMA_QUEUED48 ||
	    cmd == ATA_WRITE_DMA_QUEUED_FUA48 ||
	    cmd == ATA_WRITE_STREAM_DMA48 ||
	    cmd == ATA_DATA_SET_MANAGEMENT ||
	    cmd == ATA_READ_LOG_DMA_EXT)
		ataio->cmd.flags |= CAM_ATAIO_DMA;
	ataio->cmd.command = cmd;
	ataio->cmd.features = features;
	ataio->cmd.lba_low = lba;
	ataio->cmd.lba_mid = lba >> 8;
	ataio->cmd.lba_high = lba >> 16;
	ataio->cmd.device = ATA_DEV_LBA;
	ataio->cmd.lba_low_exp = lba >> 24;
	ataio->cmd.lba_mid_exp = lba >> 32;
	ataio->cmd.lba_high_exp = lba >> 40;
	ataio->cmd.features_exp = features >> 8;
	ataio->cmd.sector_count = sector_count;
	ataio->cmd.sector_count_exp = sector_count >> 8;
	ataio->cmd.control = 0;
}

void
ata_ncq_cmd(struct ccb_ataio *ataio, uint8_t cmd,
    uint64_t lba, uint16_t sector_count)
{

	ataio->cmd.flags = CAM_ATAIO_48BIT | CAM_ATAIO_FPDMA;
	ataio->cmd.command = cmd;
	ataio->cmd.features = sector_count;
	ataio->cmd.lba_low = lba;
	ataio->cmd.lba_mid = lba >> 8;
	ataio->cmd.lba_high = lba >> 16;
	ataio->cmd.device = ATA_DEV_LBA;
	ataio->cmd.lba_low_exp = lba >> 24;
	ataio->cmd.lba_mid_exp = lba >> 32;
	ataio->cmd.lba_high_exp = lba >> 40;
	ataio->cmd.features_exp = sector_count >> 8;
	ataio->cmd.sector_count = 0;
	ataio->cmd.sector_count_exp = 0;
	ataio->cmd.control = 0;
}

void
ata_reset_cmd(struct ccb_ataio *ataio)
{
	bzero(&ataio->cmd, sizeof(ataio->cmd));
	ataio->cmd.flags = CAM_ATAIO_CONTROL | CAM_ATAIO_NEEDRESULT;
	ataio->cmd.control = 0x04;
}

void
ata_pm_read_cmd(struct ccb_ataio *ataio, int reg, int port)
{
	bzero(&ataio->cmd, sizeof(ataio->cmd));
	ataio->cmd.flags = CAM_ATAIO_NEEDRESULT;
	ataio->cmd.command = ATA_READ_PM;
	ataio->cmd.features = reg;
	ataio->cmd.device = port & 0x0f;
}

void
ata_pm_write_cmd(struct ccb_ataio *ataio, int reg, int port, uint32_t val)
{
	bzero(&ataio->cmd, sizeof(ataio->cmd));
	ataio->cmd.flags = 0;
	ataio->cmd.command = ATA_WRITE_PM;
	ataio->cmd.features = reg;
	ataio->cmd.sector_count = val;
	ataio->cmd.lba_low = val >> 8;
	ataio->cmd.lba_mid = val >> 16;
	ataio->cmd.lba_high = val >> 24;
	ataio->cmd.device = port & 0x0f;
}

void
ata_read_log(struct ccb_ataio *ataio, uint32_t retries,
	     void (*cbfcnp)(struct cam_periph *, union ccb *),
	     uint32_t log_address, uint32_t page_number, uint16_t block_count,
	     uint32_t protocol, uint8_t *data_ptr, uint32_t dxfer_len,
	     uint32_t timeout)
{
	uint64_t lba;

	cam_fill_ataio(ataio,
	    /*retries*/ 1,
	    /*cbfcnp*/ cbfcnp,
	    /*flags*/ CAM_DIR_IN,
	    /*tag_action*/ 0,
	    /*data_ptr*/ data_ptr,
	    /*dxfer_len*/ dxfer_len,
	    /*timeout*/ timeout);

	lba = (((uint64_t)page_number & 0xff00) << 32) |
	      ((page_number & 0x00ff) << 8) |
	      (log_address & 0xff);

	ata_48bit_cmd(ataio,
	    /*cmd*/ (protocol & CAM_ATAIO_DMA) ? ATA_READ_LOG_DMA_EXT :
		     ATA_READ_LOG_EXT,
	    /*features*/ 0,
	    /*lba*/ lba,
	    /*sector_count*/ block_count);
}

void
ata_bswap(int8_t *buf, int len)
{
	u_int16_t *ptr = (u_int16_t*)(buf + len);

	while (--ptr >= (u_int16_t*)buf)
		*ptr = be16toh(*ptr);
}

void
ata_btrim(int8_t *buf, int len)
{
	int8_t *ptr;

	for (ptr = buf; ptr < buf+len; ++ptr)
		if (!*ptr || *ptr == '_')
			*ptr = ' ';
	for (ptr = buf + len - 1; ptr >= buf && *ptr == ' '; --ptr)
		*ptr = 0;
}

void
ata_bpack(int8_t *src, int8_t *dst, int len)
{
	int i, j, blank;

	for (i = j = blank = 0 ; i < len; i++) {
		if (blank && src[i] == ' ') continue;
		if (blank && src[i] != ' ') {
			dst[j++] = src[i];
			blank = 0;
			continue;
		}
		if (src[i] == ' ') {
			blank = 1;
			if (i == 0)
			continue;
		}
		dst[j++] = src[i];
	}
	while (j < len)
		dst[j++] = 0x00;
}

int
ata_max_pmode(struct ata_params *ap)
{
    if (ap->atavalid & ATA_FLAG_64_70) {
	if (ap->apiomodes & 0x02)
	    return ATA_PIO4;
	if (ap->apiomodes & 0x01)
	    return ATA_PIO3;
    }
    if (ap->mwdmamodes & 0x04)
	return ATA_PIO4;
    if (ap->mwdmamodes & 0x02)
	return ATA_PIO3;
    if (ap->mwdmamodes & 0x01)
	return ATA_PIO2;
    if ((ap->retired_piomode & ATA_RETIRED_PIO_MASK) == 0x200)
	return ATA_PIO2;
    if ((ap->retired_piomode & ATA_RETIRED_PIO_MASK) == 0x100)
	return ATA_PIO1;
    if ((ap->retired_piomode & ATA_RETIRED_PIO_MASK) == 0x000)
	return ATA_PIO0;
    return ATA_PIO0;
}

int
ata_max_wmode(struct ata_params *ap)
{
    if (ap->mwdmamodes & 0x04)
	return ATA_WDMA2;
    if (ap->mwdmamodes & 0x02)
	return ATA_WDMA1;
    if (ap->mwdmamodes & 0x01)
	return ATA_WDMA0;
    return -1;
}

int
ata_max_umode(struct ata_params *ap)
{
    if (ap->atavalid & ATA_FLAG_88) {
	if (ap->udmamodes & 0x40)
	    return ATA_UDMA6;
	if (ap->udmamodes & 0x20)
	    return ATA_UDMA5;
	if (ap->udmamodes & 0x10)
	    return ATA_UDMA4;
	if (ap->udmamodes & 0x08)
	    return ATA_UDMA3;
	if (ap->udmamodes & 0x04)
	    return ATA_UDMA2;
	if (ap->udmamodes & 0x02)
	    return ATA_UDMA1;
	if (ap->udmamodes & 0x01)
	    return ATA_UDMA0;
    }
    return -1;
}

int
ata_max_mode(struct ata_params *ap, int maxmode)
{

	if (maxmode == 0)
		maxmode = ATA_DMA_MAX;
	if (maxmode >= ATA_UDMA0 && ata_max_umode(ap) > 0)
		return (min(maxmode, ata_max_umode(ap)));
	if (maxmode >= ATA_WDMA0 && ata_max_wmode(ap) > 0)
		return (min(maxmode, ata_max_wmode(ap)));
	return (min(maxmode, ata_max_pmode(ap)));
}

char *
ata_mode2string(int mode)
{
    switch (mode) {
    case -1: return "UNSUPPORTED";
    case 0: return "NONE";
    case ATA_PIO0: return "PIO0";
    case ATA_PIO1: return "PIO1";
    case ATA_PIO2: return "PIO2";
    case ATA_PIO3: return "PIO3";
    case ATA_PIO4: return "PIO4";
    case ATA_WDMA0: return "WDMA0";
    case ATA_WDMA1: return "WDMA1";
    case ATA_WDMA2: return "WDMA2";
    case ATA_UDMA0: return "UDMA0";
    case ATA_UDMA1: return "UDMA1";
    case ATA_UDMA2: return "UDMA2";
    case ATA_UDMA3: return "UDMA3";
    case ATA_UDMA4: return "UDMA4";
    case ATA_UDMA5: return "UDMA5";
    case ATA_UDMA6: return "UDMA6";
    default:
	if (mode & ATA_DMA_MASK)
	    return "BIOSDMA";
	else
	    return "BIOSPIO";
    }
}

int
ata_string2mode(char *str)
{
	if (!strcasecmp(str, "PIO0")) return (ATA_PIO0);
	if (!strcasecmp(str, "PIO1")) return (ATA_PIO1);
	if (!strcasecmp(str, "PIO2")) return (ATA_PIO2);
	if (!strcasecmp(str, "PIO3")) return (ATA_PIO3);
	if (!strcasecmp(str, "PIO4")) return (ATA_PIO4);
	if (!strcasecmp(str, "WDMA0")) return (ATA_WDMA0);
	if (!strcasecmp(str, "WDMA1")) return (ATA_WDMA1);
	if (!strcasecmp(str, "WDMA2")) return (ATA_WDMA2);
	if (!strcasecmp(str, "UDMA0")) return (ATA_UDMA0);
	if (!strcasecmp(str, "UDMA16")) return (ATA_UDMA0);
	if (!strcasecmp(str, "UDMA1")) return (ATA_UDMA1);
	if (!strcasecmp(str, "UDMA25")) return (ATA_UDMA1);
	if (!strcasecmp(str, "UDMA2")) return (ATA_UDMA2);
	if (!strcasecmp(str, "UDMA33")) return (ATA_UDMA2);
	if (!strcasecmp(str, "UDMA3")) return (ATA_UDMA3);
	if (!strcasecmp(str, "UDMA44")) return (ATA_UDMA3);
	if (!strcasecmp(str, "UDMA4")) return (ATA_UDMA4);
	if (!strcasecmp(str, "UDMA66")) return (ATA_UDMA4);
	if (!strcasecmp(str, "UDMA5")) return (ATA_UDMA5);
	if (!strcasecmp(str, "UDMA100")) return (ATA_UDMA5);
	if (!strcasecmp(str, "UDMA6")) return (ATA_UDMA6);
	if (!strcasecmp(str, "UDMA133")) return (ATA_UDMA6);
	return (-1);
}


u_int
ata_mode2speed(int mode)
{
	switch (mode) {
	case ATA_PIO0:
	default:
		return (3300);
	case ATA_PIO1:
		return (5200);
	case ATA_PIO2:
		return (8300);
	case ATA_PIO3:
		return (11100);
	case ATA_PIO4:
		return (16700);
	case ATA_WDMA0:
		return (4200);
	case ATA_WDMA1:
		return (13300);
	case ATA_WDMA2:
		return (16700);
	case ATA_UDMA0:
		return (16700);
	case ATA_UDMA1:
		return (25000);
	case ATA_UDMA2:
		return (33300);
	case ATA_UDMA3:
		return (44400);
	case ATA_UDMA4:
		return (66700);
	case ATA_UDMA5:
		return (100000);
	case ATA_UDMA6:
		return (133000);
	}
}

u_int
ata_revision2speed(int revision)
{
	switch (revision) {
	case 1:
	default:
		return (150000);
	case 2:
		return (300000);
	case 3:
		return (600000);
	}
}

int
ata_speed2revision(u_int speed)
{
	switch (speed) {
	case 0:
		return (0);
	case 150000:
		return (1);
	case 300000:
		return (2);
	case 600000:
		return (3);
	default:
		return (-1);
	}
}

int
ata_identify_match(caddr_t identbuffer, caddr_t table_entry)
{
	struct scsi_inquiry_pattern *entry;
	struct ata_params *ident;
 
	entry = (struct scsi_inquiry_pattern *)table_entry;
	ident = (struct ata_params *)identbuffer;

	if ((cam_strmatch(ident->model, entry->product,
			  sizeof(ident->model)) == 0)
	 && (cam_strmatch(ident->revision, entry->revision,
			  sizeof(ident->revision)) == 0)) {
		return (0);
	}
        return (-1);
}

int
ata_static_identify_match(caddr_t identbuffer, caddr_t table_entry)
{
	struct scsi_static_inquiry_pattern *entry;
	struct ata_params *ident;
 
	entry = (struct scsi_static_inquiry_pattern *)table_entry;
	ident = (struct ata_params *)identbuffer;

	if ((cam_strmatch(ident->model, entry->product,
			  sizeof(ident->model)) == 0)
	 && (cam_strmatch(ident->revision, entry->revision,
			  sizeof(ident->revision)) == 0)) {
		return (0);
	}
        return (-1);
}

void
semb_receive_diagnostic_results(struct ccb_ataio *ataio,
    u_int32_t retries, void (*cbfcnp)(struct cam_periph *, union ccb*),
    uint8_t tag_action, int pcv, uint8_t page_code,
    uint8_t *data_ptr, uint16_t length, uint32_t timeout)
{

	length = min(length, 1020);
	length = (length + 3) & ~3;
	cam_fill_ataio(ataio,
		      retries,
		      cbfcnp,
		      /*flags*/CAM_DIR_IN,
		      tag_action,
		      data_ptr,
		      length,
		      timeout);
	ata_28bit_cmd(ataio, ATA_SEP_ATTN,
	    pcv ? page_code : 0, 0x02, length / 4);
}

void
semb_send_diagnostic(struct ccb_ataio *ataio,
    u_int32_t retries, void (*cbfcnp)(struct cam_periph *, union ccb *),
    uint8_t tag_action, uint8_t *data_ptr, uint16_t length, uint32_t timeout)
{

	length = min(length, 1020);
	length = (length + 3) & ~3;
	cam_fill_ataio(ataio,
		      retries,
		      cbfcnp,
		      /*flags*/length ? CAM_DIR_OUT : CAM_DIR_NONE,
		      tag_action,
		      data_ptr,
		      length,
		      timeout);
	ata_28bit_cmd(ataio, ATA_SEP_ATTN,
	    length > 0 ? data_ptr[0] : 0, 0x82, length / 4);
}

void
semb_read_buffer(struct ccb_ataio *ataio,
    u_int32_t retries, void (*cbfcnp)(struct cam_periph *, union ccb*),
    uint8_t tag_action, uint8_t page_code,
    uint8_t *data_ptr, uint16_t length, uint32_t timeout)
{

	length = min(length, 1020);
	length = (length + 3) & ~3;
	cam_fill_ataio(ataio,
		      retries,
		      cbfcnp,
		      /*flags*/CAM_DIR_IN,
		      tag_action,
		      data_ptr,
		      length,
		      timeout);
	ata_28bit_cmd(ataio, ATA_SEP_ATTN,
	    page_code, 0x00, length / 4);
}

void
semb_write_buffer(struct ccb_ataio *ataio,
    u_int32_t retries, void (*cbfcnp)(struct cam_periph *, union ccb *),
    uint8_t tag_action, uint8_t *data_ptr, uint16_t length, uint32_t timeout)
{

	length = min(length, 1020);
	length = (length + 3) & ~3;
	cam_fill_ataio(ataio,
		      retries,
		      cbfcnp,
		      /*flags*/length ? CAM_DIR_OUT : CAM_DIR_NONE,
		      tag_action,
		      data_ptr,
		      length,
		      timeout);
	ata_28bit_cmd(ataio, ATA_SEP_ATTN,
	    length > 0 ? data_ptr[0] : 0, 0x80, length / 4);
}


void
ata_zac_mgmt_out(struct ccb_ataio *ataio, uint32_t retries, 
		 void (*cbfcnp)(struct cam_periph *, union ccb *),
		 int use_ncq, uint8_t zm_action, uint64_t zone_id,
		 uint8_t zone_flags, uint16_t sector_count, uint8_t *data_ptr,
		 uint32_t dxfer_len, uint32_t timeout)
{
	uint8_t command_out, ata_flags;
	uint16_t features_out, sectors_out;
	uint32_t auxiliary;

	if (use_ncq == 0) {
		command_out = ATA_ZAC_MANAGEMENT_OUT;
		features_out = (zm_action & 0xf) | (zone_flags << 8);
		if (dxfer_len == 0) {
			ata_flags = 0;
			sectors_out = 0;
		} else {
			ata_flags = CAM_ATAIO_DMA;
			/* XXX KDM use sector count? */
			sectors_out = ((dxfer_len >> 9) & 0xffff);
		}
		auxiliary = 0;
	} else {
		if (dxfer_len == 0) {
			command_out = ATA_NCQ_NON_DATA;
			features_out = ATA_NCQ_ZAC_MGMT_OUT;
			sectors_out = 0;
		} else {
			command_out = ATA_SEND_FPDMA_QUEUED;

			/* Note that we're defaulting to normal priority */
			sectors_out = ATA_SFPDMA_ZAC_MGMT_OUT << 8;

			/*
			 * For SEND FPDMA QUEUED, the transfer length is
			 * encoded in the FEATURE register, and 0 means
			 * that 65536 512 byte blocks are to be tranferred.
			 * In practice, it seems unlikely that we'll see
			 * a transfer that large.
			 */
			if (dxfer_len == (65536 * 512)) {
				features_out = 0;
			} else {
				/*
				 * Yes, the caller can theoretically send a
				 * transfer larger than we can handle.
				 * Anyone using this function needs enough
				 * knowledge to avoid doing that.
				 */
				features_out = ((dxfer_len >> 9) & 0xffff);
			}
		}
		auxiliary = (zm_action & 0xf) | (zone_flags << 8);

		ata_flags = CAM_ATAIO_FPDMA;
	}

	cam_fill_ataio(ataio,
	    /*retries*/ retries,
	    /*cbfcnp*/ cbfcnp,
	    /*flags*/ (dxfer_len > 0) ? CAM_DIR_OUT : CAM_DIR_NONE,
	    /*tag_action*/ 0,
	    /*data_ptr*/ data_ptr,
	    /*dxfer_len*/ dxfer_len,
	    /*timeout*/ timeout);

	ata_48bit_cmd(ataio,
	    /*cmd*/ command_out,
	    /*features*/ features_out,
	    /*lba*/ zone_id,
	    /*sector_count*/ sectors_out);

	ataio->cmd.flags |= ata_flags;
	if (auxiliary != 0) {
		ataio->ata_flags |= ATA_FLAG_AUX;
		ataio->aux = auxiliary;
	}
}

void
ata_zac_mgmt_in(struct ccb_ataio *ataio, uint32_t retries, 
		void (*cbfcnp)(struct cam_periph *, union ccb *),
		int use_ncq, uint8_t zm_action, uint64_t zone_id,
		uint8_t zone_flags, uint8_t *data_ptr, uint32_t dxfer_len,
		uint32_t timeout)
{
	uint8_t command_out, ata_flags;
	uint16_t features_out, sectors_out;
	uint32_t auxiliary;

	if (use_ncq == 0) {
		command_out = ATA_ZAC_MANAGEMENT_IN;
		/* XXX KDM put a macro here */
		features_out = (zm_action & 0xf) | (zone_flags << 8);
		ata_flags = CAM_ATAIO_DMA;
		sectors_out = ((dxfer_len >> 9) & 0xffff);
		auxiliary = 0;
	} else {
		command_out = ATA_RECV_FPDMA_QUEUED;
		sectors_out = ATA_RFPDMA_ZAC_MGMT_IN << 8;
		auxiliary = (zm_action & 0xf) | (zone_flags << 8);
		ata_flags = CAM_ATAIO_FPDMA;
		/*
		 * For RECEIVE FPDMA QUEUED, the transfer length is
		 * encoded in the FEATURE register, and 0 means
		 * that 65536 512 byte blocks are to be tranferred.
		 * In practice, it is unlikely we will see a transfer that
		 * large.
		 */
		if (dxfer_len == (65536 * 512)) {
			features_out = 0;
		} else {
			/*
			 * Yes, the caller can theoretically request a
			 * transfer larger than we can handle.
			 * Anyone using this function needs enough
			 * knowledge to avoid doing that.
			 */
			features_out = ((dxfer_len >> 9) & 0xffff);
		}
	}

	cam_fill_ataio(ataio,
	    /*retries*/ retries,
	    /*cbfcnp*/ cbfcnp,
	    /*flags*/ CAM_DIR_IN,
	    /*tag_action*/ 0,
	    /*data_ptr*/ data_ptr,
	    /*dxfer_len*/ dxfer_len,
	    /*timeout*/ timeout);

	ata_48bit_cmd(ataio,
	    /*cmd*/ command_out,
	    /*features*/ features_out,
	    /*lba*/ zone_id,
	    /*sector_count*/ sectors_out);

	ataio->cmd.flags |= ata_flags;
	if (auxiliary != 0) {
		ataio->ata_flags |= ATA_FLAG_AUX;
		ataio->aux = auxiliary;
	}
}
