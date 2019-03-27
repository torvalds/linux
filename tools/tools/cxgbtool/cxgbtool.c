/**************************************************************************
SPDX-License-Identifier: BSD-3-Clause

Copyright (c) 2007-2010, Chelsio Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 3. Neither the name of the Chelsio Corporation nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.


***************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <net/if.h>
#include <net/if_types.h>
#include <sys/endian.h>

#define NMTUS 16
#define TCB_SIZE 128
#define TCB_WORDS (TCB_SIZE / 4)
#define PROTO_SRAM_LINES 128
#define PROTO_SRAM_LINE_BITS 132
#define PROTO_SRAM_LINE_NIBBLES (132 / 4)
#define PROTO_SRAM_SIZE (PROTO_SRAM_LINE_NIBBLES * PROTO_SRAM_LINES / 2)
#define PROTO_SRAM_EEPROM_ADDR 4096

#include <cxgb_ioctl.h>
#include <common/cxgb_regs.h>
#include "version.h"

struct reg_info { 
        const char *name; 
        uint16_t addr; 
        uint16_t len; 
}; 
 

#include "reg_defs.c"
#if defined(CONFIG_T3_REGS)
# include "reg_defs_t3.c"
# include "reg_defs_t3b.c"
# include "reg_defs_t3c.c"
#endif

static const char *progname;

static void
usage(FILE *fp)
{
	fprintf(fp, "Usage: %s <interface> [operation]\n", progname);
	fprintf(fp,
	    	"\tclearstats                          clear MAC statistics\n"
		"\tcontext <type> <id>                 show an SGE context\n"
		"\tdesc <qset> <queue> <idx> [<cnt>]   dump SGE descriptors\n"
		"\tfilter <idx> [<param> <val>] ...    set a filter\n"
		"\tfilter <idx> delete|clear           delete a filter\n"
		"\tfilter list                         list all filters\n"
		"\tioqs                                dump uP IOQs\n"
		"\tla                                  dump uP logic analyzer info\n"
		"\tloadboot <boot image>               download boot image\n"
		"\tloadfw <FW image>                   download firmware\n"
		"\tmdio <phy_addr> <mmd_addr>\n"
	        "\t     <reg_addr> [<val>]             read/write MDIO register\n"
		"\tmemdump cm|tx|rx <addr> <len>       dump a mem range\n"
		"\tmeminfo                             show memory info\n"
		"\tmtus [<mtu0>...<mtuN>]              read/write MTU table\n"
		"\tpktsched port <idx> <min> <max>     set TX port scheduler params\n"
		"\tpktsched tunnelq <idx> <max>\n"
		"\t         <binding>                  set TX tunnelq scheduler params\n"
		"\tpktsched tx <idx>\n"
	        "\t         [<param> <val>] ...        set Tx HW scheduler\n"
		"\tpm [<TX page spec> <RX page spec>]  read/write PM config\n"
		"\tproto                               read proto SRAM\n"
		"\tqset                                read qset parameters\n"
		"\tqsets                               read # of qsets\n"
		"\treg <address>[=<val>]               read/write register\n"
		"\tregdump [<module>]                  dump registers\n"
		"\ttcamdump <address> <count>          show TCAM contents\n"
		"\ttcb <index>                         read TCB\n"
		"\ttrace tx|rx|all on|off [not]\n"
	        "\t      [<param> <val>[:<mask>]] ...  write trace parameters\n"
		);
	exit(fp == stderr ? 1 : 0);
}

static int
doit(const char *iff_name, unsigned long cmd, void *data)
{
	static int fd = 0;
	
	if (fd == 0) {
		char buf[64];
		snprintf(buf, 64, "/dev/%s", iff_name);

		if ((fd = open(buf, O_RDWR)) < 0)
			return -1;
	}
	
	return ioctl(fd, cmd, data) < 0 ? -1 : 0;
}

static int
get_int_arg(const char *s, uint32_t *valp)
{
	char *p;

	*valp = strtoul(s, &p, 0);
	if (*p) {
		warnx("bad parameter \"%s\"", s);
		return -1;
	}
	return 0;
}

static uint32_t
read_reg(const char *iff_name, uint32_t addr)
{
	struct ch_reg reg;

	reg.addr = addr;
	
	if (doit(iff_name, CHELSIO_GETREG, &reg) < 0)
		err(1, "register read");
	return reg.val;
}

static void
write_reg(const char *iff_name, uint32_t addr, uint32_t val)
{
	struct ch_reg ch_reg;

	ch_reg.addr = addr;
	ch_reg.val = val;
	
	if (doit(iff_name, CHELSIO_SETREG, &ch_reg) < 0)
		err(1, "register write");
}

static int
register_io(int argc, char *argv[], int start_arg,
		       const char *iff_name)
{
	char *p;
	uint32_t addr, val = 0, w = 0;

	if (argc != start_arg + 1) return -1;

	addr = strtoul(argv[start_arg], &p, 0);
	if (p == argv[start_arg]) return -1;
	if (*p == '=' && p[1]) {
		val = strtoul(p + 1, &p, 0);
		w = 1;
	}
	if (*p) {
		warnx("bad parameter \"%s\"", argv[start_arg]);
		return -1;
	}

	if (w)
		write_reg(iff_name, addr, val);
	else {
		val = read_reg(iff_name, addr);
		printf("%#x [%u]\n", val, val);
	}
	return 0;
}

static int
mdio_io(int argc, char *argv[], int start_arg, const char *iff_name) 
{ 
        struct ch_mii_data p;
        unsigned int cmd, phy_addr, reg, mmd, val; 
 
        if (argc == start_arg + 3) 
                cmd = CHELSIO_GET_MIIREG; 
        else if (argc == start_arg + 4) 
                cmd = CHELSIO_SET_MIIREG; 
        else 
                return -1; 
 
        if (get_int_arg(argv[start_arg], &phy_addr) || 
            get_int_arg(argv[start_arg + 1], &mmd) || 
            get_int_arg(argv[start_arg + 2], &reg) || 
            (cmd == CHELSIO_SET_MIIREG && get_int_arg(argv[start_arg + 3], &val))) 
                return -1; 

        p.phy_id  = phy_addr | (mmd << 8); 
        p.reg_num = reg; 
        p.val_in  = val; 
 
        if (doit(iff_name, cmd, &p) < 0) 
                err(1, "MDIO %s", cmd == CHELSIO_GET_MIIREG ? "read" : "write");
        if (cmd == CHELSIO_GET_MIIREG) 
                printf("%#x [%u]\n", p.val_out, p.val_out); 
        return 0; 
} 

static inline
uint32_t xtract(uint32_t val, int shift, int len)
{
	return (val >> shift) & ((1 << len) - 1);
}

static int
dump_block_regs(const struct reg_info *reg_array, uint32_t *regs)
{
	uint32_t reg_val = 0; // silence compiler warning

	for ( ; reg_array->name; ++reg_array)
		if (!reg_array->len) {
			reg_val = regs[reg_array->addr / 4];
			printf("[%#5x] %-40s %#-10x [%u]\n", reg_array->addr,
			       reg_array->name, reg_val, reg_val);
		} else {
			uint32_t v = xtract(reg_val, reg_array->addr,
					    reg_array->len);

			printf("        %-40s %#-10x [%u]\n", reg_array->name,
			       v, v);
		}
	return 1;
}

static int
dump_regs_t2(int argc, char *argv[], int start_arg, uint32_t *regs)
{
	int match = 0;
	char *block_name = NULL;

	if (argc == start_arg + 1)
		block_name = argv[start_arg];
	else if (argc != start_arg)
		return -1;

	if (!block_name || !strcmp(block_name, "sge"))
		match += dump_block_regs(sge_regs, regs);
	if (!block_name || !strcmp(block_name, "mc3"))
		match += dump_block_regs(mc3_regs, regs);
	if (!block_name || !strcmp(block_name, "mc4"))
		match += dump_block_regs(mc4_regs, regs);
	if (!block_name || !strcmp(block_name, "tpi"))
		match += dump_block_regs(tpi_regs, regs);
	if (!block_name || !strcmp(block_name, "tp"))
		match += dump_block_regs(tp_regs, regs);
	if (!block_name || !strcmp(block_name, "rat"))
		match += dump_block_regs(rat_regs, regs);
	if (!block_name || !strcmp(block_name, "cspi"))
		match += dump_block_regs(cspi_regs, regs);
	if (!block_name || !strcmp(block_name, "espi"))
		match += dump_block_regs(espi_regs, regs);
	if (!block_name || !strcmp(block_name, "ulp"))
		match += dump_block_regs(ulp_regs, regs);
	if (!block_name || !strcmp(block_name, "pl"))
		match += dump_block_regs(pl_regs, regs);
	if (!block_name || !strcmp(block_name, "mc5"))
		match += dump_block_regs(mc5_regs, regs);
	if (!match)
		errx(1, "unknown block \"%s\"", block_name);
	return 0;
}

#if defined(CONFIG_T3_REGS)
static int
dump_regs_t3(int argc, char *argv[], int start_arg, uint32_t *regs, int is_pcie)
{
	int match = 0;
	char *block_name = NULL;

	if (argc == start_arg + 1)
		block_name = argv[start_arg];
	else if (argc != start_arg)
		return -1;

	if (!block_name || !strcmp(block_name, "sge"))
		match += dump_block_regs(sge3_regs, regs);
	if (!block_name || !strcmp(block_name, "pci"))
		match += dump_block_regs(is_pcie ? pcie0_regs : pcix1_regs,
					 regs);
	if (!block_name || !strcmp(block_name, "t3dbg"))
		match += dump_block_regs(t3dbg_regs, regs);
	if (!block_name || !strcmp(block_name, "pmrx"))
		match += dump_block_regs(mc7_pmrx_regs, regs);
	if (!block_name || !strcmp(block_name, "pmtx"))
		match += dump_block_regs(mc7_pmtx_regs, regs);
	if (!block_name || !strcmp(block_name, "cm"))
		match += dump_block_regs(mc7_cm_regs, regs);
	if (!block_name || !strcmp(block_name, "cim"))
		match += dump_block_regs(cim_regs, regs);
	if (!block_name || !strcmp(block_name, "tp"))
		match += dump_block_regs(tp1_regs, regs);
	if (!block_name || !strcmp(block_name, "ulp_rx"))
		match += dump_block_regs(ulp2_rx_regs, regs);
	if (!block_name || !strcmp(block_name, "ulp_tx"))
		match += dump_block_regs(ulp2_tx_regs, regs);
	if (!block_name || !strcmp(block_name, "pmrx"))
		match += dump_block_regs(pm1_rx_regs, regs);
	if (!block_name || !strcmp(block_name, "pmtx"))
		match += dump_block_regs(pm1_tx_regs, regs);
	if (!block_name || !strcmp(block_name, "mps"))
		match += dump_block_regs(mps0_regs, regs);
	if (!block_name || !strcmp(block_name, "cplsw"))
		match += dump_block_regs(cpl_switch_regs, regs);
	if (!block_name || !strcmp(block_name, "smb"))
		match += dump_block_regs(smb0_regs, regs);
	if (!block_name || !strcmp(block_name, "i2c"))
		match += dump_block_regs(i2cm0_regs, regs);
	if (!block_name || !strcmp(block_name, "mi1"))
		match += dump_block_regs(mi1_regs, regs);
	if (!block_name || !strcmp(block_name, "sf"))
		match += dump_block_regs(sf1_regs, regs);
	if (!block_name || !strcmp(block_name, "pl"))
		match += dump_block_regs(pl3_regs, regs);
	if (!block_name || !strcmp(block_name, "mc5"))
		match += dump_block_regs(mc5a_regs, regs);
	if (!block_name || !strcmp(block_name, "xgmac0"))
		match += dump_block_regs(xgmac0_0_regs, regs);
	if (!block_name || !strcmp(block_name, "xgmac1"))
		match += dump_block_regs(xgmac0_1_regs, regs);
	if (!match)
		errx(1, "unknown block \"%s\"", block_name);
	return 0;
}

static int
dump_regs_t3b(int argc, char *argv[], int start_arg, uint32_t *regs,
    int is_pcie)
{
	int match = 0;
	char *block_name = NULL;

	if (argc == start_arg + 1)
		block_name = argv[start_arg];
	else if (argc != start_arg)
		return -1;

	if (!block_name || !strcmp(block_name, "sge"))
		match += dump_block_regs(t3b_sge3_regs, regs);
	if (!block_name || !strcmp(block_name, "pci"))
		match += dump_block_regs(is_pcie ? t3b_pcie0_regs :
						   t3b_pcix1_regs, regs);
	if (!block_name || !strcmp(block_name, "t3dbg"))
		match += dump_block_regs(t3b_t3dbg_regs, regs);
	if (!block_name || !strcmp(block_name, "pmrx"))
		match += dump_block_regs(t3b_mc7_pmrx_regs, regs);
	if (!block_name || !strcmp(block_name, "pmtx"))
		match += dump_block_regs(t3b_mc7_pmtx_regs, regs);
	if (!block_name || !strcmp(block_name, "cm"))
		match += dump_block_regs(t3b_mc7_cm_regs, regs);
	if (!block_name || !strcmp(block_name, "cim"))
		match += dump_block_regs(t3b_cim_regs, regs);
	if (!block_name || !strcmp(block_name, "tp"))
		match += dump_block_regs(t3b_tp1_regs, regs);
	if (!block_name || !strcmp(block_name, "ulp_rx"))
		match += dump_block_regs(t3b_ulp2_rx_regs, regs);
	if (!block_name || !strcmp(block_name, "ulp_tx"))
		match += dump_block_regs(t3b_ulp2_tx_regs, regs);
	if (!block_name || !strcmp(block_name, "pmrx"))
		match += dump_block_regs(t3b_pm1_rx_regs, regs);
	if (!block_name || !strcmp(block_name, "pmtx"))
		match += dump_block_regs(t3b_pm1_tx_regs, regs);
	if (!block_name || !strcmp(block_name, "mps"))
		match += dump_block_regs(t3b_mps0_regs, regs);
	if (!block_name || !strcmp(block_name, "cplsw"))
		match += dump_block_regs(t3b_cpl_switch_regs, regs);
	if (!block_name || !strcmp(block_name, "smb"))
		match += dump_block_regs(t3b_smb0_regs, regs);
	if (!block_name || !strcmp(block_name, "i2c"))
		match += dump_block_regs(t3b_i2cm0_regs, regs);
	if (!block_name || !strcmp(block_name, "mi1"))
		match += dump_block_regs(t3b_mi1_regs, regs);
	if (!block_name || !strcmp(block_name, "sf"))
		match += dump_block_regs(t3b_sf1_regs, regs);
	if (!block_name || !strcmp(block_name, "pl"))
		match += dump_block_regs(t3b_pl3_regs, regs);
	if (!block_name || !strcmp(block_name, "mc5"))
		match += dump_block_regs(t3b_mc5a_regs, regs);
	if (!block_name || !strcmp(block_name, "xgmac0"))
		match += dump_block_regs(t3b_xgmac0_0_regs, regs);
	if (!block_name || !strcmp(block_name, "xgmac1"))
		match += dump_block_regs(t3b_xgmac0_1_regs, regs);
	if (!match)
		errx(1, "unknown block \"%s\"", block_name);
	return 0;
}

static int
dump_regs_t3c(int argc, char *argv[], int start_arg, uint32_t *regs,
    int is_pcie)
{
	int match = 0;
	char *block_name = NULL;

	if (argc == start_arg + 1)
		block_name = argv[start_arg];
	else if (argc != start_arg)
		return -1;

	if (!block_name || !strcmp(block_name, "sge"))
		match += dump_block_regs(t3c_sge3_regs, regs);
	if (!block_name || !strcmp(block_name, "pci"))
		match += dump_block_regs(is_pcie ? t3c_pcie0_regs :
						   t3c_pcix1_regs, regs);
	if (!block_name || !strcmp(block_name, "t3dbg"))
		match += dump_block_regs(t3c_t3dbg_regs, regs);
	if (!block_name || !strcmp(block_name, "pmrx"))
		match += dump_block_regs(t3c_mc7_pmrx_regs, regs);
	if (!block_name || !strcmp(block_name, "pmtx"))
		match += dump_block_regs(t3c_mc7_pmtx_regs, regs);
	if (!block_name || !strcmp(block_name, "cm"))
		match += dump_block_regs(t3c_mc7_cm_regs, regs);
	if (!block_name || !strcmp(block_name, "cim"))
		match += dump_block_regs(t3c_cim_regs, regs);
	if (!block_name || !strcmp(block_name, "tp"))
		match += dump_block_regs(t3c_tp1_regs, regs);
	if (!block_name || !strcmp(block_name, "ulp_rx"))
		match += dump_block_regs(t3c_ulp2_rx_regs, regs);
	if (!block_name || !strcmp(block_name, "ulp_tx"))
		match += dump_block_regs(t3c_ulp2_tx_regs, regs);
	if (!block_name || !strcmp(block_name, "pmrx"))
		match += dump_block_regs(t3c_pm1_rx_regs, regs);
	if (!block_name || !strcmp(block_name, "pmtx"))
		match += dump_block_regs(t3c_pm1_tx_regs, regs);
	if (!block_name || !strcmp(block_name, "mps"))
		match += dump_block_regs(t3c_mps0_regs, regs);
	if (!block_name || !strcmp(block_name, "cplsw"))
		match += dump_block_regs(t3c_cpl_switch_regs, regs);
	if (!block_name || !strcmp(block_name, "smb"))
		match += dump_block_regs(t3c_smb0_regs, regs);
	if (!block_name || !strcmp(block_name, "i2c"))
		match += dump_block_regs(t3c_i2cm0_regs, regs);
	if (!block_name || !strcmp(block_name, "mi1"))
		match += dump_block_regs(t3c_mi1_regs, regs);
	if (!block_name || !strcmp(block_name, "sf"))
		match += dump_block_regs(t3c_sf1_regs, regs);
	if (!block_name || !strcmp(block_name, "pl"))
		match += dump_block_regs(t3c_pl3_regs, regs);
	if (!block_name || !strcmp(block_name, "mc5"))
		match += dump_block_regs(t3c_mc5a_regs, regs);
	if (!block_name || !strcmp(block_name, "xgmac0"))
		match += dump_block_regs(t3c_xgmac0_0_regs, regs);
	if (!block_name || !strcmp(block_name, "xgmac1"))
		match += dump_block_regs(t3c_xgmac0_1_regs, regs);
	if (!match)
		errx(1, "unknown block \"%s\"", block_name);
	return 0;
}
#endif

static int
dump_regs(int argc, char *argv[], int start_arg, const char *iff_name)
{
	int vers, revision, is_pcie;
	struct ch_ifconf_regs regs;

	regs.len = REGDUMP_SIZE;

	/* XXX: This is never freed.  Looks like we don't care. */
	if ((regs.data = malloc(regs.len)) == NULL)
		err(1, "can't malloc");

	if (doit(iff_name, CHELSIO_IFCONF_GETREGS, &regs))
		err(1, "can't read registers");

	vers = regs.version & 0x3ff;
	revision = (regs.version >> 10) & 0x3f;
	is_pcie = (regs.version & 0x80000000) != 0;

	if (vers <= 2)
		return dump_regs_t2(argc, argv, start_arg, (uint32_t *)regs.data);
#if defined(CONFIG_T3_REGS)
	if (vers == 3) {
		if (revision == 0)
			return dump_regs_t3(argc, argv, start_arg,
					    (uint32_t *)regs.data, is_pcie);
		if (revision == 2 || revision == 3)
			return dump_regs_t3b(argc, argv, start_arg,
					     (uint32_t *)regs.data, is_pcie);
		if (revision == 4)
			return dump_regs_t3c(argc, argv, start_arg,
			    		     (uint32_t *)regs.data, is_pcie);
	}
#endif
	errx(1, "unknown card type %d.%d", vers, revision);
	return 0;
}

static int
t3_meminfo(const uint32_t *regs)
{
	enum {
		SG_EGR_CNTX_BADDR       = 0x58,
		SG_CQ_CONTEXT_BADDR     = 0x6c,
		CIM_SDRAM_BASE_ADDR     = 0x28c,
		CIM_SDRAM_ADDR_SIZE     = 0x290,
		TP_CMM_MM_BASE          = 0x314,
		TP_CMM_TIMER_BASE       = 0x318,
		TP_CMM_MM_RX_FLST_BASE  = 0x460,
		TP_CMM_MM_TX_FLST_BASE  = 0x464,
		TP_CMM_MM_PS_FLST_BASE  = 0x468,
		ULPRX_ISCSI_LLIMIT      = 0x50c,
		ULPRX_ISCSI_ULIMIT      = 0x510,
		ULPRX_TDDP_LLIMIT       = 0x51c,
		ULPRX_TDDP_ULIMIT       = 0x520,
		ULPRX_STAG_LLIMIT       = 0x52c,
		ULPRX_STAG_ULIMIT       = 0x530,
		ULPRX_RQ_LLIMIT         = 0x534,
		ULPRX_RQ_ULIMIT         = 0x538,
		ULPRX_PBL_LLIMIT        = 0x53c,
		ULPRX_PBL_ULIMIT        = 0x540,
	};

	unsigned int egr_cntxt = regs[SG_EGR_CNTX_BADDR / 4],
		     cq_cntxt = regs[SG_CQ_CONTEXT_BADDR / 4],
		     timers = regs[TP_CMM_TIMER_BASE / 4] & 0xfffffff,
		     pstructs = regs[TP_CMM_MM_BASE / 4],
		     pstruct_fl = regs[TP_CMM_MM_PS_FLST_BASE / 4],
		     rx_fl = regs[TP_CMM_MM_RX_FLST_BASE / 4],
		     tx_fl = regs[TP_CMM_MM_TX_FLST_BASE / 4],
		     cim_base = regs[CIM_SDRAM_BASE_ADDR / 4],
		     cim_size = regs[CIM_SDRAM_ADDR_SIZE / 4];
	unsigned int iscsi_ll = regs[ULPRX_ISCSI_LLIMIT / 4],
		     iscsi_ul = regs[ULPRX_ISCSI_ULIMIT / 4],
		     tddp_ll = regs[ULPRX_TDDP_LLIMIT / 4],
		     tddp_ul = regs[ULPRX_TDDP_ULIMIT / 4],
		     stag_ll = regs[ULPRX_STAG_LLIMIT / 4],
		     stag_ul = regs[ULPRX_STAG_ULIMIT / 4],
		     rq_ll = regs[ULPRX_RQ_LLIMIT / 4],
		     rq_ul = regs[ULPRX_RQ_ULIMIT / 4],
		     pbl_ll = regs[ULPRX_PBL_LLIMIT / 4],
		     pbl_ul = regs[ULPRX_PBL_ULIMIT / 4];

	printf("CM memory map:\n");
	printf("  TCB region:      0x%08x - 0x%08x [%u]\n", 0, egr_cntxt - 1,
	       egr_cntxt);
	printf("  Egress contexts: 0x%08x - 0x%08x [%u]\n", egr_cntxt,
	       cq_cntxt - 1, cq_cntxt - egr_cntxt);
	printf("  CQ contexts:     0x%08x - 0x%08x [%u]\n", cq_cntxt,
	       timers - 1, timers - cq_cntxt);
	printf("  Timers:          0x%08x - 0x%08x [%u]\n", timers,
	       pstructs - 1, pstructs - timers);
	printf("  Pstructs:        0x%08x - 0x%08x [%u]\n", pstructs,
	       pstruct_fl - 1, pstruct_fl - pstructs);
	printf("  Pstruct FL:      0x%08x - 0x%08x [%u]\n", pstruct_fl,
	       rx_fl - 1, rx_fl - pstruct_fl);
	printf("  Rx FL:           0x%08x - 0x%08x [%u]\n", rx_fl, tx_fl - 1,
	       tx_fl - rx_fl);
	printf("  Tx FL:           0x%08x - 0x%08x [%u]\n", tx_fl, cim_base - 1,
	       cim_base - tx_fl);
	printf("  uP RAM:          0x%08x - 0x%08x [%u]\n", cim_base,
	       cim_base + cim_size - 1, cim_size);

	printf("\nPMRX memory map:\n");
	printf("  iSCSI region:    0x%08x - 0x%08x [%u]\n", iscsi_ll, iscsi_ul,
	       iscsi_ul - iscsi_ll + 1);
	printf("  TCP DDP region:  0x%08x - 0x%08x [%u]\n", tddp_ll, tddp_ul,
	       tddp_ul - tddp_ll + 1);
	printf("  TPT region:      0x%08x - 0x%08x [%u]\n", stag_ll, stag_ul,
	       stag_ul - stag_ll + 1);
	printf("  RQ region:       0x%08x - 0x%08x [%u]\n", rq_ll, rq_ul,
	       rq_ul - rq_ll + 1);
	printf("  PBL region:      0x%08x - 0x%08x [%u]\n", pbl_ll, pbl_ul,
	       pbl_ul - pbl_ll + 1);
	return 0;
}

static int
meminfo(int argc, char *argv[], int start_arg, const char *iff_name)
{
	int vers;
	struct ch_ifconf_regs regs;

	(void) argc;
	(void) argv;
	(void) start_arg;

	regs.len = REGDUMP_SIZE;
	if ((regs.data = malloc(regs.len)) == NULL)
		err(1, "can't malloc");
	
	if (doit(iff_name, CHELSIO_IFCONF_GETREGS, &regs))
		err(1, "can't read registers");

	vers = regs.version & 0x3ff;
	if (vers == 3)
		return t3_meminfo((uint32_t *)regs.data);

	errx(1, "unknown card type %d", vers);
	return 0;
}

static int
mtu_tab_op(int argc, char *argv[], int start_arg, const char *iff_name)
{
	struct ch_mtus m;
	unsigned int i;

	if (argc == start_arg) {
		if (doit(iff_name, CHELSIO_GETMTUTAB, &m) < 0)
			err(1, "get MTU table");
		for (i = 0; i < m.nmtus; ++i)
			printf("%u ", m.mtus[i]);
		printf("\n");
	} else if (argc <= start_arg + NMTUS) {
		m.nmtus = argc - start_arg;

		for (i = 0; i < m.nmtus; ++i) {
			char *p;
			unsigned long mt = strtoul(argv[start_arg + i], &p, 0);

			if (*p || mt > 9600) {
				warnx("bad parameter \"%s\"",
				      argv[start_arg + i]);
				return -1;
			}
			if (i && mt < m.mtus[i - 1])
				errx(1, "MTUs must be in ascending order");
			m.mtus[i] = mt;
		}
		if (doit(iff_name, CHELSIO_SETMTUTAB, &m) < 0)
			err(1, "set MTU table");
	} else
		return -1;

	return 0;
}

#ifdef CHELSIO_INTERNAL
static void
show_egress_cntxt(uint32_t data[])
{
	printf("credits:      %u\n", data[0] & 0x7fff);
	printf("GTS:          %u\n", (data[0] >> 15) & 1);
	printf("index:        %u\n", data[0] >> 16);
	printf("queue size:   %u\n", data[1] & 0xffff);
	printf("base address: 0x%" PRIx64 "\n",
	       ((data[1] >> 16) | ((uint64_t)data[2] << 16) |
	       (((uint64_t)data[3] & 0xf) << 48)) << 12);
	printf("rsp queue #:  %u\n", (data[3] >> 4) & 7);
	printf("cmd queue #:  %u\n", (data[3] >> 7) & 1);
	printf("TUN:          %u\n", (data[3] >> 8) & 1);
	printf("TOE:          %u\n", (data[3] >> 9) & 1);
	printf("generation:   %u\n", (data[3] >> 10) & 1);
	printf("uP token:     %u\n", (data[3] >> 11) & 0xfffff);
	printf("valid:        %u\n", (data[3] >> 31) & 1);
}

static void
show_fl_cntxt(uint32_t data[])
{
	printf("base address: 0x%" PRIx64 "\n",
	       ((uint64_t)data[0] | ((uint64_t)data[1] & 0xfffff) << 32) << 12);
	printf("index:        %u\n", (data[1] >> 20) | ((data[2] & 0xf) << 12));
	printf("queue size:   %u\n", (data[2] >> 4) & 0xffff);
	printf("generation:   %u\n", (data[2] >> 20) & 1);
	printf("entry size:   %u\n",
	       (data[2] >> 21) | (data[3] & 0x1fffff) << 11);
	printf("congest thr:  %u\n", (data[3] >> 21) & 0x3ff);
	printf("GTS:          %u\n", (data[3] >> 31) & 1);
}

static void
show_response_cntxt(uint32_t data[])
{
	printf("index:        %u\n", data[0] & 0xffff);
	printf("size:         %u\n", data[0] >> 16);
	printf("base address: 0x%" PRIx64 "\n",
	       ((uint64_t)data[1] | ((uint64_t)data[2] & 0xfffff) << 32) << 12);
	printf("MSI-X/RspQ:   %u\n", (data[2] >> 20) & 0x3f);
	printf("intr enable:  %u\n", (data[2] >> 26) & 1);
	printf("intr armed:   %u\n", (data[2] >> 27) & 1);
	printf("generation:   %u\n", (data[2] >> 28) & 1);
	printf("CQ mode:      %u\n", (data[2] >> 31) & 1);
	printf("FL threshold: %u\n", data[3]);
}

static void
show_cq_cntxt(uint32_t data[])
{
	printf("index:            %u\n", data[0] & 0xffff);
	printf("size:             %u\n", data[0] >> 16);
	printf("base address:     0x%" PRIx64 "\n",
	       ((uint64_t)data[1] | ((uint64_t)data[2] & 0xfffff) << 32) << 12);
	printf("rsp queue #:      %u\n", (data[2] >> 20) & 0x3f);
	printf("AN:               %u\n", (data[2] >> 26) & 1);
	printf("armed:            %u\n", (data[2] >> 27) & 1);
	printf("ANS:              %u\n", (data[2] >> 28) & 1);
	printf("generation:       %u\n", (data[2] >> 29) & 1);
	printf("overflow mode:    %u\n", (data[2] >> 31) & 1);
	printf("credits:          %u\n", data[3] & 0xffff);
	printf("credit threshold: %u\n", data[3] >> 16);
}

static int
get_sge_context(int argc, char *argv[], int start_arg, const char *iff_name)
{
	struct ch_cntxt ctx;

	if (argc != start_arg + 2) return -1;

	if (!strcmp(argv[start_arg], "egress"))
		ctx.cntxt_type = CNTXT_TYPE_EGRESS;
	else if (!strcmp(argv[start_arg], "fl"))
		ctx.cntxt_type = CNTXT_TYPE_FL;
	else if (!strcmp(argv[start_arg], "response"))
		ctx.cntxt_type = CNTXT_TYPE_RSP;
	else if (!strcmp(argv[start_arg], "cq"))
		ctx.cntxt_type = CNTXT_TYPE_CQ;
	else {
		warnx("unknown context type \"%s\"; known types are egress, "
		      "fl, cq, and response", argv[start_arg]);
		return -1;
	}

	if (get_int_arg(argv[start_arg + 1], &ctx.cntxt_id))
		return -1;

	if (doit(iff_name, CHELSIO_GET_SGE_CONTEXT, &ctx) < 0)
		err(1, "get SGE context");

	if (!strcmp(argv[start_arg], "egress"))
		show_egress_cntxt(ctx.data);
	else if (!strcmp(argv[start_arg], "fl"))
		show_fl_cntxt(ctx.data);
	else if (!strcmp(argv[start_arg], "response"))
		show_response_cntxt(ctx.data);
	else if (!strcmp(argv[start_arg], "cq"))
		show_cq_cntxt(ctx.data);
	return 0;
}

#define ntohll(x) be64toh((x))

static int
get_sge_desc(int argc, char *argv[], int start_arg, const char *iff_name)
{
	uint64_t *p, wr_hdr;
	unsigned int n = 1, qset, qnum;
	struct ch_desc desc;

	if (argc != start_arg + 3 && argc != start_arg + 4)
		return -1;

	if (get_int_arg(argv[start_arg], &qset) ||
	    get_int_arg(argv[start_arg + 1], &qnum) ||
	    get_int_arg(argv[start_arg + 2], &desc.idx))
		return -1;

	if (argc == start_arg + 4 && get_int_arg(argv[start_arg + 3], &n))
		return -1;

	if (qnum > 5)
		errx(1, "invalid queue number %d, range is 0..5", qnum);

	desc.queue_num = qset * 6 + qnum;

	for (; n--; desc.idx++) {
		if (doit(iff_name, CHELSIO_GET_SGE_DESC, &desc) < 0)
			err(1, "get SGE descriptor");

		p = (uint64_t *)desc.data;
		wr_hdr = ntohll(*p);
		printf("Descriptor %u: cmd %u, TID %u, %s%s%s%s%u flits\n",
		       desc.idx, (unsigned int)(wr_hdr >> 56),
		       ((unsigned int)wr_hdr >> 8) & 0xfffff,
		       ((wr_hdr >> 55) & 1) ? "SOP, " : "",
		       ((wr_hdr >> 54) & 1) ? "EOP, " : "",
		       ((wr_hdr >> 53) & 1) ? "COMPL, " : "",
		       ((wr_hdr >> 52) & 1) ? "SGL, " : "",
		       (unsigned int)wr_hdr & 0xff);

		for (; desc.size; p++, desc.size -= sizeof(uint64_t))
			printf("%016" PRIx64 "%c", ntohll(*p),
			    desc.size % 32 == 8 ? '\n' : ' ');
	}
	return 0;
}
#endif

static int
get_tcb2(int argc, char *argv[], int start_arg, const char *iff_name)
{
	uint64_t *d;
	unsigned int i;
	unsigned int tcb_idx;
	struct ch_mem_range mr;

	if (argc != start_arg + 1)
		return -1;

	if (get_int_arg(argv[start_arg], &tcb_idx))
		return -1;

	mr.buf = calloc(1, TCB_SIZE);
	if (!mr.buf)
		err(1, "get TCB");

	mr.mem_id = MEM_CM;
	mr.addr   = tcb_idx * TCB_SIZE;
	mr.len    = TCB_SIZE;

	if (doit(iff_name, CHELSIO_GET_MEM, &mr) < 0)
		err(1, "get TCB");

	for (d = (uint64_t *)mr.buf, i = 0; i < TCB_SIZE / 32; i++) {
		printf("%2u:", i);
		printf(" %08x %08x %08x %08x", (uint32_t)d[1],
		       (uint32_t)(d[1] >> 32), (uint32_t)d[0],
		       (uint32_t)(d[0] >> 32));
		d += 2;
		printf(" %08x %08x %08x %08x\n", (uint32_t)d[1],
		       (uint32_t)(d[1] >> 32), (uint32_t)d[0],
		       (uint32_t)(d[0] >> 32));
		d += 2;
	}
	free(mr.buf);
	return 0;
}

static int
get_pm_page_spec(const char *s, unsigned int *page_size,
    unsigned int *num_pages)
{
	char *p;
	unsigned long val;

	val = strtoul(s, &p, 0);
	if (p == s) return -1;
	if (*p == 'x' && p[1]) {
		*num_pages = val;
		*page_size = strtoul(p + 1, &p, 0);
	} else {
		*num_pages = -1;
		*page_size = val;
	}
	*page_size <<= 10;     // KB -> bytes
	return *p;
}

static int
conf_pm(int argc, char *argv[], int start_arg, const char *iff_name)
{
	struct ch_pm pm;

	if (argc == start_arg) {
		if (doit(iff_name, CHELSIO_GET_PM, &pm) < 0)
			err(1, "read pm config");
		printf("%ux%uKB TX pages, %ux%uKB RX pages, %uKB total memory\n",
		       pm.tx_num_pg, pm.tx_pg_sz >> 10, pm.rx_num_pg,
		       pm.rx_pg_sz >> 10, pm.pm_total >> 10);
		return 0;
	}

	if (argc != start_arg + 2) return -1;

	if (get_pm_page_spec(argv[start_arg], &pm.tx_pg_sz, &pm.tx_num_pg)) {
		warnx("bad parameter \"%s\"", argv[start_arg]);
		return -1;
	}
	if (get_pm_page_spec(argv[start_arg + 1], &pm.rx_pg_sz,
			     &pm.rx_num_pg)) {
		warnx("bad parameter \"%s\"", argv[start_arg + 1]);
		return -1;
	}
	if (doit(iff_name, CHELSIO_SET_PM, &pm) < 0)
		err(1, "pm config");
	return 0;
}

#ifdef	CHELSIO_INTERNAL
static int
dump_tcam(int argc, char *argv[], int start_arg, const char *iff_name)
{
	unsigned int nwords;
	struct ch_tcam_word op;

	if (argc != start_arg + 2) return -1;

	if (get_int_arg(argv[start_arg], &op.addr) ||
	    get_int_arg(argv[start_arg + 1], &nwords))
		return -1;

	while (nwords--) {
		if (doit(iff_name, CHELSIO_READ_TCAM_WORD, &op) < 0)
			err(1, "tcam dump");

		printf("0x%08x: 0x%02x 0x%08x 0x%08x\n", op.addr,
		       op.buf[0] & 0xff, op.buf[1], op.buf[2]);
		op.addr++;
	}
	return 0;
}

static void
hexdump_8b(unsigned int start, uint64_t *data, unsigned int len)
{
	int i;

	while (len) {
		printf("0x%08x:", start);
		for (i = 0; i < 4 && len; ++i, --len)
			printf(" %016llx", (unsigned long long)*data++);
		printf("\n");
		start += 32;
	}
}

static int
dump_mc7(int argc, char *argv[], int start_arg, const char *iff_name)
{
	struct ch_mem_range mem;
	unsigned int mem_id, addr, len;

	if (argc != start_arg + 3) return -1;

	if (!strcmp(argv[start_arg], "cm"))
		mem_id = MEM_CM;
	else if (!strcmp(argv[start_arg], "rx"))
		mem_id = MEM_PMRX;
	else if (!strcmp(argv[start_arg], "tx"))
		mem_id = MEM_PMTX;
	else
		errx(1, "unknown memory \"%s\"; must be one of \"cm\", \"tx\","
			" or \"rx\"", argv[start_arg]);

	if (get_int_arg(argv[start_arg + 1], &addr) ||
	    get_int_arg(argv[start_arg + 2], &len))
		return -1;

	mem.buf = malloc(len);
	if (!mem.buf)
		err(1, "memory dump");

	mem.mem_id = mem_id;
	mem.addr   = addr;
	mem.len    = len;

	if (doit(iff_name, CHELSIO_GET_MEM, &mem) < 0)
		err(1, "memory dump");

	hexdump_8b(mem.addr, (uint64_t *)mem.buf, mem.len >> 3);
	free(mem.buf);
	return 0;
}
#endif

/* Max FW size is 64K including version, +4 bytes for the checksum. */
#define MAX_FW_IMAGE_SIZE (64 * 1024)

static int
load_fw(int argc, char *argv[], int start_arg, const char *iff_name)
{
	int fd, len;
	struct ch_mem_range op;
	const char *fname = argv[start_arg];

	if (argc != start_arg + 1) return -1;

	fd = open(fname, O_RDONLY);
	if (fd < 0)
		err(1, "load firmware");

	bzero(&op, sizeof(op));
	op.buf = malloc(MAX_FW_IMAGE_SIZE + 1);
	if (!op.buf)
		err(1, "load firmware");

	len = read(fd, op.buf, MAX_FW_IMAGE_SIZE + 1);
	if (len < 0)
		err(1, "load firmware");
 	if (len > MAX_FW_IMAGE_SIZE)
		errx(1, "FW image too large");

	op.len = len;
	if (doit(iff_name, CHELSIO_LOAD_FW, &op) < 0)
		err(1, "load firmware");

	close(fd);
	return 0;
}

/* Max BOOT size is 255*512 bytes including the BIOS boot ROM basic header */
#define MAX_BOOT_IMAGE_SIZE (0xff * 512)

static int
load_boot(int argc, char *argv[], int start_arg, const char *iff_name)
{
	int fd, len;
	struct ch_mem_range op;
	const char *fname = argv[start_arg];

	if (argc != start_arg + 1) return -1;

	fd = open(fname, O_RDONLY);
	if (fd < 0)
		err(1, "load boot image");

	op.buf = malloc(MAX_BOOT_IMAGE_SIZE + 1);
	if (!op.buf)
		err(1, "load boot image");

	len = read(fd, op.buf, MAX_BOOT_IMAGE_SIZE + 1);
	if (len < 0)
		err(1, "load boot image");
 	if (len > MAX_BOOT_IMAGE_SIZE)
		errx(1, "boot image too large");

	op.len = len;

	if (doit(iff_name, CHELSIO_LOAD_BOOT, &op) < 0)
		err(1, "load boot image");

	close(fd);
	return 0;
}

static int
dump_proto_sram(const char *iff_name)
{
	int i, j;
	uint8_t buf[PROTO_SRAM_SIZE];
	struct ch_eeprom ee;
	uint8_t *p = buf;

	bzero(buf, sizeof(buf));
	ee.offset = PROTO_SRAM_EEPROM_ADDR;
	ee.data = p;
	ee.len = sizeof(buf);
	if (doit(iff_name, CHELSIO_GET_EEPROM, &ee))
		err(1, "show protocol sram");

	for (i = 0; i < PROTO_SRAM_LINES; i++) {
		for (j = PROTO_SRAM_LINE_NIBBLES - 1; j >= 0; j--) {
			int nibble_idx = i * PROTO_SRAM_LINE_NIBBLES + j;
			uint8_t nibble = p[nibble_idx / 2];

			if (nibble_idx & 1)
				nibble >>= 4;
			else
				nibble &= 0xf;
			printf("%x", nibble);
		}
		putchar('\n');
	}
	return 0;
}

static int
proto_sram_op(int argc, char *argv[], int start_arg,
			 const char *iff_name)
{
	(void) argv;
	(void) start_arg;

	if (argc == start_arg)
		return dump_proto_sram(iff_name);
	return -1;
}

static int
dump_qset_params(const char *iff_name)
{
	struct ch_qset_params qp;

	qp.qset_idx = 0;

	while (doit(iff_name, CHELSIO_GET_QSET_PARAMS, &qp) == 0) {
		if (!qp.qset_idx)
			printf("Qset   TxQ0   TxQ1   TxQ2   RspQ   RxQ0   RxQ1"
			       "  Cong  Lat   IRQ\n");
		printf("%4u %6u %6u %6u %6u %6u %6u %5u %4u %5d\n",
		       qp.qnum,
		       qp.txq_size[0], qp.txq_size[1], qp.txq_size[2],
		       qp.rspq_size, qp.fl_size[0], qp.fl_size[1],
		       qp.cong_thres, qp.intr_lat, qp.vector);
		qp.qset_idx++;
	}
	if (!qp.qset_idx || (errno && errno != EINVAL))
		err(1, "get qset parameters");
	return 0;
}

static int
qset_config(int argc, char *argv[], int start_arg, const char *iff_name)
{
	(void) argv;

	if (argc == start_arg)
		return dump_qset_params(iff_name);

	return -1;
}

static int
qset_num_config(int argc, char *argv[], int start_arg, const char *iff_name)
{
	struct ch_reg reg;

	(void) argv;

	if (argc == start_arg) {
		if (doit(iff_name, CHELSIO_GET_QSET_NUM, &reg) < 0)
			err(1, "get qsets");
		printf("%u\n", reg.val);
		return 0;
	}

	return -1;
}

/*
 * Parse a string containing an IP address with an optional network prefix.
 */
static int
parse_ipaddr(const char *s, uint32_t *addr, uint32_t *mask)
{
	char *p, *slash;
	struct in_addr ia;

	*mask = 0xffffffffU;
	slash = strchr(s, '/');
	if (slash)
		*slash = 0;
	if (!inet_aton(s, &ia)) {
		if (slash)
			*slash = '/';
		*addr = 0;
		return -1;
	}
	*addr = ntohl(ia.s_addr);
	if (slash) {
		unsigned int prefix = strtoul(slash + 1, &p, 10);

		*slash = '/';
		if (p == slash + 1 || *p || prefix > 32)
			return -1;
		*mask <<= (32 - prefix);
	}
	return 0;
}

/*
 * Parse a string containing a value and an optional colon separated mask.
 */
static int
parse_val_mask_param(const char *s, uint32_t *val, uint32_t *mask,
    uint32_t default_mask)
{
	char *p;

	*mask = default_mask;
	*val = strtoul(s, &p, 0);
	if (p == s || *val > default_mask)
		return -1;
	if (*p == ':' && p[1])
		*mask = strtoul(p + 1, &p, 0);
	return *p || *mask > default_mask ? -1 : 0;
}

static int
parse_trace_param(const char *s, uint32_t *val, uint32_t *mask)
{
	return strchr(s, '.') ? parse_ipaddr(s, val, mask) :
				parse_val_mask_param(s, val, mask, 0xffffffffU);
}

static int
trace_config(int argc, char *argv[], int start_arg, const char *iff_name)
{
	uint32_t val, mask;
	struct ch_trace trace;

	if (argc == start_arg)
		return -1;

	memset(&trace, 0, sizeof(trace));
	if (!strcmp(argv[start_arg], "tx"))
		trace.config_tx = 1;
	else if (!strcmp(argv[start_arg], "rx"))
		trace.config_rx = 1;
	else if (!strcmp(argv[start_arg], "all"))
		trace.config_tx = trace.config_rx = 1;
	else
		errx(1, "bad trace filter \"%s\"; must be one of \"rx\", "
		     "\"tx\" or \"all\"", argv[start_arg]);

	if (argc == ++start_arg)
		return -1;
	if (!strcmp(argv[start_arg], "on")) {
		trace.trace_tx = trace.config_tx;
		trace.trace_rx = trace.config_rx;
	} else if (strcmp(argv[start_arg], "off"))
		errx(1, "bad argument \"%s\"; must be \"on\" or \"off\"",
		     argv[start_arg]);

	start_arg++;
	if (start_arg < argc && !strcmp(argv[start_arg], "not")) {
		trace.invert_match = 1;
		start_arg++;
	}

	while (start_arg + 2 <= argc) {
		int ret = parse_trace_param(argv[start_arg + 1], &val, &mask);

		if (!strcmp(argv[start_arg], "interface")) {
			trace.intf = val;
			trace.intf_mask = mask;
		} else if (!strcmp(argv[start_arg], "sip")) {
			trace.sip = val;
			trace.sip_mask = mask;
		} else if (!strcmp(argv[start_arg], "dip")) {
			trace.dip = val;
			trace.dip_mask = mask;
		} else if (!strcmp(argv[start_arg], "sport")) {
			trace.sport = val;
			trace.sport_mask = mask;
		} else if (!strcmp(argv[start_arg], "dport")) {
			trace.dport = val;
			trace.dport_mask = mask;
		} else if (!strcmp(argv[start_arg], "vlan")) {
			trace.vlan = val;
			trace.vlan_mask = mask;
		} else if (!strcmp(argv[start_arg], "proto")) {
			trace.proto = val;
			trace.proto_mask = mask;
		} else
			errx(1, "unknown trace parameter \"%s\"\n"
			     "known parameters are \"interface\", \"sip\", "
			     "\"dip\", \"sport\", \"dport\", \"vlan\", "
			     "\"proto\"", argv[start_arg]);
		if (ret < 0)
			errx(1, "bad parameter \"%s\"", argv[start_arg + 1]);
		start_arg += 2;
	}
	if (start_arg != argc)
		errx(1, "unknown parameter \"%s\"", argv[start_arg]);

	if (doit(iff_name, CHELSIO_SET_TRACE_FILTER, &trace) < 0)
		err(1, "trace");
	return 0;
}

static void
show_filters(const char *iff_name)
{
	static const char *pkt_type[] = { "*", "tcp", "udp", "frag" };
	struct ch_filter op;
	union {
		uint32_t nip;
		uint8_t octet[4];
	} nsip, ndip;
	char sip[20], dip[20];
	int header = 0;

	bzero(&op, sizeof(op));
	op.filter_id = 0xffffffff;

	do {
		if (doit(iff_name, CHELSIO_GET_FILTER, &op) < 0)
			err(1, "list filters");

		if (op.filter_id == 0xffffffff)
			break;

		if (!header) {
			printf("index         SIP                DIP     sport "
			    "dport VLAN PRI P/MAC type Q\n");
			header = 1;
		}

		nsip.nip = htonl(op.val.sip);
		ndip.nip = htonl(op.val.dip);

		sprintf(sip, "%u.%u.%u.%u/%-2u", nsip.octet[0], nsip.octet[1],
		    nsip.octet[2], nsip.octet[3],
		    op.mask.sip ? 33 - ffs(op.mask.sip) : 0);
		sprintf(dip, "%u.%u.%u.%u", ndip.octet[0], ndip.octet[1],
		    ndip.octet[2], ndip.octet[3]);
		printf("%5zu %18s %15s ", (size_t)op.filter_id, sip, dip);
		printf(op.val.sport ? "%5u " : "    * ", op.val.sport);
		printf(op.val.dport ? "%5u " : "    * ", op.val.dport);
		printf(op.val.vlan != 0xfff ? "%4u " : "   * ", op.val.vlan);
		printf(op.val.vlan_prio == 7 ?  "  * " :
		    "%1u/%1u ", op.val.vlan_prio, op.val.vlan_prio | 1);
		if (op.mac_addr_idx == 0xffff)
			printf("*/*   ");
		else if (op.mac_hit)
			printf("%1u/%3u ", (op.mac_addr_idx >> 3) & 0x1,
			    (op.mac_addr_idx) & 0x7);
		else
			printf("%1u/  * ", (op.mac_addr_idx >> 3) & 0x1);
		printf("%4s ", pkt_type[op.proto]);
		if (!op.pass)
			printf("-\n");
		else if (op.rss)
			printf("*\n");
		else
			printf("%1u\n", op.qset);
	} while (1);
}

static int
filter_config(int argc, char *argv[], int start_arg, const char *iff_name)
{
	int ret = 0;
	uint32_t val, mask;
	struct ch_filter op;

	if (argc < start_arg + 1)
		return -1;

	memset(&op, 0, sizeof(op));
	op.mac_addr_idx = 0xffff;
	op.rss = 1;

	if (argc == start_arg + 1 && !strcmp(argv[start_arg], "list")) {
		show_filters(iff_name);
		return 0;
	}

	if (get_int_arg(argv[start_arg++], &op.filter_id))
		return -1;
	if (argc == start_arg + 1 && (!strcmp(argv[start_arg], "delete") ||
				      !strcmp(argv[start_arg], "clear"))) {
		if (doit(iff_name, CHELSIO_DEL_FILTER, &op) < 0) {
			if (errno == EBUSY)
				err(1, "no filter support when offload in use");
			err(1, "delete filter");
		}
		return 0;
	}

	while (start_arg + 2 <= argc) {
		if (!strcmp(argv[start_arg], "sip")) {
			ret = parse_ipaddr(argv[start_arg + 1], &op.val.sip,
					   &op.mask.sip);
		} else if (!strcmp(argv[start_arg], "dip")) {
			ret = parse_ipaddr(argv[start_arg + 1], &op.val.dip,
					   &op.mask.dip);
		} else if (!strcmp(argv[start_arg], "sport")) {
			ret = parse_val_mask_param(argv[start_arg + 1],
						   &val, &mask, 0xffff);
			op.val.sport = val;
			op.mask.sport = mask;
		} else if (!strcmp(argv[start_arg], "dport")) {
			ret = parse_val_mask_param(argv[start_arg + 1],
						   &val, &mask, 0xffff);
			op.val.dport = val;
			op.mask.dport = mask;
		} else if (!strcmp(argv[start_arg], "vlan")) {
			ret = parse_val_mask_param(argv[start_arg + 1],
						   &val, &mask, 0xfff);
			op.val.vlan = val;
			op.mask.vlan = mask;
		} else if (!strcmp(argv[start_arg], "prio")) {
			ret = parse_val_mask_param(argv[start_arg + 1],
						   &val, &mask, 7);
			op.val.vlan_prio = val;
			op.mask.vlan_prio = mask;
		} else if (!strcmp(argv[start_arg], "mac")) {
			if (!strcmp(argv[start_arg + 1], "none"))
				val = -1;
			else
				ret = get_int_arg(argv[start_arg + 1], &val);
			op.mac_hit = val != (uint32_t)-1;
			op.mac_addr_idx = op.mac_hit ? val : 0;
		} else if (!strcmp(argv[start_arg], "type")) {
			if (!strcmp(argv[start_arg + 1], "tcp"))
				op.proto = 1;
			else if (!strcmp(argv[start_arg + 1], "udp"))
				op.proto = 2;
			else if (!strcmp(argv[start_arg + 1], "frag"))
				op.proto = 3;
			else
				errx(1, "unknown type \"%s\"; must be one of "
				     "\"tcp\", \"udp\", or \"frag\"",
				     argv[start_arg + 1]);
		} else if (!strcmp(argv[start_arg], "queue")) {
			ret = get_int_arg(argv[start_arg + 1], &val);
			op.qset = val;
			op.rss = 0;
		} else if (!strcmp(argv[start_arg], "action")) {
			if (!strcmp(argv[start_arg + 1], "pass"))
				op.pass = 1;
			else if (strcmp(argv[start_arg + 1], "drop"))
				errx(1, "unknown action \"%s\"; must be one of "
				     "\"pass\" or \"drop\"",
				     argv[start_arg + 1]);
		} else
 			errx(1, "unknown filter parameter \"%s\"\n"
			     "known parameters are \"mac\", \"sip\", "
			     "\"dip\", \"sport\", \"dport\", \"vlan\", "
			     "\"prio\", \"type\", \"queue\", and \"action\"",
			     argv[start_arg]);
		if (ret < 0)
			errx(1, "bad value \"%s\" for parameter \"%s\"",
			     argv[start_arg + 1], argv[start_arg]);
		start_arg += 2;
	}
	if (start_arg != argc)
		errx(1, "no value for \"%s\"", argv[start_arg]);

	if (doit(iff_name, CHELSIO_SET_FILTER, &op) < 0) {
		if (errno == EBUSY)
			err(1, "no filter support when offload in use");
		err(1, "set filter");
	}
	
	return 0;
}
static int
get_sched_param(int argc, char *argv[], int pos, unsigned int *valp)
{
	if (pos + 1 >= argc)
		errx(1, "missing value for %s", argv[pos]);
	if (get_int_arg(argv[pos + 1], valp))
		exit(1);
	return 0;
}

static int
tx_sched(int argc, char *argv[], int start_arg, const char *iff_name)
{
	struct ch_hw_sched op;
	unsigned int idx, val;

	if (argc < 5 || get_int_arg(argv[start_arg++], &idx))
		return -1;

	op.sched = idx;
	op.mode = op.channel = -1;
	op.kbps = op.class_ipg = op.flow_ipg = -1;

	while (argc > start_arg) {
		if (!strcmp(argv[start_arg], "mode")) {
			if (start_arg + 1 >= argc)
				errx(1, "missing value for mode");
			if (!strcmp(argv[start_arg + 1], "class"))
				op.mode = 0;
			else if (!strcmp(argv[start_arg + 1], "flow"))
				op.mode = 1;
			else
				errx(1, "bad mode \"%s\"", argv[start_arg + 1]);
		} else if (!strcmp(argv[start_arg], "channel") &&
			 !get_sched_param(argc, argv, start_arg, &val))
			op.channel = val;
		else if (!strcmp(argv[start_arg], "rate") &&
			 !get_sched_param(argc, argv, start_arg, &val))
			op.kbps = val;
		else if (!strcmp(argv[start_arg], "ipg") &&
			 !get_sched_param(argc, argv, start_arg, &val))
			op.class_ipg = val;
		else if (!strcmp(argv[start_arg], "flowipg") &&
			 !get_sched_param(argc, argv, start_arg, &val))
			op.flow_ipg = val;
		else
			errx(1, "unknown scheduler parameter \"%s\"",
			     argv[start_arg]);
		start_arg += 2;
	}

	if (doit(iff_name, CHELSIO_SET_HW_SCHED, &op) < 0)
		 err(1, "pktsched");

	return 0;
}

static int
pktsched(int argc, char *argv[], int start_arg, const char *iff_name)
{
	struct ch_pktsched_params op;
	unsigned int idx, min = -1, max, binding = -1;

	if (argc < 4)
		errx(1, "no scheduler specified");

	if (!strcmp(argv[start_arg], "port")) {
		if (argc != start_arg + 4)
			return -1;
		if (get_int_arg(argv[start_arg + 1], &idx) ||
		    get_int_arg(argv[start_arg + 2], &min) ||
		    get_int_arg(argv[start_arg + 3], &max))
			return -1;
		op.sched = 0;
	} else if (!strcmp(argv[start_arg], "tunnelq")) {
		if (argc != start_arg + 4)
			return -1;
		if (get_int_arg(argv[start_arg + 1], &idx) ||
		    get_int_arg(argv[start_arg + 2], &max) ||
		    get_int_arg(argv[start_arg + 3], &binding))
			return -1;
		op.sched = 1;
	} else if (!strcmp(argv[start_arg], "tx"))
		return tx_sched(argc, argv, start_arg + 1, iff_name);
	else
		errx(1, "unknown scheduler \"%s\"; must be one of \"port\", " 
			"\"tunnelq\" or \"tx\"", argv[start_arg]);
 
	op.idx = idx;
	op.min = min;
	op.max = max;
	op.binding = binding;
	if (doit(iff_name, CHELSIO_SET_PKTSCHED, &op) < 0)
		 err(1, "pktsched");

	return 0;
}

static int
clear_stats(int argc, char *argv[], int start_arg, const char *iff_name)
{
	(void) argc;
	(void) argv;
	(void) start_arg;

	if (doit(iff_name, CHELSIO_CLEAR_STATS, NULL) < 0)
		 err(1, "clearstats");

	return 0;
}

static int
get_up_la(int argc, char *argv[], int start_arg, const char *iff_name)
{
	struct ch_up_la la;
	int i, idx, max_idx, entries;

	(void) argc;
	(void) argv;
	(void) start_arg;

	la.stopped = 0;
	la.idx = -1;
	la.bufsize = LA_BUFSIZE;
	la.data = malloc(la.bufsize);
	if (!la.data)
		err(1, "uP_LA malloc");

	if (doit(iff_name, CHELSIO_GET_UP_LA, &la) < 0)
		 err(1, "uP_LA");

	if (la.stopped)
		printf("LA is not running\n");

	entries = la.bufsize / 4;
	idx = (int)la.idx;
	max_idx = (entries / 4) - 1;
	for (i = 0; i < max_idx; i++) {
		printf("%04x %08x %08x\n",
		       la.data[idx], la.data[idx+2], la.data[idx+1]);
		idx = (idx + 4) & (entries - 1);
	}

	return 0;
}

static int
get_up_ioqs(int argc, char *argv[], int start_arg, const char *iff_name)
{
	struct ch_up_ioqs ioqs;
	int i, entries;

	(void) argc;
	(void) argv;
	(void) start_arg;

	bzero(&ioqs, sizeof(ioqs));
	ioqs.bufsize = IOQS_BUFSIZE;
	ioqs.data = malloc(IOQS_BUFSIZE);
	if (!ioqs.data)
		err(1, "uP_IOQs malloc");

	if (doit(iff_name, CHELSIO_GET_UP_IOQS, &ioqs) < 0)
		 err(1, "uP_IOQs");

	printf("ioq_rx_enable   : 0x%08x\n", ioqs.ioq_rx_enable);
	printf("ioq_tx_enable   : 0x%08x\n", ioqs.ioq_tx_enable);
	printf("ioq_rx_status   : 0x%08x\n", ioqs.ioq_rx_status);
	printf("ioq_tx_status   : 0x%08x\n", ioqs.ioq_tx_status);
	
	entries = ioqs.bufsize / sizeof(struct t3_ioq_entry);
	for (i = 0; i < entries; i++) {
		printf("\nioq[%d].cp       : 0x%08x\n", i,
		       ioqs.data[i].ioq_cp);
		printf("ioq[%d].pp       : 0x%08x\n", i,
		       ioqs.data[i].ioq_pp);
		printf("ioq[%d].alen     : 0x%08x\n", i,
		       ioqs.data[i].ioq_alen);
		printf("ioq[%d].stats    : 0x%08x\n", i,
		       ioqs.data[i].ioq_stats);
		printf("  sop %u\n", ioqs.data[i].ioq_stats >> 16);
		printf("  eop %u\n", ioqs.data[i].ioq_stats  & 0xFFFF);
	}

	return 0;
}

static int
run_cmd(int argc, char *argv[], const char *iff_name)
{
	int r = -1;

	if (!strcmp(argv[2], "reg"))
		r = register_io(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "mdio"))
		r = mdio_io(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "mtus"))
		r = mtu_tab_op(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "pm"))
		r = conf_pm(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "regdump"))
		r = dump_regs(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "tcamdump"))
		r = dump_tcam(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "memdump"))
		r = dump_mc7(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "meminfo"))
		r = meminfo(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "context"))
		r = get_sge_context(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "desc"))
		r = get_sge_desc(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "loadfw"))
		r = load_fw(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "loadboot"))
		r = load_boot(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "proto"))
		r = proto_sram_op(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "qset"))
		r = qset_config(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "qsets"))
		r = qset_num_config(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "trace"))
		r = trace_config(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "pktsched"))
		r = pktsched(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "tcb"))
		r = get_tcb2(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "filter"))
		r = filter_config(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "clearstats"))
		r = clear_stats(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "la"))
		r = get_up_la(argc, argv, 3, iff_name);
	else if (!strcmp(argv[2], "ioqs"))
		r = get_up_ioqs(argc, argv, 3, iff_name);

	if (r == -1)
		usage(stderr);

	return (0);
}

static int
run_cmd_loop(int argc, char *argv[], const char *iff_name)
{
	int n;
	unsigned int i;
	char buf[64];
	char *args[8], *s;

	(void) argc;
	args[0] = argv[0];
	args[1] = argv[1];

	/*
	 * Fairly simplistic loop.  Displays a "> " prompt and processes any
	 * input as a cxgbtool command.  You're supposed to enter only the part
	 * after "cxgbtool cxgbX".  Use "quit" or "exit" to exit.  Any error in
	 * the command will also terminate cxgbtool.
	 */
	for (;;) {
		fprintf(stdout, "> ");
		fflush(stdout);
		n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
		if (n <= 0)
			return (0);

		if (buf[--n] != '\n')
			continue;
		else
			buf[n] = 0;

		s = &buf[0];
		for (i = 2; i < sizeof(args)/sizeof(args[0]) - 1; i++) {
			while (s && (*s == ' ' || *s == '\t'))
				s++;
			if ((args[i] = strsep(&s, " \t")) == NULL)
				break;
		}
		args[sizeof(args)/sizeof(args[0]) - 1] = 0;

		if (!strcmp(args[2], "quit") || !strcmp(args[2], "exit"))
			return (0);

		(void) run_cmd(i, args, iff_name);
	}

	/* Can't really get here */
	return (0);
}

int
main(int argc, char *argv[])
{
	int r = -1;
	const char *iff_name;

	progname = argv[0];

	if (argc == 2) {
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
			usage(stdout);
		if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version")) {
			printf("%s version %s\n", PROGNAME, VERSION);
			printf("%s\n", COPYRIGHT);
			exit(0);
		}
	}

	if (argc < 3) usage(stderr);

	iff_name = argv[1];

	if (argc == 3 && !strcmp(argv[2], "stdio"))
		r = run_cmd_loop(argc, argv, iff_name);
	else
		r = run_cmd(argc, argv, iff_name);

	return (r);
}
