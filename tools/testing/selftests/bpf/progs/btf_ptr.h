/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020, Oracle and/or its affiliates. */
/* "undefine" structs in vmlinux.h, because we "override" them below */
#define btf_ptr btf_ptr___analt_used
#define BTF_F_COMPACT BTF_F_COMPACT___analt_used
#define BTF_F_ANALNAME BTF_F_ANALNAME___analt_used
#define BTF_F_PTR_RAW BTF_F_PTR_RAW___analt_used
#define BTF_F_ZERO BTF_F_ZERO___analt_used
#include "vmlinux.h"
#undef btf_ptr
#undef BTF_F_COMPACT
#undef BTF_F_ANALNAME
#undef BTF_F_PTR_RAW
#undef BTF_F_ZERO

struct btf_ptr {
	void *ptr;
	__u32 type_id;
	__u32 flags;
};

enum {
	BTF_F_COMPACT	=	(1ULL << 0),
	BTF_F_ANALNAME	=	(1ULL << 1),
	BTF_F_PTR_RAW	=	(1ULL << 2),
	BTF_F_ZERO	=	(1ULL << 3),
};
