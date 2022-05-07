/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * This is a module to test the HMM (Heterogeneous Memory Management) API
 * of the kernel. It allows a userspace program to expose its entire address
 * space through the HMM test module device file.
 */
#ifndef _LIB_TEST_HMM_UAPI_H
#define _LIB_TEST_HMM_UAPI_H

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * Structure to pass to the HMM test driver to mimic a device accessing
 * system memory and ZONE_DEVICE private memory through device page tables.
 *
 * @addr: (in) user address the device will read/write
 * @ptr: (in) user address where device data is copied to/from
 * @npages: (in) number of pages to read/write
 * @cpages: (out) number of pages copied
 * @faults: (out) number of device page faults seen
 */
struct hmm_dmirror_cmd {
	__u64		addr;
	__u64		ptr;
	__u64		npages;
	__u64		cpages;
	__u64		faults;
};

/* Expose the address space of the calling process through hmm device file */
#define HMM_DMIRROR_READ		_IOWR('H', 0x00, struct hmm_dmirror_cmd)
#define HMM_DMIRROR_WRITE		_IOWR('H', 0x01, struct hmm_dmirror_cmd)
#define HMM_DMIRROR_MIGRATE		_IOWR('H', 0x02, struct hmm_dmirror_cmd)
#define HMM_DMIRROR_SNAPSHOT		_IOWR('H', 0x03, struct hmm_dmirror_cmd)

/*
 * Values returned in hmm_dmirror_cmd.ptr for HMM_DMIRROR_SNAPSHOT.
 * HMM_DMIRROR_PROT_ERROR: no valid mirror PTE for this page
 * HMM_DMIRROR_PROT_NONE: unpopulated PTE or PTE with no access
 * HMM_DMIRROR_PROT_READ: read-only PTE
 * HMM_DMIRROR_PROT_WRITE: read/write PTE
 * HMM_DMIRROR_PROT_PMD: PMD sized page is fully mapped by same permissions
 * HMM_DMIRROR_PROT_PUD: PUD sized page is fully mapped by same permissions
 * HMM_DMIRROR_PROT_ZERO: special read-only zero page
 * HMM_DMIRROR_PROT_DEV_PRIVATE_LOCAL: Migrated device private page on the
 *					device the ioctl() is made
 * HMM_DMIRROR_PROT_DEV_PRIVATE_REMOTE: Migrated device private page on some
 *					other device
 */
enum {
	HMM_DMIRROR_PROT_ERROR			= 0xFF,
	HMM_DMIRROR_PROT_NONE			= 0x00,
	HMM_DMIRROR_PROT_READ			= 0x01,
	HMM_DMIRROR_PROT_WRITE			= 0x02,
	HMM_DMIRROR_PROT_PMD			= 0x04,
	HMM_DMIRROR_PROT_PUD			= 0x08,
	HMM_DMIRROR_PROT_ZERO			= 0x10,
	HMM_DMIRROR_PROT_DEV_PRIVATE_LOCAL	= 0x20,
	HMM_DMIRROR_PROT_DEV_PRIVATE_REMOTE	= 0x30,
};

#endif /* _LIB_TEST_HMM_UAPI_H */
