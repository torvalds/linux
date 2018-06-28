// SPDX-License-Identifier: GPL-2.0
#ifndef _PERF_BPF_H
#define _PERF_BPF_H
#define SEC(NAME) __attribute__((section(NAME),  used))

#define probe(function, vars) \
	SEC(#function "=" #function " " #vars) function

#define license(name) \
char _license[] SEC("license") = #name; \
int _version SEC("version") = LINUX_VERSION_CODE;

#endif /* _PERF_BPF_H */
