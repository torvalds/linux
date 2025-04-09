/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

enum summary_field {
	SUMMARY_CURRENT,
	SUMMARY_MIN,
	SUMMARY_MAX,
	SUMMARY_COUNT,
	SUMMARY_SUM,
	SUMMARY_OVERFLOW,
	SUMMARY_FIELD_N
};

#ifndef __bpf__
#ifdef HAVE_BPF_SKEL
int timerlat_bpf_init(struct timerlat_params *params);
int timerlat_bpf_attach(void);
void timerlat_bpf_detach(void);
void timerlat_bpf_destroy(void);
int timerlat_bpf_wait(int timeout);
int timerlat_bpf_get_hist_value(int key,
				long long *value_irq,
				long long *value_thread,
				long long *value_user,
				int cpus);
int timerlat_bpf_get_summary_value(enum summary_field key,
				   long long *value_irq,
				   long long *value_thread,
				   long long *value_user,
				   int cpus);
static inline int have_libbpf_support(void) { return 1; }
#else
static inline int timerlat_bpf_init(struct timerlat_params *params)
{
	return -1;
}
static inline int timerlat_bpf_attach(void) { return -1; }
static inline void timerlat_bpf_detach(void) { };
static inline void timerlat_bpf_destroy(void) { };
static inline int timerlat_bpf_wait(int timeout) { return -1; }
static inline int timerlat_bpf_get_hist_value(int key,
					      long long *value_irq,
					      long long *value_thread,
					      long long *value_user,
					      int cpus)
{
	return -1;
}
static inline int timerlat_bpf_get_summary_value(enum summary_field key,
						 long long *value_irq,
						 long long *value_thread,
						 long long *value_user,
						 int cpus)
{
	return -1;
}
static inline int have_libbpf_support(void) { return 0; }
#endif /* HAVE_BPF_SKEL */
#endif /* __bpf__ */
