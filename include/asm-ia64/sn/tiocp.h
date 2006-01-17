/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003-2004 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_PCI_TIOCP_H
#define _ASM_IA64_SN_PCI_TIOCP_H

#define TIOCP_HOST_INTR_ADDR            0x003FFFFFFFFFFFFFUL
#define TIOCP_PCI64_CMDTYPE_MEM         (0x1ull << 60)


/*****************************************************************************
 *********************** TIOCP MMR structure mapping ***************************
 *****************************************************************************/

struct tiocp{

    /* 0x000000-0x00FFFF -- Local Registers */

    /* 0x000000-0x000057 -- (Legacy Widget Space) Configuration */
    u64		cp_id;				/* 0x000000 */
    u64		cp_stat;			/* 0x000008 */
    u64		cp_err_upper;			/* 0x000010 */
    u64		cp_err_lower;			/* 0x000018 */
    #define cp_err cp_err_lower
    u64		cp_control;			/* 0x000020 */
    u64		cp_req_timeout;			/* 0x000028 */
    u64		cp_intr_upper;			/* 0x000030 */
    u64		cp_intr_lower;			/* 0x000038 */
    #define cp_intr cp_intr_lower
    u64		cp_err_cmdword;			/* 0x000040 */
    u64		_pad_000048;			/* 0x000048 */
    u64		cp_tflush;			/* 0x000050 */

    /* 0x000058-0x00007F -- Bridge-specific Configuration */
    u64		cp_aux_err;			/* 0x000058 */
    u64		cp_resp_upper;			/* 0x000060 */
    u64		cp_resp_lower;			/* 0x000068 */
    #define cp_resp cp_resp_lower
    u64		cp_tst_pin_ctrl;		/* 0x000070 */
    u64		cp_addr_lkerr;			/* 0x000078 */

    /* 0x000080-0x00008F -- PMU & MAP */
    u64		cp_dir_map;			/* 0x000080 */
    u64		_pad_000088;			/* 0x000088 */

    /* 0x000090-0x00009F -- SSRAM */
    u64		cp_map_fault;			/* 0x000090 */
    u64		_pad_000098;			/* 0x000098 */

    /* 0x0000A0-0x0000AF -- Arbitration */
    u64		cp_arb;				/* 0x0000A0 */
    u64		_pad_0000A8;			/* 0x0000A8 */

    /* 0x0000B0-0x0000BF -- Number In A Can or ATE Parity Error */
    u64		cp_ate_parity_err;		/* 0x0000B0 */
    u64		_pad_0000B8;			/* 0x0000B8 */

    /* 0x0000C0-0x0000FF -- PCI/GIO */
    u64		cp_bus_timeout;			/* 0x0000C0 */
    u64		cp_pci_cfg;			/* 0x0000C8 */
    u64		cp_pci_err_upper;		/* 0x0000D0 */
    u64		cp_pci_err_lower;		/* 0x0000D8 */
    #define cp_pci_err cp_pci_err_lower
    u64		_pad_0000E0[4];			/* 0x0000{E0..F8} */

    /* 0x000100-0x0001FF -- Interrupt */
    u64		cp_int_status;			/* 0x000100 */
    u64		cp_int_enable;			/* 0x000108 */
    u64		cp_int_rst_stat;		/* 0x000110 */
    u64		cp_int_mode;			/* 0x000118 */
    u64		cp_int_device;			/* 0x000120 */
    u64		cp_int_host_err;		/* 0x000128 */
    u64		cp_int_addr[8];			/* 0x0001{30,,,68} */
    u64		cp_err_int_view;		/* 0x000170 */
    u64		cp_mult_int;			/* 0x000178 */
    u64		cp_force_always[8];		/* 0x0001{80,,,B8} */
    u64		cp_force_pin[8];		/* 0x0001{C0,,,F8} */

    /* 0x000200-0x000298 -- Device */
    u64		cp_device[4];			/* 0x0002{00,,,18} */
    u64		_pad_000220[4];			/* 0x0002{20,,,38} */
    u64		cp_wr_req_buf[4];		/* 0x0002{40,,,58} */
    u64		_pad_000260[4];			/* 0x0002{60,,,78} */
    u64		cp_rrb_map[2];			/* 0x0002{80,,,88} */
    #define cp_even_resp cp_rrb_map[0]			/* 0x000280 */
    #define cp_odd_resp  cp_rrb_map[1]			/* 0x000288 */
    u64		cp_resp_status;			/* 0x000290 */
    u64		cp_resp_clear;			/* 0x000298 */

    u64		_pad_0002A0[12];		/* 0x0002{A0..F8} */

    /* 0x000300-0x0003F8 -- Buffer Address Match Registers */
    struct {
	u64	upper;				/* 0x0003{00,,,F0} */
	u64	lower;				/* 0x0003{08,,,F8} */
    } cp_buf_addr_match[16];

    /* 0x000400-0x0005FF -- Performance Monitor Registers (even only) */
    struct {
	u64	flush_w_touch;			/* 0x000{400,,,5C0} */
	u64	flush_wo_touch;			/* 0x000{408,,,5C8} */
	u64	inflight;			/* 0x000{410,,,5D0} */
	u64	prefetch;			/* 0x000{418,,,5D8} */
	u64	total_pci_retry;		/* 0x000{420,,,5E0} */
	u64	max_pci_retry;			/* 0x000{428,,,5E8} */
	u64	max_latency;			/* 0x000{430,,,5F0} */
	u64	clear_all;			/* 0x000{438,,,5F8} */
    } cp_buf_count[8];


    /* 0x000600-0x0009FF -- PCI/X registers */
    u64		cp_pcix_bus_err_addr;		/* 0x000600 */
    u64		cp_pcix_bus_err_attr;		/* 0x000608 */
    u64		cp_pcix_bus_err_data;		/* 0x000610 */
    u64		cp_pcix_pio_split_addr;		/* 0x000618 */
    u64		cp_pcix_pio_split_attr;		/* 0x000620 */
    u64		cp_pcix_dma_req_err_attr;	/* 0x000628 */
    u64		cp_pcix_dma_req_err_addr;	/* 0x000630 */
    u64		cp_pcix_timeout;		/* 0x000638 */

    u64		_pad_000640[24];		/* 0x000{640,,,6F8} */

    /* 0x000700-0x000737 -- Debug Registers */
    u64		cp_ct_debug_ctl;		/* 0x000700 */
    u64		cp_br_debug_ctl;		/* 0x000708 */
    u64		cp_mux3_debug_ctl;		/* 0x000710 */
    u64		cp_mux4_debug_ctl;		/* 0x000718 */
    u64		cp_mux5_debug_ctl;		/* 0x000720 */
    u64		cp_mux6_debug_ctl;		/* 0x000728 */
    u64		cp_mux7_debug_ctl;		/* 0x000730 */

    u64		_pad_000738[89];		/* 0x000{738,,,9F8} */

    /* 0x000A00-0x000BFF -- PCI/X Read&Write Buffer */
    struct {
	u64	cp_buf_addr;			/* 0x000{A00,,,AF0} */
	u64	cp_buf_attr;			/* 0X000{A08,,,AF8} */
    } cp_pcix_read_buf_64[16];

    struct {
	u64	cp_buf_addr;			/* 0x000{B00,,,BE0} */
	u64	cp_buf_attr;			/* 0x000{B08,,,BE8} */
	u64	cp_buf_valid;			/* 0x000{B10,,,BF0} */
	u64	__pad1;				/* 0x000{B18,,,BF8} */
    } cp_pcix_write_buf_64[8];

    /* End of Local Registers -- Start of Address Map space */

    char	_pad_000c00[0x010000 - 0x000c00];

    /* 0x010000-0x011FF8 -- Internal ATE RAM (Auto Parity Generation) */
    u64		cp_int_ate_ram[1024];		/* 0x010000-0x011FF8 */

    char	_pad_012000[0x14000 - 0x012000];

    /* 0x014000-0x015FF8 -- Internal ATE RAM (Manual Parity Generation) */
    u64		cp_int_ate_ram_mp[1024];	/* 0x014000-0x015FF8 */

    char	_pad_016000[0x18000 - 0x016000];

    /* 0x18000-0x197F8 -- TIOCP Write Request Ram */
    u64		cp_wr_req_lower[256];		/* 0x18000 - 0x187F8 */
    u64		cp_wr_req_upper[256];		/* 0x18800 - 0x18FF8 */
    u64		cp_wr_req_parity[256];		/* 0x19000 - 0x197F8 */

    char	_pad_019800[0x1C000 - 0x019800];

    /* 0x1C000-0x1EFF8 -- TIOCP Read Response Ram */
    u64		cp_rd_resp_lower[512];		/* 0x1C000 - 0x1CFF8 */
    u64		cp_rd_resp_upper[512];		/* 0x1D000 - 0x1DFF8 */
    u64		cp_rd_resp_parity[512];		/* 0x1E000 - 0x1EFF8 */

    char	_pad_01F000[0x20000 - 0x01F000];

    /* 0x020000-0x021FFF -- Host Device (CP) Configuration Space (not used)  */
    char	_pad_020000[0x021000 - 0x20000];

    /* 0x021000-0x027FFF -- PCI Device Configuration Spaces */
    union {
	u8	c[0x1000 / 1];			/* 0x02{0000,,,7FFF} */
	u16	s[0x1000 / 2];			/* 0x02{0000,,,7FFF} */
	u32	l[0x1000 / 4];			/* 0x02{0000,,,7FFF} */
	u64	d[0x1000 / 8];			/* 0x02{0000,,,7FFF} */
	union {
	    u8	c[0x100 / 1];
	    u16	s[0x100 / 2];
	    u32	l[0x100 / 4];
	    u64	d[0x100 / 8];
	} f[8];
    } cp_type0_cfg_dev[7];				/* 0x02{1000,,,7FFF} */

    /* 0x028000-0x028FFF -- PCI Type 1 Configuration Space */
    union {
	u8	c[0x1000 / 1];			/* 0x028000-0x029000 */
	u16	s[0x1000 / 2];			/* 0x028000-0x029000 */
	u32	l[0x1000 / 4];			/* 0x028000-0x029000 */
	u64	d[0x1000 / 8];			/* 0x028000-0x029000 */
	union {
	    u8	c[0x100 / 1];
	    u16	s[0x100 / 2];
	    u32	l[0x100 / 4];
	    u64	d[0x100 / 8];
	} f[8];
    } cp_type1_cfg;					/* 0x028000-0x029000 */

    char		_pad_029000[0x030000-0x029000];

    /* 0x030000-0x030007 -- PCI Interrupt Acknowledge Cycle */
    union {
	u8	c[8 / 1];
	u16	s[8 / 2];
	u32	l[8 / 4];
	u64	d[8 / 8];
    } cp_pci_iack;					/* 0x030000-0x030007 */

    char		_pad_030007[0x040000-0x030008];

    /* 0x040000-0x040007 -- PCIX Special Cycle */
    union {
	u8	c[8 / 1];
	u16	s[8 / 2];
	u32	l[8 / 4];
	u64	d[8 / 8];
    } cp_pcix_cycle;					/* 0x040000-0x040007 */

    char		_pad_040007[0x200000-0x040008];

    /* 0x200000-0x7FFFFF -- PCI/GIO Device Spaces */
    union {
	u8	c[0x100000 / 1];
	u16	s[0x100000 / 2];
	u32	l[0x100000 / 4];
	u64	d[0x100000 / 8];
    } cp_devio_raw[6];					/* 0x200000-0x7FFFFF */

    #define cp_devio(n)  cp_devio_raw[((n)<2)?(n*2):(n+2)]

    char		_pad_800000[0xA00000-0x800000];

    /* 0xA00000-0xBFFFFF -- PCI/GIO Device Spaces w/flush  */
    union {
	u8	c[0x100000 / 1];
	u16	s[0x100000 / 2];
	u32	l[0x100000 / 4];
	u64	d[0x100000 / 8];
    } cp_devio_raw_flush[6];				/* 0xA00000-0xBFFFFF */

    #define cp_devio_flush(n)  cp_devio_raw_flush[((n)<2)?(n*2):(n+2)]

};

#endif 	/* _ASM_IA64_SN_PCI_TIOCP_H */
