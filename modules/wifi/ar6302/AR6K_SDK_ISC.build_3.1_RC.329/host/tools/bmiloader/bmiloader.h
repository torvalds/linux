/*
 * Copyright (c) 2009 Atheros Communications Inc.
 * All rights reserved.
 *
 *
 * 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
 * 
 * The file contains data structures to assist framing of bmi commands via 
 * the sysfs interface. The layout of the structures conform to the bmi 
 * parameters listed for different commands in the bmi_msdh.h header file.
 *
 */

#ifndef __BMILOADER_H__
#define __BMILOADER_H__

/* Maximum size used for BMI commands */
#define BMI_CMDSZ_MAX (BMI_DATASZ_MAX + \
                       sizeof(u32) /* cmd */ + \
                       sizeof(u32) /* addr */ + \
                       sizeof(u32))/* length */

#define BMI_COMMAND_FITS(sz) ((sz) <= BMI_CMDSZ_MAX)

/* BMI Commands */
struct bmi_done_cmd {
	A_UINT32	command;	/* BMI_DONE */
};

struct bmi_read_memory_cmd {
	A_UINT32	command;
	A_UINT32	address;
	A_UINT32	length; /* in bytes, <= BMI_DATASZ_MAX */
};

struct bmi_write_memory_cmd_hdr {
	A_UINT32	command;
	A_UINT32	address;
	A_UINT32	length;
};

struct bmi_execute_cmd {
	A_UINT32	command;
	A_UINT32	address;
	A_UINT32	param;
};

struct bmi_set_app_start_cmd {
	A_UINT32	command;
	A_UINT32	address;
};

struct bmi_read_soc_register_cmd {
	A_UINT32	command;
	A_UINT32	address;
};

struct bmi_write_soc_register_cmd {
	A_UINT32	command;
	A_UINT32	address;
	A_UINT32	value;
};

struct bmi_get_target_info_cmd {
	A_UINT32	command;
};

struct bmi_rompatch_install_cmd {
	A_UINT32	command;
	A_UINT32	address;		/* ROM address */
	A_UINT32	address_or_value;	/* RAM address or value */
	A_UINT32	size;			/* Size in bytes */
	A_UINT32	activate;		/* 0 or 1 */
};

struct bmi_rompatch_uninstall_cmd {
	A_UINT32	command;
	A_UINT32	PatchID;
};

struct bmi_rompatch_activate_cmd_hdr {
	A_UINT32	command;
	A_UINT32	rompatch_count;
};

struct bmi_rompatch_deactivate_cmd_hdr {
	A_UINT32	command;
	A_UINT32	rompatch_count;
};

struct bmi_lz_stream_start_cmd {
	A_UINT32	command;
	A_UINT32	address;
};

struct bmi_lz_data_cmd_hdr {
	A_UINT32	command;
	A_UINT32	length;	/* in bytes, <= BMI_DATASZ_MAX */
};

#endif /* __BMILOADER_H__ */
