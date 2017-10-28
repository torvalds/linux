#ifndef _LKL_LIB_CONFIG_H
#define _LKL_LIB_CONFIG_H

#define LKL_CONFIG_JSON_TOKEN_MAX 300
/* TODO dynamically allocate interface info arr */
#define LKL_IF_MAX 16

struct lkl_config {
	int ifnum;
	/* OBSOLETE: should use IFTYPE and IFPARAMS */
	char *iftap[LKL_IF_MAX];
	char *iftype[LKL_IF_MAX];
	char *ifparams[LKL_IF_MAX];
	char *ifmtu_str[LKL_IF_MAX];
	char *ifip[LKL_IF_MAX];
	char *ifipv6[LKL_IF_MAX];
	char *ifgateway[LKL_IF_MAX];
	char *ifgateway6[LKL_IF_MAX];
	char *ifmac_str[LKL_IF_MAX];
	char *ifnetmask_len[LKL_IF_MAX];
	char *ifnetmask6_len[LKL_IF_MAX];
	char *ifoffload_str[LKL_IF_MAX];
	char *ifneigh_entries[LKL_IF_MAX];
	char *ifqdisc_entries[LKL_IF_MAX];

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
};

int init_config(struct lkl_config *cfg);
int load_config_json(struct lkl_config *cfg, char *jstr);
int load_config_env(struct lkl_config *cfg);
int clean_config(struct lkl_config *cfg);
void show_config(struct lkl_config *cfg);

#endif /* _LKL_LIB_CONFIG_H */
