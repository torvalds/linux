/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2003-2011 Netlogic Microsystems (Netlogic). All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY Netlogic Microsystems ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * NETLOGIC_BSD
 * $FreeBSD$
 */

#ifndef __NLM_FMNV2_H__
#define	__NLM_FMNV2_H__

/**
* @file_name fmn.h
* @author Netlogic Microsystems
* @brief HAL for Fast message network V2
*/

/* FMN configuration registers */
#define	CMS_OUTPUTQ_CONFIG(i)		((i)*2)
#define	CMS_MAX_OUTPUTQ			1024
#define	CMS_OUTPUTQ_CREDIT_CFG		(0x2000/4)
#define	CMS_MSG_CONFIG			(0x2008/4)
#define	CMS_MSG_ERR			(0x2010/4)
#define	CMS_TRACE_CONFIG		(0x2018/4)
#define	CMS_TRACE_BASE_ADDR		(0x2020/4)
#define	CMS_TRACE_LIMIT_ADDR		(0x2028/4)
#define	CMS_TRACE_CURRENT_ADDR		(0x2030/4)
#define	CMS_MSG_ENDIAN_SWAP		(0x2038/4)

#define	CMS_CPU_PUSHQ(node, core, thread, vc)	\
		(((node)<<10) | ((core)<<4) | ((thread)<<2) | ((vc)<<0))
#define	CMS_POPQ(node, queue)	(((node)<<10) | (queue))
#define	CMS_IO_PUSHQ(node, queue)	(((node)<<10) | (queue))

#define	CMS_POPQ_QID(i)		(128+(i))

/* FMN Level Interrupt Type */
#define	CMS_LVL_INTR_DISABLE	0
#define	CMS_LVL_LOW_WATERMARK	1
#define	CMS_LVL_HI_WATERMARK	2

/* FMN Level interrupt trigger values */
#define	CMS_QUEUE_NON_EMPTY		0
#define	CMS_QUEUE_QUARTER_FULL		1
#define	CMS_QUEUE_HALF_FULL		2
#define	CMS_QUEUE_THREE_QUARTER_FULL	3
#define	CMS_QUEUE_FULL			4

/* FMN Timer Interrupt Type */
#define	CMS_TIMER_INTR_DISABLE	0
#define	CMS_TIMER_CONSUMER		1
#define	CMS_TIMER_PRODUCER		1

/* FMN timer interrupt trigger values */
#define	CMS_TWO_POW_EIGHT_CYCLES	0
#define	CMS_TWO_POW_TEN_CYCLES		1
#define	CMS_TWO_POW_TWELVE_CYCLES	2
#define	CMS_TWO_POW_FOURTEEN_CYCLES	3
#define	CMS_TWO_POW_SIXTEEN_CYCLES	4
#define	CMS_TWO_POW_EIGHTTEEN_CYCLES	5
#define	CMS_TWO_POW_TWENTY_CYCLES	6
#define	CMS_TWO_POW_TWENTYTWO_CYCLES	7

#define	CMS_QUEUE_ENA		1ULL
#define	CMS_QUEUE_DIS		0
#define	CMS_SPILL_ENA		1ULL
#define	CMS_SPILL_DIS		0

#define	CMS_MAX_VCPU_VC		4

/* Each XLP chip can hold upto 32K messages on the chip itself */
#define	CMS_ON_CHIP_MESG_SPACE	(32*1024)
#define	CMS_MAX_ONCHIP_SEGMENTS	1024
#define	CMS_MAX_SPILL_SEGMENTS_PER_QUEUE	64

/* FMN Network error */
#define	CMS_ILLEGAL_DST_ERROR		0x100
#define	CMS_BIU_TIMEOUT_ERROR		0x080
#define	CMS_BIU_ERROR			0x040
#define	CMS_SPILL_FILL_UNCORRECT_ECC_ERROR	0x020
#define	CMS_SPILL_FILL_CORRECT_ECC_ERROR	0x010
#define	CMS_SPILL_UNCORRECT_ECC_ERROR	0x008
#define	CMS_SPILL_CORRECT_ECC_ERROR		0x004
#define	CMS_OUTPUTQ_UNCORRECT_ECC_ERROR	0x002
#define	CMS_OUTPUTQ_CORRECT_ECC_ERROR	0x001

/* worst case, a single entry message consists of a 4 byte header
 * and an 8-byte entry = 12 bytes in total
 */
#define	CMS_SINGLE_ENTRY_MSG_SIZE	12
/* total spill memory needed for one FMN queue */
#define	CMS_PER_QUEUE_SPILL_MEM(spilltotmsgs)		\
		((spilltotmsgs) * (CMS_SINGLE_ENTRY_MSG_SIZE))

/* FMN Src station id's */
#define	CMS_CPU0_SRC_STID		(0 << 4)
#define	CMS_CPU1_SRC_STID		(1 << 4)
#define	CMS_CPU2_SRC_STID		(2 << 4)
#define	CMS_CPU3_SRC_STID		(3 << 4)
#define	CMS_CPU4_SRC_STID		(4 << 4)
#define	CMS_CPU5_SRC_STID		(5 << 4)
#define	CMS_CPU6_SRC_STID		(6 << 4)
#define	CMS_CPU7_SRC_STID		(7 << 4)
#define	CMS_PCIE0_SRC_STID		256
#define	CMS_PCIE1_SRC_STID		258
#define	CMS_PCIE2_SRC_STID		260
#define	CMS_PCIE3_SRC_STID		262
#define	CMS_DTE_SRC_STID		264
#define	CMS_RSA_ECC_SRC_STID		272
#define	CMS_CRYPTO_SRC_STID		281
#define	CMS_CMP_SRC_STID		298
#define	CMS_POE_SRC_STID		384
#define	CMS_NAE_SRC_STID		476

/* POPQ related defines */
#define	CMS_POPQID_START	128
#define	CMS_POPQID_END		255

#define	CMS_INT_RCVD		0x800000000000000ULL

#define	nlm_read_cms_reg(b, r)	nlm_read_reg64_xkphys(b,r)
#define	nlm_write_cms_reg(b, r, v)	nlm_write_reg64_xkphys(b,r,v)
#define	nlm_get_cms_pcibase(node)	\
	nlm_pcicfg_base(XLP_IO_CMS_OFFSET(node))
#define	nlm_get_cms_regbase(node)	\
	nlm_xkphys_map_pcibar0(nlm_get_cms_pcibase(node))

#define	XLP_CMS_ON_CHIP_PER_QUEUE_SPACE(node)			\
		((XLP_CMS_ON_CHIP_MESG_SPACE)/			\
		(nlm_read_reg(nlm_pcibase_cms(node),		\
		XLP_PCI_DEVINFO_REG0))
/* total spill memory needed */
#define	XLP_CMS_TOTAL_SPILL_MEM(node, spilltotmsgs)		\
               ((XLP_CMS_PER_QUEUE_SPILL_MEM(spilltotmsgs)) *	\
		(nlm_read_reg(nlm_pcibase_cms(node),		\
		XLP_PCI_DEVINFO_REG0))
#define	CMS_TOTAL_QUEUE_SIZE(node, spilltotmsgs)		\
		((spilltotmsgs) + (CMS_ON_CHIP_PER_QUEUE_SPACE(node)))

enum fmn_swcode {
	FMN_SWCODE_CPU0=1,
	FMN_SWCODE_CPU1,
	FMN_SWCODE_CPU2,
	FMN_SWCODE_CPU3,
	FMN_SWCODE_CPU4,
	FMN_SWCODE_CPU5,
	FMN_SWCODE_CPU6,
	FMN_SWCODE_CPU7,
	FMN_SWCODE_CPU8,
	FMN_SWCODE_CPU9,
	FMN_SWCODE_CPU10,
	FMN_SWCODE_CPU11,
	FMN_SWCODE_CPU12,
	FMN_SWCODE_CPU13,
	FMN_SWCODE_CPU14,
	FMN_SWCODE_CPU15,
	FMN_SWCODE_CPU16,
	FMN_SWCODE_CPU17,
	FMN_SWCODE_CPU18,
	FMN_SWCODE_CPU19,
	FMN_SWCODE_CPU20,
	FMN_SWCODE_CPU21,
	FMN_SWCODE_CPU22,
	FMN_SWCODE_CPU23,
	FMN_SWCODE_CPU24,
	FMN_SWCODE_CPU25,
	FMN_SWCODE_CPU26,
	FMN_SWCODE_CPU27,
	FMN_SWCODE_CPU28,
	FMN_SWCODE_CPU29,
	FMN_SWCODE_CPU30,
	FMN_SWCODE_CPU31,
	FMN_SWCODE_CPU32,
	FMN_SWCODE_PCIE0,
	FMN_SWCODE_PCIE1,
	FMN_SWCODE_PCIE2,
	FMN_SWCODE_PCIE3,
	FMN_SWCODE_DTE,
	FMN_SWCODE_CRYPTO,
	FMN_SWCODE_RSA,
	FMN_SWCODE_CMP,
	FMN_SWCODE_POE,
	FMN_SWCODE_NAE,
};

extern uint64_t nlm_cms_spill_total_messages;
extern uint32_t nlm_cms_total_stations;

extern uint64_t cms_base_addr(int node);
extern int nlm_cms_verify_credit_config (int spill_en, int tot_credit);
extern int nlm_cms_get_oc_space(int qsize, int max_queues, int qid, int *ocbase, int *ocstart, int *ocend);
extern void nlm_cms_setup_credits (uint64_t base, int destid, int srcid, int credit);
extern int nlm_cms_config_onchip_queue (uint64_t base, uint64_t cms_spill_base, int qid, int spill_en);
extern void nlm_cms_default_setup(int node, uint64_t spill_base, int spill_en, int popq_en);
extern uint64_t nlm_cms_get_onchip_queue (uint64_t base, int qid);
extern void nlm_cms_set_onchip_queue (uint64_t base, int qid, uint64_t val);
extern void nlm_cms_per_queue_level_intr(uint64_t base, int qid, int sub_type, int intr_val);
extern void nlm_cms_level_intr(int node, int sub_type, int intr_val);
extern void nlm_cms_per_queue_timer_intr(uint64_t base, int qid, int sub_type, int intr_val);
extern void nlm_cms_timer_intr(int node, int en, int sub_type, int intr_val);
extern int nlm_cms_outputq_intr_check(uint64_t base, int qid);
extern void nlm_cms_outputq_clr_intr(uint64_t base, int qid);
extern void nlm_cms_illegal_dst_error_intr(uint64_t base, int en);
extern void nlm_cms_timeout_error_intr(uint64_t base, int en);
extern void nlm_cms_biu_error_resp_intr(uint64_t base, int en);
extern void nlm_cms_spill_uncorrectable_ecc_error_intr(uint64_t base, int en);
extern void nlm_cms_spill_correctable_ecc_error_intr(uint64_t base, int en);
extern void nlm_cms_outputq_uncorrectable_ecc_error_intr(uint64_t base, int en);
extern void nlm_cms_outputq_correctable_ecc_error_intr(uint64_t base, int en);
extern uint64_t nlm_cms_network_error_status(uint64_t base);
extern int nlm_cms_get_net_error_code(uint64_t err);
extern int nlm_cms_get_net_error_syndrome(uint64_t err);
extern int nlm_cms_get_net_error_ramindex(uint64_t err);
extern int nlm_cms_get_net_error_outputq(uint64_t err);
extern void nlm_cms_trace_setup(uint64_t base, int en, uint64_t trace_base, uint64_t trace_limit, int match_dstid_en, int dst_id, int match_srcid_en, int src_id, int wrap);
extern void nlm_cms_endian_byte_swap (uint64_t base, int en);
extern uint8_t xlp_msg_send(uint8_t vc, uint8_t size);
extern int nlm_cms_alloc_spill_q(uint64_t base, int qid, uint64_t spill_base,
	int nsegs);
extern int nlm_cms_alloc_onchip_q(uint64_t base, int qid, int nsegs);

#endif
