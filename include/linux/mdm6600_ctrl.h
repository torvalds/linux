/*
     Copyright (C) 2010 Motorola, Inc.

     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License version 2 as
     published by the Free Software Foundation.

     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
     GNU General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with this program; if not, write to the Free Software
     Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
     02111-1307  USA
*/
#ifndef __LINUX_MDM_CTRL_H__
#define __LINUX_MDM_CTRL_H__

#define MDM_CTRL_MODULE_NAME "mdm6600_ctrl"
#define MAX_GPIO_NAME 20

enum {
	MDM_CTRL_GPIO_AP_STATUS_0,
	MDM_CTRL_GPIO_AP_STATUS_1,
	MDM_CTRL_GPIO_AP_STATUS_2,
	MDM_CTRL_GPIO_BP_STATUS_0,
	MDM_CTRL_GPIO_BP_STATUS_1,
	MDM_CTRL_GPIO_BP_STATUS_2,
	MDM_CTRL_GPIO_BP_RESOUT,
	MDM_CTRL_GPIO_BP_RESIN,
	MDM_CTRL_GPIO_BP_PWRON,

	MDM_CTRL_NUM_GPIOS,
};

enum {
	MDM_GPIO_DIRECTION_IN,
	MDM_GPIO_DIRECTION_OUT,
};

struct mdm_ctrl_gpio {
	unsigned int number;
	unsigned int direction;
	unsigned int default_value;
	unsigned int allocated;
	char *name;
};

struct mdm_command_gpios {
	unsigned int cmd1;
	unsigned int cmd2;
};

struct mdm_ctrl_platform_data {
	struct mdm_ctrl_gpio gpios[MDM_CTRL_NUM_GPIOS];
	struct mdm_command_gpios cmd_gpios;
};
#endif /* __LINUX_MDM_CTRL_H__ */
