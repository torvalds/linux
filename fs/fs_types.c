// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/export.h>

/*
 * fs on-disk file type to dirent file type conversion
 */
static const unsigned char fs_dtype_by_ftype[FT_MAX] = {
	[FT_UNKNOWN]	= DT_UNKNOWN,
	[FT_REG_FILE]	= DT_REG,
	[FT_DIR]	= DT_DIR,
	[FT_CHRDEV]	= DT_CHR,
	[FT_BLKDEV]	= DT_BLK,
	[FT_FIFO]	= DT_FIFO,
	[FT_SOCK]	= DT_SOCK,
	[FT_SYMLINK]	= DT_LNK
};

/**
 * fs_ftype_to_dtype() - fs on-disk file type to dirent type.
 * @filetype: The on-disk file type to convert.
 *
 * This function converts the on-disk file type value (FT_*) to the directory
 * entry type (DT_*).
 *
 * Context: Any context.
 * Return:
 * * DT_UNKNOWN		- Unknown type
 * * DT_FIFO		- FIFO
 * * DT_CHR		- Character device
 * * DT_DIR		- Directory
 * * DT_BLK		- Block device
 * * DT_REG		- Regular file
 * * DT_LNK		- Symbolic link
 * * DT_SOCK		- Local-domain socket
 */
unsigned char fs_ftype_to_dtype(unsigned int filetype)
{
	if (filetype >= FT_MAX)
		return DT_UNKNOWN;

	return fs_dtype_by_ftype[filetype];
}
EXPORT_SYMBOL_NS_GPL(fs_ftype_to_dtype, ANDROID_GKI_VFS_EXPORT_ONLY);

/*
 * dirent file type to fs on-disk file type conversion
 * Values not initialized explicitly are FT_UNKNOWN (0).
 */
static const unsigned char fs_ftype_by_dtype[DT_MAX] = {
	[DT_REG]	= FT_REG_FILE,
	[DT_DIR]	= FT_DIR,
	[DT_LNK]	= FT_SYMLINK,
	[DT_CHR]	= FT_CHRDEV,
	[DT_BLK]	= FT_BLKDEV,
	[DT_FIFO]	= FT_FIFO,
	[DT_SOCK]	= FT_SOCK,
};

/**
 * fs_umode_to_ftype() - file mode to on-disk file type.
 * @mode: The file mode to convert.
 *
 * This function converts the file mode value to the on-disk file type (FT_*).
 *
 * Context: Any context.
 * Return:
 * * FT_UNKNOWN		- Unknown type
 * * FT_REG_FILE	- Regular file
 * * FT_DIR		- Directory
 * * FT_CHRDEV		- Character device
 * * FT_BLKDEV		- Block device
 * * FT_FIFO		- FIFO
 * * FT_SOCK		- Local-domain socket
 * * FT_SYMLINK		- Symbolic link
 */
unsigned char fs_umode_to_ftype(umode_t mode)
{
	return fs_ftype_by_dtype[S_DT(mode)];
}
EXPORT_SYMBOL_GPL(fs_umode_to_ftype);

/**
 * fs_umode_to_dtype() - file mode to dirent file type.
 * @mode: The file mode to convert.
 *
 * This function converts the file mode value to the directory
 * entry type (DT_*).
 *
 * Context: Any context.
 * Return:
 * * DT_UNKNOWN		- Unknown type
 * * DT_FIFO		- FIFO
 * * DT_CHR		- Character device
 * * DT_DIR		- Directory
 * * DT_BLK		- Block device
 * * DT_REG		- Regular file
 * * DT_LNK		- Symbolic link
 * * DT_SOCK		- Local-domain socket
 */
unsigned char fs_umode_to_dtype(umode_t mode)
{
	return fs_ftype_to_dtype(fs_umode_to_ftype(mode));
}
EXPORT_SYMBOL_GPL(fs_umode_to_dtype);
