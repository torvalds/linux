// SPDX-License-Identifier: GPL-2.0-only
#ifndef XDP_SAMPLE_USER_H
#define XDP_SAMPLE_USER_H

#include <bpf/libbpf.h>
#include <linux/compiler.h>

#include "xdp_sample_shared.h"

enum stats_mask {
	_SAMPLE_REDIRECT_MAP        = 1U << 0,
	SAMPLE_RX_CNT               = 1U << 1,
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
#pragma GCC diagnostic ignored "-Wstringop-truncation"
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

#define DEFINE_SAMPLE_INIT(name)                                               \
	static int sample_init(struct name *skel, int mask)                    \
	{                                                                      \
		int ret;                                                       \
		ret = __sample_init(mask);                                     \
		if (ret < 0)                                                   \
			return ret;                                            \
		return 0;                                                      \
	}

#endif
