/*
 *  x1205.h - defines for drivers/i2c/chips/x1205.c
 *  Copyright 2004 Karen Spearel
 *  Copyright 2005 Alessandro Zummo
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef __LINUX_X1205_H__
#define __LINUX_X1205_H__

/* commands */

#define X1205_CMD_GETDATETIME	0
#define X1205_CMD_SETTIME	1
#define X1205_CMD_SETDATETIME	2
#define X1205_CMD_GETALARM	3
#define X1205_CMD_SETALARM	4
#define X1205_CMD_GETDTRIM	5
#define X1205_CMD_SETDTRIM	6
#define X1205_CMD_GETATRIM	7
#define X1205_CMD_SETATRIM	8

extern int x1205_do_command(unsigned int cmd, void *arg);
extern int x1205_direct_attach(int adapter_id,
	struct i2c_client_address_data *address_data);

#endif /* __LINUX_X1205_H__ */
