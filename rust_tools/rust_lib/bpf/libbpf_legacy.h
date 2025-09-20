/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

/*
 * Libbpf legacy APIs (either discouraged or deprecated, as mentioned in [0])
 *
 *   [0] https://docs.google.com/document/d/1UyjTZuPFWiPFyKk1tV5an11_iaRuec6U-ZESZ54nNTY
 *
 * Copyright (C) 2021 Facebook
 */
#ifndef __LIBBPF_LEGACY_BPF_H
#define __LIBBPF_LEGACY_BPF_H

#include <linux/bpf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "libbpf_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* As of libbpf 1.0 libbpf_set_strict_mode() and enum libbpf_struct_mode have
 * no effect. But they are left in libbpf_legacy.h so that applications that
 * prepared for libbpf 1.0 before final release by using
 * libbpf_set_strict_mode() still work with libbpf 1.0+ without any changes.
 */
enum libbpf_strict_mode {
	/* Turn on all supported strict features of libbpf to simulate libbpf
	 * v1.0 behavior.
	 * This will be the default behavior in libbpf v1.0.
	 */
	LIBBPF_STRICT_ALL = 0xffffffff,

	/*
	 * Disable any libbpf 1.0 behaviors. This is the default before libbpf
	 * v1.0. It won't be supported anymore in v1.0, please update your
	 * code so that it handles LIBBPF_STRICT_ALL mode before libbpf v1.0.
	 */
	LIBBPF_STRICT_NONE = 0x00,
	/*
	 * Return NULL pointers on error, not ERR_PTR(err).
	 * Additionally, libbpf also always sets errno to corresponding Exx
	 * (positive) error code.
	 */
	LIBBPF_STRICT_CLEAN_PTRS = 0x01,
	/*
	 * Return actual error codes from low-level APIs directly, not just -1.
	 * Additionally, libbpf also always sets errno to corresponding Exx
	 * (positive) error code.
	 */
	LIBBPF_STRICT_DIRECT_ERRS = 0x02,
	/*
	 * Enforce strict BPF program section (SEC()) names.
	 * E.g., while prefiously SEC("xdp_whatever") or SEC("perf_event_blah") were
	 * allowed, with LIBBPF_STRICT_SEC_PREFIX this will become
	 * unrecognized by libbpf and would have to be just SEC("xdp") and
	 * SEC("xdp") and SEC("perf_event").
	 *
	 * Note, in this mode the program pin path will be based on the
	 * function name instead of section name.
	 *
	 * Additionally, routines in the .text section are always considered
	 * sub-programs. Legacy behavior allows for a single routine in .text
	 * to be a program.
	 */
	LIBBPF_STRICT_SEC_NAME = 0x04,
	/*
	 * Disable the global 'bpf_objects_list'. Maintaining this list adds
	 * a race condition to bpf_object__open() and bpf_object__close().
	 * Clients can maintain it on their own if it is valuable for them.
	 */
	LIBBPF_STRICT_NO_OBJECT_LIST = 0x08,
	/*
	 * Automatically bump RLIMIT_MEMLOCK using setrlimit() before the
	 * first BPF program or map creation operation. This is done only if
	 * kernel is too old to support memcg-based memory accounting for BPF
	 * subsystem. By default, RLIMIT_MEMLOCK limit is set to RLIM_INFINITY,
	 * but it can be overridden with libbpf_set_memlock_rlim() API.
	 * Note that libbpf_set_memlock_rlim() needs to be called before
	 * the very first bpf_prog_load(), bpf_map_create() or bpf_object__load()
	 * operation.
	 */
	LIBBPF_STRICT_AUTO_RLIMIT_MEMLOCK = 0x10,
	/*
	 * Error out on any SEC("maps") map definition, which are deprecated
	 * in favor of BTF-defined map definitions in SEC(".maps").
	 */
	LIBBPF_STRICT_MAP_DEFINITIONS = 0x20,

	__LIBBPF_STRICT_LAST,
};

LIBBPF_API int libbpf_set_strict_mode(enum libbpf_strict_mode mode);

/**
 * @brief **libbpf_get_error()** extracts the error code from the passed
 * pointer
 * @param ptr pointer returned from libbpf API function
 * @return error code; or 0 if no error occurred
 *
 * Note, as of libbpf 1.0 this function is not necessary and not recommended
 * to be used. Libbpf doesn't return error code embedded into the pointer
 * itself. Instead, NULL is returned on error and error code is passed through
 * thread-local errno variable. **libbpf_get_error()** is just returning -errno
 * value if it receives NULL, which is correct only if errno hasn't been
 * modified between libbpf API call and corresponding **libbpf_get_error()**
 * call. Prefer to check return for NULL and use errno directly.
 *
 * This API is left in libbpf 1.0 to allow applications that were 1.0-ready
 * before final libbpf 1.0 without needing to change them.
 */
LIBBPF_API long libbpf_get_error(const void *ptr);

#define DECLARE_LIBBPF_OPTS LIBBPF_OPTS

/* "Discouraged" APIs which don't follow consistent libbpf naming patterns.
 * They are normally a trivial aliases or wrappers for proper APIs and are
 * left to minimize unnecessary disruption for users of libbpf. But they
 * shouldn't be used going forward.
 */

struct bpf_program;
struct bpf_map;
struct btf;
struct btf_ext;

LIBBPF_API struct btf *libbpf_find_kernel_btf(void);

LIBBPF_API enum bpf_prog_type bpf_program__get_type(const struct bpf_program *prog);
LIBBPF_API enum bpf_attach_type bpf_program__get_expected_attach_type(const struct bpf_program *prog);
LIBBPF_API const char *bpf_map__get_pin_path(const struct bpf_map *map);
LIBBPF_API const void *btf__get_raw_data(const struct btf *btf, __u32 *size);
LIBBPF_API const void *btf_ext__get_raw_data(const struct btf_ext *btf_ext, __u32 *size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __LIBBPF_LEGACY_BPF_H */
