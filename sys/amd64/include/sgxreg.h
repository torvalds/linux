/*-
 * Copyright (c) 2017 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
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
 *
 * $FreeBSD$
 */

/* Machine-defined variables. */

#ifndef _MACHINE_SGXREG_H_
#define _MACHINE_SGXREG_H_

/* Error codes. */
#define	SGX_SUCCESS			0
#define	SGX_INVALID_SIG_STRUCT		1	/* EINIT */
#define	SGX_INVALID_ATTRIBUTE		2	/* EINIT, EGETKEY */
#define	SGX_BLSTATE			3	/* EBLOCK */
#define	SGX_INVALID_MEASUREMENT		4	/* EINIT */
#define	SGX_NOTBLOCKABLE		5	/* EBLOCK */
#define	SGX_PG_INVLD			6	/* EBLOCK */
#define	SGX_LOCKFAIL			7	/* EBLOCK, EMODPR, EMODT */
#define	SGX_INVALID_SIGNATURE		8	/* EINIT */
#define	SGX_MAC_COMPARE_FAIL		9	/* ELDB, ELDU */
#define	SGX_PAGE_NOT_BLOCKED		10	/* EWB */
#define	SGX_NOT_TRACKED			11	/* EWB, EACCEPT */
#define	SGX_VA_SLOT_OCCUPIED		12	/* EWB */
#define	SGX_CHILD_PRESENT		13	/* EWB, EREMOVE */
#define	SGX_ENCLAVE_ACT			14	/* EREMOVE */
#define	SGX_ENTRYEPOCH_LOCKED		15	/* EBLOCK */
#define	SGX_INVALID_EINIT_TOKEN		16	/* EINIT */
#define	SGX_PREV_TRK_INCMPL		17	/* ETRACK */
#define	SGX_PG_IS_SECS			18	/* EBLOCK */
#define	SGX_PAGE_ATTRIBUTES_MISMATCH	19	/* EACCEPT, EACCEPTCOPY */
#define	SGX_PAGE_NOT_MODIFIABLE		20	/* EMODPR, EMODT */
#define	SGX_INVALID_CPUSVN		32	/* EINIT, EGETKEY */
#define	SGX_INVALID_ISVSVN		64	/* EGETKEY */
#define	SGX_UNMASKED_EVENT		128	/* EINIT */
#define	SGX_INVALID_KEYNAME		256	/* EGETKEY */

/*
 * 2.10 Page Information (PAGEINFO)
 * PAGEINFO is an architectural data structure that is used as a parameter
 * to the EPC-management instructions. It requires 32-Byte alignment.
 */
struct page_info {
	uint64_t linaddr;
	uint64_t srcpge;
	union {
		struct secinfo *secinfo;
		uint64_t pcmd;
	};
	uint64_t secs;
} __aligned(32);

/*
 * 2.11 Security Information (SECINFO)
 * The SECINFO data structure holds meta-data about an enclave page.
 */
struct secinfo {
	uint64_t flags;
#define	SECINFO_FLAGS_PT_S	8	/* Page type shift */
#define	SECINFO_FLAGS_PT_M	(0xff << SECINFO_FLAGS_PT_S)
	uint64_t reserved[7];
} __aligned(64);

/*
 * 2.7.1 ATTRIBUTES
 * The ATTRIBUTES data structure is comprised of bit-granular fields that
 * are used in the SECS, CPUID enumeration, the REPORT and the KEYREQUEST
 * structures.
 */
struct secs_attr {
	uint8_t		reserved1: 1;
	uint8_t		debug: 1;
	uint8_t		mode64bit: 1;
	uint8_t		reserved2: 1;
	uint8_t		provisionkey: 1;
	uint8_t		einittokenkey: 1;
	uint8_t		reserved3: 2;
#define	SECS_ATTR_RSV4_SIZE	7
	uint8_t		reserved4[SECS_ATTR_RSV4_SIZE];
	uint64_t	xfrm;			/* X-Feature Request Mask */
};

/*
 * 2.7 SGX Enclave Control Structure (SECS)
 * The SECS data structure requires 4K-Bytes alignment.
 */
struct secs {
	uint64_t	size;
	uint64_t	base;
	uint32_t	ssa_frame_size;
	uint32_t	misc_select;
#define	SECS_RSV1_SIZE	24
	uint8_t		reserved1[SECS_RSV1_SIZE];
	struct secs_attr attributes;
	uint8_t		mr_enclave[32];
#define	SECS_RSV2_SIZE	32
	uint8_t		reserved2[SECS_RSV2_SIZE];
	uint8_t		mr_signer[32];
#define	SECS_RSV3_SIZE	96
	uint8_t		reserved3[SECS_RSV3_SIZE];
	uint16_t	isv_prod_id;
	uint16_t	isv_svn;
#define	SECS_RSV4_SIZE	3836
	uint8_t		reserved4[SECS_RSV4_SIZE];
};

/*
 * 2.8 Thread Control Structure (TCS)
 * Each executing thread in the enclave is associated with a
 * Thread Control Structure. It requires 4K-Bytes alignment.
 */
struct tcs {
	uint64_t	reserved1;
	uint64_t	flags;
	uint64_t	ossa;
	uint32_t	cssa;
	uint32_t	nssa;
	uint64_t	oentry;
	uint64_t	reserved2;
	uint64_t	ofsbasgx;
	uint64_t	ogsbasgx;
	uint32_t	fslimit;
	uint32_t	gslimit;
	uint64_t	reserved3[503];
};

#endif /* !_MACHINE_SGXREG_H_ */
