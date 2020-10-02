/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KERNEL_READ_FILE_H
#define _LINUX_KERNEL_READ_FILE_H

#include <linux/file.h>
#include <linux/types.h>

/* This is a list of *what* is being read, not *how* nor *where*. */
#define __kernel_read_file_id(id) \
	id(UNKNOWN, unknown)		\
	id(FIRMWARE, firmware)		\
	id(MODULE, kernel-module)		\
	id(KEXEC_IMAGE, kexec-image)		\
	id(KEXEC_INITRAMFS, kexec-initramfs)	\
	id(POLICY, security-policy)		\
	id(X509_CERTIFICATE, x509-certificate)	\
	id(MAX_ID, )

#define __fid_enumify(ENUM, dummy) READING_ ## ENUM,
#define __fid_stringify(dummy, str) #str,

enum kernel_read_file_id {
	__kernel_read_file_id(__fid_enumify)
};

static const char * const kernel_read_file_str[] = {
	__kernel_read_file_id(__fid_stringify)
};

static inline const char *kernel_read_file_id_str(enum kernel_read_file_id id)
{
	if ((unsigned int)id >= READING_MAX_ID)
		return kernel_read_file_str[READING_UNKNOWN];

	return kernel_read_file_str[id];
}

int kernel_read_file(struct file *file,
		     void **buf, size_t buf_size,
		     enum kernel_read_file_id id);
int kernel_read_file_from_path(const char *path,
			       void **buf, size_t buf_size,
			       enum kernel_read_file_id id);
int kernel_read_file_from_path_initns(const char *path,
				      void **buf, size_t buf_size,
				      enum kernel_read_file_id id);
int kernel_read_file_from_fd(int fd,
			     void **buf, size_t buf_size,
			     enum kernel_read_file_id id);

#endif /* _LINUX_KERNEL_READ_FILE_H */
