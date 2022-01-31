// SPDX-License-Identifier: GPL-2.0-only
#ifndef XDP_SAMPLE_USER_H
#define XDP_SAMPLE_USER_H

#include <bpf/libbpf.h>
#include <linux/compiler.h>

#include "xdp_sample_shared.h"

enum stats_mask {
	_SAMPLE_REDIRECT_MAP         = 1U << 0,
	SAMPLE_RX_CNT                = 1U << 1,
	SAMPLE_REDIRECT_ERR_CNT      = 1U << 2,
	SAMPLE_CPUMAP_ENQUEUE_CNT    = 1U << 3,
	SAMPLE_CPUMAP_KTHREAD_CNT    = 1U << 4,
	SAMPLE_EXCEPTION_CNT         = 1U << 5,
	SAMPLE_DEVMAP_XMIT_CNT       = 1U << 6,
	SAMPLE_REDIRECT_CNT          = 1U << 7,
	SAMPLE_REDIRECT_MAP_CNT      = SAMPLE_REDIRECT_CNT | _SAMPLE_REDIRECT_MAP,
	SAMPLE_REDIRECT_ERR_MAP_CNT  = SAMPLE_REDIRECT_ERR_CNT | _SAMPLE_REDIRECT_MAP,
	SAMPLE_DEVMAP_XMIT_CNT_MULTI = 1U << 8,
	SAMPLE_SKIP_HEADING	     = 1U << 9,
};

/* Exit return codes */
#define EXIT_OK			0
#define EXIT_FAIL		1
#define EXIT_FAIL_OPTION	2
#define EXIT_FAIL_XDP		3
#define EXIT_FAIL_BPF		4
#define EXIT_FAIL_MEM		5

int sample_setup_maps(struct bpf_map **maps);
int __sample_init(int mask);
void sample_exit(int status);
int sample_run(int interval, void (*post_cb)(void *), void *ctx);

void sample_switch_mode(void);
int sample_install_xdp(struct bpf_program *xdp_prog, int ifindex, bool generic,
		       bool force);
void sample_usage(char *argv[], const struct option *long_options,
		  const char *doc, int mask, bool error);

const char *get_driver_name(int ifindex);
int get_mac_addr(int ifindex, void *mac_addr);

#pragma GCC diagnostic push
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#endif
__attribute__((unused))
static inline char *safe_strncpy(char *dst, const char *src, size_t size)
{
	if (!size)
		return dst;
	strncpy(dst, src, size - 1);
	dst[size - 1] = '\0';
	return dst;
}
#pragma GCC diagnostic pop

#define __attach_tp(name)                                                      \
	({                                                                     \
		if (!bpf_program__is_tracing(skel->progs.name))                \
			return -EINVAL;                                        \
		skel->links.name = bpf_program__attach(skel->progs.name);      \
		if (!skel->links.name)                                         \
			return -errno;                                         \
	})

#define sample_init_pre_load(skel)                                             \
	({                                                                     \
		skel->rodata->nr_cpus = libbpf_num_possible_cpus();            \
		sample_setup_maps((struct bpf_map *[]){                        \
			skel->maps.rx_cnt, skel->maps.redir_err_cnt,           \
			skel->maps.cpumap_enqueue_cnt,                         \
			skel->maps.cpumap_kthread_cnt,                         \
			skel->maps.exception_cnt, skel->maps.devmap_xmit_cnt,  \
			skel->maps.devmap_xmit_cnt_multi });                   \
	})

#define DEFINE_SAMPLE_INIT(name)                                               \
	static int sample_init(struct name *skel, int mask)                    \
	{                                                                      \
		int ret;                                                       \
		ret = __sample_init(mask);                                     \
		if (ret < 0)                                                   \
			return ret;                                            \
		if (mask & SAMPLE_REDIRECT_MAP_CNT)                            \
			__attach_tp(tp_xdp_redirect_map);                      \
		if (mask & SAMPLE_REDIRECT_CNT)                                \
			__attach_tp(tp_xdp_redirect);                          \
		if (mask & SAMPLE_REDIRECT_ERR_MAP_CNT)                        \
			__attach_tp(tp_xdp_redirect_map_err);                  \
		if (mask & SAMPLE_REDIRECT_ERR_CNT)                            \
			__attach_tp(tp_xdp_redirect_err);                      \
		if (mask & SAMPLE_CPUMAP_ENQUEUE_CNT)                          \
			__attach_tp(tp_xdp_cpumap_enqueue);                    \
		if (mask & SAMPLE_CPUMAP_KTHREAD_CNT)                          \
			__attach_tp(tp_xdp_cpumap_kthread);                    \
		if (mask & SAMPLE_EXCEPTION_CNT)                               \
			__attach_tp(tp_xdp_exception);                         \
		if (mask & SAMPLE_DEVMAP_XMIT_CNT)                             \
			__attach_tp(tp_xdp_devmap_xmit);                       \
		if (mask & SAMPLE_DEVMAP_XMIT_CNT_MULTI)                       \
			__attach_tp(tp_xdp_devmap_xmit_multi);                 \
		return 0;                                                      \
	}

#endif
