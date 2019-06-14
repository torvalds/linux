// SPDX-License-Identifier: GPL-2.0-only
/*
 * oxfw_command.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) 2014 Takashi Sakamoto
 */

#include "oxfw.h"

int avc_stream_set_format(struct fw_unit *unit, enum avc_general_plug_dir dir,
			  unsigned int pid, u8 *format, unsigned int len)
{
	u8 *buf;
	int err;

	buf = kmalloc(len + 10, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x00;		/* CONTROL */
	buf[1] = 0xff;		/* UNIT */
	buf[2] = 0xbf;		/* EXTENDED STREAM FORMAT INFORMATION */
	buf[3] = 0xc0;		/* SINGLE subfunction */
	buf[4] = dir;		/* Plug Direction */
	buf[5] = 0x00;		/* UNIT */
	buf[6] = 0x00;		/* PCR (Isochronous Plug) */
	buf[7] = 0xff & pid;	/* Plug ID */
	buf[8] = 0xff;		/* Padding */
	buf[9] = 0xff;		/* Support status in response */
	memcpy(buf + 10, format, len);

	/* do transaction and check buf[1-8] are the same against command */
	err = fcp_avc_transaction(unit, buf, len + 10, buf, len + 10,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) |
				  BIT(6) | BIT(7) | BIT(8));
	if (err < 0)
		;
	else if (err < len + 10)
		err = -EIO;
	else if (buf[0] == 0x08) /* NOT IMPLEMENTED */
		err = -ENOSYS;
	else if (buf[0] == 0x0a) /* REJECTED */
		err = -EINVAL;
	else
		err = 0;

	kfree(buf);

	return err;
}

int avc_stream_get_format(struct fw_unit *unit,
			  enum avc_general_plug_dir dir, unsigned int pid,
			  u8 *buf, unsigned int *len, unsigned int eid)
{
	unsigned int subfunc;
	int err;

	if (eid == 0xff)
		subfunc = 0xc0;	/* SINGLE */
	else
		subfunc = 0xc1;	/* LIST */

	buf[0] = 0x01;		/* STATUS */
	buf[1] = 0xff;		/* UNIT */
	buf[2] = 0xbf;		/* EXTENDED STREAM FORMAT INFORMATION */
	buf[3] = subfunc;	/* SINGLE or LIST */
	buf[4] = dir;		/* Plug Direction */
	buf[5] = 0x00;		/* Unit */
	buf[6] = 0x00;		/* PCR (Isochronous Plug) */
	buf[7] = 0xff & pid;	/* Plug ID */
	buf[8] = 0xff;		/* Padding */
	buf[9] = 0xff;		/* support status in response */
	buf[10] = 0xff & eid;	/* entry ID for LIST subfunction */
	buf[11] = 0xff;		/* padding */

	/* do transaction and check buf[1-7] are the same against command */
	err = fcp_avc_transaction(unit, buf, 12, buf, *len,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) |
				  BIT(6) | BIT(7));
	if (err < 0)
		;
	else if (err < 12)
		err = -EIO;
	else if (buf[0] == 0x08)	/* NOT IMPLEMENTED */
		err = -ENOSYS;
	else if (buf[0] == 0x0a)	/* REJECTED */
		err = -EINVAL;
	else if (buf[0] == 0x0b)	/* IN TRANSITION */
		err = -EAGAIN;
	/* LIST subfunction has entry ID */
	else if ((subfunc == 0xc1) && (buf[10] != eid))
		err = -EIO;
	if (err < 0)
		goto end;

	/* keep just stream format information */
	if (subfunc == 0xc0) {
		memmove(buf, buf + 10, err - 10);
		*len = err - 10;
	} else {
		memmove(buf, buf + 11, err - 11);
		*len = err - 11;
	}

	err = 0;
end:
	return err;
}

int avc_general_inquiry_sig_fmt(struct fw_unit *unit, unsigned int rate,
				enum avc_general_plug_dir dir,
				unsigned short pid)
{
	unsigned int sfc;
	u8 *buf;
	int err;

	for (sfc = 0; sfc < CIP_SFC_COUNT; sfc++) {
		if (amdtp_rate_table[sfc] == rate)
			break;
	}
	if (sfc == CIP_SFC_COUNT)
		return -EINVAL;

	buf = kzalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x02;		/* SPECIFIC INQUIRY */
	buf[1] = 0xff;		/* UNIT */
	if (dir == AVC_GENERAL_PLUG_DIR_IN)
		buf[2] = 0x19;	/* INPUT PLUG SIGNAL FORMAT */
	else
		buf[2] = 0x18;	/* OUTPUT PLUG SIGNAL FORMAT */
	buf[3] = 0xff & pid;	/* plug id */
	buf[4] = 0x90;		/* EOH_1, Form_1, FMT. AM824 */
	buf[5] = 0x07 & sfc;	/* FDF-hi. AM824, frequency */
	buf[6] = 0xff;		/* FDF-mid. AM824, SYT hi (not used) */
	buf[7] = 0xff;		/* FDF-low. AM824, SYT lo (not used) */

	/* do transaction and check buf[1-5] are the same against command */
	err = fcp_avc_transaction(unit, buf, 8, buf, 8,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5));
	if (err < 0)
		;
	else if (err < 8)
		err = -EIO;
	else if (buf[0] == 0x08)	/* NOT IMPLEMENTED */
		err = -ENOSYS;
	if (err < 0)
		goto end;

	err = 0;
end:
	kfree(buf);
	return err;
}
