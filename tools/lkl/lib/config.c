#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lkl_host.h>

#include "config.h"
#include "../../perf/pmu-events/jsmn.h"

static int jsoneq(const char *json, jsmntok_t *tok, const char *s)
{
	if (tok->type == JSMN_STRING &&
		(int) strlen(s) == tok->end - tok->start &&
		strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
		return 0;
	}
	return -1;
}

static int cfgcpy(char **to, char *from)
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

static int cfgncpy(char **to, char *from, int len)
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
		jsmntok_t *toks, char *jstr, int startpos)
{
	int ifidx, pos, posend, ret;
	char **cfgptr;

	if (!cfg || !toks || !jstr)
		return -1;
	pos = startpos;
	pos++;
	if (toks[pos].type != JSMN_ARRAY) {
		lkl_printf("unexpected json type, json array expected\n");
		return -1;
	}
	cfg->ifnum = toks[pos].size;
	if (cfg->ifnum >= LKL_IF_MAX) {
		lkl_printf("exceeded max number of interface (%d)\n",
				LKL_IF_MAX);
		return -1;
	}
	pos++;
	for (ifidx = 0; ifidx < cfg->ifnum; ifidx++) {
		if (toks[pos].type != JSMN_OBJECT) {
			lkl_printf("object json type expected\n");
			return -1;
		}
		posend = pos + toks[pos].size;
		pos++;
		for (; pos < posend; pos += 2) {
			if (toks[pos].type != JSMN_STRING) {
				lkl_printf("object json type expected\n");
				return -1;
			}
			if (jsoneq(jstr, &toks[pos], "type") == 0) {
				cfgptr = &cfg->iftype[ifidx];
			} else if (jsoneq(jstr, &toks[pos], "param") == 0) {
				cfgptr = &cfg->ifparams[ifidx];
			} else if (jsoneq(jstr, &toks[pos], "mtu") == 0) {
				cfgptr = &cfg->ifmtu_str[ifidx];
			} else if (jsoneq(jstr, &toks[pos], "ip") == 0) {
				cfgptr = &cfg->ifip[ifidx];
			} else if (jsoneq(jstr, &toks[pos], "ipv6") == 0) {
				cfgptr = &cfg->ifipv6[ifidx];
			} else if (jsoneq(jstr, &toks[pos], "ifgateway") == 0) {
				cfgptr = &cfg->ifgateway[ifidx];
			} else if (jsoneq(jstr, &toks[pos],
							"ifgateway6") == 0) {
				cfgptr = &cfg->ifgateway6[ifidx];
			} else if (jsoneq(jstr, &toks[pos], "mac") == 0) {
				cfgptr = &cfg->ifmac_str[ifidx];
			} else if (jsoneq(jstr, &toks[pos], "masklen") == 0) {
				cfgptr = &cfg->ifnetmask_len[ifidx];
			} else if (jsoneq(jstr, &toks[pos], "masklen6") == 0) {
				cfgptr = &cfg->ifnetmask6_len[ifidx];
			} else if (jsoneq(jstr, &toks[pos], "neigh") == 0) {
				cfgptr = &cfg->ifneigh_entries[ifidx];
			} else if (jsoneq(jstr, &toks[pos], "qdisc") == 0) {
				cfgptr = &cfg->ifqdisc_entries[ifidx];
			} else if (jsoneq(jstr, &toks[pos], "offload") == 0) {
				cfgptr = &cfg->ifoffload_str[ifidx];
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

int load_config_json(struct lkl_config *cfg, char *jstr)
{
	int pos, ret;
	char **cfgptr;
	jsmn_parser jp;
	jsmntok_t toks[LKL_CONFIG_JSON_TOKEN_MAX];

	if (!cfg || !jstr)
		return -1;
	jsmn_init(&jp);
	ret = jsmn_parse(&jp, jstr, strlen(jstr), toks, ARRAY_SIZE(toks));
	if (ret != JSMN_SUCCESS) {
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

void show_config(struct lkl_config *cfg)
{
	int i;

	if (!cfg)
		return;
	lkl_printf("gateway: %s\n", cfg->gateway);
	lkl_printf("gateway6: %s\n", cfg->gateway6);
	lkl_printf("debug: %s\n", cfg->debug);
	lkl_printf("mount: %s\n", cfg->mount);
	lkl_printf("singlecpu: %s\n", cfg->single_cpu);
	lkl_printf("sysctl: %s\n", cfg->sysctls);
	lkl_printf("cmdline: %s\n", cfg->boot_cmdline);
	lkl_printf("dump: %s\n", cfg->dump);
	lkl_printf("delay: %s\n", cfg->delay_main);
	for (i = 0; i < cfg->ifnum; i++) {
		lkl_printf("ifmac[%d] = %s\n", i, cfg->ifmac_str[i]);
		lkl_printf("ifmtu[%d] = %s\n", i, cfg->ifmtu_str[i]);
		lkl_printf("iftype[%d] = %s\n", i, cfg->iftype[i]);
		lkl_printf("ifparam[%d] = %s\n", i, cfg->ifparams[i]);
		lkl_printf("ifip[%d] = %s\n", i, cfg->ifip[i]);
		lkl_printf("ifmasklen[%d] = %s\n", i, cfg->ifnetmask_len[i]);
		lkl_printf("ifgateway[%d] = %s\n", i, cfg->ifgateway[i]);
		lkl_printf("ifip6[%d] = %s\n", i, cfg->ifipv6[i]);
		lkl_printf("ifmasklen6[%d] = %s\n", i, cfg->ifnetmask6_len[i]);
		lkl_printf("ifgateway6[%d] = %s\n", i, cfg->ifgateway6[i]);
		lkl_printf("ifoffload[%d] = %s\n", i, cfg->ifoffload_str[i]);
		lkl_printf("ifneigh[%d] = %s\n", i, cfg->ifneigh_entries[i]);
		lkl_printf("ifqdisk[%d] = %s\n", i, cfg->ifqdisc_entries[i]);
	}
}

int load_config_env(struct lkl_config *cfg)
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

	if (!cfg)
		return -1;
	if (envtap || enviftype)
		cfg->ifnum = 1;
	ret = cfgcpy(&cfg->iftap[0], envtap);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->iftype[0], enviftype);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->ifparams[0], envifparams);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->ifmtu_str[0], envmtu_str);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->ifip[0], envip);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->ifipv6[0], envipv6);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->ifgateway[0], envifgateway);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->ifgateway6[0], envifgateway6);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->ifmac_str[0], envmac_str);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->ifnetmask_len[0], envnetmask_len);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->ifnetmask6_len[0], envnetmask6_len);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->ifoffload_str[0], envoffload_str);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->ifneigh_entries[0], envneigh_entries);
	if (ret < 0)
		return ret;
	ret = cfgcpy(&cfg->ifqdisc_entries[0], envqdisc_entries);
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

int init_config(struct lkl_config *cfg)
{
	if (!cfg)
		return -1;

	memset(cfg, 0, sizeof(struct lkl_config));

	return 0;
}

static void free_cfgparam(char *cfgparam)
{
	if (cfgparam)
		free(cfgparam);
}

int clean_config(struct lkl_config *cfg)
{
	int i;

	if (!cfg)
		return -1;
	for (i = 0; i < LKL_IF_MAX; i++) {
		free_cfgparam(cfg->iftap[i]);
		free_cfgparam(cfg->iftype[i]);
		free_cfgparam(cfg->ifparams[i]);
		free_cfgparam(cfg->ifmtu_str[i]);
		free_cfgparam(cfg->ifip[i]);
		free_cfgparam(cfg->ifipv6[i]);
		free_cfgparam(cfg->ifgateway[i]);
		free_cfgparam(cfg->ifgateway6[i]);
		free_cfgparam(cfg->ifmac_str[i]);
		free_cfgparam(cfg->ifnetmask_len[i]);
		free_cfgparam(cfg->ifnetmask6_len[i]);
		free_cfgparam(cfg->ifoffload_str[i]);
		free_cfgparam(cfg->ifneigh_entries[i]);
		free_cfgparam(cfg->ifqdisc_entries[i]);
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
	return 0;
}
