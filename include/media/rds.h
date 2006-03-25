/*

    Types and defines needed for RDS. This is included by
    saa6588.c and every driver (e.g. bttv-driver.c) that wants
    to use the saa6588 module.

    Instead of having a seperate rds.h, I'd prefer to include
    this stuff in one of the already existing files like tuner.h

    (c) 2005 by Hans J. Koch

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef _RDS_H
#define _RDS_H

struct rds_command {
	unsigned int  block_count;
	int           result;
	unsigned char __user *buffer;
	struct file   *instance;
	poll_table    *event_list;
};

#define RDS_CMD_OPEN	_IOW('R',1,int)
#define RDS_CMD_CLOSE	_IOW('R',2,int)
#define RDS_CMD_READ	_IOR('R',3,int)
#define RDS_CMD_POLL	_IOR('R',4,int)

#endif
