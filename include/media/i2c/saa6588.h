/* SPDX-License-Identifier: GPL-2.0-or-later */
/*

    Types and defines needed for RDS. This is included by
    saa6588.c and every driver (e.g. bttv-driver.c) that wants
    to use the saa6588 module.

    (c) 2005 by Hans J. Koch


*/

#ifndef _SAA6588_H
#define _SAA6588_H

struct saa6588_command {
	unsigned int  block_count;
	bool          nonblocking;
	int           result;
	unsigned char __user *buffer;
	struct file   *instance;
	poll_table    *event_list;
	__poll_t      poll_mask;
};

/* These ioctls are internal to the kernel */
#define SAA6588_CMD_CLOSE	_IOW('R', 2, int)
#define SAA6588_CMD_READ	_IOR('R', 3, int)
#define SAA6588_CMD_POLL	_IOR('R', 4, int)

#endif
