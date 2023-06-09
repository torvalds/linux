#include <stdlib.h>
#define _HAVE_STRING_ARCH_strtok_r
#include <string.h>
#ifndef __MINGW32__
#ifdef __MSYS__
#include <cygwin/socket.h>
#endif
#include <arpa/inet.h>
#else
#define inet_pton lkl_inet_pton
#endif
#include <lkl_host.h>
#include <lkl_config.h>

#ifdef LKL_HOST_CONFIG_JSMN
#include "jsmn.h"

static int jsoneq(const char *json, jsmntok_t *tok, const char *s)
{
	if (tok->type == JSMN_STRING &&
		(int) strlen(s) == tok->end - tok->start &&
		strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
		return 0;
	}
	return -1;
}

static int cfgncpy(char **to, const char *from, int len)
{
	if (!from)
		return 0;
	if (*to)
		free(*to);
	*to = (char *)malloc((len + 1) * sizeof(char));
	if (*to == NULL) {
		lkl_printf("malloc failed\n");
		return -1;
	}
	strncpy(*to, from, len + 1);
	(*to)[len] = '\0';
	return 0;
}

static int parse_ifarr(struct lkl_config *cfg,
		jsmntok_t *toks, const char *jstr, int startpos)
{
	int ifidx, pos, posend, ret;
	char **cfgptr;
	struct lkl_config_iface *iface, *prev = NULL;

	if (!cfg || !toks || !jstr)
		return -1;
	pos = startpos;
	pos++;
	if (toks[pos].type != JSMN_ARRAY) {
		lkl_printf("unexpected json type, json array expected\n");
		return -1;
	}

	cfg->ifnum = toks[pos].size;
	pos++;
	iface = cfg->ifaces;

	for (ifidx = 0; ifidx < cfg->ifnum; ifidx++) {
		if (toks[pos].type != JSMN_OBJECT) {
			lkl_printf("object json type expected\n");
			return -1;
		}

		posend = pos + toks[pos].size * 2;
		pos++;
		iface = malloc(sizeof(struct lkl_config_iface));
		memset(iface, 0, sizeof(struct lkl_config_iface));

		if (prev)
			prev->next = iface;
		else
			cfg->ifaces = iface;
		prev = iface;

		for (; pos < posend; pos += 2) {
			if (toks[pos].type != JSMN_STRING) {
				lkl_printf("object json type expected\n");
				return -1;
			}
			if (jsoneq(jstr, &toks[pos], "type") == 0) {
				cfgptr = &iface->iftype;
			} else if (jsoneq(jstr, &toks[pos], "param") == 0) {
				cfgptr = &iface->ifparams;
			} else if (jsoneq(jstr, &toks[pos], "mtu") == 0) {
				cfgptr = &iface->ifmtu_str;
			} else if (jsoneq(jstr, &toks[pos], "ip") == 0) {
				cfgptr = &iface->ifip;
			} else if (jsoneq(jstr, &toks[pos], "ipv6") == 0) {
				cfgptr = &iface->ifipv6;
			} else if (jsoneq(jstr, &toks[pos], "ifgateway") == 0) {
				cfgptr = &iface->ifgateway;
			} else if (jsoneq(jstr, &toks[pos],
							"ifgateway6") == 0) {
				cfgptr = &iface->ifgateway6;
			} else if (jsoneq(jstr, &toks[pos], "mac") == 0) {
				cfgptr = &iface->ifmac_str;
			} else if (jsoneq(jstr, &toks[pos], "masklen") == 0) {
				cfgptr = &iface->ifnetmask_len;
			} else if (jsoneq(jstr, &toks[pos], "masklen6") == 0) {
				cfgptr = &iface->ifnetmask6_len;
			} else if (jsoneq(jstr, &toks[pos], "neigh") == 0) {
				cfgptr = &iface->ifneigh_entries;
			} else if (jsoneq(jstr, &toks[pos], "qdisc") == 0) {
				cfgptr = &iface->ifqdisc_entries;
			} else if (jsoneq(jstr, &toks[pos], "offload") == 0) {
				cfgptr = &iface->ifoffload_str;
			} else {
				lkl_printf("unexpected key: %.*s\n",
						toks[pos].end-toks[pos].start,
						jstr + toks[pos].start);
				return -1;
			}
			ret = cfgncpy(cfgptr, jstr + toks[pos+1].start,
					toks[pos+1].end-toks[pos+1].start);
			if (ret < 0)
				return ret;
		}
	}
	return pos - startpos;
}

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

int lkl_load_config_json(struct lkl_config *cfg, const char *jstr)
{
	int ret;
	unsigned int pos;
	char **cfgptr;
	jsmn_parser jp;
	jsmntok_t toks[LKL_CONFIG_JSON_TOKEN_MAX];

	if (!cfg || !jstr)
		return -1;
	jsmn_init(&jp);
	ret = jsmn_parse(&jp, jstr, strlen(jstr), toks, ARRAY_SIZE(toks));
	if (ret < 0) {
		lkl_printf("failed to parse json\n");
		return -1;
	}
	if (toks[0].type != JSMN_OBJECT) {
		lkl_printf("object json type expected\n");
		return -1;
	}
	for (pos = 1; pos < jp.toknext; pos++) {
		if (toks[pos].type != JSMN_STRING) {
			lkl_printf("string json type expected\n");
			return -1;
		}
		if (jsoneq(jstr, &toks[pos], "interfaces") == 0) {
			ret = parse_ifarr(cfg, toks, jstr, pos);
			if (ret < 0)
				return ret;
			pos += ret;
			pos--;
			continue;
		}
		if (jsoneq(jstr, &toks[pos], "gateway") == 0) {
			cfgptr = &cfg->gateway;
		} else if (jsoneq(jstr, &toks[pos], "gateway6") == 0) {
			cfgptr = &cfg->gateway6;
		} else if (jsoneq(jstr, &toks[pos], "debug") == 0) {
			cfgptr = &cfg->debug;
		} else if (jsoneq(jstr, &toks[pos], "mount") == 0) {
			cfgptr = &cfg->mount;
		} else if (jsoneq(jstr, &toks[pos], "singlecpu") == 0) {
			cfgptr = &cfg->single_cpu;
		} else if (jsoneq(jstr, &toks[pos], "sysctl") == 0) {
			cfgptr = &cfg->sysctls;
		} else if (jsoneq(jstr, &toks[pos], "boot_cmdline") == 0) {
			cfgptr = &cfg->boot_cmdline;
		} else if (jsoneq(jstr, &toks[pos], "dump") == 0) {
			cfgptr = &cfg->dump;
		} else if (jsoneq(jstr, &toks[pos], "delay_main") == 0) {
			cfgptr = &cfg->delay_main;
		} else if (jsoneq(jstr, &toks[pos], "nameserver") == 0) {
			cfgptr = &cfg->nameserver;
		} else {
			lkl_printf("unexpected key in json %.*s\n",
					toks[pos].end-toks[pos].start,
					jstr + toks[pos].start);
			return -1;
		}
		pos++;
		ret = cfgncpy(cfgptr, jstr + toks[pos].start,
				toks[pos].end-toks[pos].start);
		if (ret < 0)
			return ret;
	}
	return 0;
}
#endif

static int cfgcpy(char **to, const char *from)
{
	if (!from)
		return 0;
	if (*to)
		free(*to);
	*to = (char *)malloc((strlen(from) + 1) * sizeof(char));
	if (*to == NULL) {
		lkl_printf("malloc failed\n");
		return -1;
	}
	strcpy(*to, from);
	return 0;
}

void lkl_show_config(struct lkl_config *cfg)
{
	struct lkl_config_iface *iface;
	int i = 0;

	if (!cfg)
		return;
	lkl_printf("gateway: %s\n", cfg->gateway);
	lkl_printf("gateway6: %s\n", cfg->gateway6);
	lkl_printf("nameserver: %s\n", cfg->nameserver);
	lkl_printf("debug: %s\n", cfg->debug);
	lkl_printf("mount: %s\n", cfg->mount);
	lkl_printf("singlecpu: %s\n", cfg->single_cpu);
	lkl_printf("sysctl: %s\n", cfg->sysctls);
	lkl_printf("cmdline: %s\n", cfg->boot_cmdline);
	lkl_printf("dump: %s\n", cfg->dump);
	lkl_printf("delay: %s\n", cfg->delay_main);

	for (iface = cfg->ifaces; iface; iface = iface->next, i++) {
		lkl_printf("ifmac[%d] = %s\n", i, iface->ifmac_str);
		lkl_printf("ifmtu[%d] = %s\n", i, iface->ifmtu_str);
		lkl_printf("iftype[%d] = %s\n", i, iface->iftype);
		lkl_printf("ifparam[%d] = %s\n", i, iface->ifparams);
		lkl_printf("ifip[%d] = %s\n", i, iface->ifip);
		lkl_printf("ifmasklen[%d] = %s\n", i, iface->ifnetmask_len);
		lkl_printf("ifgateway[%d] = %s\n", i, iface->ifgateway);
		lkl_printf("ifip6[%d] = %s\n", i, iface->ifipv6);
		lkl_printf("ifmasklen6[%d] = %s\n", i, iface->ifnetmask6_len);
		lkl_printf("ifgateway6[%d] = %s\n", i, iface->ifgateway6);
		lkl_printf("ifoffload[%d] = %s\n", i, iface->ifoffload_str);
		lkl_printf("ifneigh[%d] = %s\n", i, iface->ifneigh_entries);
		lkl_printf("ifqdisk[%d] = %s\n", i, iface->ifqdisc_entries);
	}
}

int lkl_load_config_env(struct lkl_config *cfg)
{
	int ret;
	char *envtap = getenv("LKL_HIJACK_NET_TAP");
	char *enviftype = getenv("LKL_HIJACK_NET_IFTYPE");
	char *envifparams = getenv("LKL_HIJACK_NET_IFPARAMS");
	char *envmtu_str = getenv("LKL_HIJACK_NET_MTU");
	char *envip = getenv("LKL_HIJACK_NET_IP");
	char *envipv6 = getenv("LKL_HIJACK_NET_IPV6");
	char *envifgateway = getenv("LKL_HIJACK_NET_IFGATEWAY");
	char *envifgateway6 = getenv("LKL_HIJACK_NET_IFGATEWAY6");
	char *envmac_str = getenv("LKL_HIJACK_NET_MAC");
	char *envnetmask_len = getenv("LKL_HIJACK_NET_NETMASK_LEN");
	char *envnetmask6_len = getenv("LKL_HIJACK_NET_NETMASK6_LEN");
	char *envgateway = getenv("LKL_HIJACK_NET_GATEWAY");
	char *envgateway6 = getenv("LKL_HIJACK_NET_GATEWAY6");
	char *envdebug = getenv("LKL_HIJACK_DEBUG");
	char *envmount = getenv("LKL_HIJACK_MOUNT");
	char *envneigh_entries = getenv("LKL_HIJACK_NET_NEIGHBOR");
	char *envqdisc_entries = getenv("LKL_HIJACK_NET_QDISC");
	char *envsingle_cpu = getenv("LKL_HIJACK_SINGLE_CPU");
	char *envoffload_str = getenv("LKL_HIJACK_OFFLOAD");
	char *envsysctls = getenv("LKL_HIJACK_SYSCTL");
	char *envboot_cmdline = getenv("LKL_HIJACK_BOOT_CMDLINE") ? : "";
	char *envdump = getenv("LKL_HIJACK_DUMP");
	struct lkl_config_iface *iface;

	if (!cfg)
		return -1;
	if (envtap || enviftype)
		cfg->ifnum = 1;

	iface = malloc(sizeof(struct lkl_config_iface));
	memset(iface, 0, sizeof(struct lkl_config_iface));

	ret = cfgcpy(&iface->iftap, envtap);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&iface->iftype, enviftype);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&iface->ifparams, envifparams);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&iface->ifmtu_str, envmtu_str);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&iface->ifip, envip);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&iface->ifipv6, envipv6);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&iface->ifgateway, envifgateway);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&iface->ifgateway6, envifgateway6);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&iface->ifmac_str, envmac_str);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&iface->ifnetmask_len, envnetmask_len);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&iface->ifnetmask6_len, envnetmask6_len);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&iface->ifoffload_str, envoffload_str);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&iface->ifneigh_entries, envneigh_entries);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&iface->ifqdisc_entries, envqdisc_entries);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->gateway, envgateway);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->gateway6, envgateway6);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->debug, envdebug);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->mount, envmount);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->single_cpu, envsingle_cpu);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->sysctls, envsysctls);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->boot_cmdline, envboot_cmdline);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->dump, envdump);
	if (ret < 0)
		return ret;
	return 0;
}

static int parse_mac_str(char *mac_str, __lkl__u8 mac[LKL_ETH_ALEN])
{
	char delim[] = ":";
	char *saveptr = NULL, *token = NULL;
	int i = 0;

	if (!mac_str)
		return 0;

	for (token = strtok_r(mac_str, delim, &saveptr);
	     i < LKL_ETH_ALEN; i++) {
		if (!token) {
			/* The address is too short */
			return -1;
		}

		mac[i] = (__lkl__u8) strtol(token, NULL, 16);
		token = strtok_r(NULL, delim, &saveptr);
	}

	if (strtok_r(NULL, delim, &saveptr)) {
		/* The address is too long */
		return -1;
	}

	return 1;
}

/* Add permanent neighbor entries in the form of "ip|mac;ip|mac;..." */
static void add_neighbor(int ifindex, char *entries)
{
	char *saveptr = NULL, *token = NULL;
	char *ip = NULL, *mac_str = NULL;
	int ret = 0;
	__lkl__u8 mac[LKL_ETH_ALEN];
	char ip_addr[16];
	int af;

	for (token = strtok_r(entries, ";", &saveptr); token;
	     token = strtok_r(NULL, ";", &saveptr)) {
		ip = strtok(token, "|");
		mac_str = strtok(NULL, "|");
		if (ip == NULL || mac_str == NULL || strtok(NULL, "|") != NULL)
			return;

		af = LKL_AF_INET;
		ret = inet_pton(LKL_AF_INET, ip, ip_addr);
		if (ret == 0) {
			ret = inet_pton(LKL_AF_INET6, ip, ip_addr);
			af = LKL_AF_INET6;
		}
		if (ret != 1) {
			lkl_printf("Bad ip address: %s\n", ip);
			return;
		}

		ret = parse_mac_str(mac_str, mac);
		if (ret != 1) {
			lkl_printf("Failed to parse mac: %s\n", mac_str);
			return;
		}
		ret = lkl_add_neighbor(ifindex, af, ip_addr, mac);
		if (ret) {
			lkl_printf("Failed to add neighbor entry: %s\n",
				   lkl_strerror(ret));
			return;
		}
	}
}

/* We don't have an easy way to make FILE*s out of our fds, so we
 * can't use e.g. fgets
 */
static int dump_file(char *path)
{
	int ret = -1, bytes_read = 0;
	char str[1024] = { 0 };
	int fd;

	fd = lkl_sys_open(path, LKL_O_RDONLY, 0);

	if (fd < 0) {
		lkl_printf("%s lkl_sys_open %s: %s\n",
			   __func__, path, lkl_strerror(fd));
		return -1;
	}

	/* Need to print this out in order to make sense of the output */
	lkl_printf("Reading from %s:\n==========\n", path);
	while ((ret = lkl_sys_read(fd, str, sizeof(str) - 1)) > 0)
		bytes_read += lkl_printf("%s", str);
	lkl_printf("==========\n");

	if (ret) {
		lkl_printf("%s lkl_sys_read %s: %s\n",
			   __func__, path, lkl_strerror(ret));
		return -1;
	}

	return 0;
}

static void mount_cmds_exec(char *_cmds, int (*callback)(char *))
{
	char *saveptr = NULL, *token;
	int ret = 0;
	char *cmds = strdup(_cmds);

	token = strtok_r(cmds, ",", &saveptr);

	while (token && ret >= 0) {
		ret = callback(token);
		token = strtok_r(NULL, ",", &saveptr);
	}

	if (ret < 0)
		lkl_printf("%s: failed parsing %s\n", __func__, _cmds);

	free(cmds);
}

static int lkl_config_netdev_create(struct lkl_config *cfg,
				    struct lkl_config_iface *iface)
{
	int ret, offload = 0;
	struct lkl_netdev_args nd_args;
	__lkl__u8 mac[LKL_ETH_ALEN] = {0};
	struct lkl_netdev *nd = NULL;

	if (iface->ifoffload_str)
		offload = strtol(iface->ifoffload_str, NULL, 0);
	memset(&nd_args, 0, sizeof(struct lkl_netdev_args));

	if (iface->iftap) {
		lkl_printf("WARN: LKL_HIJACK_NET_TAP is now obsoleted.\n");
		lkl_printf("use LKL_HIJACK_NET_IFTYPE and PARAMS\n");
		nd = lkl_netdev_tap_create(iface->iftap, offload);
	}

	if (!nd && iface->iftype && iface->ifparams) {
		if ((strcmp(iface->iftype, "tap") == 0)) {
			nd = lkl_netdev_tap_create(iface->ifparams, offload);
		} else if ((strcmp(iface->iftype, "macvtap") == 0)) {
			nd = lkl_netdev_macvtap_create(iface->ifparams,
						       offload);
		} else if ((strcmp(iface->iftype, "dpdk") == 0)) {
			nd = lkl_netdev_dpdk_create(iface->ifparams, offload,
						    mac);
		} else if ((strcmp(iface->iftype, "pipe") == 0)) {
			nd = lkl_netdev_pipe_create(iface->ifparams, offload);
		} else {
			if (offload) {
				lkl_printf("WARN: %s isn't supported on %s\n",
					   "LKL_HIJACK_OFFLOAD",
					   iface->iftype);
				lkl_printf(
					"WARN: Disabling offload features.\n");
			}
			offload = 0;
		}
		if (strcmp(iface->iftype, "vde") == 0)
			nd = lkl_netdev_vde_create(iface->ifparams);
		if (strcmp(iface->iftype, "raw") == 0)
			nd = lkl_netdev_raw_create(iface->ifparams);
	}

	if (nd) {
		if ((mac[0] != 0) || (mac[1] != 0) ||
				(mac[2] != 0) || (mac[3] != 0) ||
				(mac[4] != 0) || (mac[5] != 0)) {
			nd_args.mac = mac;
		} else {
			ret = parse_mac_str(iface->ifmac_str, mac);

			if (ret < 0) {
				lkl_printf("failed to parse mac\n");
				return -1;
			} else if (ret > 0) {
				nd_args.mac = mac;
			} else {
				nd_args.mac = NULL;
			}
		}

		nd_args.offload = offload;
		ret = lkl_netdev_add(nd, &nd_args);
		if (ret < 0) {
			lkl_printf("failed to add netdev: %s\n",
				   lkl_strerror(ret));
			return -1;
		}
		nd->id = ret;
		iface->nd = nd;
	}
	return 0;
}

static int lkl_config_netdev_configure(struct lkl_config *cfg,
				       struct lkl_config_iface *iface)
{
	int ret, nd_ifindex = -1;
	struct lkl_netdev *nd = iface->nd;

	if (!nd) {
		lkl_printf("no netdev available %s\n", iface ? iface->ifparams
			   : "(null)");
		return -1;
	}

	if (nd->id >= 0) {
		nd_ifindex = lkl_netdev_get_ifindex(nd->id);
		if (nd_ifindex > 0)
			lkl_if_up(nd_ifindex);
		else
			lkl_printf(
				"failed to get ifindex for netdev id %d: %s\n",
				nd->id, lkl_strerror(nd_ifindex));
	}

	if (nd_ifindex >= 0 && iface->ifmtu_str) {
		int mtu = atoi(iface->ifmtu_str);

		ret = lkl_if_set_mtu(nd_ifindex, mtu);
		if (ret < 0)
			lkl_printf("failed to set MTU: %s\n",
				   lkl_strerror(ret));
	}

	if (nd_ifindex >= 0 && iface->ifip && iface->ifnetmask_len) {
		unsigned int addr;

		if (inet_pton(LKL_AF_INET, iface->ifip,
			      (struct lkl_in_addr *)&addr) != 1)
			lkl_printf("Invalid ipv4 address: %s\n", iface->ifip);

		int nmlen = atoi(iface->ifnetmask_len);

		if (addr != LKL_INADDR_NONE && nmlen > 0 && nmlen < 32) {
			ret = lkl_if_set_ipv4(nd_ifindex, addr, nmlen);
			if (ret < 0)
				lkl_printf("failed to set IPv4 address: %s\n",
					   lkl_strerror(ret));
		}
		if (iface->ifgateway) {
			unsigned int gwaddr;

			if (inet_pton(LKL_AF_INET, iface->ifgateway,
				      (struct lkl_in_addr *)&gwaddr) != 1)
				lkl_printf("Invalid ipv4 gateway: %s\n",
					   iface->ifgateway);

			if (gwaddr != LKL_INADDR_NONE) {
				ret = lkl_if_set_ipv4_gateway(nd_ifindex,
						addr, nmlen, gwaddr);
				if (ret < 0)
					lkl_printf(
						"failed to set v4 if gw: %s\n",
						lkl_strerror(ret));
			}
		}
	}

	if (nd_ifindex >= 0 && iface->ifipv6 &&
			iface->ifnetmask6_len) {
		struct lkl_in6_addr addr;
		unsigned int pflen = atoi(iface->ifnetmask6_len);

		if (inet_pton(LKL_AF_INET6, iface->ifipv6,
			      (struct lkl_in6_addr *)&addr) != 1) {
			lkl_printf("Invalid ipv6 addr: %s\n",
				   iface->ifipv6);
		}  else {
			ret = lkl_if_set_ipv6(nd_ifindex, &addr, pflen);
			if (ret < 0)
				lkl_printf("failed to set IPv6 address: %s\n",
					   lkl_strerror(ret));
		}
		if (iface->ifgateway6) {
			char gwaddr[16];

			if (inet_pton(LKL_AF_INET6, iface->ifgateway6,
								gwaddr) != 1) {
				lkl_printf("Invalid ipv6 gateway: %s\n",
					   iface->ifgateway6);
			} else {
				ret = lkl_if_set_ipv6_gateway(nd_ifindex,
						&addr, pflen, gwaddr);
				if (ret < 0)
					lkl_printf(
						"failed to set v6 if gw: %s\n",
						lkl_strerror(ret));
			}
		}
	}

	if (nd_ifindex >= 0 && iface->ifneigh_entries)
		add_neighbor(nd_ifindex, iface->ifneigh_entries);

	if (nd_ifindex >= 0 && iface->ifqdisc_entries)
		lkl_qdisc_parse_add(nd_ifindex, iface->ifqdisc_entries);

	return 0;
}

static void free_cfgparam(char *cfgparam)
{
	if (cfgparam)
		free(cfgparam);
}

static int lkl_clean_config(struct lkl_config *cfg)
{
	struct lkl_config_iface *iface;

	if (!cfg)
		return -1;

	for (iface = cfg->ifaces; iface; iface = iface->next) {
		free_cfgparam(iface->iftap);
		free_cfgparam(iface->iftype);
		free_cfgparam(iface->ifparams);
		free_cfgparam(iface->ifmtu_str);
		free_cfgparam(iface->ifip);
		free_cfgparam(iface->ifipv6);
		free_cfgparam(iface->ifgateway);
		free_cfgparam(iface->ifgateway6);
		free_cfgparam(iface->ifmac_str);
		free_cfgparam(iface->ifnetmask_len);
		free_cfgparam(iface->ifnetmask6_len);
		free_cfgparam(iface->ifoffload_str);
		free_cfgparam(iface->ifneigh_entries);
		free_cfgparam(iface->ifqdisc_entries);
	}
	free_cfgparam(cfg->gateway);
	free_cfgparam(cfg->gateway6);
	free_cfgparam(cfg->debug);
	free_cfgparam(cfg->mount);
	free_cfgparam(cfg->single_cpu);
	free_cfgparam(cfg->sysctls);
	free_cfgparam(cfg->boot_cmdline);
	free_cfgparam(cfg->dump);
	free_cfgparam(cfg->delay_main);
	free_cfgparam(cfg->nameserver);
	return 0;
}


int lkl_load_config_pre(struct lkl_config *cfg)
{
	int lkl_debug, ret;
	struct lkl_config_iface *iface;

	if (!cfg)
		return 0;

	if (cfg->debug)
		lkl_debug = strtol(cfg->debug, NULL, 0);

	if (!cfg->debug || (lkl_debug == 0))
		lkl_host_ops.print = NULL;

	for (iface = cfg->ifaces; iface; iface = iface->next) {
		ret = lkl_config_netdev_create(cfg, iface);
		if (ret < 0)
			return -1;
	}

	return 0;
}

int lkl_load_config_post(struct lkl_config *cfg)
{
	int ret;
	struct lkl_config_iface *iface;

	if (!cfg)
		return 0;

	if (cfg->mount)
		mount_cmds_exec(cfg->mount, lkl_mount_fs);

	for (iface = cfg->ifaces; iface; iface = iface->next) {
		ret = lkl_config_netdev_configure(cfg, iface);
		if (ret < 0)
			break;
	}

	if (cfg->gateway) {
		unsigned int gwaddr;

		if (inet_pton(LKL_AF_INET, cfg->gateway,
			      (struct lkl_in_addr *)&gwaddr) != 1)
			lkl_printf("Invalid ipv4 gateway: %s\n", cfg->gateway);

		if (gwaddr != LKL_INADDR_NONE) {
			ret = lkl_set_ipv4_gateway(gwaddr);
			if (ret < 0)
				lkl_printf("failed to set IPv4 gateway: %s\n",
					   lkl_strerror(ret));
		}
	}

	if (cfg->gateway6) {
		char gw[16];

		if (inet_pton(LKL_AF_INET6, cfg->gateway6, gw) != 1) {
			lkl_printf("Invalid ipv6 gateway: %s\n", cfg->gateway6);
		} else {
			ret = lkl_set_ipv6_gateway(gw);
			if (ret < 0)
				lkl_printf("failed to set IPv6 gateway: %s\n",
					   lkl_strerror(ret));
		}
	}

	if (cfg->sysctls)
		lkl_sysctl_parse_write(cfg->sysctls);

	/* put a delay before calling main() */
	if (cfg->delay_main) {
		unsigned long delay = strtoul(cfg->delay_main, NULL, 10);

		if (delay == ~0UL)
			lkl_printf("got invalid delay_main value (%s)\n",
				   cfg->delay_main);
		else {
			lkl_printf("sleeping %lu usec\n", delay);
			usleep(delay);
		}
	}

	if (cfg->nameserver) {
		int fd;
		char ns[32] = "nameserver ";

		/* ignore error */
		lkl_sys_mkdir("/etc", 0xff);
		lkl_sys_chdir("/etc");
		fd = lkl_sys_open("/etc/resolv.conf", LKL_O_CREAT | LKL_O_RDWR, 0);

		strcat(ns, cfg->nameserver);
		lkl_sys_write(fd, ns, sizeof(ns));
		lkl_sys_close(fd);
	}

	return 0;
}

int lkl_unload_config(struct lkl_config *cfg)
{
	struct lkl_config_iface *iface;

	if (cfg) {
		if (cfg->dump)
			mount_cmds_exec(cfg->dump, dump_file);

		for (iface = cfg->ifaces; iface; iface = iface->next) {
			if (iface->nd) {
				if (iface->nd->id >= 0)
					lkl_netdev_remove(iface->nd->id);
				lkl_netdev_free(iface->nd);
			}
		}

		lkl_clean_config(cfg);
	}

	return 0;
}
