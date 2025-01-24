/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_UTIL_BTF
#define __PERF_UTIL_BTF 1

struct btf;
struct btf_member;

const struct btf_member *__btf_type__find_member_by_name(struct btf *btf,
							 int type_id, const char *member_name);
#endif // __PERF_UTIL_BTF
