/* SPDX-License-Identifier: LGPL-2.0+ WITH Linux-syscall-note */
/*
 * userio: virtual serio device support
 * Copyright (C) 2015 Red Hat
 * Copyright (C) 2015 Lyude (Stephen Chandler Paul) <cpaul@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * This is the public header used for user-space communication with the userio
 * driver. __attribute__((__packed__)) is used for all structs to keep ABI
 * compatibility between all architectures.
 */

#ifndef _USERIO_H
#define _USERIO_H

#include <linux/types.h>

enum userio_cmd_type {
	USERIO_CMD_REGISTER = 0,
	USERIO_CMD_SET_PORT_TYPE = 1,
	USERIO_CMD_SEND_INTERRUPT = 2
};

/*
 * userio Commands
 * All commands sent to /dev/userio are encoded using this structure. The type
 * field should contain a USERIO_CMD* value that indicates what kind of command
 * is being sent to userio. The data field should contain the accompanying
 * argument for the command, if there is one.
 */
struct userio_cmd {
	__u8 type;
	__u8 data;
} __attribute__((__packed__));

#endif /* !_USERIO_H */
