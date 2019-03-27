/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2014 Qlogic Corporation
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * File: qls_dump.h
 */

#ifndef _QLS_DUMP_H_
#define _QLS_DUMP_H_

#define Q81_MPID_COOKIE 0x5555aaaa

typedef struct qls_mpid_glbl_hdr
{
	uint32_t	cookie;
	uint8_t		id[16];
	uint32_t	time_lo;
	uint32_t	time_hi;
	uint32_t	img_size;
	uint32_t	hdr_size;
	uint8_t		info[220];
} qls_mpid_glbl_hdr_t;

typedef struct qls_mpid_seg_hdr
{
	uint32_t	cookie;
	uint32_t	seg_num;
	uint32_t	seg_size;
	uint32_t	extra;
	uint8_t		desc[16];
} qls_mpid_seg_hdr_t;

enum
{
	Q81_MPI_CORE_REGS_ADDR		= 0x00030000,
	Q81_MPI_CORE_REGS_CNT		= 127,
	Q81_MPI_CORE_SH_REGS_CNT	= 16,
	Q81_TEST_REGS_ADDR		= 0x00001000,
	Q81_TEST_REGS_CNT		= 23,
	Q81_RMII_REGS_ADDR		= 0x00001040,
	Q81_RMII_REGS_CNT		= 64,
	Q81_FCMAC1_REGS_ADDR		= 0x00001080,
	Q81_FCMAC2_REGS_ADDR		= 0x000010c0,
	Q81_FCMAC_REGS_CNT		= 64,
	Q81_FC1_MBX_REGS_ADDR		= 0x00001100,
	Q81_FC2_MBX_REGS_ADDR		= 0x00001240,
	Q81_FC_MBX_REGS_CNT		= 64,
	Q81_IDE_REGS_ADDR		= 0x00001140,
	Q81_IDE_REGS_CNT		= 64,
	Q81_NIC1_MBX_REGS_ADDR		= 0x00001180,
	Q81_NIC2_MBX_REGS_ADDR		= 0x00001280,
	Q81_NIC_MBX_REGS_CNT		= 64,
	Q81_SMBUS_REGS_ADDR		= 0x00001200,
	Q81_SMBUS_REGS_CNT		= 64,
	Q81_I2C_REGS_ADDR		= 0x00001fc0,
	Q81_I2C_REGS_CNT		= 64,
	Q81_MEMC_REGS_ADDR		= 0x00003000,
	Q81_MEMC_REGS_CNT		= 256,
	Q81_PBUS_REGS_ADDR		= 0x00007c00,
	Q81_PBUS_REGS_CNT		= 256,
	Q81_MDE_REGS_ADDR		= 0x00010000,
	Q81_MDE_REGS_CNT		= 6,
	Q81_CODE_RAM_ADDR		= 0x00020000,
	Q81_CODE_RAM_CNT		= 0x2000,
	Q81_MEMC_RAM_ADDR		= 0x00100000,
	Q81_MEMC_RAM_CNT		= 0x2000,
	Q81_XGMAC_REGISTER_END		= 0x740,
};

#define Q81_PROBE_DATA_LENGTH_WORDS	((64*2) + 1)
#define Q81_NUMBER_OF_PROBES		34

#define Q81_PROBE_SIZE		\
		(Q81_PROBE_DATA_LENGTH_WORDS * Q81_NUMBER_OF_PROBES)

#define Q81_NUMBER_ROUTING_REG_ENTRIES	48
#define Q81_WORDS_PER_ROUTING_REG_ENTRY	4

#define Q81_ROUT_REG_SIZE		\
	(Q81_NUMBER_ROUTING_REG_ENTRIES * Q81_WORDS_PER_ROUTING_REG_ENTRY)

#define Q81_MAC_PROTOCOL_REGISTER_WORDS	((512 * 3) + (32 * 2) + (4096 * 1) +\
					 (4096 * 1) + (4 * 2) +\
					 (8 * 2) + (16 * 1) +\
					 (4 * 1) + (4 * 4) + (4 * 1))

#define Q81_WORDS_PER_MAC_PROT_ENTRY	2
#define Q81_MAC_REG_SIZE		\
		(Q81_MAC_PROTOCOL_REGISTER_WORDS * Q81_WORDS_PER_MAC_PROT_ENTRY)

#define Q81_MAX_SEMAPHORE_FUNCTIONS 5

#define Q81_WQC_WORD_SIZE	6
#define Q81_NUMBER_OF_WQCS	128
#define Q81_WQ_SIZE		(Q81_WQC_WORD_SIZE * Q81_NUMBER_OF_WQCS)

#define Q81_CQC_WORD_SIZE	13
#define Q81_NUMBER_OF_CQCS	128
#define Q81_CQ_SIZE		(Q81_CQC_WORD_SIZE * Q81_NUMBER_OF_CQCS)

struct qls_mpi_coredump {
	qls_mpid_glbl_hdr_t	mpi_global_header;

	qls_mpid_seg_hdr_t	core_regs_seg_hdr;
	uint32_t		mpi_core_regs[Q81_MPI_CORE_REGS_CNT];
	uint32_t		mpi_core_sh_regs[Q81_MPI_CORE_SH_REGS_CNT];

	qls_mpid_seg_hdr_t	test_logic_regs_seg_hdr;
	uint32_t		test_logic_regs[Q81_TEST_REGS_CNT];

	qls_mpid_seg_hdr_t	rmii_regs_seg_hdr;
	uint32_t		rmii_regs[Q81_RMII_REGS_CNT];

	qls_mpid_seg_hdr_t	fcmac1_regs_seg_hdr;
	uint32_t		fcmac1_regs[Q81_FCMAC_REGS_CNT];

	qls_mpid_seg_hdr_t	fcmac2_regs_seg_hdr;
	uint32_t		fcmac2_regs[Q81_FCMAC_REGS_CNT];

	qls_mpid_seg_hdr_t	fc1_mbx_regs_seg_hdr;
	uint32_t		fc1_mbx_regs[Q81_FC_MBX_REGS_CNT];

	qls_mpid_seg_hdr_t	ide_regs_seg_hdr;
	uint32_t		ide_regs[Q81_IDE_REGS_CNT];

	qls_mpid_seg_hdr_t	nic1_mbx_regs_seg_hdr;
	uint32_t		nic1_mbx_regs[Q81_NIC_MBX_REGS_CNT];

	qls_mpid_seg_hdr_t	smbus_regs_seg_hdr;
	uint32_t		smbus_regs[Q81_SMBUS_REGS_CNT];

	qls_mpid_seg_hdr_t	fc2_mbx_regs_seg_hdr;
	uint32_t		fc2_mbx_regs[Q81_FC_MBX_REGS_CNT];

	qls_mpid_seg_hdr_t	nic2_mbx_regs_seg_hdr;
	uint32_t		nic2_mbx_regs[Q81_NIC_MBX_REGS_CNT];

	qls_mpid_seg_hdr_t	i2c_regs_seg_hdr;
	uint32_t		i2c_regs[Q81_I2C_REGS_CNT];

	qls_mpid_seg_hdr_t	memc_regs_seg_hdr;
	uint32_t		memc_regs[Q81_MEMC_REGS_CNT];

	qls_mpid_seg_hdr_t	pbus_regs_seg_hdr;
	uint32_t		pbus_regs[Q81_PBUS_REGS_CNT];

	qls_mpid_seg_hdr_t	mde_regs_seg_hdr;
	uint32_t		mde_regs[Q81_MDE_REGS_CNT];

	qls_mpid_seg_hdr_t	xaui1_an_hdr;
	uint32_t		serdes1_xaui_an[14];

	qls_mpid_seg_hdr_t	xaui1_hss_pcs_hdr;
	uint32_t		serdes1_xaui_hss_pcs[33];

	qls_mpid_seg_hdr_t	xfi1_an_hdr;
	uint32_t		serdes1_xfi_an[14];

	qls_mpid_seg_hdr_t	xfi1_train_hdr;
	uint32_t		serdes1_xfi_train[12];

	qls_mpid_seg_hdr_t	xfi1_hss_pcs_hdr;
	uint32_t		serdes1_xfi_hss_pcs[15];

	qls_mpid_seg_hdr_t	xfi1_hss_tx_hdr;
	uint32_t		serdes1_xfi_hss_tx[32];

	qls_mpid_seg_hdr_t	xfi1_hss_rx_hdr;
	uint32_t		serdes1_xfi_hss_rx[32];

	qls_mpid_seg_hdr_t	xfi1_hss_pll_hdr;
	uint32_t		serdes1_xfi_hss_pll[32];

	qls_mpid_seg_hdr_t	xaui2_an_hdr;
	uint32_t		serdes2_xaui_an[14];

	qls_mpid_seg_hdr_t	xaui2_hss_pcs_hdr;
	uint32_t		serdes2_xaui_hss_pcs[33];

	qls_mpid_seg_hdr_t	xfi2_an_hdr;
	uint32_t		serdes2_xfi_an[14];

	qls_mpid_seg_hdr_t	xfi2_train_hdr;
	uint32_t		serdes2_xfi_train[12];

	qls_mpid_seg_hdr_t	xfi2_hss_pcs_hdr;
	uint32_t		serdes2_xfi_hss_pcs[15];

	qls_mpid_seg_hdr_t	xfi2_hss_tx_hdr;
	uint32_t		serdes2_xfi_hss_tx[32];

	qls_mpid_seg_hdr_t	xfi2_hss_rx_hdr;
	uint32_t		serdes2_xfi_hss_rx[32];

	qls_mpid_seg_hdr_t	xfi2_hss_pll_hdr;
	uint32_t		serdes2_xfi_hss_pll[32];

	qls_mpid_seg_hdr_t	nic1_regs_seg_hdr;
	uint32_t		nic1_regs[64];

	qls_mpid_seg_hdr_t	nic2_regs_seg_hdr;
	uint32_t		nic2_regs[64];

	qls_mpid_seg_hdr_t	intr_states_seg_hdr;
	uint32_t		intr_states[MAX_RX_RINGS];

	qls_mpid_seg_hdr_t	xgmac1_seg_hdr;
	uint32_t		xgmac1[Q81_XGMAC_REGISTER_END];

	qls_mpid_seg_hdr_t	xgmac2_seg_hdr;
	uint32_t		xgmac2[Q81_XGMAC_REGISTER_END];

	qls_mpid_seg_hdr_t	probe_dump_seg_hdr;
	uint32_t		probe_dump[Q81_PROBE_SIZE];

	qls_mpid_seg_hdr_t	routing_reg_seg_hdr;
	uint32_t		routing_regs[Q81_ROUT_REG_SIZE];

	qls_mpid_seg_hdr_t	mac_prot_reg_seg_hdr;
	uint32_t		mac_prot_regs[Q81_MAC_REG_SIZE];

	qls_mpid_seg_hdr_t	sem_regs_seg_hdr;
	uint32_t		sem_regs[Q81_MAX_SEMAPHORE_FUNCTIONS];

	qls_mpid_seg_hdr_t	ets_seg_hdr;
	uint32_t		ets[8+2];

	qls_mpid_seg_hdr_t	wqc1_seg_hdr;
	uint32_t		wqc1[Q81_WQ_SIZE];

	qls_mpid_seg_hdr_t	cqc1_seg_hdr;
	uint32_t		cqc1[Q81_CQ_SIZE];

	qls_mpid_seg_hdr_t	wqc2_seg_hdr;
	uint32_t		wqc2[Q81_WQ_SIZE];

	qls_mpid_seg_hdr_t	cqc2_seg_hdr;
	uint32_t		cqc2[Q81_CQ_SIZE];

	qls_mpid_seg_hdr_t	code_ram_seg_hdr;
	uint32_t		code_ram[Q81_CODE_RAM_CNT];

	qls_mpid_seg_hdr_t	memc_ram_seg_hdr;
	uint32_t		memc_ram[Q81_MEMC_RAM_CNT];
};
typedef struct qls_mpi_coredump qls_mpi_coredump_t;

#define Q81_BAD_DATA	0xDEADBEEF

#endif /* #ifndef  _QLS_DUMP_H_ */

