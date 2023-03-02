/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022 Benjamin Tissoires
 */

#ifndef __HID_BPF_ATTACH_H
#define __HID_BPF_ATTACH_H

struct attach_prog_args {
	int prog_fd;
	unsigned int hid;
	int retval;
};

#endif /* __HID_BPF_ATTACH_H */
