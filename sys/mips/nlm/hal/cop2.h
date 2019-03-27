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

#ifndef __NLM_HAL_COP2_H__
#define	__NLM_HAL_COP2_H__

#define	COP2_TX_BUF		0
#define	COP2_RX_BUF		1
#define	COP2_TXMSGSTATUS	2
#define	COP2_RXMSGSTATUS	3
#define	COP2_MSGSTATUS1		4
#define	COP2_MSGCONFIG		5
#define	COP2_MSGERROR		6

#define	CROSSTHR_POPQ_EN	0x01
#define	VC0_POPQ_EN		0x02
#define	VC1_POPQ_EN		0x04
#define	VC2_POPQ_EN		0x08
#define	VC3_POPQ_EN		0x10
#define	ALL_VC_POPQ_EN		0x1E
#define	ALL_VC_CT_POPQ_EN	0x1F

struct nlm_fmn_msg {
	uint64_t msg[4];
};

#define	NLM_DEFINE_COP2_ACCESSORS32(name, reg, sel)		\
static inline uint32_t nlm_read_c2_##name(void)			\
{								\
	uint32_t __rv;						\
	__asm__ __volatile__ (					\
	".set	push\n"						\
	".set	noreorder\n"					\
	".set	mips64\n"					\
	"mfc2	%0, $%1, %2\n"					\
	".set	pop\n"						\
	: "=r" (__rv)						\
	: "i" (reg), "i" (sel));				\
	return __rv;						\
}								\
								\
static inline void nlm_write_c2_##name(uint32_t val)		\
{								\
	__asm__ __volatile__(					\
	".set	push\n"						\
	".set	noreorder\n"					\
	".set	mips64\n"					\
	"mtc2	%0, $%1, %2\n"					\
	".set	pop\n"						\
	: : "r" (val), "i" (reg), "i" (sel));			\
} struct __hack

#if (__mips == 64)
#define	NLM_DEFINE_COP2_ACCESSORS64(name, reg, sel)		\
static inline uint64_t nlm_read_c2_##name(void)			\
{								\
	uint64_t __rv;						\
	__asm__ __volatile__ (					\
	".set	push\n"						\
	".set	noreorder\n"					\
	".set	mips64\n"					\
	"dmfc2	%0, $%1, %2\n"					\
	".set	pop\n"						\
	: "=r" (__rv)						\
	: "i" (reg), "i" (sel));				\
	return __rv;						\
}								\
								\
static inline void nlm_write_c2_##name(uint64_t val)		\
{								\
	__asm__ __volatile__ (					\
	".set	push\n"						\
	".set	noreorder\n"					\
	".set	mips64\n"					\
	"dmtc2	%0, $%1, %2\n"					\
	".set	pop\n"						\
	: : "r" (val), "i" (reg), "i" (sel));			\
} struct __hack

#else

#define	NLM_DEFINE_COP2_ACCESSORS64(name, reg, sel)		\
static inline uint64_t nlm_read_c2_##name(void)			\
{								\
	uint32_t __high, __low;					\
	__asm__ __volatile__ (					\
	".set	push\n"						\
	".set	noreorder\n"					\
	".set	mips64\n"					\
	"dmfc2	$8, $%2, %3\n"					\
	"dsra32	%0, $8, 0\n"					\
	"sll	%1, $8, 0\n"					\
	".set	pop\n"						\
	: "=r"(__high), "=r"(__low)				\
	: "i"(reg), "i"(sel)					\
	: "$8");						\
								\
	return ((uint64_t)__high << 32) | __low;		\
}								\
								\
static inline void nlm_write_c2_##name(uint64_t val)		\
{								\
	uint32_t __high = val >> 32;				\
	uint32_t __low = val & 0xffffffff;			\
	__asm__ __volatile__ (					\
	".set	push\n"						\
	".set	noreorder\n"					\
	".set	mips64\n"					\
	"dsll32	$8, %1, 0\n"					\
	"dsll32	$9, %0, 0\n"					\
	"dsrl32	$8, $8, 0\n"					\
	"or	$8, $8, $9\n"					\
	"dmtc2	$8, $%2, %3\n"					\
	".set	pop\n"						\
	: : "r"(__high), "r"(__low),  "i"(reg), "i"(sel)	\
	: "$8", "$9");						\
} struct __hack

#endif

NLM_DEFINE_COP2_ACCESSORS64(txbuf0, COP2_TX_BUF, 0);
NLM_DEFINE_COP2_ACCESSORS64(txbuf1, COP2_TX_BUF, 1);
NLM_DEFINE_COP2_ACCESSORS64(txbuf2, COP2_TX_BUF, 2);
NLM_DEFINE_COP2_ACCESSORS64(txbuf3, COP2_TX_BUF, 3);

NLM_DEFINE_COP2_ACCESSORS64(rxbuf0, COP2_RX_BUF, 0);
NLM_DEFINE_COP2_ACCESSORS64(rxbuf1, COP2_RX_BUF, 1);
NLM_DEFINE_COP2_ACCESSORS64(rxbuf2, COP2_RX_BUF, 2);
NLM_DEFINE_COP2_ACCESSORS64(rxbuf3, COP2_RX_BUF, 3);

NLM_DEFINE_COP2_ACCESSORS32(txmsgstatus, COP2_TXMSGSTATUS, 0);
NLM_DEFINE_COP2_ACCESSORS32(rxmsgstatus, COP2_RXMSGSTATUS, 0);
NLM_DEFINE_COP2_ACCESSORS32(msgstatus1, COP2_MSGSTATUS1, 0);
NLM_DEFINE_COP2_ACCESSORS32(msgconfig, COP2_MSGCONFIG, 0);
NLM_DEFINE_COP2_ACCESSORS32(msgerror0, COP2_MSGERROR, 0);
NLM_DEFINE_COP2_ACCESSORS32(msgerror1, COP2_MSGERROR, 1);
NLM_DEFINE_COP2_ACCESSORS32(msgerror2, COP2_MSGERROR, 2);
NLM_DEFINE_COP2_ACCESSORS32(msgerror3, COP2_MSGERROR, 3);

/* successful completion returns 1, else 0 */
static inline int
nlm_msgsend(int val)
{
	int result;
	__asm__ volatile (
		".set push\n"
		".set noreorder\n"
		".set mips64\n"
		"move	$8, %1\n"
		"sync\n"
		"/* msgsnds	$9, $8 */\n"
		".word	0x4a084801\n"
		"move	%0, $9\n"
		".set pop\n"
		: "=r" (result)
		: "r" (val)
		: "$8", "$9");
	return result;
}

static inline int
nlm_msgld(int vc)
{
	int val;
	__asm__ volatile (
		".set push\n"
		".set noreorder\n"
		".set mips64\n"
		"move	$8, %1\n"
		"/* msgld	$9, $8 */\n"
		".word 0x4a084802\n"
		"move	%0, $9\n"
		".set pop\n"
		: "=r" (val)
		: "r" (vc)
		: "$8", "$9");
	return val;
}

static inline void
nlm_msgwait(int vc)
{
	__asm__ volatile (
		".set push\n"
		".set noreorder\n"
		".set mips64\n"
		"move	$8, %0\n"
		"/* msgwait	$8 */\n"
		".word 0x4a080003\n"
		".set pop\n"
		: : "r" (vc)
		: "$8");
}

static inline int
nlm_fmn_msgsend(int dstid, int size, int swcode, struct nlm_fmn_msg *m)
{
	uint32_t flags, status;
	int rv;

	size -= 1;
	flags = nlm_save_flags_cop2();
	switch (size) {
	case 3:
		nlm_write_c2_txbuf3(m->msg[3]);
	case 2:
		nlm_write_c2_txbuf2(m->msg[2]);
	case 1:
		nlm_write_c2_txbuf1(m->msg[1]);
	case 0:
		nlm_write_c2_txbuf0(m->msg[0]);
	}

	dstid |= ((swcode << 24) | (size << 16));
	status = nlm_msgsend(dstid);
	rv = !status;
	if (rv != 0)
		rv = nlm_read_c2_txmsgstatus();
	nlm_restore_flags(flags);

	return rv;
}

static inline int
nlm_fmn_msgrcv(int vc, int *srcid, int *size, int *code, struct nlm_fmn_msg *m)
{
	uint32_t status;
	uint32_t msg_status, flags;
	int tmp_sz, rv;

	flags = nlm_save_flags_cop2();
	status = nlm_msgld(vc); /* will return 0, if error */
	rv = !status;
	if (rv == 0) {
		msg_status = nlm_read_c2_rxmsgstatus();
		*size = ((msg_status >> 26) & 0x3) + 1;
		*code = (msg_status >> 18) & 0xff;
		*srcid = (msg_status >> 4) & 0xfff;
		tmp_sz = *size - 1;
		switch (tmp_sz) {
		case 3:
			m->msg[3] = nlm_read_c2_rxbuf3();
		case 2:
			m->msg[2] = nlm_read_c2_rxbuf2();
		case 1:
			m->msg[1] = nlm_read_c2_rxbuf1();
		case 0:
			m->msg[0] = nlm_read_c2_rxbuf0();
		}
	}
	nlm_restore_flags(flags);

	return rv;
}

static inline void
nlm_fmn_cpu_init(int int_vec, int ecc_en, int v0pe, int v1pe, int v2pe, int v3pe)
{
	uint32_t val = nlm_read_c2_msgconfig();

	/* Note: in XLP PRM 0.8.1, the int_vec bits are un-documented
	 * in msgconfig register of cop2.
	 * As per chip/cpu RTL, [16:20] bits consist of int_vec.
	 */
	val |= (((int_vec & 0x1f) << 16) |
		((ecc_en & 0x1) << 8) |
		((v3pe & 0x1) << 4) |
		((v2pe & 0x1) << 3) |
		((v1pe & 0x1) << 2) |
		((v0pe & 0x1) << 1));

	nlm_write_c2_msgconfig(val);
}
#endif
