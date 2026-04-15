// SPDX-License-Identifier: GPL-2.0

#include <linux/gpu_buddy.h>

#ifdef CONFIG_GPU_BUDDY

__rust_helper u64 rust_helper_gpu_buddy_block_offset(const struct gpu_buddy_block *block)
{
	return gpu_buddy_block_offset(block);
}

__rust_helper unsigned int rust_helper_gpu_buddy_block_order(struct gpu_buddy_block *block)
{
	return gpu_buddy_block_order(block);
}

#endif /* CONFIG_GPU_BUDDY */
