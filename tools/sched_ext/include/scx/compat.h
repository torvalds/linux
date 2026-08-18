/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */
#ifndef __SCX_COMPAT_H
#define __SCX_COMPAT_H

#include <bpf/btf.h>
#include <bpf/libbpf.h>
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
#define SCX_OPS_ALWAYS_ENQ_IMMED SCX_OPS_FLAG(SCX_OPS_ALWAYS_ENQ_IMMED)

#define SCX_PICK_IDLE_FLAG(name) __COMPAT_ENUM_OR_ZERO("scx_pick_idle_cpu_flags", #name)

#define SCX_PICK_IDLE_CORE SCX_PICK_IDLE_FLAG(SCX_PICK_IDLE_CORE)
#define SCX_PICK_IDLE_IN_NODE SCX_PICK_IDLE_FLAG(SCX_PICK_IDLE_IN_NODE)

static inline long scx_hotplug_seq(void)
{
	int fd;
	char buf[32];
	char *endptr;
	ssize_t len;
	long val;

	fd = open("/sys/kernel/sched_ext/hotplug_seq", O_RDONLY);
	if (fd < 0)
		return -ENOENT;

	len = read(fd, buf, sizeof(buf) - 1);
	SCX_BUG_ON(len <= 0, "read failed (%ld)", len);
	buf[len] = 0;
	close(fd);

	errno = 0;
	val = strtoul(buf, &endptr, 10);
	SCX_BUG_ON(errno == ERANGE || endptr == buf ||
		   (*endptr != '\n' && *endptr != '\0'), "invalid num hotplug events: %ld", val);

	return val;
}

/*
 * Open the sched_ext_ops skeleton.
 *
 * struct sched_ext_ops can change over time. Two complementary mechanisms
 * keep BPF schedulers built against newer headers running on older kernels:
 *
 * 1. Load-time fix-up (this macro). For each optional ops callback or field
 *    added to struct sched_ext_ops, an explicit stanza below probes the
 *    running kernel's BTF via __COMPAT_struct_has_field() and, if the field
 *    is missing, clears it in the in-memory struct_ops (with a warning to
 *    stderr) before load. Handles additive changes - a new stanza must be
 *    added here for each new optional field.
 *
 * 2. Multi-variant struct_ops via compat.bpf.h::SCX_OPS_DEFINE(). That
 *    macro can be expanded to emit several variants of struct sched_ext_ops,
 *    and SCX_OPS_LOAD()/ATTACH() can pick the right one based on what the
 *    kernel supports. Needed when an existing operation has to change
 *    incompatibly (e.g. a callback signature changes); the load-time
 *    fix-up above only handles purely additive changes.
 *
 * ec7e3b0463e1 ("implement-ops") in https://github.com/sched-ext/sched_ext is
 * the current minimum required kernel version.
 *
 * COMPAT:
 * - v6.17: ops.cgroup_set_bandwidth()
 * - v6.19: ops.cgroup_set_idle()
 * - v7.1:  ops.sub_attach(), ops.sub_detach(), ops.sub_cgroup_id
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
	if (__skel->struct_ops.__ops_name->cgroup_set_bandwidth &&		\
	    !__COMPAT_struct_has_field("sched_ext_ops", "cgroup_set_bandwidth")) { \
		fprintf(stderr, "WARNING: kernel doesn't support ops.cgroup_set_bandwidth()\n"); \
		__skel->struct_ops.__ops_name->cgroup_set_bandwidth = NULL;	\
	}									\
	if (__skel->struct_ops.__ops_name->cgroup_set_idle &&			\
	    !__COMPAT_struct_has_field("sched_ext_ops", "cgroup_set_idle")) { \
		fprintf(stderr, "WARNING: kernel doesn't support ops.cgroup_set_idle()\n"); \
		__skel->struct_ops.__ops_name->cgroup_set_idle = NULL;	\
	}									\
	if (__skel->struct_ops.__ops_name->sub_attach &&			\
	    !__COMPAT_struct_has_field("sched_ext_ops", "sub_attach")) {	\
		fprintf(stderr, "WARNING: kernel doesn't support ops.sub_attach()\n"); \
		__skel->struct_ops.__ops_name->sub_attach = NULL;		\
	}									\
	if (__skel->struct_ops.__ops_name->sub_detach &&			\
	    !__COMPAT_struct_has_field("sched_ext_ops", "sub_detach")) {	\
		fprintf(stderr, "WARNING: kernel doesn't support ops.sub_detach()\n"); \
		__skel->struct_ops.__ops_name->sub_detach = NULL;		\
	}									\
	if (__skel->struct_ops.__ops_name->sub_cgroup_id > 0 &&		\
	    !__COMPAT_struct_has_field("sched_ext_ops", "sub_cgroup_id")) { \
		fprintf(stderr, "WARNING: kernel doesn't support ops.sub_cgroup_id\n"); \
		__skel->struct_ops.__ops_name->sub_cgroup_id = 0;		\
	}									\
	__skel; 								\
})

/*
 * Associate non-struct_ops BPF programs with the scheduler's struct_ops map so
 * that scx_prog_sched() can determine which scheduler a BPF program belongs
 * to. Requires libbpf >= 1.7.
 */
#if LIBBPF_MAJOR_VERSION > 1 ||							\
	(LIBBPF_MAJOR_VERSION == 1 && LIBBPF_MINOR_VERSION >= 7)
static inline void __scx_ops_assoc_prog(struct bpf_program *prog,
					struct bpf_map *map,
					const char *ops_name)
{
	s32 err = bpf_program__assoc_struct_ops(prog, map, NULL);
	if (err)
		fprintf(stderr,
			"ERROR: Failed to associate %s with %s: %d\n",
			bpf_program__name(prog), ops_name, err);
}
#else
static inline void __scx_ops_assoc_prog(struct bpf_program *prog,
					struct bpf_map *map,
					const char *ops_name)
{
}
#endif

/* See SCX_OPS_OPEN() above for backward-compatibility handling. */
#define SCX_OPS_LOAD(__skel, __ops_name, __scx_name, __uei_name) ({		\
	struct bpf_program *__prog;						\
	UEI_SET_SIZE(__skel, __ops_name, __uei_name);				\
	SCX_BUG_ON(__scx_name##__load((__skel)), "Failed to load skel");	\
	bpf_object__for_each_program(__prog, (__skel)->obj) {			\
		if (bpf_program__type(__prog) == BPF_PROG_TYPE_STRUCT_OPS)	\
			continue;						\
		__scx_ops_assoc_prog(__prog, (__skel)->maps.__ops_name,		\
				     #__ops_name);				\
	}									\
})

/*
 * New versions of bpftool now emit additional link placeholders for BPF maps,
 * and set up BPF skeleton in such a way that libbpf will auto-attach BPF maps
 * automatically, assuming libbpf is recent enough (v1.5+). Old libbpf will do
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
