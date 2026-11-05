/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_CLOSE_RANGE_H
#define _UAPI_LINUX_CLOSE_RANGE_H

/*
 * A macro of one of these names defined before this header is parsed, by
 * a libc or by a program's own fallback, would replace the enumerator.
 */
#undef CLOSE_RANGE_UNSHARE
#undef CLOSE_RANGE_CLOEXEC
#undef CLOSE_RANGE_EXCEPT
#undef CLOSE_RANGE_CLOEXEC_ONLY

enum close_range_flags {
	/* Unshare the file descriptor table before closing file descriptors. */
	CLOSE_RANGE_UNSHARE	= (1U << 1),

	/* Set the FD_CLOEXEC bit instead of closing the file descriptor. */
	CLOSE_RANGE_CLOEXEC	= (1U << 2),

	/* Act on every file descriptor outside of the given range instead. */
	CLOSE_RANGE_EXCEPT	= (1U << 3),

	/* Only close file descriptors that have the FD_CLOEXEC bit set. */
	CLOSE_RANGE_CLOEXEC_ONLY	= (1U << 4),
};

/* Keep #ifdef working and let glibc skip its own definitions. */
#define CLOSE_RANGE_UNSHARE		CLOSE_RANGE_UNSHARE
#define CLOSE_RANGE_CLOEXEC		CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_EXCEPT		CLOSE_RANGE_EXCEPT
#define CLOSE_RANGE_CLOEXEC_ONLY	CLOSE_RANGE_CLOEXEC_ONLY

#endif /* _UAPI_LINUX_CLOSE_RANGE_H */

