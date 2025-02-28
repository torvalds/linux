// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>
#include <linux/nfs_common.h>
#include <linux/nfs4.h>

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
	{ NFSERR_IO,		-EIO		},
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

	for (i = 0; i < ARRAY_SIZE(nfs_errtbl); i++) {
		if (nfs_errtbl[i].stat == (int)status)
			return nfs_errtbl[i].errno;
	}
	return -EIO;
}
EXPORT_SYMBOL_GPL(nfs_stat_to_errno);

/*
 * We need to translate between nfs v4 status return values and
 * the local errno values which may not be the same.
 *
 * nfs4_errtbl_common[] is used before more specialized mappings
 * available in nfs4_errtbl[] or nfs4_errtbl_localio[].
 */
static const struct {
	int stat;
	int errno;
} nfs4_errtbl_common[] = {
	{ NFS4_OK,		0		},
	{ NFS4ERR_PERM,		-EPERM		},
	{ NFS4ERR_NOENT,	-ENOENT		},
	{ NFS4ERR_IO,		-EIO		},
	{ NFS4ERR_NXIO,		-ENXIO		},
	{ NFS4ERR_ACCESS,	-EACCES		},
	{ NFS4ERR_EXIST,	-EEXIST		},
	{ NFS4ERR_XDEV,		-EXDEV		},
	{ NFS4ERR_NOTDIR,	-ENOTDIR	},
	{ NFS4ERR_ISDIR,	-EISDIR		},
	{ NFS4ERR_INVAL,	-EINVAL		},
	{ NFS4ERR_FBIG,		-EFBIG		},
	{ NFS4ERR_NOSPC,	-ENOSPC		},
	{ NFS4ERR_ROFS,		-EROFS		},
	{ NFS4ERR_MLINK,	-EMLINK		},
	{ NFS4ERR_NAMETOOLONG,	-ENAMETOOLONG	},
	{ NFS4ERR_NOTEMPTY,	-ENOTEMPTY	},
	{ NFS4ERR_DQUOT,	-EDQUOT		},
	{ NFS4ERR_STALE,	-ESTALE		},
	{ NFS4ERR_BADHANDLE,	-EBADHANDLE	},
	{ NFS4ERR_BAD_COOKIE,	-EBADCOOKIE	},
	{ NFS4ERR_NOTSUPP,	-ENOTSUPP	},
	{ NFS4ERR_TOOSMALL,	-ETOOSMALL	},
	{ NFS4ERR_BADTYPE,	-EBADTYPE	},
	{ NFS4ERR_SYMLINK,	-ELOOP		},
	{ NFS4ERR_DEADLOCK,	-EDEADLK	},
};

static const struct {
	int stat;
	int errno;
} nfs4_errtbl[] = {
	{ NFS4ERR_SERVERFAULT,	-EREMOTEIO	},
	{ NFS4ERR_LOCKED,	-EAGAIN		},
	{ NFS4ERR_OP_ILLEGAL,	-EOPNOTSUPP	},
	{ NFS4ERR_NOXATTR,	-ENODATA	},
	{ NFS4ERR_XATTR2BIG,	-E2BIG		},
};

/*
 * Convert an NFS error code to a local one.
 * This one is used by NFSv4.
 */
int nfs4_stat_to_errno(int stat)
{
	int i;

	/* First check nfs4_errtbl_common */
	for (i = 0; i < ARRAY_SIZE(nfs4_errtbl_common); i++) {
		if (nfs4_errtbl_common[i].stat == stat)
			return nfs4_errtbl_common[i].errno;
	}
	/* Then check nfs4_errtbl */
	for (i = 0; i < ARRAY_SIZE(nfs4_errtbl); i++) {
		if (nfs4_errtbl[i].stat == stat)
			return nfs4_errtbl[i].errno;
	}
	if (stat <= 10000 || stat > 10100) {
		/* The server is looney tunes. */
		return -EREMOTEIO;
	}
	/* If we cannot translate the error, the recovery routines should
	 * handle it.
	 * Note: remaining NFSv4 error codes have values > 10000, so should
	 * not conflict with native Linux error codes.
	 */
	return -stat;
}
EXPORT_SYMBOL_GPL(nfs4_stat_to_errno);

/*
 * This table is useful for conversion from local errno to NFS error.
 * It provides more logically correct mappings for use with LOCALIO
 * (which is focused on converting from errno to NFS status).
 */
static const struct {
	int stat;
	int errno;
} nfs4_errtbl_localio[] = {
	/* Map errors differently than nfs4_errtbl */
	{ NFS4ERR_IO,		-EREMOTEIO	},
	{ NFS4ERR_DELAY,	-EAGAIN		},
	{ NFS4ERR_FBIG,		-E2BIG		},
	/* Map errors not handled by nfs4_errtbl */
	{ NFS4ERR_STALE,	-EBADF		},
	{ NFS4ERR_STALE,	-EOPENSTALE	},
	{ NFS4ERR_DELAY,	-ETIMEDOUT	},
	{ NFS4ERR_DELAY,	-ERESTARTSYS	},
	{ NFS4ERR_DELAY,	-ENOMEM		},
	{ NFS4ERR_IO,		-ETXTBSY	},
	{ NFS4ERR_IO,		-EBUSY		},
	{ NFS4ERR_SERVERFAULT,	-ESERVERFAULT	},
	{ NFS4ERR_SERVERFAULT,	-ENFILE		},
	{ NFS4ERR_IO,		-EUCLEAN	},
	{ NFS4ERR_PERM,		-ENOKEY		},
};

/*
 * Convert an errno to an NFS error code for LOCALIO.
 */
__u32 nfs_localio_errno_to_nfs4_stat(int errno)
{
	int i;

	/* First check nfs4_errtbl_common */
	for (i = 0; i < ARRAY_SIZE(nfs4_errtbl_common); i++) {
		if (nfs4_errtbl_common[i].errno == errno)
			return nfs4_errtbl_common[i].stat;
	}
	/* Then check nfs4_errtbl_localio */
	for (i = 0; i < ARRAY_SIZE(nfs4_errtbl_localio); i++) {
		if (nfs4_errtbl_localio[i].errno == errno)
			return nfs4_errtbl_localio[i].stat;
	}
	/* If we cannot translate the error, the recovery routines should
	 * handle it.
	 * Note: remaining NFSv4 error codes have values > 10000, so should
	 * not conflict with native Linux error codes.
	 */
	return NFS4ERR_SERVERFAULT;
}
EXPORT_SYMBOL_GPL(nfs_localio_errno_to_nfs4_stat);
