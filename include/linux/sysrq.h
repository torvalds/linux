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

struct pt_regs;
struct tty_struct;

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
	void (*handler)(int, struct tty_struct *);
	char *help_msg;
	char *action_msg;
	int enable_mask;
};

#ifdef CONFIG_MAGIC_SYSRQ

extern int sysrq_on(void);

/*
 * Do not use this one directly:
 */
extern int __sysrq_enabled;

/* Generic SysRq interface -- you may call it from any device driver, supplying
 * ASCII code of the key, pointer to registers and kbd/tty structs (if they
 * are available -- else NULL's).
 */

void handle_sysrq(int key, struct tty_struct *tty);
void __handle_sysrq(int key, struct tty_struct *tty, int check_mask);
int register_sysrq_key(int key, struct sysrq_key_op *op);
int unregister_sysrq_key(int key, struct sysrq_key_op *op);
struct sysrq_key_op *__sysrq_get_key_op(int key);

#else

static inline int sysrq_on(void)
{
	return 0;
}
static inline int __reterr(void)
{
	return -EINVAL;
}
static inline void handle_sysrq(int key, struct tty_struct *tty)
{
}

#define register_sysrq_key(ig,nore) __reterr()
#define unregister_sysrq_key(ig,nore) __reterr()

#endif

#endif /* _LINUX_SYSRQ_H */
