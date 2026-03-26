// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Christian Brauner <brauner@kernel.org> */

/*
 * BPF LSM block device integrity tracker for dm-verity.
 *
 * Tracks block devices in a hashmap keyed by bd_dev.  When dm-verity
 * calls security_bdev_setintegrity() during verity_preresume(), the
 * setintegrity hook records the roothash and signature-validity data.
 * The free hook cleans up when the device goes away.  The alloc hook
 * counts allocations for test validation.
 *
 * The sleepable hooks exercise bpf_copy_from_user() to verify that
 * the sleepable classification actually permits sleepable helpers.
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct verity_info {
	__u8  has_roothash;	/* LSM_INT_DMVERITY_ROOTHASH seen */
	__u8  sig_valid;	/* LSM_INT_DMVERITY_SIG_VALID value (non-NULL = valid) */
	__u32 setintegrity_cnt;	/* total setintegrity calls for this dev */
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 64);
	__type(key, __u32);		/* dev_t from bdev->bd_dev */
	__type(value, struct verity_info);
} verity_devices SEC(".maps");

/* Global counters exposed to userspace via skeleton bss. */
int alloc_count;

char _license[] SEC("license") = "GPL";

SEC("lsm.s/bdev_setintegrity")
int BPF_PROG(bdev_setintegrity, struct block_device *bdev,
	     enum lsm_integrity_type type, const void *value, size_t size)
{
	struct verity_info zero = {};
	struct verity_info *info;
	__u32 dev;
	char buf;

	/*
	 * Exercise a sleepable helper to confirm the verifier
	 * allows it in this sleepable hook.
	 */
	(void)bpf_copy_from_user(&buf, sizeof(buf), NULL);

	dev = bdev->bd_dev;

	info = bpf_map_lookup_elem(&verity_devices, &dev);
	if (!info) {
		bpf_map_update_elem(&verity_devices, &dev, &zero, BPF_NOEXIST);
		info = bpf_map_lookup_elem(&verity_devices, &dev);
		if (!info)
			return 0;
	}

	if (type == LSM_INT_DMVERITY_ROOTHASH)
		info->has_roothash = 1;
	else if (type == LSM_INT_DMVERITY_SIG_VALID)
		info->sig_valid = (value != NULL);

	__sync_fetch_and_add(&info->setintegrity_cnt, 1);

	return 0;
}

SEC("lsm/bdev_free_security")
void BPF_PROG(bdev_free_security, struct block_device *bdev)
{
	__u32 dev = bdev->bd_dev;

	bpf_map_delete_elem(&verity_devices, &dev);
}

SEC("lsm.s/bdev_alloc_security")
int BPF_PROG(bdev_alloc_security, struct block_device *bdev)
{
	char buf;

	/*
	 * Exercise a sleepable helper to confirm the verifier
	 * allows it in this sleepable hook.
	 */
	(void)bpf_copy_from_user(&buf, sizeof(buf), NULL);

	__sync_fetch_and_add(&alloc_count, 1);

	return 0;
}
