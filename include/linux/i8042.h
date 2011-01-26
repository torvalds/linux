#ifndef _LINUX_I8042_H
#define _LINUX_I8042_H

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

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

struct serio;

#if defined(CONFIG_SERIO_I8042) || defined(CONFIG_SERIO_I8042_MODULE)

void i8042_lock_chip(void);
void i8042_unlock_chip(void);
int i8042_command(unsigned char *param, int command);
bool i8042_check_port_owner(const struct serio *);
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

static inline bool i8042_check_port_owner(const struct serio *serio)
{
	return false;
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
