/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_I8042_H
#define _LINUX_I8042_H


#include <linux/types.h>

/*
 * Standard commands.
 */

#define I8042_CMD_CTL_RCTR	0x0120
#define I8042_CMD_CTL_WCTR	0x1060
#define I8042_CMD_CTL_TEST	0x01aa

#define I8042_CMD_KBD_DISABLE	0x00ad
#define I8042_CMD_KBD_ENABLE	0x00ae
#define I8042_CMD_KBD_TEST	0x01ab
#define I8042_CMD_KBD_LOOP	0x11d2

#define I8042_CMD_AUX_DISABLE	0x00a7
#define I8042_CMD_AUX_ENABLE	0x00a8
#define I8042_CMD_AUX_TEST	0x01a9
#define I8042_CMD_AUX_SEND	0x10d4
#define I8042_CMD_AUX_LOOP	0x11d3

#define I8042_CMD_MUX_PFX	0x0090
#define I8042_CMD_MUX_SEND	0x1090

/*
 * Status register bits.
 */

#define I8042_STR_PARITY	0x80
#define I8042_STR_TIMEOUT	0x40
#define I8042_STR_AUXDATA	0x20
#define I8042_STR_KEYLOCK	0x10
#define I8042_STR_CMDDAT	0x08
#define I8042_STR_MUXERR	0x04
#define I8042_STR_IBF		0x02
#define I8042_STR_OBF		0x01

/*
 * Control register bits.
 */

#define I8042_CTR_KBDINT	0x01
#define I8042_CTR_AUXINT	0x02
#define I8042_CTR_IGNKEYLOCK	0x08
#define I8042_CTR_KBDDIS	0x10
#define I8042_CTR_AUXDIS	0x20
#define I8042_CTR_XLATE		0x40

struct serio;

#if defined(CONFIG_SERIO_I8042) || defined(CONFIG_SERIO_I8042_MODULE)

void i8042_lock_chip(void);
void i8042_unlock_chip(void);
int i8042_command(unsigned char *param, int command);
int i8042_install_filter(bool (*filter)(unsigned char data, unsigned char str,
					struct serio *serio));
int i8042_remove_filter(bool (*filter)(unsigned char data, unsigned char str,
				       struct serio *serio));

#else

static inline void i8042_lock_chip(void)
{
}

static inline void i8042_unlock_chip(void)
{
}

static inline int i8042_command(unsigned char *param, int command)
{
	return -ENODEV;
}

static inline int i8042_install_filter(bool (*filter)(unsigned char data, unsigned char str,
					struct serio *serio))
{
	return -ENODEV;
}

static inline int i8042_remove_filter(bool (*filter)(unsigned char data, unsigned char str,
				       struct serio *serio))
{
	return -ENODEV;
}

#endif

#endif
