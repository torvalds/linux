/*
 * intel_pt_pkt_decoder.h: Intel Processor Trace support
 * Copyright (c) 2013-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef INCLUDE__INTEL_PT_PKT_DECODER_H__
#define INCLUDE__INTEL_PT_PKT_DECODER_H__

#include <stddef.h>
#include <stdint.h>

#define INTEL_PT_PKT_DESC_MAX	256

#define INTEL_PT_NEED_MORE_BYTES	-1
#define INTEL_PT_BAD_PACKET		-2

#define INTEL_PT_PSB_STR		"\002\202\002\202\002\202\002\202" \
					"\002\202\002\202\002\202\002\202"
#define INTEL_PT_PSB_LEN		16

#define INTEL_PT_PKT_MAX_SZ		16

enum intel_pt_pkt_type {
	INTEL_PT_BAD,
	INTEL_PT_PAD,
	INTEL_PT_TNT,
	INTEL_PT_TIP_PGD,
	INTEL_PT_TIP_PGE,
	INTEL_PT_TSC,
	INTEL_PT_TMA,
	INTEL_PT_MODE_EXEC,
	INTEL_PT_MODE_TSX,
	INTEL_PT_MTC,
	INTEL_PT_TIP,
	INTEL_PT_FUP,
	INTEL_PT_CYC,
	INTEL_PT_VMCS,
	INTEL_PT_PSB,
	INTEL_PT_PSBEND,
	INTEL_PT_CBR,
	INTEL_PT_TRACESTOP,
	INTEL_PT_PIP,
	INTEL_PT_OVF,
	INTEL_PT_MNT,
	INTEL_PT_PTWRITE,
	INTEL_PT_PTWRITE_IP,
	INTEL_PT_EXSTOP,
	INTEL_PT_EXSTOP_IP,
	INTEL_PT_MWAIT,
	INTEL_PT_PWRE,
	INTEL_PT_PWRX,
};

struct intel_pt_pkt {
	enum intel_pt_pkt_type	type;
	int			count;
	uint64_t		payload;
};

const char *intel_pt_pkt_name(enum intel_pt_pkt_type);

int intel_pt_get_packet(const unsigned char *buf, size_t len,
			struct intel_pt_pkt *packet);

int intel_pt_pkt_desc(const struct intel_pt_pkt *packet, char *buf, size_t len);

#endif
