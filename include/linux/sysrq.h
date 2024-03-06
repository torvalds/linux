/* SPDX-License-Identifier: GPL-2.0 */
/* -*- linux-c -*-
 *
 *	$Id: sysrq.h,v 1.3 1997/07/17 11:54:33 mj Exp $
 *
 *	Linux Magic System Request Key Hacks
 *
 *	(c) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	(c) 2000 Crutcher Dunnavant <crutcher+kernel@datastacks.com>
 *	overhauled to use key registration
 *	based upon discusions in irc://irc.openprojects.net/#kernelnewbies
 */

#ifndef _LINUX_SYSRQ_H
#define _LINUX_SYSRQ_H

#include <linux/errno.h>
#include <linux/types.h>

/* Possible values of bitmask for enabling sysrq functions */
/* 0x0001 is reserved for enable everything */
#define SYSRQ_ENABLE_LOG	0x0002
#define SYSRQ_ENABLE_KEYBOARD	0x0004
#define SYSRQ_ENABLE_DUMP	0x0008
#define SYSRQ_ENABLE_SYNC	0x0010
#define SYSRQ_ENABLE_REMOUNT	0x0020
#define SYSRQ_ENABLE_SIGNAL	0x0040
#define SYSRQ_ENABLE_BOOT	0x0080
#define SYSRQ_ENABLE_RTNICE	0x0100

struct sysrq_key_op {
	void (* const handler)(u8);
	const char * const help_msg;
	const char * const action_msg;
	const int enable_mask;
};

#ifdef CONFIG_MAGIC_SYSRQ

/* Generic SysRq interface -- you may call it from any device driver, supplying
 * ASCII code of the key, pointer to registers and kbd/tty structs (if they
 * are available -- else NULL's).
 */

void handle_sysrq(u8 key);
void __handle_sysrq(u8 key, bool check_mask);
int register_sysrq_key(u8 key, const struct sysrq_key_op *op);
int unregister_sysrq_key(u8 key, const struct sysrq_key_op *op);
extern const struct sysrq_key_op *__sysrq_reboot_op;

int sysrq_toggle_support(int enable_mask);
int sysrq_mask(void);

#else

static inline void handle_sysrq(u8 key)
{
}

static inline void __handle_sysrq(u8 key, bool check_mask)
{
}

static inline int register_sysrq_key(u8 key, const struct sysrq_key_op *op)
{
	return -EINVAL;
}

static inline int unregister_sysrq_key(u8 key, const struct sysrq_key_op *op)
{
	return -EINVAL;
}

static inline int sysrq_mask(void)
{
	/* Magic SysRq disabled mask */
	return 0;
}

#endif

#endif /* _LINUX_SYSRQ_H */
