/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2017 ASPEED Technology Inc. */
/* Copyright (c) 2018 Intel Corporation */

#ifndef __JTAG_DRV_H__
#define __JTAG_DRV_H__

enum xfer_mode {
	HW_MODE = 0,
	SW_MODE
} xfer_mode;

struct tck_bitbang {
	__u8	tms;
	__u8	tdi;
	__u8	tdo;
} __attribute__((__packed__));

struct scan_xfer {
	__u8	mode;
	__u32	tap_state;
	__u32	length;
	__u8	*tdi;
	__u32	tdi_bytes;
	__u8	*tdo;
	__u32	tdo_bytes;
	__u32	end_tap_state;
} __attribute__((__packed__));

struct set_tck_param {
	__u8	mode;
	__u32	tck;
} __attribute__((__packed__));

struct get_tck_param {
	__u8	mode;
	__u32	tck;
} __attribute__((__packed__));

struct tap_state_param {
	__u8	mode;
	__u32	from_state;
	__u32	to_state;
} __attribute__((__packed__));

enum jtag_states {
	jtag_tlr,
	jtag_rti,
	jtag_sel_dr,
	jtag_cap_dr,
	jtag_shf_dr,
	jtag_ex1_dr,
	jtag_pau_dr,
	jtag_ex2_dr,
	jtag_upd_dr,
	jtag_sel_ir,
	jtag_cap_ir,
	jtag_shf_ir,
	jtag_ex1_ir,
	jtag_pau_ir,
	jtag_ex2_ir,
	jtag_upd_ir
} jtag_states;

#define JTAGIOC_BASE 'T'

#define AST_JTAG_SET_TCK _IOW(JTAGIOC_BASE, 3, struct set_tck_param)
#define AST_JTAG_GET_TCK _IOR(JTAGIOC_BASE, 4, struct get_tck_param)
#define AST_JTAG_BITBANG _IOWR(JTAGIOC_BASE, 5, struct tck_bitbang)
#define AST_JTAG_SET_TAPSTATE _IOW(JTAGIOC_BASE, 6, struct tap_state_param)
#define AST_JTAG_READWRITESCAN _IOWR(JTAGIOC_BASE, 7, struct scan_xfer)

#endif
