/*-
 * Copyright (c) 2011 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: Navdeep Parhar <np@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <net/sff8472.h>
#include <netinet/in.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pcap.h>

#include "t4_ioctl.h"
#include "tcb_common.h"

#define in_range(val, lo, hi) ( val < 0 || (val <= hi && val >= lo))
#define	max(x, y) ((x) > (y) ? (x) : (y))

static const char *progname, *nexus;
static int chip_id;	/* 4 for T4, 5 for T5 */

struct reg_info {
	const char *name;
	uint32_t addr;
	uint32_t len;
};

struct mod_regs {
	const char *name;
	const struct reg_info *ri;
};

struct field_desc {
	const char *name;     /* Field name */
	unsigned short start; /* Start bit position */
	unsigned short end;   /* End bit position */
	unsigned char shift;  /* # of low order bits omitted and implicitly 0 */
	unsigned char hex;    /* Print field in hex instead of decimal */
	unsigned char islog2; /* Field contains the base-2 log of the value */
};

#include "reg_defs_t4.c"
#include "reg_defs_t5.c"
#include "reg_defs_t6.c"
#include "reg_defs_t4vf.c"

static void
usage(FILE *fp)
{
	fprintf(fp, "Usage: %s <nexus> [operation]\n", progname);
	fprintf(fp,
	    "\tclearstats <port>                   clear port statistics\n"
	    "\tcontext <type> <id>                 show an SGE context\n"
	    "\tdumpstate <dump.bin>                dump chip state\n"
	    "\tfilter <idx> [<param> <val>] ...    set a filter\n"
	    "\tfilter <idx> delete|clear [prio 1]  delete a filter\n"
	    "\tfilter list                         list all filters\n"
	    "\tfilter mode [<match>] ...           get/set global filter mode\n"
	    "\thashfilter [<param> <val>] ...      set a hashfilter\n"
	    "\thashfilter <idx> delete|clear       delete a hashfilter\n"
	    "\thashfilter list                     list all hashfilters\n"
	    "\thashfilter mode                     get global hashfilter mode\n"
	    "\ti2c <port> <devaddr> <addr> [<len>] read from i2c device\n"
	    "\tloadboot <bi.bin> [pf|offset <val>] install boot image\n"
	    "\tloadboot clear [pf|offset <val>]    remove boot image\n"
	    "\tloadboot-cfg <bc.bin>               install boot config\n"
	    "\tloadboot-cfg clear                  remove boot config\n"
	    "\tloadcfg <fw-config.txt>             install configuration file\n"
	    "\tloadcfg clear                       remove configuration file\n"
	    "\tloadfw <fw-image.bin>               install firmware\n"
	    "\tmemdump <addr> <len>                dump a memory range\n"
	    "\tmodinfo <port> [raw]                optics/cable information\n"
	    "\tpolicy <policy.txt>                 install offload policy\n"
	    "\tpolicy clear                        remove offload policy\n"
	    "\treg <address>[=<val>]               read/write register\n"
	    "\treg64 <address>[=<val>]             read/write 64 bit register\n"
	    "\tregdump [<module>] ...              dump registers\n"
	    "\tsched-class params <param> <val> .. configure TX scheduler class\n"
	    "\tsched-queue <port> <queue> <class>  bind NIC queues to TX Scheduling class\n"
	    "\tstdio                               interactive mode\n"
	    "\ttcb <tid>                           read TCB\n"
	    "\ttracer <idx> tx<n>|rx<n>            set and enable a tracer\n"
	    "\ttracer <idx> disable|enable         disable or enable a tracer\n"
	    "\ttracer list                         list all tracers\n"
	    );
}

static inline unsigned int
get_card_vers(unsigned int version)
{
	return (version & 0x3ff);
}

static int
real_doit(unsigned long cmd, void *data, const char *cmdstr)
{
	static int fd = -1;
	int rc = 0;

	if (fd == -1) {
		char buf[64];

		snprintf(buf, sizeof(buf), "/dev/%s", nexus);
		if ((fd = open(buf, O_RDWR)) < 0) {
			warn("open(%s)", nexus);
			rc = errno;
			return (rc);
		}
	}

	rc = ioctl(fd, cmd, data);
	if (rc < 0) {
		warn("%s", cmdstr);
		rc = errno;
	}

	return (rc);
}
#define doit(x, y) real_doit(x, y, #x)

static char *
str_to_number(const char *s, long *val, long long *vall)
{
	char *p;

	if (vall)
		*vall = strtoll(s, &p, 0);
	else if (val)
		*val = strtol(s, &p, 0);
	else
		p = NULL;

	return (p);
}

static int
read_reg(long addr, int size, long long *val)
{
	struct t4_reg reg;
	int rc;

	reg.addr = (uint32_t) addr;
	reg.size = (uint32_t) size;
	reg.val = 0;

	rc = doit(CHELSIO_T4_GETREG, &reg);

	*val = reg.val;

	return (rc);
}

static int
write_reg(long addr, int size, long long val)
{
	struct t4_reg reg;

	reg.addr = (uint32_t) addr;
	reg.size = (uint32_t) size;
	reg.val = (uint64_t) val;

	return doit(CHELSIO_T4_SETREG, &reg);
}

static int
register_io(int argc, const char *argv[], int size)
{
	char *p, *v;
	long addr;
	long long val;
	int w = 0, rc;

	if (argc == 1) {
		/* <reg> OR <reg>=<value> */

		p = str_to_number(argv[0], &addr, NULL);
		if (*p) {
			if (*p != '=') {
				warnx("invalid register \"%s\"", argv[0]);
				return (EINVAL);
			}

			w = 1;
			v = p + 1;
			p = str_to_number(v, NULL, &val);

			if (*p) {
				warnx("invalid value \"%s\"", v);
				return (EINVAL);
			}
		}

	} else if (argc == 2) {
		/* <reg> <value> */

		w = 1;

		p = str_to_number(argv[0], &addr, NULL);
		if (*p) {
			warnx("invalid register \"%s\"", argv[0]);
			return (EINVAL);
		}

		p = str_to_number(argv[1], NULL, &val);
		if (*p) {
			warnx("invalid value \"%s\"", argv[1]);
			return (EINVAL);
		}
	} else {
		warnx("reg: invalid number of arguments (%d)", argc);
		return (EINVAL);
	}

	if (w)
		rc = write_reg(addr, size, val);
	else {
		rc = read_reg(addr, size, &val);
		if (rc == 0)
			printf("0x%llx [%llu]\n", val, val);
	}

	return (rc);
}

static inline uint32_t
xtract(uint32_t val, int shift, int len)
{
	return (val >> shift) & ((1 << len) - 1);
}

static int
dump_block_regs(const struct reg_info *reg_array, const uint32_t *regs)
{
	uint32_t reg_val = 0;

	for ( ; reg_array->name; ++reg_array)
		if (!reg_array->len) {
			reg_val = regs[reg_array->addr / 4];
			printf("[%#7x] %-47s %#-10x %u\n", reg_array->addr,
			       reg_array->name, reg_val, reg_val);
		} else {
			uint32_t v = xtract(reg_val, reg_array->addr,
					    reg_array->len);

			printf("    %*u:%u %-47s %#-10x %u\n",
			       reg_array->addr < 10 ? 3 : 2,
			       reg_array->addr + reg_array->len - 1,
			       reg_array->addr, reg_array->name, v, v);
		}

	return (1);
}

static int
dump_regs_table(int argc, const char *argv[], const uint32_t *regs,
    const struct mod_regs *modtab, int nmodules)
{
	int i, j, match;

	for (i = 0; i < argc; i++) {
		for (j = 0; j < nmodules; j++) {
			if (!strcmp(argv[i], modtab[j].name))
				break;
		}

		if (j == nmodules) {
			warnx("invalid register block \"%s\"", argv[i]);
			fprintf(stderr, "\nAvailable blocks:");
			for ( ; nmodules; nmodules--, modtab++)
				fprintf(stderr, " %s", modtab->name);
			fprintf(stderr, "\n");
			return (EINVAL);
		}
	}

	for ( ; nmodules; nmodules--, modtab++) {

		match = argc == 0 ? 1 : 0;
		for (i = 0; !match && i < argc; i++) {
			if (!strcmp(argv[i], modtab->name))
				match = 1;
		}

		if (match)
			dump_block_regs(modtab->ri, regs);
	}

	return (0);
}

#define T4_MODREGS(name) { #name, t4_##name##_regs }
static int
dump_regs_t4(int argc, const char *argv[], const uint32_t *regs)
{
	static struct mod_regs t4_mod[] = {
		T4_MODREGS(sge),
		{ "pci", t4_pcie_regs },
		T4_MODREGS(dbg),
		T4_MODREGS(mc),
		T4_MODREGS(ma),
		{ "edc0", t4_edc_0_regs },
		{ "edc1", t4_edc_1_regs },
		T4_MODREGS(cim),
		T4_MODREGS(tp),
		T4_MODREGS(ulp_rx),
		T4_MODREGS(ulp_tx),
		{ "pmrx", t4_pm_rx_regs },
		{ "pmtx", t4_pm_tx_regs },
		T4_MODREGS(mps),
		{ "cplsw", t4_cpl_switch_regs },
		T4_MODREGS(smb),
		{ "i2c", t4_i2cm_regs },
		T4_MODREGS(mi),
		T4_MODREGS(uart),
		T4_MODREGS(pmu),
		T4_MODREGS(sf),
		T4_MODREGS(pl),
		T4_MODREGS(le),
		T4_MODREGS(ncsi),
		T4_MODREGS(xgmac)
	};

	return dump_regs_table(argc, argv, regs, t4_mod, nitems(t4_mod));
}
#undef T4_MODREGS

#define T5_MODREGS(name) { #name, t5_##name##_regs }
static int
dump_regs_t5(int argc, const char *argv[], const uint32_t *regs)
{
	static struct mod_regs t5_mod[] = {
		T5_MODREGS(sge),
		{ "pci", t5_pcie_regs },
		T5_MODREGS(dbg),
		{ "mc0", t5_mc_0_regs },
		{ "mc1", t5_mc_1_regs },
		T5_MODREGS(ma),
		{ "edc0", t5_edc_t50_regs },
		{ "edc1", t5_edc_t51_regs },
		T5_MODREGS(cim),
		T5_MODREGS(tp),
		{ "ulprx", t5_ulp_rx_regs },
		{ "ulptx", t5_ulp_tx_regs },
		{ "pmrx", t5_pm_rx_regs },
		{ "pmtx", t5_pm_tx_regs },
		T5_MODREGS(mps),
		{ "cplsw", t5_cpl_switch_regs },
		T5_MODREGS(smb),
		{ "i2c", t5_i2cm_regs },
		T5_MODREGS(mi),
		T5_MODREGS(uart),
		T5_MODREGS(pmu),
		T5_MODREGS(sf),
		T5_MODREGS(pl),
		T5_MODREGS(le),
		T5_MODREGS(ncsi),
		T5_MODREGS(mac),
		{ "hma", t5_hma_t5_regs }
	};

	return dump_regs_table(argc, argv, regs, t5_mod, nitems(t5_mod));
}
#undef T5_MODREGS

#define T6_MODREGS(name) { #name, t6_##name##_regs }
static int
dump_regs_t6(int argc, const char *argv[], const uint32_t *regs)
{
	static struct mod_regs t6_mod[] = {
		T6_MODREGS(sge),
		{ "pci", t6_pcie_regs },
		T6_MODREGS(dbg),
		{ "mc0", t6_mc_0_regs },
		T6_MODREGS(ma),
		{ "edc0", t6_edc_t60_regs },
		{ "edc1", t6_edc_t61_regs },
		T6_MODREGS(cim),
		T6_MODREGS(tp),
		{ "ulprx", t6_ulp_rx_regs },
		{ "ulptx", t6_ulp_tx_regs },
		{ "pmrx", t6_pm_rx_regs },
		{ "pmtx", t6_pm_tx_regs },
		T6_MODREGS(mps),
		{ "cplsw", t6_cpl_switch_regs },
		T6_MODREGS(smb),
		{ "i2c", t6_i2cm_regs },
		T6_MODREGS(mi),
		T6_MODREGS(uart),
		T6_MODREGS(pmu),
		T6_MODREGS(sf),
		T6_MODREGS(pl),
		T6_MODREGS(le),
		T6_MODREGS(ncsi),
		T6_MODREGS(mac),
		{ "hma", t6_hma_t6_regs }
	};

	return dump_regs_table(argc, argv, regs, t6_mod, nitems(t6_mod));
}
#undef T6_MODREGS

static int
dump_regs_t4vf(int argc, const char *argv[], const uint32_t *regs)
{
	static struct mod_regs t4vf_mod[] = {
		{ "sge", t4vf_sge_regs },
		{ "mps", t4vf_mps_regs },
		{ "pl", t4vf_pl_regs },
		{ "mbdata", t4vf_mbdata_regs },
		{ "cim", t4vf_cim_regs },
	};

	return dump_regs_table(argc, argv, regs, t4vf_mod, nitems(t4vf_mod));
}

static int
dump_regs_t5vf(int argc, const char *argv[], const uint32_t *regs)
{
	static struct mod_regs t5vf_mod[] = {
		{ "sge", t5vf_sge_regs },
		{ "mps", t4vf_mps_regs },
		{ "pl", t5vf_pl_regs },
		{ "mbdata", t4vf_mbdata_regs },
		{ "cim", t4vf_cim_regs },
	};

	return dump_regs_table(argc, argv, regs, t5vf_mod, nitems(t5vf_mod));
}

static int
dump_regs_t6vf(int argc, const char *argv[], const uint32_t *regs)
{
	static struct mod_regs t6vf_mod[] = {
		{ "sge", t5vf_sge_regs },
		{ "mps", t4vf_mps_regs },
		{ "pl", t6vf_pl_regs },
		{ "mbdata", t4vf_mbdata_regs },
		{ "cim", t4vf_cim_regs },
	};

	return dump_regs_table(argc, argv, regs, t6vf_mod, nitems(t6vf_mod));
}

static int
dump_regs(int argc, const char *argv[])
{
	int vers, revision, rc;
	struct t4_regdump regs;
	uint32_t len;

	len = max(T4_REGDUMP_SIZE, T5_REGDUMP_SIZE);
	regs.data = calloc(1, len);
	if (regs.data == NULL) {
		warnc(ENOMEM, "regdump");
		return (ENOMEM);
	}

	regs.len = len;
	rc = doit(CHELSIO_T4_REGDUMP, &regs);
	if (rc != 0)
		return (rc);

	vers = get_card_vers(regs.version);
	revision = (regs.version >> 10) & 0x3f;

	if (vers == 4) {
		if (revision == 0x3f)
			rc = dump_regs_t4vf(argc, argv, regs.data);
		else
			rc = dump_regs_t4(argc, argv, regs.data);
	} else if (vers == 5) {
		if (revision == 0x3f)
			rc = dump_regs_t5vf(argc, argv, regs.data);
		else
			rc = dump_regs_t5(argc, argv, regs.data);
	} else if (vers == 6) {
		if (revision == 0x3f)
			rc = dump_regs_t6vf(argc, argv, regs.data);
		else
			rc = dump_regs_t6(argc, argv, regs.data);
	} else {
		warnx("%s (type %d, rev %d) is not a known card.",
		    nexus, vers, revision);
		return (ENOTSUP);
	}

	free(regs.data);
	return (rc);
}

static void
do_show_info_header(uint32_t mode)
{
	uint32_t i;

	printf("%4s %8s", "Idx", "Hits");
	for (i = T4_FILTER_FCoE; i <= T4_FILTER_IP_FRAGMENT; i <<= 1) {
		switch (mode & i) {
		case T4_FILTER_FCoE:
			printf(" FCoE");
			break;

		case T4_FILTER_PORT:
			printf(" Port");
			break;

		case T4_FILTER_VNIC:
			if (mode & T4_FILTER_IC_VNIC)
				printf("   VFvld:PF:VF");
			else
				printf("     vld:oVLAN");
			break;

		case T4_FILTER_VLAN:
			printf("      vld:VLAN");
			break;

		case T4_FILTER_IP_TOS:
			printf("   TOS");
			break;

		case T4_FILTER_IP_PROTO:
			printf("  Prot");
			break;

		case T4_FILTER_ETH_TYPE:
			printf("   EthType");
			break;

		case T4_FILTER_MAC_IDX:
			printf("  MACIdx");
			break;

		case T4_FILTER_MPS_HIT_TYPE:
			printf(" MPS");
			break;

		case T4_FILTER_IP_FRAGMENT:
			printf(" Frag");
			break;

		default:
			/* compressed filter field not enabled */
			break;
		}
	}
	printf(" %20s %20s %9s %9s %s\n",
	    "DIP", "SIP", "DPORT", "SPORT", "Action");
}

/*
 * Parse an argument sub-vector as a { <parameter name> <value>[:<mask>] }
 * ordered tuple.  If the parameter name in the argument sub-vector does not
 * match the passed in parameter name, then a zero is returned for the
 * function and no parsing is performed.  If there is a match, then the value
 * and optional mask are parsed and returned in the provided return value
 * pointers.  If no optional mask is specified, then a default mask of all 1s
 * will be returned.
 *
 * An error in parsing the value[:mask] will result in an error message and
 * program termination.
 */
static int
parse_val_mask(const char *param, const char *args[], uint32_t *val,
    uint32_t *mask, int hashfilter)
{
	long l;
	char *p;

	if (strcmp(param, args[0]) != 0)
		return (EINVAL);

	p = str_to_number(args[1], &l, NULL);
	if (l >= 0 && l <= UINT32_MAX) {
		*val = (uint32_t)l;
		if (p > args[1]) {
			if (p[0] == 0) {
				*mask = ~0;
				return (0);
			}

			if (p[0] == ':' && p[1] != 0) {
				if (hashfilter) {
					warnx("param %s: mask not allowed for "
					    "hashfilter or nat params", param);
					return (EINVAL);
				}
				p = str_to_number(p + 1, &l, NULL);
				if (l >= 0 && l <= UINT32_MAX && p[0] == 0) {
					*mask = (uint32_t)l;
					return (0);
				}
			}
		}
	}

	warnx("parameter \"%s\" has bad \"value[:mask]\" %s",
	    args[0], args[1]);

	return (EINVAL);
}

/*
 * Parse an argument sub-vector as a { <parameter name> <addr>[/<mask>] }
 * ordered tuple.  If the parameter name in the argument sub-vector does not
 * match the passed in parameter name, then a zero is returned for the
 * function and no parsing is performed.  If there is a match, then the value
 * and optional mask are parsed and returned in the provided return value
 * pointers.  If no optional mask is specified, then a default mask of all 1s
 * will be returned.
 *
 * The value return parameter "afp" is used to specify the expected address
 * family -- IPv4 or IPv6 -- of the address[/mask] and return its actual
 * format.  A passed in value of AF_UNSPEC indicates that either IPv4 or IPv6
 * is acceptable; AF_INET means that only IPv4 addresses are acceptable; and
 * AF_INET6 means that only IPv6 are acceptable.  AF_INET is returned for IPv4
 * and AF_INET6 for IPv6 addresses, respectively.  IPv4 address/mask pairs are
 * returned in the first four bytes of the address and mask return values with
 * the address A.B.C.D returned with { A, B, C, D } returned in addresses { 0,
 * 1, 2, 3}, respectively.
 *
 * An error in parsing the value[:mask] will result in an error message and
 * program termination.
 */
static int
parse_ipaddr(const char *param, const char *args[], int *afp, uint8_t addr[],
    uint8_t mask[], int maskless)
{
	const char *colon, *afn;
	char *slash;
	uint8_t *m;
	int af, ret;
	unsigned int masksize;

	/*
	 * Is this our parameter?
	 */
	if (strcmp(param, args[0]) != 0)
		return (EINVAL);

	/*
	 * Fundamental IPv4 versus IPv6 selection.
	 */
	colon = strchr(args[1], ':');
	if (!colon) {
		afn = "IPv4";
		af = AF_INET;
		masksize = 32;
	} else {
		afn = "IPv6";
		af = AF_INET6;
		masksize = 128;
	}
	if (*afp == AF_UNSPEC)
		*afp = af;
	else if (*afp != af) {
		warnx("address %s is not of expected family %s",
		    args[1], *afp == AF_INET ? "IP" : "IPv6");
		return (EINVAL);
	}

	/*
	 * Parse address (temporarily stripping off any "/mask"
	 * specification).
	 */
	slash = strchr(args[1], '/');
	if (slash)
		*slash = 0;
	ret = inet_pton(af, args[1], addr);
	if (slash)
		*slash = '/';
	if (ret <= 0) {
		warnx("Cannot parse %s %s address %s", param, afn, args[1]);
		return (EINVAL);
	}

	/*
	 * Parse optional mask specification.
	 */
	if (slash) {
		char *p;
		unsigned int prefix = strtoul(slash + 1, &p, 10);

		if (maskless) {
			warnx("mask cannot be provided for maskless specification");
			return (EINVAL);
		}

		if (p == slash + 1) {
			warnx("missing address prefix for %s", param);
			return (EINVAL);
		}
		if (*p) {
			warnx("%s is not a valid address prefix", slash + 1);
			return (EINVAL);
		}
		if (prefix > masksize) {
			warnx("prefix %u is too long for an %s address",
			     prefix, afn);
			return (EINVAL);
		}
		memset(mask, 0, masksize / 8);
		masksize = prefix;
	}

	if (mask != NULL) {
		/*
		 * Fill in mask.
		 */
		for (m = mask; masksize >= 8; m++, masksize -= 8)
			*m = ~0;
		if (masksize)
			*m = ~0 << (8 - masksize);
	}

	return (0);
}

/*
 * Parse an argument sub-vector as a { <parameter name> <value> } ordered
 * tuple.  If the parameter name in the argument sub-vector does not match the
 * passed in parameter name, then a zero is returned for the function and no
 * parsing is performed.  If there is a match, then the value is parsed and
 * returned in the provided return value pointer.
 */
static int
parse_val(const char *param, const char *args[], uint32_t *val)
{
	char *p;
	long l;

	if (strcmp(param, args[0]) != 0)
		return (EINVAL);

	p = str_to_number(args[1], &l, NULL);
	if (*p || l < 0 || l > UINT32_MAX) {
		warnx("parameter \"%s\" has bad \"value\" %s", args[0], args[1]);
		return (EINVAL);
	}

	*val = (uint32_t)l;
	return (0);
}

static void
filters_show_ipaddr(int type, uint8_t *addr, uint8_t *addrm)
{
	int noctets, octet;

	printf(" ");
	if (type == 0) {
		noctets = 4;
		printf("%3s", " ");
	} else
	noctets = 16;

	for (octet = 0; octet < noctets; octet++)
		printf("%02x", addr[octet]);
	printf("/");
	for (octet = 0; octet < noctets; octet++)
		printf("%02x", addrm[octet]);
}

static void
do_show_one_filter_info(struct t4_filter *t, uint32_t mode)
{
	uint32_t i;

	printf("%4d", t->idx);
	if (t->hits == UINT64_MAX)
		printf(" %8s", "-");
	else
		printf(" %8ju", t->hits);

	/*
	 * Compressed header portion of filter.
	 */
	for (i = T4_FILTER_FCoE; i <= T4_FILTER_IP_FRAGMENT; i <<= 1) {
		switch (mode & i) {
		case T4_FILTER_FCoE:
			printf("  %1d/%1d", t->fs.val.fcoe, t->fs.mask.fcoe);
			break;

		case T4_FILTER_PORT:
			printf("  %1d/%1d", t->fs.val.iport, t->fs.mask.iport);
			break;

		case T4_FILTER_VNIC:
			if (mode & T4_FILTER_IC_VNIC) {
				printf(" %1d:%1x:%02x/%1d:%1x:%02x",
				    t->fs.val.pfvf_vld,
				    (t->fs.val.vnic >> 13) & 0x7,
				    t->fs.val.vnic & 0x1fff,
				    t->fs.mask.pfvf_vld,
				    (t->fs.mask.vnic >> 13) & 0x7,
				    t->fs.mask.vnic & 0x1fff);
			} else {
				printf(" %1d:%04x/%1d:%04x",
				    t->fs.val.ovlan_vld, t->fs.val.vnic,
				    t->fs.mask.ovlan_vld, t->fs.mask.vnic);
			}
			break;

		case T4_FILTER_VLAN:
			printf(" %1d:%04x/%1d:%04x",
			    t->fs.val.vlan_vld, t->fs.val.vlan,
			    t->fs.mask.vlan_vld, t->fs.mask.vlan);
			break;

		case T4_FILTER_IP_TOS:
			printf(" %02x/%02x", t->fs.val.tos, t->fs.mask.tos);
			break;

		case T4_FILTER_IP_PROTO:
			printf(" %02x/%02x", t->fs.val.proto, t->fs.mask.proto);
			break;

		case T4_FILTER_ETH_TYPE:
			printf(" %04x/%04x", t->fs.val.ethtype,
			    t->fs.mask.ethtype);
			break;

		case T4_FILTER_MAC_IDX:
			printf(" %03x/%03x", t->fs.val.macidx,
			    t->fs.mask.macidx);
			break;

		case T4_FILTER_MPS_HIT_TYPE:
			printf(" %1x/%1x", t->fs.val.matchtype,
			    t->fs.mask.matchtype);
			break;

		case T4_FILTER_IP_FRAGMENT:
			printf("  %1d/%1d", t->fs.val.frag, t->fs.mask.frag);
			break;

		default:
			/* compressed filter field not enabled */
			break;
		}
	}

	/*
	 * Fixed portion of filter.
	 */
	filters_show_ipaddr(t->fs.type, t->fs.val.dip, t->fs.mask.dip);
	filters_show_ipaddr(t->fs.type, t->fs.val.sip, t->fs.mask.sip);
	printf(" %04x/%04x %04x/%04x",
		 t->fs.val.dport, t->fs.mask.dport,
		 t->fs.val.sport, t->fs.mask.sport);

	/*
	 * Variable length filter action.
	 */
	if (t->fs.action == FILTER_DROP)
		printf(" Drop");
	else if (t->fs.action == FILTER_SWITCH) {
		printf(" Switch: port=%d", t->fs.eport);
	if (t->fs.newdmac)
		printf(
			", dmac=%02x:%02x:%02x:%02x:%02x:%02x "
			", l2tidx=%d",
			t->fs.dmac[0], t->fs.dmac[1],
			t->fs.dmac[2], t->fs.dmac[3],
			t->fs.dmac[4], t->fs.dmac[5],
			t->l2tidx);
	if (t->fs.newsmac)
		printf(
			", smac=%02x:%02x:%02x:%02x:%02x:%02x "
			", smtidx=%d",
			t->fs.smac[0], t->fs.smac[1],
			t->fs.smac[2], t->fs.smac[3],
			t->fs.smac[4], t->fs.smac[5],
			t->smtidx);
	if (t->fs.newvlan == VLAN_REMOVE)
		printf(", vlan=none");
	else if (t->fs.newvlan == VLAN_INSERT)
		printf(", vlan=insert(%x)", t->fs.vlan);
	else if (t->fs.newvlan == VLAN_REWRITE)
		printf(", vlan=rewrite(%x)", t->fs.vlan);
	} else {
		printf(" Pass: Q=");
		if (t->fs.dirsteer == 0) {
			printf("RSS");
			if (t->fs.maskhash)
				printf("(region %d)", t->fs.iq << 1);
		} else {
			printf("%d", t->fs.iq);
			if (t->fs.dirsteerhash == 0)
				printf("(QID)");
			else
				printf("(hash)");
		}
	}
	if (chip_id <= 5 && t->fs.prio)
		printf(" Prio");
	if (t->fs.rpttid)
		printf(" RptTID");
	printf("\n");
}

static int
show_filters(int hash)
{
	uint32_t mode = 0, header, hpfilter = 0;
	struct t4_filter t;
	int rc;

	/* Get the global filter mode first */
	rc = doit(CHELSIO_T4_GET_FILTER_MODE, &mode);
	if (rc != 0)
		return (rc);

	if (!hash && chip_id >= 6) {
		header = 0;
		bzero(&t, sizeof (t));
		t.idx = 0;
		t.fs.hash = 0;
		t.fs.prio = 1;
		for (t.idx = 0; ; t.idx++) {
			rc = doit(CHELSIO_T4_GET_FILTER, &t);
			if (rc != 0 || t.idx == 0xffffffff)
				break;

			if (!header) {
				printf("High Priority TCAM Region:\n");
				do_show_info_header(mode);
				header = 1;
				hpfilter = 1;
			}
			do_show_one_filter_info(&t, mode);
		}
	}

	header = 0;
	bzero(&t, sizeof (t));
	t.idx = 0;
	t.fs.hash = hash;
	for (t.idx = 0; ; t.idx++) {
		rc = doit(CHELSIO_T4_GET_FILTER, &t);
		if (rc != 0 || t.idx == 0xffffffff)
			break;

		if (!header) {
			if (hpfilter)
				printf("\nNormal Priority TCAM Region:\n");
			do_show_info_header(mode);
			header = 1;
		}
		do_show_one_filter_info(&t, mode);
	}

	return (rc);
}

static int
get_filter_mode(int hashfilter)
{
	uint32_t mode = hashfilter;
	int rc;

	rc = doit(CHELSIO_T4_GET_FILTER_MODE, &mode);
	if (rc != 0)
		return (rc);

	if (mode & T4_FILTER_IPv4)
		printf("ipv4 ");

	if (mode & T4_FILTER_IPv6)
		printf("ipv6 ");

	if (mode & T4_FILTER_IP_SADDR)
		printf("sip ");

	if (mode & T4_FILTER_IP_DADDR)
		printf("dip ");

	if (mode & T4_FILTER_IP_SPORT)
		printf("sport ");

	if (mode & T4_FILTER_IP_DPORT)
		printf("dport ");

	if (mode & T4_FILTER_IP_FRAGMENT)
		printf("frag ");

	if (mode & T4_FILTER_MPS_HIT_TYPE)
		printf("matchtype ");

	if (mode & T4_FILTER_MAC_IDX)
		printf("macidx ");

	if (mode & T4_FILTER_ETH_TYPE)
		printf("ethtype ");

	if (mode & T4_FILTER_IP_PROTO)
		printf("proto ");

	if (mode & T4_FILTER_IP_TOS)
		printf("tos ");

	if (mode & T4_FILTER_VLAN)
		printf("vlan ");

	if (mode & T4_FILTER_VNIC) {
		if (mode & T4_FILTER_IC_VNIC)
			printf("vnic_id ");
		else
			printf("ovlan ");
	}

	if (mode & T4_FILTER_PORT)
		printf("iport ");

	if (mode & T4_FILTER_FCoE)
		printf("fcoe ");

	printf("\n");

	return (0);
}

static int
set_filter_mode(int argc, const char *argv[])
{
	uint32_t mode = 0;
	int vnic = 0, ovlan = 0;

	for (; argc; argc--, argv++) {
		if (!strcmp(argv[0], "frag"))
			mode |= T4_FILTER_IP_FRAGMENT;

		if (!strcmp(argv[0], "matchtype"))
			mode |= T4_FILTER_MPS_HIT_TYPE;

		if (!strcmp(argv[0], "macidx"))
			mode |= T4_FILTER_MAC_IDX;

		if (!strcmp(argv[0], "ethtype"))
			mode |= T4_FILTER_ETH_TYPE;

		if (!strcmp(argv[0], "proto"))
			mode |= T4_FILTER_IP_PROTO;

		if (!strcmp(argv[0], "tos"))
			mode |= T4_FILTER_IP_TOS;

		if (!strcmp(argv[0], "vlan"))
			mode |= T4_FILTER_VLAN;

		if (!strcmp(argv[0], "ovlan")) {
			mode |= T4_FILTER_VNIC;
			ovlan++;
		}

		if (!strcmp(argv[0], "vnic_id")) {
			mode |= T4_FILTER_VNIC;
			mode |= T4_FILTER_IC_VNIC;
			vnic++;
		}

		if (!strcmp(argv[0], "iport"))
			mode |= T4_FILTER_PORT;

		if (!strcmp(argv[0], "fcoe"))
			mode |= T4_FILTER_FCoE;
	}

	if (vnic > 0 && ovlan > 0) {
		warnx("\"vnic_id\" and \"ovlan\" are mutually exclusive.");
		return (EINVAL);
	}

	return doit(CHELSIO_T4_SET_FILTER_MODE, &mode);
}

static int
del_filter(uint32_t idx, int prio, int hashfilter)
{
	struct t4_filter t;

	t.fs.prio = prio;
	t.fs.hash = hashfilter;
	t.idx = idx;

	return doit(CHELSIO_T4_DEL_FILTER, &t);
}

#define MAX_VLANID (4095)

static int
set_filter(uint32_t idx, int argc, const char *argv[], int hash)
{
	int rc, af = AF_UNSPEC, start_arg = 0;
	struct t4_filter t;

	if (argc < 2) {
		warnc(EINVAL, "%s", __func__);
		return (EINVAL);
	};
	bzero(&t, sizeof (t));
	t.idx = idx;
	t.fs.hitcnts = 1;
	t.fs.hash = hash;

	for (start_arg = 0; start_arg + 2 <= argc; start_arg += 2) {
		const char **args = &argv[start_arg];
		uint32_t val, mask;

		if (!strcmp(argv[start_arg], "type")) {
			int newaf;
			if (!strcasecmp(argv[start_arg + 1], "ipv4"))
				newaf = AF_INET;
			else if (!strcasecmp(argv[start_arg + 1], "ipv6"))
				newaf = AF_INET6;
			else {
				warnx("invalid type \"%s\"; "
				    "must be one of \"ipv4\" or \"ipv6\"",
				    argv[start_arg + 1]);
				return (EINVAL);
			}

			if (af != AF_UNSPEC && af != newaf) {
				warnx("conflicting IPv4/IPv6 specifications.");
				return (EINVAL);
			}
			af = newaf;
		} else if (!parse_val_mask("fcoe", args, &val, &mask, hash)) {
			t.fs.val.fcoe = val;
			t.fs.mask.fcoe = mask;
		} else if (!parse_val_mask("iport", args, &val, &mask, hash)) {
			t.fs.val.iport = val;
			t.fs.mask.iport = mask;
		} else if (!parse_val_mask("ovlan", args, &val, &mask, hash)) {
			t.fs.val.vnic = val;
			t.fs.mask.vnic = mask;
			t.fs.val.ovlan_vld = 1;
			t.fs.mask.ovlan_vld = 1;
		} else if (!parse_val_mask("ivlan", args, &val, &mask, hash)) {
			t.fs.val.vlan = val;
			t.fs.mask.vlan = mask;
			t.fs.val.vlan_vld = 1;
			t.fs.mask.vlan_vld = 1;
		} else if (!parse_val_mask("pf", args, &val, &mask, hash)) {
			t.fs.val.vnic &= 0x1fff;
			t.fs.val.vnic |= (val & 0x7) << 13;
			t.fs.mask.vnic &= 0x1fff;
			t.fs.mask.vnic |= (mask & 0x7) << 13;
			t.fs.val.pfvf_vld = 1;
			t.fs.mask.pfvf_vld = 1;
		} else if (!parse_val_mask("vf", args, &val, &mask, hash)) {
			t.fs.val.vnic &= 0xe000;
			t.fs.val.vnic |= val & 0x1fff;
			t.fs.mask.vnic &= 0xe000;
			t.fs.mask.vnic |= mask & 0x1fff;
			t.fs.val.pfvf_vld = 1;
			t.fs.mask.pfvf_vld = 1;
		} else if (!parse_val_mask("tos", args, &val, &mask, hash)) {
			t.fs.val.tos = val;
			t.fs.mask.tos = mask;
		} else if (!parse_val_mask("proto", args, &val, &mask, hash)) {
			t.fs.val.proto = val;
			t.fs.mask.proto = mask;
		} else if (!parse_val_mask("ethtype", args, &val, &mask, hash)) {
			t.fs.val.ethtype = val;
			t.fs.mask.ethtype = mask;
		} else if (!parse_val_mask("macidx", args, &val, &mask, hash)) {
			t.fs.val.macidx = val;
			t.fs.mask.macidx = mask;
		} else if (!parse_val_mask("matchtype", args, &val, &mask, hash)) {
			t.fs.val.matchtype = val;
			t.fs.mask.matchtype = mask;
		} else if (!parse_val_mask("frag", args, &val, &mask, hash)) {
			t.fs.val.frag = val;
			t.fs.mask.frag = mask;
		} else if (!parse_val_mask("dport", args, &val, &mask, hash)) {
			t.fs.val.dport = val;
			t.fs.mask.dport = mask;
		} else if (!parse_val_mask("sport", args, &val, &mask, hash)) {
			t.fs.val.sport = val;
			t.fs.mask.sport = mask;
		} else if (!parse_ipaddr("dip", args, &af, t.fs.val.dip,
		    t.fs.mask.dip, hash)) {
			/* nada */;
		} else if (!parse_ipaddr("sip", args, &af, t.fs.val.sip,
		    t.fs.mask.sip, hash)) {
			/* nada */;
		} else if (!parse_ipaddr("nat_dip", args, &af, t.fs.nat_dip, NULL, 1)) {
			/*nada*/;
		} else if (!parse_ipaddr("nat_sip", args, &af, t.fs.nat_sip, NULL, 1)) {
			/*nada*/
		} else if (!parse_val_mask("nat_dport", args, &val, &mask, 1)) {
			t.fs.nat_dport = val;
		} else if (!parse_val_mask("nat_sport", args, &val, &mask, 1)) {
			t.fs.nat_sport = val;
		} else if (!strcmp(argv[start_arg], "action")) {
			if (!strcmp(argv[start_arg + 1], "pass"))
				t.fs.action = FILTER_PASS;
			else if (!strcmp(argv[start_arg + 1], "drop"))
				t.fs.action = FILTER_DROP;
			else if (!strcmp(argv[start_arg + 1], "switch"))
				t.fs.action = FILTER_SWITCH;
			else {
				warnx("invalid action \"%s\"; must be one of"
				     " \"pass\", \"drop\" or \"switch\"",
				     argv[start_arg + 1]);
				return (EINVAL);
			}
		} else if (!parse_val("hitcnts", args, &val)) {
			t.fs.hitcnts = val;
		} else if (!parse_val("prio", args, &val)) {
			if (hash) {
				warnx("Hashfilters doesn't support \"prio\"\n");
				return (EINVAL);
			}
			if (val != 0 && val != 1) {
				warnx("invalid priority \"%s\"; must be"
				     " \"0\" or \"1\"", argv[start_arg + 1]);
				return (EINVAL);
			}
			t.fs.prio = val;
		} else if (!parse_val("rpttid", args, &val)) {
			t.fs.rpttid = 1;
		} else if (!parse_val("queue", args, &val)) {
			t.fs.dirsteer = 1;	/* direct steer */
			t.fs.iq = val;		/* to the iq with this cntxt_id */
		} else if (!parse_val("tcbhash", args, &val)) {
			t.fs.dirsteerhash = 1;	/* direct steer */
			/* XXX: use (val << 1) as the rss_hash? */
			t.fs.iq = val;
		} else if (!parse_val("tcbrss", args, &val)) {
			t.fs.maskhash = 1;	/* steer to RSS region */
			/*
			 * val = start idx of the region but the internal TCB
			 * field is 10b only and is left shifted by 1 before use.
			 */
			t.fs.iq = val >> 1;
		} else if (!parse_val("eport", args, &val)) {
			t.fs.eport = val;
		} else if (!parse_val("swapmac", args, &val)) {
			t.fs.swapmac = 1;
		} else if (!strcmp(argv[start_arg], "nat")) {
			if (!strcmp(argv[start_arg + 1], "dip"))
				t.fs.nat_mode = NAT_MODE_DIP;
			else if (!strcmp(argv[start_arg + 1], "dip-dp"))
				t.fs.nat_mode = NAT_MODE_DIP_DP;
			else if (!strcmp(argv[start_arg + 1], "dip-dp-sip"))
				t.fs.nat_mode = NAT_MODE_DIP_DP_SIP;
			else if (!strcmp(argv[start_arg + 1], "dip-dp-sp"))
				t.fs.nat_mode = NAT_MODE_DIP_DP_SP;
			else if (!strcmp(argv[start_arg + 1], "sip-sp"))
				t.fs.nat_mode = NAT_MODE_SIP_SP;
			else if (!strcmp(argv[start_arg + 1], "dip-sip-sp"))
				t.fs.nat_mode = NAT_MODE_DIP_SIP_SP;
			else if (!strcmp(argv[start_arg + 1], "all"))
				t.fs.nat_mode = NAT_MODE_ALL;
			else {
				warnx("unknown nat type \"%s\"; known types are dip, "
				      "dip-dp, dip-dp-sip, dip-dp-sp, sip-sp, "
				      "dip-sip-sp, and all", argv[start_arg + 1]);
				return (EINVAL);
			}
		} else if (!parse_val("natseq", args, &val)) {
			t.fs.nat_seq_chk = val;
		} else if (!parse_val("natflag", args, &val)) {
			t.fs.nat_flag_chk = 1;
		} else if (!strcmp(argv[start_arg], "dmac")) {
			struct ether_addr *daddr;

			daddr = ether_aton(argv[start_arg + 1]);
			if (daddr == NULL) {
				warnx("invalid dmac address \"%s\"",
				    argv[start_arg + 1]);
				return (EINVAL);
			}
			memcpy(t.fs.dmac, daddr, ETHER_ADDR_LEN);
			t.fs.newdmac = 1;
		} else if (!strcmp(argv[start_arg], "smac")) {
			struct ether_addr *saddr;

			saddr = ether_aton(argv[start_arg + 1]);
			if (saddr == NULL) {
				warnx("invalid smac address \"%s\"",
				    argv[start_arg + 1]);
				return (EINVAL);
			}
			memcpy(t.fs.smac, saddr, ETHER_ADDR_LEN);
			t.fs.newsmac = 1;
		} else if (!strcmp(argv[start_arg], "vlan")) {
			char *p;
			if (!strcmp(argv[start_arg + 1], "none")) {
				t.fs.newvlan = VLAN_REMOVE;
			} else if (argv[start_arg + 1][0] == '=') {
				t.fs.newvlan = VLAN_REWRITE;
			} else if (argv[start_arg + 1][0] == '+') {
				t.fs.newvlan = VLAN_INSERT;
			} else {
				warnx("unknown vlan parameter \"%s\"; must"
				     " be one of \"none\", \"=<vlan>\", "
				     " \"+<vlan>\"", argv[start_arg + 1]);
				return (EINVAL);
			}
			if (t.fs.newvlan == VLAN_REWRITE ||
			    t.fs.newvlan == VLAN_INSERT) {
				t.fs.vlan = strtoul(argv[start_arg + 1] + 1,
				    &p, 0);
				if (p == argv[start_arg + 1] + 1 || p[0] != 0 ||
				    t.fs.vlan > MAX_VLANID) {
					warnx("invalid vlan \"%s\"",
					     argv[start_arg + 1]);
					return (EINVAL);
				}
			}
		} else {
			warnx("invalid parameter \"%s\"", argv[start_arg]);
			return (EINVAL);
		}
	}
	if (start_arg != argc) {
		warnx("no value for \"%s\"", argv[start_arg]);
		return (EINVAL);
	}

	/*
	 * Check basic sanity of option combinations.
	 */
	if (t.fs.action != FILTER_SWITCH &&
	    (t.fs.eport || t.fs.newdmac || t.fs.newsmac || t.fs.newvlan ||
	    t.fs.swapmac || t.fs.nat_mode)) {
		warnx("port, dmac, smac, vlan, and nat only make sense with"
		     " \"action switch\"");
		return (EINVAL);
	}
	if (!t.fs.nat_mode && (t.fs.nat_seq_chk || t.fs.nat_flag_chk ||
	    *t.fs.nat_dip || *t.fs.nat_sip || t.fs.nat_dport || t.fs.nat_sport)) {
		warnx("nat params only make sense with valid nat mode");
		return (EINVAL);
	}
	if (t.fs.action != FILTER_PASS &&
	    (t.fs.rpttid || t.fs.dirsteer || t.fs.maskhash)) {
		warnx("rpttid, queue and tcbhash don't make sense with"
		     " action \"drop\" or \"switch\"");
		return (EINVAL);
	}
	if (t.fs.val.ovlan_vld && t.fs.val.pfvf_vld) {
		warnx("ovlan and vnic_id (pf/vf) are mutually exclusive");
		return (EINVAL);
	}

	t.fs.type = (af == AF_INET6 ? 1 : 0); /* default IPv4 */
	rc = doit(CHELSIO_T4_SET_FILTER, &t);
	if (hash && rc == 0)
		printf("%d\n", t.idx);
	return (rc);
}

static int
filter_cmd(int argc, const char *argv[], int hashfilter)
{
	long long val;
	uint32_t idx;
	char *s;

	if (argc == 0) {
		warnx("%sfilter: no arguments.", hashfilter ? "hash" : "");
		return (EINVAL);
	};

	/* list */
	if (strcmp(argv[0], "list") == 0) {
		if (argc != 1)
			warnx("trailing arguments after \"list\" ignored.");

		return show_filters(hashfilter);
	}

	/* mode */
	if (argc == 1 && strcmp(argv[0], "mode") == 0)
		return get_filter_mode(hashfilter);

	/* mode <mode> */
	if (!hashfilter && strcmp(argv[0], "mode") == 0)
		return set_filter_mode(argc - 1, argv + 1);

	/* <idx> ... */
	s = str_to_number(argv[0], NULL, &val);
	if (*s || val < 0 || val > 0xffffffffU) {
		if (hashfilter) {
			/*
			 * No numeric index means this must be a request to
			 * create a new hashfilter and we are already at the
			 * paramter/value list.
			 */
			idx = (uint32_t) -1;
			goto setf;
		}
		warnx("\"%s\" is neither an index nor a filter subcommand.",
		    argv[0]);
		return (EINVAL);
	}
	idx = (uint32_t) val;

	/* <idx> delete|clear [prio 0|1] */
	if ((argc == 2 || argc == 4) &&
	    (strcmp(argv[1], "delete") == 0 || strcmp(argv[1], "clear") == 0)) {
		int prio = 0;

		if (argc == 4) {
			if (hashfilter) {
				warnx("stray arguments after \"%s\".", argv[1]);
				return (EINVAL);
			}

			if (strcmp(argv[2], "prio") != 0) {
				warnx("\"prio\" is the only valid keyword "
				    "after \"%s\", found \"%s\" instead.",
				    argv[1], argv[2]);
				return (EINVAL);
			}

			s = str_to_number(argv[3], NULL, &val);
			if (*s || val < 0 || val > 1) {
				warnx("%s \"%s\"; must be \"0\" or \"1\".",
				    argv[2], argv[3]);
				return (EINVAL);
			}
			prio = (int)val;
		}
		return del_filter(idx, prio, hashfilter);
	}

	/* skip <idx> */
	argc--;
	argv++;

setf:
	/* [<param> <val>] ... */
	return set_filter(idx, argc, argv, hashfilter);
}

/*
 * Shows the fields of a multi-word structure.  The structure is considered to
 * consist of @nwords 32-bit words (i.e, it's an (@nwords * 32)-bit structure)
 * whose fields are described by @fd.  The 32-bit words are given in @words
 * starting with the least significant 32-bit word.
 */
static void
show_struct(const uint32_t *words, int nwords, const struct field_desc *fd)
{
	unsigned int w = 0;
	const struct field_desc *p;

	for (p = fd; p->name; p++)
		w = max(w, strlen(p->name));

	while (fd->name) {
		unsigned long long data;
		int first_word = fd->start / 32;
		int shift = fd->start % 32;
		int width = fd->end - fd->start + 1;
		unsigned long long mask = (1ULL << width) - 1;

		data = (words[first_word] >> shift) |
		       ((uint64_t)words[first_word + 1] << (32 - shift));
		if (shift)
		       data |= ((uint64_t)words[first_word + 2] << (64 - shift));
		data &= mask;
		if (fd->islog2)
			data = 1 << data;
		printf("%-*s ", w, fd->name);
		printf(fd->hex ? "%#llx\n" : "%llu\n", data << fd->shift);
		fd++;
	}
}

#define FIELD(name, start, end) { name, start, end, 0, 0, 0 }
#define FIELD1(name, start) FIELD(name, start, start)

static void
show_t5t6_ctxt(const struct t4_sge_context *p, int vers)
{
	static struct field_desc egress_t5[] = {
		FIELD("DCA_ST:", 181, 191),
		FIELD1("StatusPgNS:", 180),
		FIELD1("StatusPgRO:", 179),
		FIELD1("FetchNS:", 178),
		FIELD1("FetchRO:", 177),
		FIELD1("Valid:", 176),
		FIELD("PCIeDataChannel:", 174, 175),
		FIELD1("StatusPgTPHintEn:", 173),
		FIELD("StatusPgTPHint:", 171, 172),
		FIELD1("FetchTPHintEn:", 170),
		FIELD("FetchTPHint:", 168, 169),
		FIELD1("FCThreshOverride:", 167),
		{ "WRLength:", 162, 166, 9, 0, 1 },
		FIELD1("WRLengthKnown:", 161),
		FIELD1("ReschedulePending:", 160),
		FIELD1("OnChipQueue:", 159),
		FIELD1("FetchSizeMode:", 158),
		{ "FetchBurstMin:", 156, 157, 4, 0, 1 },
		FIELD1("FLMPacking:", 155),
		FIELD("FetchBurstMax:", 153, 154),
		FIELD("uPToken:", 133, 152),
		FIELD1("uPTokenEn:", 132),
		FIELD1("UserModeIO:", 131),
		FIELD("uPFLCredits:", 123, 130),
		FIELD1("uPFLCreditEn:", 122),
		FIELD("FID:", 111, 121),
		FIELD("HostFCMode:", 109, 110),
		FIELD1("HostFCOwner:", 108),
		{ "CIDXFlushThresh:", 105, 107, 0, 0, 1 },
		FIELD("CIDX:", 89, 104),
		FIELD("PIDX:", 73, 88),
		{ "BaseAddress:", 18, 72, 9, 1 },
		FIELD("QueueSize:", 2, 17),
		FIELD1("QueueType:", 1),
		FIELD1("CachePriority:", 0),
		{ NULL }
	};
	static struct field_desc egress_t6[] = {
		FIELD("DCA_ST:", 181, 191),
		FIELD1("StatusPgNS:", 180),
		FIELD1("StatusPgRO:", 179),
		FIELD1("FetchNS:", 178),
		FIELD1("FetchRO:", 177),
		FIELD1("Valid:", 176),
		FIELD1("ReschedulePending_1:", 175),
		FIELD1("PCIeDataChannel:", 174),
		FIELD1("StatusPgTPHintEn:", 173),
		FIELD("StatusPgTPHint:", 171, 172),
		FIELD1("FetchTPHintEn:", 170),
		FIELD("FetchTPHint:", 168, 169),
		FIELD1("FCThreshOverride:", 167),
		{ "WRLength:", 162, 166, 9, 0, 1 },
		FIELD1("WRLengthKnown:", 161),
		FIELD1("ReschedulePending:", 160),
		FIELD("TimerIx:", 157, 159),
		FIELD1("FetchBurstMin:", 156),
		FIELD1("FLMPacking:", 155),
		FIELD("FetchBurstMax:", 153, 154),
		FIELD("uPToken:", 133, 152),
		FIELD1("uPTokenEn:", 132),
		FIELD1("UserModeIO:", 131),
		FIELD("uPFLCredits:", 123, 130),
		FIELD1("uPFLCreditEn:", 122),
		FIELD("FID:", 111, 121),
		FIELD("HostFCMode:", 109, 110),
		FIELD1("HostFCOwner:", 108),
		{ "CIDXFlushThresh:", 105, 107, 0, 0, 1 },
		FIELD("CIDX:", 89, 104),
		FIELD("PIDX:", 73, 88),
		{ "BaseAddress:", 18, 72, 9, 1 },
		FIELD("QueueSize:", 2, 17),
		FIELD1("QueueType:", 1),
		FIELD1("FetchSizeMode:", 0),
		{ NULL }
	};
	static struct field_desc fl_t5[] = {
		FIELD("DCA_ST:", 181, 191),
		FIELD1("StatusPgNS:", 180),
		FIELD1("StatusPgRO:", 179),
		FIELD1("FetchNS:", 178),
		FIELD1("FetchRO:", 177),
		FIELD1("Valid:", 176),
		FIELD("PCIeDataChannel:", 174, 175),
		FIELD1("StatusPgTPHintEn:", 173),
		FIELD("StatusPgTPHint:", 171, 172),
		FIELD1("FetchTPHintEn:", 170),
		FIELD("FetchTPHint:", 168, 169),
		FIELD1("FCThreshOverride:", 167),
		FIELD1("ReschedulePending:", 160),
		FIELD1("OnChipQueue:", 159),
		FIELD1("FetchSizeMode:", 158),
		{ "FetchBurstMin:", 156, 157, 4, 0, 1 },
		FIELD1("FLMPacking:", 155),
		FIELD("FetchBurstMax:", 153, 154),
		FIELD1("FLMcongMode:", 152),
		FIELD("MaxuPFLCredits:", 144, 151),
		FIELD("FLMcontextID:", 133, 143),
		FIELD1("uPTokenEn:", 132),
		FIELD1("UserModeIO:", 131),
		FIELD("uPFLCredits:", 123, 130),
		FIELD1("uPFLCreditEn:", 122),
		FIELD("FID:", 111, 121),
		FIELD("HostFCMode:", 109, 110),
		FIELD1("HostFCOwner:", 108),
		{ "CIDXFlushThresh:", 105, 107, 0, 0, 1 },
		FIELD("CIDX:", 89, 104),
		FIELD("PIDX:", 73, 88),
		{ "BaseAddress:", 18, 72, 9, 1 },
		FIELD("QueueSize:", 2, 17),
		FIELD1("QueueType:", 1),
		FIELD1("CachePriority:", 0),
		{ NULL }
	};
	static struct field_desc ingress_t5[] = {
		FIELD("DCA_ST:", 143, 153),
		FIELD1("ISCSICoalescing:", 142),
		FIELD1("Queue_Valid:", 141),
		FIELD1("TimerPending:", 140),
		FIELD1("DropRSS:", 139),
		FIELD("PCIeChannel:", 137, 138),
		FIELD1("SEInterruptArmed:", 136),
		FIELD1("CongestionMgtEnable:", 135),
		FIELD1("NoSnoop:", 134),
		FIELD1("RelaxedOrdering:", 133),
		FIELD1("GTSmode:", 132),
		FIELD1("TPHintEn:", 131),
		FIELD("TPHint:", 129, 130),
		FIELD1("UpdateScheduling:", 128),
		FIELD("UpdateDelivery:", 126, 127),
		FIELD1("InterruptSent:", 125),
		FIELD("InterruptIDX:", 114, 124),
		FIELD1("InterruptDestination:", 113),
		FIELD1("InterruptArmed:", 112),
		FIELD("RxIntCounter:", 106, 111),
		FIELD("RxIntCounterThreshold:", 104, 105),
		FIELD1("Generation:", 103),
		{ "BaseAddress:", 48, 102, 9, 1 },
		FIELD("PIDX:", 32, 47),
		FIELD("CIDX:", 16, 31),
		{ "QueueSize:", 4, 15, 4, 0 },
		{ "QueueEntrySize:", 2, 3, 4, 0, 1 },
		FIELD1("QueueEntryOverride:", 1),
		FIELD1("CachePriority:", 0),
		{ NULL }
	};
	static struct field_desc ingress_t6[] = {
		FIELD1("SP_NS:", 158),
		FIELD1("SP_RO:", 157),
		FIELD1("SP_TPHintEn:", 156),
		FIELD("SP_TPHint:", 154, 155),
		FIELD("DCA_ST:", 143, 153),
		FIELD1("ISCSICoalescing:", 142),
		FIELD1("Queue_Valid:", 141),
		FIELD1("TimerPending:", 140),
		FIELD1("DropRSS:", 139),
		FIELD("PCIeChannel:", 137, 138),
		FIELD1("SEInterruptArmed:", 136),
		FIELD1("CongestionMgtEnable:", 135),
		FIELD1("NoSnoop:", 134),
		FIELD1("RelaxedOrdering:", 133),
		FIELD1("GTSmode:", 132),
		FIELD1("TPHintEn:", 131),
		FIELD("TPHint:", 129, 130),
		FIELD1("UpdateScheduling:", 128),
		FIELD("UpdateDelivery:", 126, 127),
		FIELD1("InterruptSent:", 125),
		FIELD("InterruptIDX:", 114, 124),
		FIELD1("InterruptDestination:", 113),
		FIELD1("InterruptArmed:", 112),
		FIELD("RxIntCounter:", 106, 111),
		FIELD("RxIntCounterThreshold:", 104, 105),
		FIELD1("Generation:", 103),
		{ "BaseAddress:", 48, 102, 9, 1 },
		FIELD("PIDX:", 32, 47),
		FIELD("CIDX:", 16, 31),
		{ "QueueSize:", 4, 15, 4, 0 },
		{ "QueueEntrySize:", 2, 3, 4, 0, 1 },
		FIELD1("QueueEntryOverride:", 1),
		FIELD1("CachePriority:", 0),
		{ NULL }
	};
	static struct field_desc flm_t5[] = {
		FIELD1("Valid:", 89),
		FIELD("SplitLenMode:", 87, 88),
		FIELD1("TPHintEn:", 86),
		FIELD("TPHint:", 84, 85),
		FIELD1("NoSnoop:", 83),
		FIELD1("RelaxedOrdering:", 82),
		FIELD("DCA_ST:", 71, 81),
		FIELD("EQid:", 54, 70),
		FIELD("SplitEn:", 52, 53),
		FIELD1("PadEn:", 51),
		FIELD1("PackEn:", 50),
		FIELD1("Cache_Lock :", 49),
		FIELD1("CongDrop:", 48),
		FIELD("PackOffset:", 16, 47),
		FIELD("CIDX:", 8, 15),
		FIELD("PIDX:", 0, 7),
		{ NULL }
	};
	static struct field_desc flm_t6[] = {
		FIELD1("Valid:", 89),
		FIELD("SplitLenMode:", 87, 88),
		FIELD1("TPHintEn:", 86),
		FIELD("TPHint:", 84, 85),
		FIELD1("NoSnoop:", 83),
		FIELD1("RelaxedOrdering:", 82),
		FIELD("DCA_ST:", 71, 81),
		FIELD("EQid:", 54, 70),
		FIELD("SplitEn:", 52, 53),
		FIELD1("PadEn:", 51),
		FIELD1("PackEn:", 50),
		FIELD1("Cache_Lock :", 49),
		FIELD1("CongDrop:", 48),
		FIELD1("Inflight:", 47),
		FIELD1("CongEn:", 46),
		FIELD1("CongMode:", 45),
		FIELD("PackOffset:", 20, 39),
		FIELD("CIDX:", 8, 15),
		FIELD("PIDX:", 0, 7),
		{ NULL }
	};
	static struct field_desc conm_t5[] = {
		FIELD1("CngMPSEnable:", 21),
		FIELD("CngTPMode:", 19, 20),
		FIELD1("CngDBPHdr:", 18),
		FIELD1("CngDBPData:", 17),
		FIELD1("CngIMSG:", 16),
		{ "CngChMap:", 0, 15, 0, 1, 0 },
		{ NULL }
	};

	if (p->mem_id == SGE_CONTEXT_EGRESS) {
		if (p->data[0] & 2)
			show_struct(p->data, 6, fl_t5);
		else if (vers == 5)
			show_struct(p->data, 6, egress_t5);
		else
			show_struct(p->data, 6, egress_t6);
	} else if (p->mem_id == SGE_CONTEXT_FLM)
		show_struct(p->data, 3, vers == 5 ? flm_t5 : flm_t6);
	else if (p->mem_id == SGE_CONTEXT_INGRESS)
		show_struct(p->data, 5, vers == 5 ? ingress_t5 : ingress_t6);
	else if (p->mem_id == SGE_CONTEXT_CNM)
		show_struct(p->data, 1, conm_t5);
}

static void
show_t4_ctxt(const struct t4_sge_context *p)
{
	static struct field_desc egress_t4[] = {
		FIELD1("StatusPgNS:", 180),
		FIELD1("StatusPgRO:", 179),
		FIELD1("FetchNS:", 178),
		FIELD1("FetchRO:", 177),
		FIELD1("Valid:", 176),
		FIELD("PCIeDataChannel:", 174, 175),
		FIELD1("DCAEgrQEn:", 173),
		FIELD("DCACPUID:", 168, 172),
		FIELD1("FCThreshOverride:", 167),
		FIELD("WRLength:", 162, 166),
		FIELD1("WRLengthKnown:", 161),
		FIELD1("ReschedulePending:", 160),
		FIELD1("OnChipQueue:", 159),
		FIELD1("FetchSizeMode", 158),
		{ "FetchBurstMin:", 156, 157, 4, 0, 1 },
		{ "FetchBurstMax:", 153, 154, 6, 0, 1 },
		FIELD("uPToken:", 133, 152),
		FIELD1("uPTokenEn:", 132),
		FIELD1("UserModeIO:", 131),
		FIELD("uPFLCredits:", 123, 130),
		FIELD1("uPFLCreditEn:", 122),
		FIELD("FID:", 111, 121),
		FIELD("HostFCMode:", 109, 110),
		FIELD1("HostFCOwner:", 108),
		{ "CIDXFlushThresh:", 105, 107, 0, 0, 1 },
		FIELD("CIDX:", 89, 104),
		FIELD("PIDX:", 73, 88),
		{ "BaseAddress:", 18, 72, 9, 1 },
		FIELD("QueueSize:", 2, 17),
		FIELD1("QueueType:", 1),
		FIELD1("CachePriority:", 0),
		{ NULL }
	};
	static struct field_desc fl_t4[] = {
		FIELD1("StatusPgNS:", 180),
		FIELD1("StatusPgRO:", 179),
		FIELD1("FetchNS:", 178),
		FIELD1("FetchRO:", 177),
		FIELD1("Valid:", 176),
		FIELD("PCIeDataChannel:", 174, 175),
		FIELD1("DCAEgrQEn:", 173),
		FIELD("DCACPUID:", 168, 172),
		FIELD1("FCThreshOverride:", 167),
		FIELD1("ReschedulePending:", 160),
		FIELD1("OnChipQueue:", 159),
		FIELD1("FetchSizeMode", 158),
		{ "FetchBurstMin:", 156, 157, 4, 0, 1 },
		{ "FetchBurstMax:", 153, 154, 6, 0, 1 },
		FIELD1("FLMcongMode:", 152),
		FIELD("MaxuPFLCredits:", 144, 151),
		FIELD("FLMcontextID:", 133, 143),
		FIELD1("uPTokenEn:", 132),
		FIELD1("UserModeIO:", 131),
		FIELD("uPFLCredits:", 123, 130),
		FIELD1("uPFLCreditEn:", 122),
		FIELD("FID:", 111, 121),
		FIELD("HostFCMode:", 109, 110),
		FIELD1("HostFCOwner:", 108),
		{ "CIDXFlushThresh:", 105, 107, 0, 0, 1 },
		FIELD("CIDX:", 89, 104),
		FIELD("PIDX:", 73, 88),
		{ "BaseAddress:", 18, 72, 9, 1 },
		FIELD("QueueSize:", 2, 17),
		FIELD1("QueueType:", 1),
		FIELD1("CachePriority:", 0),
		{ NULL }
	};
	static struct field_desc ingress_t4[] = {
		FIELD1("NoSnoop:", 145),
		FIELD1("RelaxedOrdering:", 144),
		FIELD1("GTSmode:", 143),
		FIELD1("ISCSICoalescing:", 142),
		FIELD1("Valid:", 141),
		FIELD1("TimerPending:", 140),
		FIELD1("DropRSS:", 139),
		FIELD("PCIeChannel:", 137, 138),
		FIELD1("SEInterruptArmed:", 136),
		FIELD1("CongestionMgtEnable:", 135),
		FIELD1("DCAIngQEnable:", 134),
		FIELD("DCACPUID:", 129, 133),
		FIELD1("UpdateScheduling:", 128),
		FIELD("UpdateDelivery:", 126, 127),
		FIELD1("InterruptSent:", 125),
		FIELD("InterruptIDX:", 114, 124),
		FIELD1("InterruptDestination:", 113),
		FIELD1("InterruptArmed:", 112),
		FIELD("RxIntCounter:", 106, 111),
		FIELD("RxIntCounterThreshold:", 104, 105),
		FIELD1("Generation:", 103),
		{ "BaseAddress:", 48, 102, 9, 1 },
		FIELD("PIDX:", 32, 47),
		FIELD("CIDX:", 16, 31),
		{ "QueueSize:", 4, 15, 4, 0 },
		{ "QueueEntrySize:", 2, 3, 4, 0, 1 },
		FIELD1("QueueEntryOverride:", 1),
		FIELD1("CachePriority:", 0),
		{ NULL }
	};
	static struct field_desc flm_t4[] = {
		FIELD1("NoSnoop:", 79),
		FIELD1("RelaxedOrdering:", 78),
		FIELD1("Valid:", 77),
		FIELD("DCACPUID:", 72, 76),
		FIELD1("DCAFLEn:", 71),
		FIELD("EQid:", 54, 70),
		FIELD("SplitEn:", 52, 53),
		FIELD1("PadEn:", 51),
		FIELD1("PackEn:", 50),
		FIELD1("DBpriority:", 48),
		FIELD("PackOffset:", 16, 47),
		FIELD("CIDX:", 8, 15),
		FIELD("PIDX:", 0, 7),
		{ NULL }
	};
	static struct field_desc conm_t4[] = {
		FIELD1("CngDBPHdr:", 6),
		FIELD1("CngDBPData:", 5),
		FIELD1("CngIMSG:", 4),
		{ "CngChMap:", 0, 3, 0, 1, 0},
		{ NULL }
	};

	if (p->mem_id == SGE_CONTEXT_EGRESS)
		show_struct(p->data, 6, (p->data[0] & 2) ? fl_t4 : egress_t4);
	else if (p->mem_id == SGE_CONTEXT_FLM)
		show_struct(p->data, 3, flm_t4);
	else if (p->mem_id == SGE_CONTEXT_INGRESS)
		show_struct(p->data, 5, ingress_t4);
	else if (p->mem_id == SGE_CONTEXT_CNM)
		show_struct(p->data, 1, conm_t4);
}

#undef FIELD
#undef FIELD1

static int
get_sge_context(int argc, const char *argv[])
{
	int rc;
	char *p;
	long cid;
	struct t4_sge_context cntxt = {0};

	if (argc != 2) {
		warnx("sge_context: incorrect number of arguments.");
		return (EINVAL);
	}

	if (!strcmp(argv[0], "egress"))
		cntxt.mem_id = SGE_CONTEXT_EGRESS;
	else if (!strcmp(argv[0], "ingress"))
		cntxt.mem_id = SGE_CONTEXT_INGRESS;
	else if (!strcmp(argv[0], "fl"))
		cntxt.mem_id = SGE_CONTEXT_FLM;
	else if (!strcmp(argv[0], "cong"))
		cntxt.mem_id = SGE_CONTEXT_CNM;
	else {
		warnx("unknown context type \"%s\"; known types are egress, "
		    "ingress, fl, and cong.", argv[0]);
		return (EINVAL);
	}

	p = str_to_number(argv[1], &cid, NULL);
	if (*p) {
		warnx("invalid context id \"%s\"", argv[1]);
		return (EINVAL);
	}
	cntxt.cid = cid;

	rc = doit(CHELSIO_T4_GET_SGE_CONTEXT, &cntxt);
	if (rc != 0)
		return (rc);

	if (chip_id == 4)
		show_t4_ctxt(&cntxt);
	else
		show_t5t6_ctxt(&cntxt, chip_id);

	return (0);
}

static int
loadfw(int argc, const char *argv[])
{
	int rc, fd;
	struct t4_data data = {0};
	const char *fname = argv[0];
	struct stat st = {0};

	if (argc != 1) {
		warnx("loadfw: incorrect number of arguments.");
		return (EINVAL);
	}

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		warn("open(%s)", fname);
		return (errno);
	}

	if (fstat(fd, &st) < 0) {
		warn("fstat");
		close(fd);
		return (errno);
	}

	data.len = st.st_size;
	data.data = mmap(0, data.len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (data.data == MAP_FAILED) {
		warn("mmap");
		close(fd);
		return (errno);
	}

	rc = doit(CHELSIO_T4_LOAD_FW, &data);
	munmap(data.data, data.len);
	close(fd);
	return (rc);
}

static int
loadcfg(int argc, const char *argv[])
{
	int rc, fd;
	struct t4_data data = {0};
	const char *fname = argv[0];
	struct stat st = {0};

	if (argc != 1) {
		warnx("loadcfg: incorrect number of arguments.");
		return (EINVAL);
	}

	if (strcmp(fname, "clear") == 0)
		return (doit(CHELSIO_T4_LOAD_CFG, &data));

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		warn("open(%s)", fname);
		return (errno);
	}

	if (fstat(fd, &st) < 0) {
		warn("fstat");
		close(fd);
		return (errno);
	}

	data.len = st.st_size;
	data.len &= ~3;		/* Clip off to make it a multiple of 4 */
	data.data = mmap(0, data.len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (data.data == MAP_FAILED) {
		warn("mmap");
		close(fd);
		return (errno);
	}

	rc = doit(CHELSIO_T4_LOAD_CFG, &data);
	munmap(data.data, data.len);
	close(fd);
	return (rc);
}

static int
dumpstate(int argc, const char *argv[])
{
	int rc, fd;
	struct t4_cudbg_dump dump = {0};
	const char *fname = argv[0];

	if (argc != 1) {
		warnx("dumpstate: incorrect number of arguments.");
		return (EINVAL);
	}

	dump.wr_flash = 0;
	memset(&dump.bitmap, 0xff, sizeof(dump.bitmap));
	dump.len = 8 * 1024 * 1024;
	dump.data = malloc(dump.len);
	if (dump.data == NULL) {
		return (ENOMEM);
	}

	rc = doit(CHELSIO_T4_CUDBG_DUMP, &dump);
	if (rc != 0)
		goto done;

	fd = open(fname, O_CREAT | O_TRUNC | O_EXCL | O_WRONLY,
	    S_IRUSR | S_IRGRP | S_IROTH);
	if (fd < 0) {
		warn("open(%s)", fname);
		rc = errno;
		goto done;
	}
	write(fd, dump.data, dump.len);
	close(fd);
done:
	free(dump.data);
	return (rc);
}

static int
read_mem(uint32_t addr, uint32_t len, void (*output)(uint32_t *, uint32_t))
{
	int rc;
	struct t4_mem_range mr;

	mr.addr = addr;
	mr.len = len;
	mr.data = malloc(mr.len);

	if (mr.data == 0) {
		warn("read_mem: malloc");
		return (errno);
	}

	rc = doit(CHELSIO_T4_GET_MEM, &mr);
	if (rc != 0)
		goto done;

	if (output)
		(*output)(mr.data, mr.len);
done:
	free(mr.data);
	return (rc);
}

static int
loadboot(int argc, const char *argv[])
{
	int rc, fd;
	long l;
	char *p;
	struct t4_bootrom br = {0};
	const char *fname = argv[0];
	struct stat st = {0};

	if (argc == 1) {
		br.pf_offset = 0;
		br.pfidx_addr = 0;
	} else if (argc == 3) {
		if (!strcmp(argv[1], "pf"))
			br.pf_offset = 0;
		else if (!strcmp(argv[1], "offset"))
			br.pf_offset = 1;
		else
			return (EINVAL);

		p = str_to_number(argv[2], &l, NULL);
		if (*p)
			return (EINVAL);
		br.pfidx_addr = l;
	} else {
		warnx("loadboot: incorrect number of arguments.");
		return (EINVAL);
	}

	if (strcmp(fname, "clear") == 0)
		return (doit(CHELSIO_T4_LOAD_BOOT, &br));

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		warn("open(%s)", fname);
		return (errno);
	}

	if (fstat(fd, &st) < 0) {
		warn("fstat");
		close(fd);
		return (errno);
	}

	br.len = st.st_size;
	br.data = mmap(0, br.len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (br.data == MAP_FAILED) {
		warn("mmap");
		close(fd);
		return (errno);
	}

	rc = doit(CHELSIO_T4_LOAD_BOOT, &br);
	munmap(br.data, br.len);
	close(fd);
	return (rc);
}

static int
loadbootcfg(int argc, const char *argv[])
{
	int rc, fd;
	struct t4_data bc = {0};
	const char *fname = argv[0];
	struct stat st = {0};

	if (argc != 1) {
		warnx("loadbootcfg: incorrect number of arguments.");
		return (EINVAL);
	}

	if (strcmp(fname, "clear") == 0)
		return (doit(CHELSIO_T4_LOAD_BOOTCFG, &bc));

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		warn("open(%s)", fname);
		return (errno);
	}

	if (fstat(fd, &st) < 0) {
		warn("fstat");
		close(fd);
		return (errno);
	}

	bc.len = st.st_size;
	bc.data = mmap(0, bc.len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (bc.data == MAP_FAILED) {
		warn("mmap");
		close(fd);
		return (errno);
	}

	rc = doit(CHELSIO_T4_LOAD_BOOTCFG, &bc);
	munmap(bc.data, bc.len);
	close(fd);
	return (rc);
}

/*
 * Display memory as list of 'n' 4-byte values per line.
 */
static void
show_mem(uint32_t *buf, uint32_t len)
{
	const char *s;
	int i, n = 8;

	while (len) {
		for (i = 0; len && i < n; i++, buf++, len -= 4) {
			s = i ? " " : "";
			printf("%s%08x", s, htonl(*buf));
		}
		printf("\n");
	}
}

static int
memdump(int argc, const char *argv[])
{
	char *p;
	long l;
	uint32_t addr, len;

	if (argc != 2) {
		warnx("incorrect number of arguments.");
		return (EINVAL);
	}

	p = str_to_number(argv[0], &l, NULL);
	if (*p) {
		warnx("invalid address \"%s\"", argv[0]);
		return (EINVAL);
	}
	addr = l;

	p = str_to_number(argv[1], &l, NULL);
	if (*p) {
		warnx("memdump: invalid length \"%s\"", argv[1]);
		return (EINVAL);
	}
	len = l;

	return (read_mem(addr, len, show_mem));
}

/*
 * Display TCB as list of 'n' 4-byte values per line.
 */
static void
show_tcb(uint32_t *buf, uint32_t len)
{
	unsigned char *tcb = (unsigned char *)buf;
	const char *s;
	int i, n = 8;

	while (len) {
		for (i = 0; len && i < n; i++, buf++, len -= 4) {
			s = i ? " " : "";
			printf("%s%08x", s, htonl(*buf));
		}
		printf("\n");
	}
	set_tcb_info(TIDTYPE_TCB, chip_id);
	set_print_style(PRNTSTYL_COMP);
	swizzle_tcb(tcb);
	parse_n_display_xcb(tcb);
}

#define A_TP_CMM_TCB_BASE 0x7d10
#define TCB_SIZE 128
static int
read_tcb(int argc, const char *argv[])
{
	char *p;
	long l;
	long long val;
	unsigned int tid;
	uint32_t addr;
	int rc;

	if (argc != 1) {
		warnx("incorrect number of arguments.");
		return (EINVAL);
	}

	p = str_to_number(argv[0], &l, NULL);
	if (*p) {
		warnx("invalid tid \"%s\"", argv[0]);
		return (EINVAL);
	}
	tid = l;

	rc = read_reg(A_TP_CMM_TCB_BASE, 4, &val);
	if (rc != 0)
		return (rc);

	addr = val + tid * TCB_SIZE;

	return (read_mem(addr, TCB_SIZE, show_tcb));
}

static int
read_i2c(int argc, const char *argv[])
{
	char *p;
	long l;
	struct t4_i2c_data i2cd;
	int rc, i;

	if (argc < 3 || argc > 4) {
		warnx("incorrect number of arguments.");
		return (EINVAL);
	}

	p = str_to_number(argv[0], &l, NULL);
	if (*p || l > UCHAR_MAX) {
		warnx("invalid port id \"%s\"", argv[0]);
		return (EINVAL);
	}
	i2cd.port_id = l;

	p = str_to_number(argv[1], &l, NULL);
	if (*p || l > UCHAR_MAX) {
		warnx("invalid i2c device address \"%s\"", argv[1]);
		return (EINVAL);
	}
	i2cd.dev_addr = l;

	p = str_to_number(argv[2], &l, NULL);
	if (*p || l > UCHAR_MAX) {
		warnx("invalid byte offset \"%s\"", argv[2]);
		return (EINVAL);
	}
	i2cd.offset = l;

	if (argc == 4) {
		p = str_to_number(argv[3], &l, NULL);
		if (*p || l > sizeof(i2cd.data)) {
			warnx("invalid number of bytes \"%s\"", argv[3]);
			return (EINVAL);
		}
		i2cd.len = l;
	} else
		i2cd.len = 1;

	rc = doit(CHELSIO_T4_GET_I2C, &i2cd);
	if (rc != 0)
		return (rc);

	for (i = 0; i < i2cd.len; i++)
		printf("0x%x [%u]\n", i2cd.data[i], i2cd.data[i]);

	return (0);
}

static int
clearstats(int argc, const char *argv[])
{
	char *p;
	long l;
	uint32_t port;

	if (argc != 1) {
		warnx("incorrect number of arguments.");
		return (EINVAL);
	}

	p = str_to_number(argv[0], &l, NULL);
	if (*p) {
		warnx("invalid port id \"%s\"", argv[0]);
		return (EINVAL);
	}
	port = l;

	return doit(CHELSIO_T4_CLEAR_STATS, &port);
}

static int
show_tracers(void)
{
	struct t4_tracer t;
	char *s;
	int rc, port_idx, i;
	long long val;

	/* Magic values: MPS_TRC_CFG = 0x9800. MPS_TRC_CFG[1:1] = TrcEn */
	rc = read_reg(0x9800, 4, &val);
	if (rc != 0)
		return (rc);
	printf("tracing is %s\n", val & 2 ? "ENABLED" : "DISABLED");

	t.idx = 0;
	for (t.idx = 0; ; t.idx++) {
		rc = doit(CHELSIO_T4_GET_TRACER, &t);
		if (rc != 0 || t.idx == 0xff)
			break;

		if (t.tp.port < 4) {
			s = "Rx";
			port_idx = t.tp.port;
		} else if (t.tp.port < 8) {
			s = "Tx";
			port_idx = t.tp.port - 4;
		} else if (t.tp.port < 12) {
			s = "loopback";
			port_idx = t.tp.port - 8;
		} else if (t.tp.port < 16) {
			s = "MPS Rx";
			port_idx = t.tp.port - 12;
		} else if (t.tp.port < 20) {
			s = "MPS Tx";
			port_idx = t.tp.port - 16;
		} else {
			s = "unknown";
			port_idx = t.tp.port;
		}

		printf("\ntracer %u (currently %s) captures ", t.idx,
		    t.enabled ? "ENABLED" : "DISABLED");
		if (t.tp.port < 8)
			printf("port %u %s, ", port_idx, s);
		else
			printf("%s %u, ", s, port_idx);
		printf("snap length: %u, min length: %u\n", t.tp.snap_len,
		    t.tp.min_len);
		printf("packets captured %smatch filter\n",
		    t.tp.invert ? "do not " : "");
		if (t.tp.skip_ofst) {
			printf("filter pattern: ");
			for (i = 0; i < t.tp.skip_ofst * 2; i += 2)
				printf("%08x%08x", t.tp.data[i],
				    t.tp.data[i + 1]);
			printf("/");
			for (i = 0; i < t.tp.skip_ofst * 2; i += 2)
				printf("%08x%08x", t.tp.mask[i],
				    t.tp.mask[i + 1]);
			printf("@0\n");
		}
		printf("filter pattern: ");
		for (i = t.tp.skip_ofst * 2; i < T4_TRACE_LEN / 4; i += 2)
			printf("%08x%08x", t.tp.data[i], t.tp.data[i + 1]);
		printf("/");
		for (i = t.tp.skip_ofst * 2; i < T4_TRACE_LEN / 4; i += 2)
			printf("%08x%08x", t.tp.mask[i], t.tp.mask[i + 1]);
		printf("@%u\n", (t.tp.skip_ofst + t.tp.skip_len) * 8);
	}

	return (rc);
}

static int
tracer_onoff(uint8_t idx, int enabled)
{
	struct t4_tracer t;

	t.idx = idx;
	t.enabled = enabled;
	t.valid = 0;

	return doit(CHELSIO_T4_SET_TRACER, &t);
}

static void
create_tracing_ifnet()
{
	char *cmd[] = {
		"/sbin/ifconfig", __DECONST(char *, nexus), "create", NULL
	};
	char *env[] = {NULL};

	if (vfork() == 0) {
		close(STDERR_FILENO);
		execve(cmd[0], cmd, env);
		_exit(0);
	}
}

/*
 * XXX: Allow user to specify snaplen, minlen, and pattern (including inverted
 * matching).  Right now this is a quick-n-dirty implementation that traces the
 * first 128B of all tx or rx on a port
 */
static int
set_tracer(uint8_t idx, int argc, const char *argv[])
{
	struct t4_tracer t;
	int len, port;

	bzero(&t, sizeof (t));
	t.idx = idx;
	t.enabled = 1;
	t.valid = 1;

	if (argc != 1) {
		warnx("must specify tx<n> or rx<n>.");
		return (EINVAL);
	}

	len = strlen(argv[0]);
	if (len != 3) {
		warnx("argument must be 3 characters (tx<n> or rx<n>)");
		return (EINVAL);
	}

	if (strncmp(argv[0], "tx", 2) == 0) {
		port = argv[0][2] - '0';
		if (port < 0 || port > 3) {
			warnx("'%c' in %s is invalid", argv[0][2], argv[0]);
			return (EINVAL);
		}
		port += 4;
	} else if (strncmp(argv[0], "rx", 2) == 0) {
		port = argv[0][2] - '0';
		if (port < 0 || port > 3) {
			warnx("'%c' in %s is invalid", argv[0][2], argv[0]);
			return (EINVAL);
		}
	} else {
		warnx("argument '%s' isn't tx<n> or rx<n>", argv[0]);
		return (EINVAL);
	}

	t.tp.snap_len = 128;
	t.tp.min_len = 0;
	t.tp.skip_ofst = 0;
	t.tp.skip_len = 0;
	t.tp.invert = 0;
	t.tp.port = port;

	create_tracing_ifnet();
	return doit(CHELSIO_T4_SET_TRACER, &t);
}

static int
tracer_cmd(int argc, const char *argv[])
{
	long long val;
	uint8_t idx;
	char *s;

	if (argc == 0) {
		warnx("tracer: no arguments.");
		return (EINVAL);
	};

	/* list */
	if (strcmp(argv[0], "list") == 0) {
		if (argc != 1)
			warnx("trailing arguments after \"list\" ignored.");

		return show_tracers();
	}

	/* <idx> ... */
	s = str_to_number(argv[0], NULL, &val);
	if (*s || val > 0xff) {
		warnx("\"%s\" is neither an index nor a tracer subcommand.",
		    argv[0]);
		return (EINVAL);
	}
	idx = (int8_t)val;

	/* <idx> disable */
	if (argc == 2 && strcmp(argv[1], "disable") == 0)
		return tracer_onoff(idx, 0);

	/* <idx> enable */
	if (argc == 2 && strcmp(argv[1], "enable") == 0)
		return tracer_onoff(idx, 1);

	/* <idx> ... */
	return set_tracer(idx, argc - 1, argv + 1);
}

static int
modinfo_raw(int port_id)
{
	uint8_t offset;
	struct t4_i2c_data i2cd;
	int rc;

	for (offset = 0; offset < 96; offset += sizeof(i2cd.data)) {
		bzero(&i2cd, sizeof(i2cd));
		i2cd.port_id = port_id;
		i2cd.dev_addr = 0xa0;
		i2cd.offset = offset;
		i2cd.len = sizeof(i2cd.data);
		rc = doit(CHELSIO_T4_GET_I2C, &i2cd);
		if (rc != 0)
			return (rc);
		printf("%02x:  %02x %02x %02x %02x  %02x %02x %02x %02x",
		    offset, i2cd.data[0], i2cd.data[1], i2cd.data[2],
		    i2cd.data[3], i2cd.data[4], i2cd.data[5], i2cd.data[6],
		    i2cd.data[7]);

		printf("  %c%c%c%c %c%c%c%c\n",
		    isprint(i2cd.data[0]) ? i2cd.data[0] : '.',
		    isprint(i2cd.data[1]) ? i2cd.data[1] : '.',
		    isprint(i2cd.data[2]) ? i2cd.data[2] : '.',
		    isprint(i2cd.data[3]) ? i2cd.data[3] : '.',
		    isprint(i2cd.data[4]) ? i2cd.data[4] : '.',
		    isprint(i2cd.data[5]) ? i2cd.data[5] : '.',
		    isprint(i2cd.data[6]) ? i2cd.data[6] : '.',
		    isprint(i2cd.data[7]) ? i2cd.data[7] : '.');
	}

	return (0);
}

static int
modinfo(int argc, const char *argv[])
{
	long port;
	char string[16], *p;
	struct t4_i2c_data i2cd;
	int rc, i;
	uint16_t temp, vcc, tx_bias, tx_power, rx_power;

	if (argc < 1) {
		warnx("must supply a port");
		return (EINVAL);
	}

	if (argc > 2) {
		warnx("too many arguments");
		return (EINVAL);
	}

	p = str_to_number(argv[0], &port, NULL);
	if (*p || port > UCHAR_MAX) {
		warnx("invalid port id \"%s\"", argv[0]);
		return (EINVAL);
	}

	if (argc == 2) {
		if (!strcmp(argv[1], "raw"))
			return (modinfo_raw(port));
		else {
			warnx("second argument can only be \"raw\"");
			return (EINVAL);
		}
	}

	bzero(&i2cd, sizeof(i2cd));
	i2cd.len = 1;
	i2cd.port_id = port;
	i2cd.dev_addr = SFF_8472_BASE;

	i2cd.offset = SFF_8472_ID;
	if ((rc = doit(CHELSIO_T4_GET_I2C, &i2cd)) != 0)
		goto fail;

	if (i2cd.data[0] > SFF_8472_ID_LAST)
		printf("Unknown ID\n");
	else
		printf("ID: %s\n", sff_8472_id[i2cd.data[0]]);

	bzero(&string, sizeof(string));
	for (i = SFF_8472_VENDOR_START; i < SFF_8472_VENDOR_END; i++) {
		i2cd.offset = i;
		if ((rc = doit(CHELSIO_T4_GET_I2C, &i2cd)) != 0)
			goto fail;
		string[i - SFF_8472_VENDOR_START] = i2cd.data[0];
	}
	printf("Vendor %s\n", string);

	bzero(&string, sizeof(string));
	for (i = SFF_8472_SN_START; i < SFF_8472_SN_END; i++) {
		i2cd.offset = i;
		if ((rc = doit(CHELSIO_T4_GET_I2C, &i2cd)) != 0)
			goto fail;
		string[i - SFF_8472_SN_START] = i2cd.data[0];
	}
	printf("SN %s\n", string);

	bzero(&string, sizeof(string));
	for (i = SFF_8472_PN_START; i < SFF_8472_PN_END; i++) {
		i2cd.offset = i;
		if ((rc = doit(CHELSIO_T4_GET_I2C, &i2cd)) != 0)
			goto fail;
		string[i - SFF_8472_PN_START] = i2cd.data[0];
	}
	printf("PN %s\n", string);

	bzero(&string, sizeof(string));
	for (i = SFF_8472_REV_START; i < SFF_8472_REV_END; i++) {
		i2cd.offset = i;
		if ((rc = doit(CHELSIO_T4_GET_I2C, &i2cd)) != 0)
			goto fail;
		string[i - SFF_8472_REV_START] = i2cd.data[0];
	}
	printf("Rev %s\n", string);

	i2cd.offset = SFF_8472_DIAG_TYPE;
	if ((rc = doit(CHELSIO_T4_GET_I2C, &i2cd)) != 0)
		goto fail;

	if ((char )i2cd.data[0] & (SFF_8472_DIAG_IMPL |
				   SFF_8472_DIAG_INTERNAL)) {

		/* Switch to reading from the Diagnostic address. */
		i2cd.dev_addr = SFF_8472_DIAG;
		i2cd.len = 1;

		i2cd.offset = SFF_8472_TEMP;
		if ((rc = doit(CHELSIO_T4_GET_I2C, &i2cd)) != 0)
			goto fail;
		temp = i2cd.data[0] << 8;
		printf("Temp: ");
		if ((temp & SFF_8472_TEMP_SIGN) == SFF_8472_TEMP_SIGN)
			printf("-");
		else
			printf("+");
		printf("%dC\n", (temp & SFF_8472_TEMP_MSK) >>
		    SFF_8472_TEMP_SHIFT);

		i2cd.offset = SFF_8472_VCC;
		if ((rc = doit(CHELSIO_T4_GET_I2C, &i2cd)) != 0)
			goto fail;
		vcc = i2cd.data[0] << 8;
		printf("Vcc %fV\n", vcc / SFF_8472_VCC_FACTOR);

		i2cd.offset = SFF_8472_TX_BIAS;
		if ((rc = doit(CHELSIO_T4_GET_I2C, &i2cd)) != 0)
			goto fail;
		tx_bias = i2cd.data[0] << 8;
		printf("TX Bias %fuA\n", tx_bias / SFF_8472_BIAS_FACTOR);

		i2cd.offset = SFF_8472_TX_POWER;
		if ((rc = doit(CHELSIO_T4_GET_I2C, &i2cd)) != 0)
			goto fail;
		tx_power = i2cd.data[0] << 8;
		printf("TX Power %fmW\n", tx_power / SFF_8472_POWER_FACTOR);

		i2cd.offset = SFF_8472_RX_POWER;
		if ((rc = doit(CHELSIO_T4_GET_I2C, &i2cd)) != 0)
			goto fail;
		rx_power = i2cd.data[0] << 8;
		printf("RX Power %fmW\n", rx_power / SFF_8472_POWER_FACTOR);

	} else
		printf("Diagnostics not supported.\n");

	return(0);

fail:
	if (rc == EPERM)
		warnx("No module/cable in port %ld", port);
	return (rc);

}

/* XXX: pass in a low/high and do range checks as well */
static int
get_sched_param(const char *param, const char *args[], long *val)
{
	char *p;

	if (strcmp(param, args[0]) != 0)
		return (EINVAL);

	p = str_to_number(args[1], val, NULL);
	if (*p) {
		warnx("parameter \"%s\" has bad value \"%s\"", args[0],
		    args[1]);
		return (EINVAL);
	}

	return (0);
}

static int
sched_class(int argc, const char *argv[])
{
	struct t4_sched_params op;
	int errs, i;

	memset(&op, 0xff, sizeof(op));
	op.subcmd = -1;
	op.type = -1;
	if (argc == 0) {
		warnx("missing scheduling sub-command");
		return (EINVAL);
	}
	if (!strcmp(argv[0], "config")) {
		op.subcmd = SCHED_CLASS_SUBCMD_CONFIG;
		op.u.config.minmax = -1;
	} else if (!strcmp(argv[0], "params")) {
		op.subcmd = SCHED_CLASS_SUBCMD_PARAMS;
		op.u.params.level = op.u.params.mode = op.u.params.rateunit =
		    op.u.params.ratemode = op.u.params.channel =
		    op.u.params.cl = op.u.params.minrate = op.u.params.maxrate =
		    op.u.params.weight = op.u.params.pktsize = -1;
	} else {
		warnx("invalid scheduling sub-command \"%s\"", argv[0]);
		return (EINVAL);
	}

	/* Decode remaining arguments ... */
	errs = 0;
	for (i = 1; i < argc; i += 2) {
		const char **args = &argv[i];
		long l;

		if (i + 1 == argc) {
			warnx("missing argument for \"%s\"", args[0]);
			errs++;
			break;
		}

		if (!strcmp(args[0], "type")) {
			if (!strcmp(args[1], "packet"))
				op.type = SCHED_CLASS_TYPE_PACKET;
			else {
				warnx("invalid type parameter \"%s\"", args[1]);
				errs++;
			}

			continue;
		}

		if (op.subcmd == SCHED_CLASS_SUBCMD_CONFIG) {
			if(!get_sched_param("minmax", args, &l))
				op.u.config.minmax = (int8_t)l;
			else {
				warnx("unknown scheduler config parameter "
				    "\"%s\"", args[0]);
				errs++;
			}

			continue;
		}

		/* Rest applies only to SUBCMD_PARAMS */
		if (op.subcmd != SCHED_CLASS_SUBCMD_PARAMS)
			continue;

		if (!strcmp(args[0], "level")) {
			if (!strcmp(args[1], "cl-rl"))
				op.u.params.level = SCHED_CLASS_LEVEL_CL_RL;
			else if (!strcmp(args[1], "cl-wrr"))
				op.u.params.level = SCHED_CLASS_LEVEL_CL_WRR;
			else if (!strcmp(args[1], "ch-rl"))
				op.u.params.level = SCHED_CLASS_LEVEL_CH_RL;
			else {
				warnx("invalid level parameter \"%s\"",
				    args[1]);
				errs++;
			}
		} else if (!strcmp(args[0], "mode")) {
			if (!strcmp(args[1], "class"))
				op.u.params.mode = SCHED_CLASS_MODE_CLASS;
			else if (!strcmp(args[1], "flow"))
				op.u.params.mode = SCHED_CLASS_MODE_FLOW;
			else {
				warnx("invalid mode parameter \"%s\"", args[1]);
				errs++;
			}
		} else if (!strcmp(args[0], "rate-unit")) {
			if (!strcmp(args[1], "bits"))
				op.u.params.rateunit = SCHED_CLASS_RATEUNIT_BITS;
			else if (!strcmp(args[1], "pkts"))
				op.u.params.rateunit = SCHED_CLASS_RATEUNIT_PKTS;
			else {
				warnx("invalid rate-unit parameter \"%s\"",
				    args[1]);
				errs++;
			}
		} else if (!strcmp(args[0], "rate-mode")) {
			if (!strcmp(args[1], "relative"))
				op.u.params.ratemode = SCHED_CLASS_RATEMODE_REL;
			else if (!strcmp(args[1], "absolute"))
				op.u.params.ratemode = SCHED_CLASS_RATEMODE_ABS;
			else {
				warnx("invalid rate-mode parameter \"%s\"",
				    args[1]);
				errs++;
			}
		} else if (!get_sched_param("channel", args, &l))
			op.u.params.channel = (int8_t)l;
		else if (!get_sched_param("class", args, &l))
			op.u.params.cl = (int8_t)l;
		else if (!get_sched_param("min-rate", args, &l))
			op.u.params.minrate = (int32_t)l;
		else if (!get_sched_param("max-rate", args, &l))
			op.u.params.maxrate = (int32_t)l;
		else if (!get_sched_param("weight", args, &l))
			op.u.params.weight = (int16_t)l;
		else if (!get_sched_param("pkt-size", args, &l))
			op.u.params.pktsize = (int16_t)l;
		else {
			warnx("unknown scheduler parameter \"%s\"", args[0]);
			errs++;
		}
	}

	/*
	 * Catch some logical fallacies in terms of argument combinations here
	 * so we can offer more than just the EINVAL return from the driver.
	 * The driver will be able to catch a lot more issues since it knows
	 * the specifics of the device hardware capabilities like how many
	 * channels, classes, etc. the device supports.
	 */
	if (op.type < 0) {
		warnx("sched \"type\" parameter missing");
		errs++;
	}
	if (op.subcmd == SCHED_CLASS_SUBCMD_CONFIG) {
		if (op.u.config.minmax < 0) {
			warnx("sched config \"minmax\" parameter missing");
			errs++;
		}
	}
	if (op.subcmd == SCHED_CLASS_SUBCMD_PARAMS) {
		if (op.u.params.level < 0) {
			warnx("sched params \"level\" parameter missing");
			errs++;
		}
		if (op.u.params.mode < 0 &&
		    op.u.params.level == SCHED_CLASS_LEVEL_CL_RL) {
			warnx("sched params \"mode\" parameter missing");
			errs++;
		}
		if (op.u.params.rateunit < 0 &&
		    (op.u.params.level == SCHED_CLASS_LEVEL_CL_RL ||
		    op.u.params.level == SCHED_CLASS_LEVEL_CH_RL)) {
			warnx("sched params \"rate-unit\" parameter missing");
			errs++;
		}
		if (op.u.params.ratemode < 0 &&
		    (op.u.params.level == SCHED_CLASS_LEVEL_CL_RL ||
		    op.u.params.level == SCHED_CLASS_LEVEL_CH_RL)) {
			warnx("sched params \"rate-mode\" parameter missing");
			errs++;
		}
		if (op.u.params.channel < 0) {
			warnx("sched params \"channel\" missing");
			errs++;
		}
		if (op.u.params.cl < 0 &&
		    (op.u.params.level == SCHED_CLASS_LEVEL_CL_RL ||
		    op.u.params.level == SCHED_CLASS_LEVEL_CL_WRR)) {
			warnx("sched params \"class\" missing");
			errs++;
		}
		if (op.u.params.maxrate < 0 &&
		    (op.u.params.level == SCHED_CLASS_LEVEL_CL_RL ||
		    op.u.params.level == SCHED_CLASS_LEVEL_CH_RL)) {
			warnx("sched params \"max-rate\" missing for "
			    "rate-limit level");
			errs++;
		}
		if (op.u.params.level == SCHED_CLASS_LEVEL_CL_WRR &&
		    (op.u.params.weight < 1 || op.u.params.weight > 99)) {
			warnx("sched params \"weight\" missing or invalid "
			    "(not 1-99) for weighted-round-robin level");
			errs++;
		}
		if (op.u.params.pktsize < 0 &&
		    op.u.params.level == SCHED_CLASS_LEVEL_CL_RL) {
			warnx("sched params \"pkt-size\" missing for "
			    "rate-limit level");
			errs++;
		}
		if (op.u.params.mode == SCHED_CLASS_MODE_FLOW &&
		    op.u.params.ratemode != SCHED_CLASS_RATEMODE_ABS) {
			warnx("sched params mode flow needs rate-mode absolute");
			errs++;
		}
		if (op.u.params.ratemode == SCHED_CLASS_RATEMODE_REL &&
		    !in_range(op.u.params.maxrate, 1, 100)) {
                        warnx("sched params \"max-rate\" takes "
			    "percentage value(1-100) for rate-mode relative");
                        errs++;
                }
                if (op.u.params.ratemode == SCHED_CLASS_RATEMODE_ABS &&
		    !in_range(op.u.params.maxrate, 1, 100000000)) {
                        warnx("sched params \"max-rate\" takes "
			    "value(1-100000000) for rate-mode absolute");
                        errs++;
                }
                if (op.u.params.maxrate > 0 &&
		    op.u.params.maxrate < op.u.params.minrate) {
                        warnx("sched params \"max-rate\" is less than "
			    "\"min-rate\"");
                        errs++;
                }
	}

	if (errs > 0) {
		warnx("%d error%s in sched-class command", errs,
		    errs == 1 ? "" : "s");
		return (EINVAL);
	}

	return doit(CHELSIO_T4_SCHED_CLASS, &op);
}

static int
sched_queue(int argc, const char *argv[])
{
	struct t4_sched_queue op = {0};
	char *p;
	long val;

	if (argc != 3) {
		/* need "<port> <queue> <class> */
		warnx("incorrect number of arguments.");
		return (EINVAL);
	}

	p = str_to_number(argv[0], &val, NULL);
	if (*p || val > UCHAR_MAX) {
		warnx("invalid port id \"%s\"", argv[0]);
		return (EINVAL);
	}
	op.port = (uint8_t)val;

	if (!strcmp(argv[1], "all") || !strcmp(argv[1], "*"))
		op.queue = -1;
	else {
		p = str_to_number(argv[1], &val, NULL);
		if (*p || val < -1) {
			warnx("invalid queue \"%s\"", argv[1]);
			return (EINVAL);
		}
		op.queue = (int8_t)val;
	}

	if (!strcmp(argv[2], "unbind") || !strcmp(argv[2], "clear"))
		op.cl = -1;
	else {
		p = str_to_number(argv[2], &val, NULL);
		if (*p || val < -1) {
			warnx("invalid class \"%s\"", argv[2]);
			return (EINVAL);
		}
		op.cl = (int8_t)val;
	}

	return doit(CHELSIO_T4_SCHED_QUEUE, &op);
}

static int
parse_offload_settings_word(const char *s, char **pnext, const char *ws,
    int *pneg, struct offload_settings *os)
{

	while (*s == '!') {
		(*pneg)++;
		s++;
	}

	if (!strcmp(s, "not")) {
		(*pneg)++;
		return (0);
	}

	if (!strcmp(s, "offload")) {
		os->offload = (*pneg + 1) & 1;
		*pneg = 0;
	} else if (!strcmp(s , "coalesce")) {
		os->rx_coalesce = (*pneg + 1) & 1;
		*pneg = 0;
	} else if (!strcmp(s, "timestamp") || !strcmp(s, "tstamp")) {
		os->tstamp = (*pneg + 1) & 1;
		*pneg = 0;
	} else if (!strcmp(s, "sack")) {
		os->sack = (*pneg + 1) & 1;
		*pneg = 0;
	} else if (!strcmp(s, "nagle")) {
		os->nagle = (*pneg + 1) & 1;
		*pneg = 0;
	} else if (!strcmp(s, "ecn")) {
		os->ecn = (*pneg + 1) & 1;
		*pneg = 0;
	} else if (!strcmp(s, "ddp")) {
		os->ddp = (*pneg + 1) & 1;
		*pneg = 0;
	} else if (!strcmp(s, "tls")) {
		os->tls = (*pneg + 1) & 1;
		*pneg = 0;
	} else {
		char *param, *p;
		long val;

		/* Settings with additional parameter handled here. */

		if (*pneg) {
			warnx("\"%s\" is not a valid keyword, or it does not "
			    "support negation.", s);
			return (EINVAL);
		}

		while ((param = strsep(pnext, ws)) != NULL) {
			if (*param != '\0')
				break;
		}
		if (param == NULL) {
			warnx("\"%s\" is not a valid keyword, or it requires a "
			    "parameter that has not been provided.", s);
			return (EINVAL);
		}

		if (!strcmp(s, "cong")) {
			if (!strcmp(param, "reno"))
				os->cong_algo = 0;
			else if (!strcmp(param, "tahoe"))
				os->cong_algo = 1;
			else if (!strcmp(param, "newreno"))
				os->cong_algo = 2;
			else if (!strcmp(param, "highspeed"))
				os->cong_algo = 3;
			else {
				warnx("unknown congestion algorithm \"%s\".", s);
				return (EINVAL);
			}
		} else if (!strcmp(s, "class")) {
			val = -1;
			p = str_to_number(param, &val, NULL);
			/* (nsched_cls - 1) is spelled 15 here. */
			if (*p || val < 0 || val > 15) {
				warnx("invalid scheduling class \"%s\".  "
				    "\"class\" needs an integer value where "
				    "0 <= value <= 15", param);
				return (EINVAL);
			}
			os->sched_class = val;
		} else if (!strcmp(s, "bind") || !strcmp(s, "txq") ||
		    !strcmp(s, "rxq")) {
			val = -1;
			if (strcmp(param, "random")) {
				p = str_to_number(param, &val, NULL);
				if (*p || val < 0 || val > 0xffff) {
					warnx("invalid queue specification "
					    "\"%s\".  \"%s\" needs an integer"
					    " value, or \"random\".",
					    param, s);
					return (EINVAL);
				}
			}
			if (!strcmp(s, "bind")) {
				os->txq = val;
				os->rxq = val;
			} else if (!strcmp(s, "txq")) {
				os->txq = val;
			} else if (!strcmp(s, "rxq")) {
				os->rxq = val;
			} else {
				return (EDOOFUS);
			}
		} else if (!strcmp(s, "mss")) {
			val = -1;
			p = str_to_number(param, &val, NULL);
			if (*p || val <= 0) {
				warnx("invalid MSS specification \"%s\".  "
				    "\"mss\" needs a positive integer value",
				    param);
				return (EINVAL);
			}
			os->mss = val;
		} else  {
			warnx("unknown settings keyword: \"%s\"", s);
			return (EINVAL);
		}
	}

	return (0);
}

static int
parse_offload_settings(const char *settings_ro, struct offload_settings *os)
{
	const char *ws = " \f\n\r\v\t";
	char *settings, *s, *next;
	int rc, nsettings, neg;
	static const struct offload_settings default_settings = {
		.offload = 0,	/* No settings imply !offload */
		.rx_coalesce = -1,
		.cong_algo = -1,
		.sched_class = -1,
		.tstamp = -1,
		.sack = -1,
		.nagle = -1,
		.ecn = -1,
		.ddp = -1,
		.tls = -1,
		.txq = -1,
		.rxq = -1,
		.mss = -1,
	};

	*os = default_settings;

	next = settings = strdup(settings_ro);
	if (settings == NULL) {
		warn (NULL);
		return (errno);
	}

	nsettings = 0;
	rc = 0;
	neg = 0;
	while ((s = strsep(&next, ws)) != NULL) {
		if (*s == '\0')
			continue;
		nsettings++;
		rc = parse_offload_settings_word(s, &next, ws, &neg, os);
		if (rc != 0)
			goto done;
	}
	if (nsettings == 0) {
		warnx("no settings provided");
		rc = EINVAL;
		goto done;
	}
	if (neg > 0) {
		warnx("%d stray negation(s) at end of offload settings", neg);
		rc = EINVAL;
		goto done;
	}
done:
	free(settings);
	return (rc);
}

static int
isempty_line(char *line, size_t llen)
{

	/* skip leading whitespace */
	while (isspace(*line)) {
		line++;
		llen--;
	}
	if (llen == 0 || *line == '#' || *line == '\n')
		return (1);

	return (0);
}

static int
special_offload_rule(char *str)
{

	/* skip leading whitespaces */
	while (isspace(*str))
		str++;

	/* check for special strings: "-", "all", "any" */
	if (*str == '-') {
		str++;
	} else if (!strncmp(str, "all", 3) || !strncmp(str, "any", 3)) {
		str += 3;
	} else {
		return (0);
	}

	/* skip trailing whitespaces */
	while (isspace(*str))
		str++;

	return (*str == '\0');
}

/*
 * A rule has 3 parts: an open-type, a match expression, and offload settings.
 *
 * [<open-type>] <expr> => <settings>
 */
static int
parse_offload_policy_line(size_t lno, char *line, size_t llen, pcap_t *pd,
    struct offload_rule *r)
{
	char *expr, *settings, *s;

	bzero(r, sizeof(*r));

	/* Skip leading whitespace. */
	while (isspace(*line))
		line++;
	/* Trim trailing whitespace */
	s = &line[llen - 1];
	while (isspace(*s)) {
		*s-- = '\0';
		llen--;
	}

	/*
	 * First part of the rule: '[X]' where X = A/D/L/P
	 */
	if (*line++ != '[') {
		warnx("missing \"[\" on line %zd", lno);
		return (EINVAL);
	}
	switch (*line) {
	case 'A':
	case 'D':
	case 'L':
	case 'P':
		r->open_type = *line;
		break;
	default:
		warnx("invalid socket-type \"%c\" on line %zd.", *line, lno);
		return (EINVAL);
	}
	line++;
	if (*line++ != ']') {
		warnx("missing \"]\" after \"[%c\" on line %zd",
		    r->open_type, lno);
		return (EINVAL);
	}

	/* Skip whitespace. */
	while (isspace(*line))
		line++;

	/*
	 * Rest of the rule: <expr> => <settings>
	 */
	expr = line;
	s = strstr(line, "=>");
	if (s == NULL)
		return (EINVAL);
	settings = s + 2;
	while (isspace(*settings))
		settings++;
	*s = '\0';

	/*
	 * <expr> is either a special name (all, any) or a pcap-filter(7).
	 * In case of a special name the bpf_prog stays all-zero.
	 */
	if (!special_offload_rule(expr)) {
		if (pcap_compile(pd, &r->bpf_prog, expr, 1,
		    PCAP_NETMASK_UNKNOWN) < 0) {
			warnx("failed to compile \"%s\" on line %zd: %s", expr,
			    lno, pcap_geterr(pd));
			return (EINVAL);
		}
	}

	/* settings to apply on a match. */
	if (parse_offload_settings(settings, &r->settings) != 0) {
		warnx("failed to parse offload settings \"%s\" on line %zd",
		    settings, lno);
		pcap_freecode(&r->bpf_prog);
		return (EINVAL);
	}

	return (0);

}

/*
 * Note that op itself is not dynamically allocated.
 */
static void
free_offload_policy(struct t4_offload_policy *op)
{
	int i;

	for (i = 0; i < op->nrules; i++) {
		/*
		 * pcap_freecode can cope with empty bpf_prog, which is the case
		 * for an rule that matches on 'any/all/-'.
		 */
		pcap_freecode(&op->rule[i].bpf_prog);
	}
	free(op->rule);
	op->nrules = 0;
	op->rule = NULL;
}

#define REALLOC_STRIDE 32

/*
 * Fills up op->nrules and op->rule.
 */
static int
parse_offload_policy(const char *fname, struct t4_offload_policy *op)
{
	FILE *fp;
	char *line;
	int lno, maxrules, rc;
	size_t lcap, llen;
	struct offload_rule *r;
	pcap_t *pd;

	fp = fopen(fname, "r");
	if (fp == NULL) {
		warn("Unable to open file \"%s\"", fname);
		return (errno);
	}
	pd = pcap_open_dead(DLT_EN10MB, 128);
	if (pd == NULL) {
		warnx("Failed to open pcap device");
		fclose(fp);
		return (EIO);
	}

	rc = 0;
	lno = 0;
	lcap = 0;
	maxrules = 0;
	op->nrules = 0;
	op->rule = NULL;
	line = NULL;

	while ((llen = getline(&line, &lcap, fp)) != -1) {
		lno++;

		/* Skip empty lines. */
		if (isempty_line(line, llen))
			continue;

		if (op->nrules == maxrules) {
			maxrules += REALLOC_STRIDE;
			r = realloc(op->rule,
			    maxrules * sizeof(struct offload_rule));
			if (r == NULL) {
				warnx("failed to allocate memory for %d rules",
				    maxrules);
				rc = ENOMEM;
				goto done;
			}
			op->rule = r;
		}

		r = &op->rule[op->nrules];
		rc = parse_offload_policy_line(lno, line, llen, pd, r);
		if (rc != 0) {
			warnx("Error parsing line %d of \"%s\"", lno, fname);
			goto done;
		}

		op->nrules++;
	}
	free(line);

	if (!feof(fp)) {
		warn("Error while reading from file \"%s\" at line %d",
		    fname, lno);
		rc = errno;
		goto done;
	}

	if (op->nrules == 0) {
		warnx("No valid rules found in \"%s\"", fname);
		rc = EINVAL;
	}
done:
	pcap_close(pd);
	fclose(fp);
	if (rc != 0) {
		free_offload_policy(op);
	}

	return (rc);
}

static int
load_offload_policy(int argc, const char *argv[])
{
	int rc = 0;
	const char *fname = argv[0];
	struct t4_offload_policy op = {0};

	if (argc != 1) {
		warnx("incorrect number of arguments.");
		return (EINVAL);
	}

	if (!strcmp(fname, "clear") || !strcmp(fname, "none")) {
		/* op.nrules is 0 and that means clear policy */
		return (doit(CHELSIO_T4_SET_OFLD_POLICY, &op));
	}

	rc = parse_offload_policy(fname, &op);
	if (rc != 0) {
		/* Error message displayed already */
		return (EINVAL);
	}

	rc = doit(CHELSIO_T4_SET_OFLD_POLICY, &op);
	free_offload_policy(&op);

	return (rc);
}

static int
run_cmd(int argc, const char *argv[])
{
	int rc = -1;
	const char *cmd = argv[0];

	/* command */
	argc--;
	argv++;

	if (!strcmp(cmd, "reg") || !strcmp(cmd, "reg32"))
		rc = register_io(argc, argv, 4);
	else if (!strcmp(cmd, "reg64"))
		rc = register_io(argc, argv, 8);
	else if (!strcmp(cmd, "regdump"))
		rc = dump_regs(argc, argv);
	else if (!strcmp(cmd, "filter"))
		rc = filter_cmd(argc, argv, 0);
	else if (!strcmp(cmd, "context"))
		rc = get_sge_context(argc, argv);
	else if (!strcmp(cmd, "loadfw"))
		rc = loadfw(argc, argv);
	else if (!strcmp(cmd, "memdump"))
		rc = memdump(argc, argv);
	else if (!strcmp(cmd, "tcb"))
		rc = read_tcb(argc, argv);
	else if (!strcmp(cmd, "i2c"))
		rc = read_i2c(argc, argv);
	else if (!strcmp(cmd, "clearstats"))
		rc = clearstats(argc, argv);
	else if (!strcmp(cmd, "tracer"))
		rc = tracer_cmd(argc, argv);
	else if (!strcmp(cmd, "modinfo"))
		rc = modinfo(argc, argv);
	else if (!strcmp(cmd, "sched-class"))
		rc = sched_class(argc, argv);
	else if (!strcmp(cmd, "sched-queue"))
		rc = sched_queue(argc, argv);
	else if (!strcmp(cmd, "loadcfg"))
		rc = loadcfg(argc, argv);
	else if (!strcmp(cmd, "loadboot"))
		rc = loadboot(argc, argv);
	else if (!strcmp(cmd, "loadboot-cfg"))
		rc = loadbootcfg(argc, argv);
	else if (!strcmp(cmd, "dumpstate"))
		rc = dumpstate(argc, argv);
	else if (!strcmp(cmd, "policy"))
		rc = load_offload_policy(argc, argv);
	else if (!strcmp(cmd, "hashfilter"))
		rc = filter_cmd(argc, argv, 1);
	else {
		rc = EINVAL;
		warnx("invalid command \"%s\"", cmd);
	}

	return (rc);
}

#define MAX_ARGS 15
static int
run_cmd_loop(void)
{
	int i, rc = 0;
	char buffer[128], *buf;
	const char *args[MAX_ARGS + 1];

	/*
	 * Simple loop: displays a "> " prompt and processes any input as a
	 * cxgbetool command.  You're supposed to enter only the part after
	 * "cxgbetool t4nexX".  Use "quit" or "exit" to exit.
	 */
	for (;;) {
		fprintf(stdout, "> ");
		fflush(stdout);
		buf = fgets(buffer, sizeof(buffer), stdin);
		if (buf == NULL) {
			if (ferror(stdin)) {
				warn("stdin error");
				rc = errno;	/* errno from fgets */
			}
			break;
		}

		i = 0;
		while ((args[i] = strsep(&buf, " \t\n")) != NULL) {
			if (args[i][0] != 0 && ++i == MAX_ARGS)
				break;
		}
		args[i] = 0;

		if (i == 0)
			continue;	/* skip empty line */

		if (!strcmp(args[0], "quit") || !strcmp(args[0], "exit"))
			break;

		rc = run_cmd(i, args);
	}

	/* rc normally comes from the last command (not including quit/exit) */
	return (rc);
}

int
main(int argc, const char *argv[])
{
	int rc = -1;

	progname = argv[0];

	if (argc == 2) {
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			usage(stdout);
			exit(0);
		}
	}

	if (argc < 3) {
		usage(stderr);
		exit(EINVAL);
	}

	nexus = argv[1];
	chip_id = nexus[1] - '0';

	/* progname and nexus */
	argc -= 2;
	argv += 2;

	if (argc == 1 && !strcmp(argv[0], "stdio"))
		rc = run_cmd_loop();
	else
		rc = run_cmd(argc, argv);

	return (rc);
}
