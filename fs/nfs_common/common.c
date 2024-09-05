// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>
#include <linux/nfs_common.h>

/*
 * We need to translate between nfs status return values and
 * the local errno values which may not be the same.
 */
static const struct {
	int stat;
	int errno;
} nfs_errtbl[] = {
	{ NFS_OK,		0		},
	{ NFSERR_PERM,		-EPERM		},
	{ NFSERR_NOENT,		-ENOENT		},
	{ NFSERR_IO,		-errno_NFSERR_IO},
	{ NFSERR_NXIO,		-ENXIO		},
/*	{ NFSERR_EAGAIN,	-EAGAIN		}, */
	{ NFSERR_ACCES,		-EACCES		},
	{ NFSERR_EXIST,		-EEXIST		},
	{ NFSERR_XDEV,		-EXDEV		},
	{ NFSERR_NODEV,		-ENODEV		},
	{ NFSERR_NOTDIR,	-ENOTDIR	},
	{ NFSERR_ISDIR,		-EISDIR		},
	{ NFSERR_INVAL,		-EINVAL		},
	{ NFSERR_FBIG,		-EFBIG		},
	{ NFSERR_NOSPC,		-ENOSPC		},
	{ NFSERR_ROFS,		-EROFS		},
	{ NFSERR_MLINK,		-EMLINK		},
	{ NFSERR_NAMETOOLONG,	-ENAMETOOLONG	},
	{ NFSERR_NOTEMPTY,	-ENOTEMPTY	},
	{ NFSERR_DQUOT,		-EDQUOT		},
	{ NFSERR_STALE,		-ESTALE		},
	{ NFSERR_REMOTE,	-EREMOTE	},
#ifdef EWFLUSH
	{ NFSERR_WFLUSH,	-EWFLUSH	},
#endif
	{ NFSERR_BADHANDLE,	-EBADHANDLE	},
	{ NFSERR_NOT_SYNC,	-ENOTSYNC	},
	{ NFSERR_BAD_COOKIE,	-EBADCOOKIE	},
	{ NFSERR_NOTSUPP,	-ENOTSUPP	},
	{ NFSERR_TOOSMALL,	-ETOOSMALL	},
	{ NFSERR_SERVERFAULT,	-EREMOTEIO	},
	{ NFSERR_BADTYPE,	-EBADTYPE	},
	{ NFSERR_JUKEBOX,	-EJUKEBOX	},
	{ -1,			-EIO		}
};

/**
 * nfs_stat_to_errno - convert an NFS status code to a local errno
 * @status: NFS status code to convert
 *
 * Returns a local errno value, or -EIO if the NFS status code is
 * not recognized.  This function is used jointly by NFSv2 and NFSv3.
 */
int nfs_stat_to_errno(enum nfs_stat status)
{
	int i;

	for (i = 0; nfs_errtbl[i].stat != -1; i++) {
		if (nfs_errtbl[i].stat == (int)status)
			return nfs_errtbl[i].errno;
	}
	return nfs_errtbl[i].errno;
}
EXPORT_SYMBOL_GPL(nfs_stat_to_errno);
