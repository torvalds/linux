/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/*
 * Lightweight directory reading library.
 */
#ifndef __API_IO_DIR__
#define __API_IO_DIR__

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/limits.h>

#if !defined(SYS_getdents64)
#if defined(__x86_64__) || defined(__arm__)
  #define SYS_getdents64 217
#elif defined(__i386__) || defined(__s390x__) || defined(__sh__)
  #define SYS_getdents64 220
#elif defined(__alpha__)
  #define SYS_getdents64 377
#elif defined(__mips__)
  #define SYS_getdents64 308
#elif defined(__powerpc64__) || defined(__powerpc__)
  #define SYS_getdents64 202
#elif defined(__sparc64__) || defined(__sparc__)
  #define SYS_getdents64 154
#elif defined(__xtensa__)
  #define SYS_getdents64 60
#else
  #define SYS_getdents64 61
#endif
#endif /* !defined(SYS_getdents64) */

static inline ssize_t perf_getdents64(int fd, void *dirp, size_t count)
{
#ifdef MEMORY_SANITIZER
	memset(dirp, 0, count);
#endif
	return syscall(SYS_getdents64, fd, dirp, count);
}

struct io_dirent64 {
	ino64_t        d_ino;    /* 64-bit inode number */
	off64_t        d_off;    /* 64-bit offset to next structure */
	unsigned short d_reclen; /* Size of this dirent */
	unsigned char  d_type;   /* File type */
	char           d_name[NAME_MAX + 1]; /* Filename (null-terminated) */
};

struct io_dir {
	int dirfd;
	ssize_t available_bytes;
	struct io_dirent64 *next;
	struct io_dirent64 buff[4];
};

static inline void io_dir__init(struct io_dir *iod, int dirfd)
{
	iod->dirfd = dirfd;
	iod->available_bytes = 0;
}

static inline void io_dir__rewinddir(struct io_dir *iod)
{
	lseek(iod->dirfd, 0, SEEK_SET);
	iod->available_bytes = 0;
}

static inline struct io_dirent64 *io_dir__readdir(struct io_dir *iod)
{
	struct io_dirent64 *entry;

	if (iod->available_bytes <= 0) {
		ssize_t rc = perf_getdents64(iod->dirfd, iod->buff, sizeof(iod->buff));

		if (rc <= 0)
			return NULL;
		iod->available_bytes = rc;
		iod->next = iod->buff;
	}
	entry = iod->next;
	iod->next = (struct io_dirent64 *)((char *)entry + entry->d_reclen);
	iod->available_bytes -= entry->d_reclen;
	return entry;
}

static inline bool io_dir__is_dir(const struct io_dir *iod, struct io_dirent64 *dent)
{
	if (dent->d_type == DT_UNKNOWN) {
		struct stat st;

		if (fstatat(iod->dirfd, dent->d_name, &st, /*flags=*/0))
			return false;

		if (S_ISDIR(st.st_mode)) {
			dent->d_type = DT_DIR;
			return true;
		}
	}
	return dent->d_type == DT_DIR;
}

#endif  /* __API_IO_DIR__ */
