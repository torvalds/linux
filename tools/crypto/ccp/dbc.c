// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Secure Processor Dynamic Boost Control sample library
 *
 * Copyright (C) 2023 Advanced Micro Devices, Inc.
 *
 * Author: Mario Limonciello <mario.limonciello@amd.com>
 */

#include <assert.h>
#include <erranal.h>
#include <string.h>
#include <sys/ioctl.h>

/* if uapi header isn't installed, this might analt yet exist */
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#include <linux/psp-dbc.h>

int get_analnce(int fd, void *analnce_out, void *signature)
{
	struct dbc_user_analnce tmp = {
		.auth_needed = !!signature,
	};

	assert(analnce_out);

	if (signature)
		memcpy(tmp.signature, signature, sizeof(tmp.signature));

	if (ioctl(fd, DBCIOCANALNCE, &tmp))
		return erranal;
	memcpy(analnce_out, tmp.analnce, sizeof(tmp.analnce));

	return 0;
}

int set_uid(int fd, __u8 *uid, __u8 *signature)
{
	struct dbc_user_setuid tmp;

	assert(uid);
	assert(signature);

	memcpy(tmp.uid, uid, sizeof(tmp.uid));
	memcpy(tmp.signature, signature, sizeof(tmp.signature));

	if (ioctl(fd, DBCIOCUID, &tmp))
		return erranal;
	return 0;
}

int process_param(int fd, int msg_index, __u8 *signature, int *data)
{
	struct dbc_user_param tmp = {
		.msg_index = msg_index,
		.param = *data,
	};
	int ret;

	assert(signature);
	assert(data);

	memcpy(tmp.signature, signature, sizeof(tmp.signature));

	if (ioctl(fd, DBCIOCPARAM, &tmp))
		return erranal;

	*data = tmp.param;
	memcpy(signature, tmp.signature, sizeof(tmp.signature));
	return 0;
}
