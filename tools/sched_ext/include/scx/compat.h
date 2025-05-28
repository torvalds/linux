/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */
#ifndef __SCX_COMPAT_H
#define __SCX_COMPAT_H

#include <bpf/btf.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

struct btf *__COMPAT_vmlinux_btf __attribute__((weak));

static inline void __COMPAT_load_vmlinux_btf(void)
{
	if (!__COMPAT_vmlinux_btf) {
		__COMPAT_vmlinux_btf = btf__load_vmlinux_btf();
		SCX_BUG_ON(!__COMPAT_vmlinux_btf, "btf__load_vmlinux_btf()");
	}
}

static inline bool __COMPAT_read_enum(const char *type, const char *name, u64 *v)
{
	const struct btf_type *t;
	const char *n;
	s32 tid;
	int i;

	__COMPAT_load_vmlinux_btf();

	tid = btf__find_by_name(__COMPAT_vmlinux_btf, type);
	if (tid < 0)
		return false;

	t = btf__type_by_id(__COMPAT_vmlinux_btf, tid);
	SCX_BUG_ON(!t, "btf__type_by_id(%d)", tid);

	if (btf_is_enum(t)) {
		struct btf_enum *e = btf_enum(t);

		for (i = 0; i < BTF_INFO_VLEN(t->info); i++) {
			n = btf__name_by_offset(__COMPAT_vmlinux_btf, e[i].name_off);
			SCX_BUG_ON(!n, "btf__name_by_offset()");
			if (!strcmp(n, name)) {
				*v = e[i].val;
				return true;
			}
		}
	} else if (btf_is_enum64(t)) {
		struct btf_enum64 *e = btf_enum64(t);

		for (i = 0; i < BTF_INFO_VLEN(t->info); i++) {
			n = btf__name_by_offset(__COMPAT_vmlinux_btf, e[i].name_off);
			SCX_BUG_ON(!n, "btf__name_by_offset()");
			if (!strcmp(n, name)) {
				*v = btf_enum64_value(&e[i]);
				return true;
			}
		}
	}

	return false;
}

#define __COMPAT_ENUM_OR_ZERO(__type, __ent)					\
({										\
	u64 __val = 0;								\
	__COMPAT_read_enum(__type, __ent, &__val);				\
	__val;									\
})

static inline bool __COMPAT_has_ksym(const char *ksym)
{
	__COMPAT_load_vmlinux_btf();
	return btf__find_by_name(__COMPAT_vmlinux_btf, ksym) >= 0;
}

static inline bool __COMPAT_struct_has_field(const char *type, const char *field)
{
	const struct btf_type *t;
	const struct btf_member *m;
	const char *n;
	s32 tid;
	int i;

	__COMPAT_load_vmlinux_btf();
	tid = btf__find_by_name_kind(__COMPAT_vmlinux_btf, type, BTF_KIND_STRUCT);
	if (tid < 0)
		return false;

	t = btf__type_by_id(__COMPAT_vmlinux_btf, tid);
	SCX_BUG_ON(!t, "btf__type_by_id(%d)", tid);

	m = btf_members(t);

	for (i = 0; i < BTF_INFO_VLEN(t->info); i++) {
		n = btf__name_by_offset(__COMPAT_vmlinux_btf, m[i].name_off);
		SCX_BUG_ON(!n, "btf__name_by_offset()");
			if (!strcmp(n, field))
				return true;
	}

	return false;
}

#define SCX_OPS_FLAG(name) __COMPAT_ENUM_OR_ZERO("scx_ops_flags", #name)

#define SCX_OPS_KEEP_BUILTIN_IDLE SCX_OPS_FLAG(SCX_OPS_KEEP_BUILTIN_IDLE)
#define SCX_OPS_ENQ_LAST SCX_OPS_FLAG(SCX_OPS_ENQ_LAST)
#define SCX_OPS_ENQ_EXITING  SCX_OPS_FLAG(SCX_OPS_ENQ_EXITING)
#define SCX_OPS_SWITCH_PARTIAL SCX_OPS_FLAG(SCX_OPS_SWITCH_PARTIAL)
#define SCX_OPS_ENQ_MIGRATION_DISABLED SCX_OPS_FLAG(SCX_OPS_ENQ_MIGRATION_DISABLED)
#define SCX_OPS_ALLOW_QUEUED_WAKEUP SCX_OPS_FLAG(SCX_OPS_ALLOW_QUEUED_WAKEUP)
#define SCX_OPS_BUILTIN_IDLE_PER_NODE SCX_OPS_FLAG(SCX_OPS_BUILTIN_IDLE_PER_NODE)

#define SCX_PICK_IDLE_FLAG(name) __COMPAT_ENUM_OR_ZERO("scx_pick_idle_cpu_flags", #name)

#define SCX_PICK_IDLE_CORE SCX_PICK_IDLE_FLAG(SCX_PICK_IDLE_CORE)
#define SCX_PICK_IDLE_IN_NODE SCX_PICK_IDLE_FLAG(SCX_PICK_IDLE_IN_NODE)

static inline long scx_hotplug_seq(void)
{
	int fd;
	char buf[32];
	ssize_t len;
	long val;

	fd = open("/sys/kernel/sched_ext/hotplug_seq", O_RDONLY);
	if (fd < 0)
		return -ENOENT;

	len = read(fd, buf, sizeof(buf) - 1);
	SCX_BUG_ON(len <= 0, "read failed (%ld)", len);
	buf[len] = 0;
	close(fd);

	val = strtoul(buf, NULL, 10);
	SCX_BUG_ON(val < 0, "invalid num hotplug events: %lu", val);

	return val;
}

/*
 * struct sched_ext_ops can change over time. If compat.bpf.h::SCX_OPS_DEFINE()
 * is used to define ops and compat.h::SCX_OPS_LOAD/ATTACH() are used to load
 * and attach it, backward compatibility is automatically maintained where
 * reasonable.
 *
 * ec7e3b0463e1 ("implement-ops") in https://github.com/sched-ext/sched_ext is
 * the current minimum required kernel version.
 */
#define SCX_OPS_OPEN(__ops_name, __scx_name) ({					\
	struct __scx_name *__skel;						\
										\
	SCX_BUG_ON(!__COMPAT_struct_has_field("sched_ext_ops", "dump"),		\
		   "sched_ext_ops.dump() missing, kernel too old?");		\
										\
	__skel = __scx_name##__open();						\
	SCX_BUG_ON(!__skel, "Could not open " #__scx_name);			\
	__skel->struct_ops.__ops_name->hotplug_seq = scx_hotplug_seq();		\
	SCX_ENUM_INIT(__skel);							\
	__skel; 								\
})

#define SCX_OPS_LOAD(__skel, __ops_name, __scx_name, __uei_name) ({		\
	UEI_SET_SIZE(__skel, __ops_name, __uei_name);				\
	SCX_BUG_ON(__scx_name##__load((__skel)), "Failed to load skel");	\
})

/*
 * New versions of bpftool now emit additional link placeholders for BPF maps,
 * and set up BPF skeleton in such a way that libbpf will auto-attach BPF maps
 * automatically, assumming libbpf is recent enough (v1.5+). Old libbpf will do
 * nothing with those links and won't attempt to auto-attach maps.
 *
 * To maintain compatibility with older libbpf while avoiding trying to attach
 * twice, disable the autoattach feature on newer libbpf.
 */
#if LIBBPF_MAJOR_VERSION > 1 ||							\
	(LIBBPF_MAJOR_VERSION == 1 && LIBBPF_MINOR_VERSION >= 5)
#define __SCX_OPS_DISABLE_AUTOATTACH(__skel, __ops_name)			\
	bpf_map__set_autoattach((__skel)->maps.__ops_name, false)
#else
#define __SCX_OPS_DISABLE_AUTOATTACH(__skel, __ops_name) do {} while (0)
#endif

#define SCX_OPS_ATTACH(__skel, __ops_name, __scx_name) ({			\
	struct bpf_link *__link;						\
	__SCX_OPS_DISABLE_AUTOATTACH(__skel, __ops_name);			\
	SCX_BUG_ON(__scx_name##__attach((__skel)), "Failed to attach skel");	\
	__link = bpf_map__attach_struct_ops((__skel)->maps.__ops_name);		\
	SCX_BUG_ON(!__link, "Failed to attach struct_ops");			\
	__link;									\
})

#endif	/* __SCX_COMPAT_H */
