// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025 Google LLC.
 */

#include <linux/fs.h>
#include <linux/bpf_lsm.h>

/*
 * Strong definition of the mmap_file() BPF LSM hook. The __nullable suffix on
 * the struct file pointer parameter name marks it as PTR_MAYBE_NULL. This
 * explicitly enforces that BPF LSM programs check for NULL before attempting to
 * dereference it.
 */
int bpf_lsm_mmap_file(struct file *file__nullable, unsigned long reqprot,
		      unsigned long prot, unsigned long flags)
{
	return 0;
}
