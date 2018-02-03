/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "usbip_common.h"
#include "utils.h"
#include "sysfs_utils.h"

int modify_match_busid(char *busid, int add)
{
	char attr_name[] = "match_busid";
	char command[SYSFS_BUS_ID_SIZE + 4];
	char match_busid_attr_path[SYSFS_PATH_MAX];
	int rc;
	int cmd_size;

	snprintf(match_busid_attr_path, sizeof(match_busid_attr_path),
		 "%s/%s/%s/%s/%s/%s", SYSFS_MNT_PATH, SYSFS_BUS_NAME,
		 SYSFS_BUS_TYPE, SYSFS_DRIVERS_NAME, USBIP_HOST_DRV_NAME,
		 attr_name);

	if (add)
		cmd_size = snprintf(command, SYSFS_BUS_ID_SIZE + 4, "add %s",
				    busid);
	else
		cmd_size = snprintf(command, SYSFS_BUS_ID_SIZE + 4, "del %s",
				    busid);

	rc = write_sysfs_attribute(match_busid_attr_path, command,
				   cmd_size);
	if (rc < 0) {
		dbg("failed to write match_busid: %s", strerror(errno));
		return -1;
	}

	return 0;
}
