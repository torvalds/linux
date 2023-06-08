#ifndef _LKL_LIB_CONFIG_H
#define _LKL_LIB_CONFIG_H

#include "lkl_autoconf.h"

#define LKL_CONFIG_JSON_TOKEN_MAX 300

struct lkl_config_iface {
	struct lkl_config_iface *next;
	struct lkl_netdev *nd;

	/* OBSOLETE: should use IFTYPE and IFPARAMS */
	char *iftap;
	char *iftype;
	char *ifparams;
	char *ifmtu_str;
	char *ifip;
	char *ifipv6;
	char *ifgateway;
	char *ifgateway6;
	char *ifmac_str;
	char *ifnetmask_len;
	char *ifnetmask6_len;
	char *ifoffload_str;
	char *ifneigh_entries;
	char *ifqdisc_entries;
};

struct lkl_config {
	int ifnum;
	struct lkl_config_iface *ifaces;

	char *gateway;
	char *gateway6;
	char *debug;
	char *mount;
	/* single_cpu mode:
	 * 0: Don't pin to single CPU (default).
	 * 1: Pin only LKL kernel threads to single CPU.
	 * 2: Pin all LKL threads to single CPU including all LKL kernel threads
	 * and device polling threads. Avoid this mode if having busy polling
	 * threads.
	 *
	 * mode 2 can achieve better TCP_RR but worse TCP_STREAM than mode 1.
	 * You should choose the best for your application and virtio device
	 * type.
	 */
	char *single_cpu;
	char *sysctls;
	char *boot_cmdline;
	char *dump;
	char *delay_main;
	char *nameserver;
};

#ifdef LKL_HOST_CONFIG_JSMN
int lkl_load_config_json(struct lkl_config *cfg, const char *jstr);
#else
static inline int lkl_load_config_json(struct lkl_config *cfg,
				const char *jstr)
{
	return -1;
}
#endif
int lkl_load_config_env(struct lkl_config *cfg);
void lkl_show_config(struct lkl_config *cfg);
int lkl_load_config_pre(struct lkl_config *cfg);
int lkl_load_config_post(struct lkl_config *cfg);
int lkl_unload_config(struct lkl_config *cfg);

#endif /* _LKL_LIB_CONFIG_H */
