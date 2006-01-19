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
#include "xfs.h"
#include <linux/proc_fs.h>

DEFINE_PER_CPU(struct xfsstats, xfsstats);

STATIC int
xfs_read_xfsstats(
	char		*buffer,
	char		**start,
	off_t		offset,
	int		count,
	int		*eof,
	void		*data)
{
	int		c, i, j, len, val;
	__uint64_t	xs_xstrat_bytes = 0;
	__uint64_t	xs_write_bytes = 0;
	__uint64_t	xs_read_bytes = 0;

	static const struct xstats_entry {
		char	*desc;
		int	endpoint;
	} xstats[] = {
		{ "extent_alloc",	XFSSTAT_END_EXTENT_ALLOC	},
		{ "abt",		XFSSTAT_END_ALLOC_BTREE		},
		{ "blk_map",		XFSSTAT_END_BLOCK_MAPPING	},
		{ "bmbt",		XFSSTAT_END_BLOCK_MAP_BTREE	},
		{ "dir",		XFSSTAT_END_DIRECTORY_OPS	},
		{ "trans",		XFSSTAT_END_TRANSACTIONS	},
		{ "ig",			XFSSTAT_END_INODE_OPS		},
		{ "log",		XFSSTAT_END_LOG_OPS		},
		{ "push_ail",		XFSSTAT_END_TAIL_PUSHING	},
		{ "xstrat",		XFSSTAT_END_WRITE_CONVERT	},
		{ "rw",			XFSSTAT_END_READ_WRITE_OPS	},
		{ "attr",		XFSSTAT_END_ATTRIBUTE_OPS	},
		{ "icluster",		XFSSTAT_END_INODE_CLUSTER	},
		{ "vnodes",		XFSSTAT_END_VNODE_OPS		},
		{ "buf",		XFSSTAT_END_BUF			},
	};

	/* Loop over all stats groups */
	for (i=j=len = 0; i < sizeof(xstats)/sizeof(struct xstats_entry); i++) {
		len += sprintf(buffer + len, xstats[i].desc);
		/* inner loop does each group */
		while (j < xstats[i].endpoint) {
			val = 0;
			/* sum over all cpus */
			for (c = 0; c < NR_CPUS; c++) {
				if (!cpu_possible(c)) continue;
				val += *(((__u32*)&per_cpu(xfsstats, c) + j));
			}
			len += sprintf(buffer + len, " %u", val);
			j++;
		}
		buffer[len++] = '\n';
	}
	/* extra precision counters */
	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_possible(i)) continue;
		xs_xstrat_bytes += per_cpu(xfsstats, i).xs_xstrat_bytes;
		xs_write_bytes += per_cpu(xfsstats, i).xs_write_bytes;
		xs_read_bytes += per_cpu(xfsstats, i).xs_read_bytes;
	}

	len += sprintf(buffer + len, "xpc %Lu %Lu %Lu\n",
			xs_xstrat_bytes, xs_write_bytes, xs_read_bytes);
	len += sprintf(buffer + len, "debug %u\n",
#if defined(DEBUG)
		1);
#else
		0);
#endif

	if (offset >= len) {
		*start = buffer;
		*eof = 1;
		return 0;
	}
	*start = buffer + offset;
	if ((len -= offset) > count)
		return count;
	*eof = 1;

	return len;
}

void
xfs_init_procfs(void)
{
	if (!proc_mkdir("fs/xfs", NULL))
		return;
	create_proc_read_entry("fs/xfs/stat", 0, NULL, xfs_read_xfsstats, NULL);
}

void
xfs_cleanup_procfs(void)
{
	remove_proc_entry("fs/xfs/stat", NULL);
	remove_proc_entry("fs/xfs", NULL);
}
