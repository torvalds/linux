/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_MEMFD_H
#define _UAPI_LINUX_MEMFD_H

#include <asm-generic/hugetlb_encode.h>

/* flags for memfd_create(2) (unsigned int) */
#define MFD_CLOEXEC		0x0001U
#define MFD_ALLOW_SEALING	0x0002U
#define MFD_HUGETLB		0x0004U
/* not executable and sealed to prevent changing to executable. */
#define MFD_NOEXEC_SEAL		0x0008U
/* executable */
#define MFD_EXEC		0x0010U

/*
 * Huge page size encoding when MFD_HUGETLB is specified, and a huge page
 * size other than the default is desired.  See hugetlb_encode.h.
 * All known huge page size encodings are provided here.  It is the
 * responsibility of the application to know which sizes are supported on
 * the running system.  See mmap(2) man page for details.
 */
#define MFD_HUGE_SHIFT	HUGETLB_FLAG_ENCODE_SHIFT
#define MFD_HUGE_MASK	HUGETLB_FLAG_ENCODE_MASK

#define MFD_HUGE_64KB	HUGETLB_FLAG_ENCODE_64KB
#define MFD_HUGE_512KB	HUGETLB_FLAG_ENCODE_512KB
#define MFD_HUGE_1MB	HUGETLB_FLAG_ENCODE_1MB
#define MFD_HUGE_2MB	HUGETLB_FLAG_ENCODE_2MB
#define MFD_HUGE_8MB	HUGETLB_FLAG_ENCODE_8MB
#define MFD_HUGE_16MB	HUGETLB_FLAG_ENCODE_16MB
#define MFD_HUGE_32MB	HUGETLB_FLAG_ENCODE_32MB
#define MFD_HUGE_256MB	HUGETLB_FLAG_ENCODE_256MB
#define MFD_HUGE_512MB	HUGETLB_FLAG_ENCODE_512MB
#define MFD_HUGE_1GB	HUGETLB_FLAG_ENCODE_1GB
#define MFD_HUGE_2GB	HUGETLB_FLAG_ENCODE_2GB
#define MFD_HUGE_16GB	HUGETLB_FLAG_ENCODE_16GB

#endif /* _UAPI_LINUX_MEMFD_H */
