/*
 * Copyright © 2016 Intel Corporation
 *
 * Authors:
 *    Rafael Antognolli <rafael.antognolli@intel.com>
 *    Scott  Bauer      <scott.bauer@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef LINUX_OPAL_H
#define LINUX_OPAL_H

#include <uapi/linux/sed-opal.h>
#include <linux/kernel.h>

/*
 * These constant values come from:
 * SPC-4 section
 * 6.30 SECURITY PROTOCOL IN command / table 265.
 */
enum {
	TCG_SECP_00 = 0,
	TCG_SECP_01,
};
struct opal_dev;

#define IO_BUFFER_LENGTH 2048
#define MAX_TOKS 64

typedef int (*opal_step)(struct opal_dev *dev);
typedef int (sec_send_recv)(struct opal_dev *ctx, u16 spsp, u8 secp,
			    void *buffer, size_t len, bool send);


enum opal_atom_width {
	OPAL_WIDTH_TINY,
	OPAL_WIDTH_SHORT,
	OPAL_WIDTH_MEDIUM,
	OPAL_WIDTH_LONG,
	OPAL_WIDTH_TOKEN
};

/*
 * Token defs derived from:
 * TCG_Storage_Architecture_Core_Spec_v2.01_r1.00
 * 3.2.2 Data Stream Encoding
 */
enum opal_response_token {
	OPAL_DTA_TOKENID_BYTESTRING = 0xe0,
	OPAL_DTA_TOKENID_SINT = 0xe1,
	OPAL_DTA_TOKENID_UINT = 0xe2,
	OPAL_DTA_TOKENID_TOKEN = 0xe3, /* actual token is returned */
	OPAL_DTA_TOKENID_INVALID = 0X0
};

/*
 * On the parsed response, we don't store again the toks that are already
 * stored in the response buffer. Instead, for each token, we just store a
 * pointer to the position in the buffer where the token starts, and the size
 * of the token in bytes.
 */
struct opal_resp_tok {
	const u8 *pos;
	size_t len;
	enum opal_response_token type;
	enum opal_atom_width width;
	union {
		u64 u;
		s64 s;
	} stored;
};

/*
 * From the response header it's not possible to know how many tokens there are
 * on the payload. So we hardcode that the maximum will be MAX_TOKS, and later
 * if we start dealing with messages that have more than that, we can increase
 * this number. This is done to avoid having to make two passes through the
 * response, the first one counting how many tokens we have and the second one
 * actually storing the positions.
 */
struct parsed_resp {
	int num;
	struct opal_resp_tok toks[MAX_TOKS];
};

/**
 * struct opal_dev - The structure representing a OPAL enabled SED.
 * @supported: Whether or not OPAL is supported on this controller.
 * @send_recv: The combined sec_send/sec_recv function pointer.
 * @opal_step: A series of opal methods that are necessary to complete a command.
 * @func_data: An array of parameters for the opal methods above.
 * @state: Describes the current opal_step we're working on.
 * @dev_lock: Locks the entire opal_dev structure.
 * @parsed: Parsed response from controller.
 * @prev_data: Data returned from a method to the controller.
 * @unlk_lst: A list of Locking ranges to unlock on this device during a resume.
 */
struct opal_dev {
	bool initialized;
	bool supported;
	sec_send_recv *send_recv;

	const opal_step *funcs;
	void **func_data;
	int state;
	struct mutex dev_lock;
	u16 comid;
	u32 hsn;
	u32 tsn;
	u64 align;
	u64 lowest_lba;

	size_t pos;
	u8 cmd[IO_BUFFER_LENGTH];
	u8 resp[IO_BUFFER_LENGTH];

	struct parsed_resp parsed;
	size_t prev_d_len;
	void *prev_data;

	struct list_head unlk_lst;
};

#ifdef CONFIG_BLK_SED_OPAL
bool opal_unlock_from_suspend(struct opal_dev *dev);
void init_opal_dev(struct opal_dev *opal_dev, sec_send_recv *send_recv);
int sed_ioctl(struct opal_dev *dev, unsigned int cmd, void __user *ioctl_ptr);

static inline bool is_sed_ioctl(unsigned int cmd)
{
	switch (cmd) {
	case IOC_OPAL_SAVE:
	case IOC_OPAL_LOCK_UNLOCK:
	case IOC_OPAL_TAKE_OWNERSHIP:
	case IOC_OPAL_ACTIVATE_LSP:
	case IOC_OPAL_SET_PW:
	case IOC_OPAL_ACTIVATE_USR:
	case IOC_OPAL_REVERT_TPR:
	case IOC_OPAL_LR_SETUP:
	case IOC_OPAL_ADD_USR_TO_LR:
	case IOC_OPAL_ENABLE_DISABLE_MBR:
	case IOC_OPAL_ERASE_LR:
	case IOC_OPAL_SECURE_ERASE_LR:
		return true;
	}
	return false;
}
#else
static inline bool is_sed_ioctl(unsigned int cmd)
{
	return false;
}

static inline int sed_ioctl(struct opal_dev *dev, unsigned int cmd,
			    void __user *ioctl_ptr)
{
	return 0;
}
static inline bool opal_unlock_from_suspend(struct opal_dev *dev)
{
	return false;
}
static inline void init_opal_dev(struct opal_dev *opal_dev,
				 sec_send_recv *send_recv)
{
	opal_dev->supported = false;
	opal_dev->initialized = true;
}
#endif /* CONFIG_BLK_SED_OPAL */
#endif /* LINUX_OPAL_H */
