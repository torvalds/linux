/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <xfs.h>

/* IRIX interpretation of an uuid_t */
typedef struct {
	__be32	uu_timelow;
	__be16	uu_timemid;
	__be16	uu_timehi;
	__be16	uu_clockseq;
	__be16	uu_node[3];
} xfs_uu_t;

/*
 * uuid_getnodeuniq - obtain the node unique fields of a UUID.
 *
 * This is not in any way a standard or condoned UUID function;
 * it just something that's needed for user-level file handles.
 */
void
uuid_getnodeuniq(uuid_t *uuid, int fsid [2])
{
	xfs_uu_t *uup = (xfs_uu_t *)uuid;

	fsid[0] = (be16_to_cpu(uup->uu_clockseq) << 16) |
		   be16_to_cpu(uup->uu_timemid);
	fsid[1] = be32_to_cpu(uup->uu_timelow);
}

int
uuid_is_nil(uuid_t *uuid)
{
	int	i;
	char	*cp = (char *)uuid;

	if (uuid == NULL)
		return 0;
	/* implied check of version number here... */
	for (i = 0; i < sizeof *uuid; i++)
		if (*cp++) return 0;	/* not nil */
	return 1;	/* is nil */
}
