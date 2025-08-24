/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#ifndef _UAPIEGM_H
#define _UAPIEGM_H

#define EGM_TYPE ('E')

struct egm_bad_pages_info {
	__aligned_u64 offset;
	__aligned_u64 size;
};

struct egm_bad_pages_list {
	__u32 argsz;
	/* out */
	__u32 count;
	/* out */
	struct egm_bad_pages_info bad_pages[];
};

#define EGM_BAD_PAGES_LIST     _IO(EGM_TYPE, 100)

#endif /* _UAPIEGM_H */
