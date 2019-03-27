/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#ifndef _SYS_DTRACE_H
#define	_SYS_DTRACE_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * DTrace Dynamic Tracing Software: Kernel Interfaces
 *
 * Note: The contents of this file are private to the implementation of the
 * Solaris system and DTrace subsystem and are subject to change at any time
 * without notice.  Applications and drivers using these interfaces will fail
 * to run on future releases.  These interfaces should not be used for any
 * purpose except those expressly outlined in dtrace(7D) and libdtrace(3LIB).
 * Please refer to the "Solaris Dynamic Tracing Guide" for more information.
 */

#ifndef _ASM

#include <sys/types.h>
#include <sys/modctl.h>
#include <sys/processor.h>
#ifdef illumos
#include <sys/systm.h>
#else
#include <sys/cpuvar.h>
#include <sys/param.h>
#include <sys/linker.h>
#include <sys/ioccom.h>
#include <sys/ucred.h>
typedef int model_t;
#endif
#include <sys/ctf_api.h>
#ifdef illumos
#include <sys/cyclic.h>
#include <sys/int_limits.h>
#else
#include <sys/stdint.h>
#endif

/*
 * DTrace Universal Constants and Typedefs
 */
#define	DTRACE_CPUALL		-1	/* all CPUs */
#define	DTRACE_IDNONE		0	/* invalid probe identifier */
#define	DTRACE_EPIDNONE		0	/* invalid enabled probe identifier */
#define	DTRACE_AGGIDNONE	0	/* invalid aggregation identifier */
#define	DTRACE_AGGVARIDNONE	0	/* invalid aggregation variable ID */
#define	DTRACE_CACHEIDNONE	0	/* invalid predicate cache */
#define	DTRACE_PROVNONE		0	/* invalid provider identifier */
#define	DTRACE_METAPROVNONE	0	/* invalid meta-provider identifier */
#define	DTRACE_ARGNONE		-1	/* invalid argument index */

#define	DTRACE_PROVNAMELEN	64
#define	DTRACE_MODNAMELEN	64
#define	DTRACE_FUNCNAMELEN	192
#define	DTRACE_NAMELEN		64
#define	DTRACE_FULLNAMELEN	(DTRACE_PROVNAMELEN + DTRACE_MODNAMELEN + \
				DTRACE_FUNCNAMELEN + DTRACE_NAMELEN + 4)
#define	DTRACE_ARGTYPELEN	128

typedef uint32_t dtrace_id_t;		/* probe identifier */
typedef uint32_t dtrace_epid_t;		/* enabled probe identifier */
typedef uint32_t dtrace_aggid_t;	/* aggregation identifier */
typedef int64_t dtrace_aggvarid_t;	/* aggregation variable identifier */
typedef uint16_t dtrace_actkind_t;	/* action kind */
typedef int64_t dtrace_optval_t;	/* option value */
typedef uint32_t dtrace_cacheid_t;	/* predicate cache identifier */

typedef enum dtrace_probespec {
	DTRACE_PROBESPEC_NONE = -1,
	DTRACE_PROBESPEC_PROVIDER = 0,
	DTRACE_PROBESPEC_MOD,
	DTRACE_PROBESPEC_FUNC,
	DTRACE_PROBESPEC_NAME
} dtrace_probespec_t;

/*
 * DTrace Intermediate Format (DIF)
 *
 * The following definitions describe the DTrace Intermediate Format (DIF), a
 * a RISC-like instruction set and program encoding used to represent
 * predicates and actions that can be bound to DTrace probes.  The constants
 * below defining the number of available registers are suggested minimums; the
 * compiler should use DTRACEIOC_CONF to dynamically obtain the number of
 * registers provided by the current DTrace implementation.
 */
#define	DIF_VERSION_1	1		/* DIF version 1: Solaris 10 Beta */
#define	DIF_VERSION_2	2		/* DIF version 2: Solaris 10 FCS */
#define	DIF_VERSION	DIF_VERSION_2	/* latest DIF instruction set version */
#define	DIF_DIR_NREGS	8		/* number of DIF integer registers */
#define	DIF_DTR_NREGS	8		/* number of DIF tuple registers */

#define	DIF_OP_OR	1		/* or	r1, r2, rd */
#define	DIF_OP_XOR	2		/* xor	r1, r2, rd */
#define	DIF_OP_AND	3		/* and	r1, r2, rd */
#define	DIF_OP_SLL	4		/* sll	r1, r2, rd */
#define	DIF_OP_SRL	5		/* srl	r1, r2, rd */
#define	DIF_OP_SUB	6		/* sub	r1, r2, rd */
#define	DIF_OP_ADD	7		/* add	r1, r2, rd */
#define	DIF_OP_MUL	8		/* mul	r1, r2, rd */
#define	DIF_OP_SDIV	9		/* sdiv	r1, r2, rd */
#define	DIF_OP_UDIV	10		/* udiv r1, r2, rd */
#define	DIF_OP_SREM	11		/* srem r1, r2, rd */
#define	DIF_OP_UREM	12		/* urem r1, r2, rd */
#define	DIF_OP_NOT	13		/* not	r1, rd */
#define	DIF_OP_MOV	14		/* mov	r1, rd */
#define	DIF_OP_CMP	15		/* cmp	r1, r2 */
#define	DIF_OP_TST	16		/* tst  r1 */
#define	DIF_OP_BA	17		/* ba	label */
#define	DIF_OP_BE	18		/* be	label */
#define	DIF_OP_BNE	19		/* bne	label */
#define	DIF_OP_BG	20		/* bg	label */
#define	DIF_OP_BGU	21		/* bgu	label */
#define	DIF_OP_BGE	22		/* bge	label */
#define	DIF_OP_BGEU	23		/* bgeu	label */
#define	DIF_OP_BL	24		/* bl	label */
#define	DIF_OP_BLU	25		/* blu	label */
#define	DIF_OP_BLE	26		/* ble	label */
#define	DIF_OP_BLEU	27		/* bleu	label */
#define	DIF_OP_LDSB	28		/* ldsb	[r1], rd */
#define	DIF_OP_LDSH	29		/* ldsh	[r1], rd */
#define	DIF_OP_LDSW	30		/* ldsw [r1], rd */
#define	DIF_OP_LDUB	31		/* ldub	[r1], rd */
#define	DIF_OP_LDUH	32		/* lduh	[r1], rd */
#define	DIF_OP_LDUW	33		/* lduw	[r1], rd */
#define	DIF_OP_LDX	34		/* ldx	[r1], rd */
#define	DIF_OP_RET	35		/* ret	rd */
#define	DIF_OP_NOP	36		/* nop */
#define	DIF_OP_SETX	37		/* setx	intindex, rd */
#define	DIF_OP_SETS	38		/* sets strindex, rd */
#define	DIF_OP_SCMP	39		/* scmp	r1, r2 */
#define	DIF_OP_LDGA	40		/* ldga	var, ri, rd */
#define	DIF_OP_LDGS	41		/* ldgs var, rd */
#define	DIF_OP_STGS	42		/* stgs var, rs */
#define	DIF_OP_LDTA	43		/* ldta var, ri, rd */
#define	DIF_OP_LDTS	44		/* ldts var, rd */
#define	DIF_OP_STTS	45		/* stts var, rs */
#define	DIF_OP_SRA	46		/* sra	r1, r2, rd */
#define	DIF_OP_CALL	47		/* call	subr, rd */
#define	DIF_OP_PUSHTR	48		/* pushtr type, rs, rr */
#define	DIF_OP_PUSHTV	49		/* pushtv type, rs, rv */
#define	DIF_OP_POPTS	50		/* popts */
#define	DIF_OP_FLUSHTS	51		/* flushts */
#define	DIF_OP_LDGAA	52		/* ldgaa var, rd */
#define	DIF_OP_LDTAA	53		/* ldtaa var, rd */
#define	DIF_OP_STGAA	54		/* stgaa var, rs */
#define	DIF_OP_STTAA	55		/* sttaa var, rs */
#define	DIF_OP_LDLS	56		/* ldls	var, rd */
#define	DIF_OP_STLS	57		/* stls	var, rs */
#define	DIF_OP_ALLOCS	58		/* allocs r1, rd */
#define	DIF_OP_COPYS	59		/* copys  r1, r2, rd */
#define	DIF_OP_STB	60		/* stb	r1, [rd] */
#define	DIF_OP_STH	61		/* sth	r1, [rd] */
#define	DIF_OP_STW	62		/* stw	r1, [rd] */
#define	DIF_OP_STX	63		/* stx	r1, [rd] */
#define	DIF_OP_ULDSB	64		/* uldsb [r1], rd */
#define	DIF_OP_ULDSH	65		/* uldsh [r1], rd */
#define	DIF_OP_ULDSW	66		/* uldsw [r1], rd */
#define	DIF_OP_ULDUB	67		/* uldub [r1], rd */
#define	DIF_OP_ULDUH	68		/* ulduh [r1], rd */
#define	DIF_OP_ULDUW	69		/* ulduw [r1], rd */
#define	DIF_OP_ULDX	70		/* uldx  [r1], rd */
#define	DIF_OP_RLDSB	71		/* rldsb [r1], rd */
#define	DIF_OP_RLDSH	72		/* rldsh [r1], rd */
#define	DIF_OP_RLDSW	73		/* rldsw [r1], rd */
#define	DIF_OP_RLDUB	74		/* rldub [r1], rd */
#define	DIF_OP_RLDUH	75		/* rlduh [r1], rd */
#define	DIF_OP_RLDUW	76		/* rlduw [r1], rd */
#define	DIF_OP_RLDX	77		/* rldx  [r1], rd */
#define	DIF_OP_XLATE	78		/* xlate xlrindex, rd */
#define	DIF_OP_XLARG	79		/* xlarg xlrindex, rd */

#define	DIF_INTOFF_MAX		0xffff	/* highest integer table offset */
#define	DIF_STROFF_MAX		0xffff	/* highest string table offset */
#define	DIF_REGISTER_MAX	0xff	/* highest register number */
#define	DIF_VARIABLE_MAX	0xffff	/* highest variable identifier */
#define	DIF_SUBROUTINE_MAX	0xffff	/* highest subroutine code */

#define	DIF_VAR_ARRAY_MIN	0x0000	/* lowest numbered array variable */
#define	DIF_VAR_ARRAY_UBASE	0x0080	/* lowest user-defined array */
#define	DIF_VAR_ARRAY_MAX	0x00ff	/* highest numbered array variable */

#define	DIF_VAR_OTHER_MIN	0x0100	/* lowest numbered scalar or assc */
#define	DIF_VAR_OTHER_UBASE	0x0500	/* lowest user-defined scalar or assc */
#define	DIF_VAR_OTHER_MAX	0xffff	/* highest numbered scalar or assc */

#define	DIF_VAR_ARGS		0x0000	/* arguments array */
#define	DIF_VAR_REGS		0x0001	/* registers array */
#define	DIF_VAR_UREGS		0x0002	/* user registers array */
#define	DIF_VAR_CURTHREAD	0x0100	/* thread pointer */
#define	DIF_VAR_TIMESTAMP	0x0101	/* timestamp */
#define	DIF_VAR_VTIMESTAMP	0x0102	/* virtual timestamp */
#define	DIF_VAR_IPL		0x0103	/* interrupt priority level */
#define	DIF_VAR_EPID		0x0104	/* enabled probe ID */
#define	DIF_VAR_ID		0x0105	/* probe ID */
#define	DIF_VAR_ARG0		0x0106	/* first argument */
#define	DIF_VAR_ARG1		0x0107	/* second argument */
#define	DIF_VAR_ARG2		0x0108	/* third argument */
#define	DIF_VAR_ARG3		0x0109	/* fourth argument */
#define	DIF_VAR_ARG4		0x010a	/* fifth argument */
#define	DIF_VAR_ARG5		0x010b	/* sixth argument */
#define	DIF_VAR_ARG6		0x010c	/* seventh argument */
#define	DIF_VAR_ARG7		0x010d	/* eighth argument */
#define	DIF_VAR_ARG8		0x010e	/* ninth argument */
#define	DIF_VAR_ARG9		0x010f	/* tenth argument */
#define	DIF_VAR_STACKDEPTH	0x0110	/* stack depth */
#define	DIF_VAR_CALLER		0x0111	/* caller */
#define	DIF_VAR_PROBEPROV	0x0112	/* probe provider */
#define	DIF_VAR_PROBEMOD	0x0113	/* probe module */
#define	DIF_VAR_PROBEFUNC	0x0114	/* probe function */
#define	DIF_VAR_PROBENAME	0x0115	/* probe name */
#define	DIF_VAR_PID		0x0116	/* process ID */
#define	DIF_VAR_TID		0x0117	/* (per-process) thread ID */
#define	DIF_VAR_EXECNAME	0x0118	/* name of executable */
#define	DIF_VAR_ZONENAME	0x0119	/* zone name associated with process */
#define	DIF_VAR_WALLTIMESTAMP	0x011a	/* wall-clock timestamp */
#define	DIF_VAR_USTACKDEPTH	0x011b	/* user-land stack depth */
#define	DIF_VAR_UCALLER		0x011c	/* user-level caller */
#define	DIF_VAR_PPID		0x011d	/* parent process ID */
#define	DIF_VAR_UID		0x011e	/* process user ID */
#define	DIF_VAR_GID		0x011f	/* process group ID */
#define	DIF_VAR_ERRNO		0x0120	/* thread errno */
#define	DIF_VAR_EXECARGS	0x0121	/* process arguments */
#define	DIF_VAR_JID		0x0122	/* process jail id */
#define	DIF_VAR_JAILNAME	0x0123	/* process jail name */

#ifndef illumos
#define	DIF_VAR_CPU		0x0200
#endif

#define	DIF_SUBR_RAND			0
#define	DIF_SUBR_MUTEX_OWNED		1
#define	DIF_SUBR_MUTEX_OWNER		2
#define	DIF_SUBR_MUTEX_TYPE_ADAPTIVE	3
#define	DIF_SUBR_MUTEX_TYPE_SPIN	4
#define	DIF_SUBR_RW_READ_HELD		5
#define	DIF_SUBR_RW_WRITE_HELD		6
#define	DIF_SUBR_RW_ISWRITER		7
#define	DIF_SUBR_COPYIN			8
#define	DIF_SUBR_COPYINSTR		9
#define	DIF_SUBR_SPECULATION		10
#define	DIF_SUBR_PROGENYOF		11
#define	DIF_SUBR_STRLEN			12
#define	DIF_SUBR_COPYOUT		13
#define	DIF_SUBR_COPYOUTSTR		14
#define	DIF_SUBR_ALLOCA			15
#define	DIF_SUBR_BCOPY			16
#define	DIF_SUBR_COPYINTO		17
#define	DIF_SUBR_MSGDSIZE		18
#define	DIF_SUBR_MSGSIZE		19
#define	DIF_SUBR_GETMAJOR		20
#define	DIF_SUBR_GETMINOR		21
#define	DIF_SUBR_DDI_PATHNAME		22
#define	DIF_SUBR_STRJOIN		23
#define	DIF_SUBR_LLTOSTR		24
#define	DIF_SUBR_BASENAME		25
#define	DIF_SUBR_DIRNAME		26
#define	DIF_SUBR_CLEANPATH		27
#define	DIF_SUBR_STRCHR			28
#define	DIF_SUBR_STRRCHR		29
#define	DIF_SUBR_STRSTR			30
#define	DIF_SUBR_STRTOK			31
#define	DIF_SUBR_SUBSTR			32
#define	DIF_SUBR_INDEX			33
#define	DIF_SUBR_RINDEX			34
#define	DIF_SUBR_HTONS			35
#define	DIF_SUBR_HTONL			36
#define	DIF_SUBR_HTONLL			37
#define	DIF_SUBR_NTOHS			38
#define	DIF_SUBR_NTOHL			39
#define	DIF_SUBR_NTOHLL			40
#define	DIF_SUBR_INET_NTOP		41
#define	DIF_SUBR_INET_NTOA		42
#define	DIF_SUBR_INET_NTOA6		43
#define	DIF_SUBR_TOUPPER		44
#define	DIF_SUBR_TOLOWER		45
#define	DIF_SUBR_MEMREF			46
#define	DIF_SUBR_SX_SHARED_HELD		47
#define	DIF_SUBR_SX_EXCLUSIVE_HELD	48
#define	DIF_SUBR_SX_ISEXCLUSIVE		49
#define	DIF_SUBR_MEMSTR			50
#define	DIF_SUBR_GETF			51
#define	DIF_SUBR_JSON			52
#define	DIF_SUBR_STRTOLL		53
#define	DIF_SUBR_MAX			53	/* max subroutine value */

typedef uint32_t dif_instr_t;

#define	DIF_INSTR_OP(i)			(((i) >> 24) & 0xff)
#define	DIF_INSTR_R1(i)			(((i) >> 16) & 0xff)
#define	DIF_INSTR_R2(i)			(((i) >>  8) & 0xff)
#define	DIF_INSTR_RD(i)			((i) & 0xff)
#define	DIF_INSTR_RS(i)			((i) & 0xff)
#define	DIF_INSTR_LABEL(i)		((i) & 0xffffff)
#define	DIF_INSTR_VAR(i)		(((i) >>  8) & 0xffff)
#define	DIF_INSTR_INTEGER(i)		(((i) >>  8) & 0xffff)
#define	DIF_INSTR_STRING(i)		(((i) >>  8) & 0xffff)
#define	DIF_INSTR_SUBR(i)		(((i) >>  8) & 0xffff)
#define	DIF_INSTR_TYPE(i)		(((i) >> 16) & 0xff)
#define	DIF_INSTR_XLREF(i)		(((i) >>  8) & 0xffff)

#define	DIF_INSTR_FMT(op, r1, r2, d) \
	(((op) << 24) | ((r1) << 16) | ((r2) << 8) | (d))

#define	DIF_INSTR_NOT(r1, d)		(DIF_INSTR_FMT(DIF_OP_NOT, r1, 0, d))
#define	DIF_INSTR_MOV(r1, d)		(DIF_INSTR_FMT(DIF_OP_MOV, r1, 0, d))
#define	DIF_INSTR_CMP(op, r1, r2)	(DIF_INSTR_FMT(op, r1, r2, 0))
#define	DIF_INSTR_TST(r1)		(DIF_INSTR_FMT(DIF_OP_TST, r1, 0, 0))
#define	DIF_INSTR_BRANCH(op, label)	(((op) << 24) | (label))
#define	DIF_INSTR_LOAD(op, r1, d)	(DIF_INSTR_FMT(op, r1, 0, d))
#define	DIF_INSTR_STORE(op, r1, d)	(DIF_INSTR_FMT(op, r1, 0, d))
#define	DIF_INSTR_SETX(i, d)		((DIF_OP_SETX << 24) | ((i) << 8) | (d))
#define	DIF_INSTR_SETS(s, d)		((DIF_OP_SETS << 24) | ((s) << 8) | (d))
#define	DIF_INSTR_RET(d)		(DIF_INSTR_FMT(DIF_OP_RET, 0, 0, d))
#define	DIF_INSTR_NOP			(DIF_OP_NOP << 24)
#define	DIF_INSTR_LDA(op, v, r, d)	(DIF_INSTR_FMT(op, v, r, d))
#define	DIF_INSTR_LDV(op, v, d)		(((op) << 24) | ((v) << 8) | (d))
#define	DIF_INSTR_STV(op, v, rs)	(((op) << 24) | ((v) << 8) | (rs))
#define	DIF_INSTR_CALL(s, d)		((DIF_OP_CALL << 24) | ((s) << 8) | (d))
#define	DIF_INSTR_PUSHTS(op, t, r2, rs)	(DIF_INSTR_FMT(op, t, r2, rs))
#define	DIF_INSTR_POPTS			(DIF_OP_POPTS << 24)
#define	DIF_INSTR_FLUSHTS		(DIF_OP_FLUSHTS << 24)
#define	DIF_INSTR_ALLOCS(r1, d)		(DIF_INSTR_FMT(DIF_OP_ALLOCS, r1, 0, d))
#define	DIF_INSTR_COPYS(r1, r2, d)	(DIF_INSTR_FMT(DIF_OP_COPYS, r1, r2, d))
#define	DIF_INSTR_XLATE(op, r, d)	(((op) << 24) | ((r) << 8) | (d))

#define	DIF_REG_R0	0		/* %r0 is always set to zero */

/*
 * A DTrace Intermediate Format Type (DIF Type) is used to represent the types
 * of variables, function and associative array arguments, and the return type
 * for each DIF object (shown below).  It contains a description of the type,
 * its size in bytes, and a module identifier.
 */
typedef struct dtrace_diftype {
	uint8_t dtdt_kind;		/* type kind (see below) */
	uint8_t dtdt_ckind;		/* type kind in CTF */
	uint8_t dtdt_flags;		/* type flags (see below) */
	uint8_t dtdt_pad;		/* reserved for future use */
	uint32_t dtdt_size;		/* type size in bytes (unless string) */
} dtrace_diftype_t;

#define	DIF_TYPE_CTF		0	/* type is a CTF type */
#define	DIF_TYPE_STRING		1	/* type is a D string */

#define	DIF_TF_BYREF		0x1	/* type is passed by reference */
#define	DIF_TF_BYUREF		0x2	/* user type is passed by reference */

/*
 * A DTrace Intermediate Format variable record is used to describe each of the
 * variables referenced by a given DIF object.  It contains an integer variable
 * identifier along with variable scope and properties, as shown below.  The
 * size of this structure must be sizeof (int) aligned.
 */
typedef struct dtrace_difv {
	uint32_t dtdv_name;		/* variable name index in dtdo_strtab */
	uint32_t dtdv_id;		/* variable reference identifier */
	uint8_t dtdv_kind;		/* variable kind (see below) */
	uint8_t dtdv_scope;		/* variable scope (see below) */
	uint16_t dtdv_flags;		/* variable flags (see below) */
	dtrace_diftype_t dtdv_type;	/* variable type (see above) */
} dtrace_difv_t;

#define	DIFV_KIND_ARRAY		0	/* variable is an array of quantities */
#define	DIFV_KIND_SCALAR	1	/* variable is a scalar quantity */

#define	DIFV_SCOPE_GLOBAL	0	/* variable has global scope */
#define	DIFV_SCOPE_THREAD	1	/* variable has thread scope */
#define	DIFV_SCOPE_LOCAL	2	/* variable has local scope */

#define	DIFV_F_REF		0x1	/* variable is referenced by DIFO */
#define	DIFV_F_MOD		0x2	/* variable is written by DIFO */

/*
 * DTrace Actions
 *
 * The upper byte determines the class of the action; the low bytes determines
 * the specific action within that class.  The classes of actions are as
 * follows:
 *
 *   [ no class ]                  <= May record process- or kernel-related data
 *   DTRACEACT_PROC                <= Only records process-related data
 *   DTRACEACT_PROC_DESTRUCTIVE    <= Potentially destructive to processes
 *   DTRACEACT_KERNEL              <= Only records kernel-related data
 *   DTRACEACT_KERNEL_DESTRUCTIVE  <= Potentially destructive to the kernel
 *   DTRACEACT_SPECULATIVE         <= Speculation-related action
 *   DTRACEACT_AGGREGATION         <= Aggregating action
 */
#define	DTRACEACT_NONE			0	/* no action */
#define	DTRACEACT_DIFEXPR		1	/* action is DIF expression */
#define	DTRACEACT_EXIT			2	/* exit() action */
#define	DTRACEACT_PRINTF		3	/* printf() action */
#define	DTRACEACT_PRINTA		4	/* printa() action */
#define	DTRACEACT_LIBACT		5	/* library-controlled action */
#define	DTRACEACT_TRACEMEM		6	/* tracemem() action */
#define	DTRACEACT_TRACEMEM_DYNSIZE	7	/* dynamic tracemem() size */
#define	DTRACEACT_PRINTM		8	/* printm() action (BSD) */

#define	DTRACEACT_PROC			0x0100
#define	DTRACEACT_USTACK		(DTRACEACT_PROC + 1)
#define	DTRACEACT_JSTACK		(DTRACEACT_PROC + 2)
#define	DTRACEACT_USYM			(DTRACEACT_PROC + 3)
#define	DTRACEACT_UMOD			(DTRACEACT_PROC + 4)
#define	DTRACEACT_UADDR			(DTRACEACT_PROC + 5)

#define	DTRACEACT_PROC_DESTRUCTIVE	0x0200
#define	DTRACEACT_STOP			(DTRACEACT_PROC_DESTRUCTIVE + 1)
#define	DTRACEACT_RAISE			(DTRACEACT_PROC_DESTRUCTIVE + 2)
#define	DTRACEACT_SYSTEM		(DTRACEACT_PROC_DESTRUCTIVE + 3)
#define	DTRACEACT_FREOPEN		(DTRACEACT_PROC_DESTRUCTIVE + 4)

#define	DTRACEACT_PROC_CONTROL		0x0300

#define	DTRACEACT_KERNEL		0x0400
#define	DTRACEACT_STACK			(DTRACEACT_KERNEL + 1)
#define	DTRACEACT_SYM			(DTRACEACT_KERNEL + 2)
#define	DTRACEACT_MOD			(DTRACEACT_KERNEL + 3)

#define	DTRACEACT_KERNEL_DESTRUCTIVE	0x0500
#define	DTRACEACT_BREAKPOINT		(DTRACEACT_KERNEL_DESTRUCTIVE + 1)
#define	DTRACEACT_PANIC			(DTRACEACT_KERNEL_DESTRUCTIVE + 2)
#define	DTRACEACT_CHILL			(DTRACEACT_KERNEL_DESTRUCTIVE + 3)

#define	DTRACEACT_SPECULATIVE		0x0600
#define	DTRACEACT_SPECULATE		(DTRACEACT_SPECULATIVE + 1)
#define	DTRACEACT_COMMIT		(DTRACEACT_SPECULATIVE + 2)
#define	DTRACEACT_DISCARD		(DTRACEACT_SPECULATIVE + 3)

#define	DTRACEACT_CLASS(x)		((x) & 0xff00)

#define	DTRACEACT_ISDESTRUCTIVE(x)	\
	(DTRACEACT_CLASS(x) == DTRACEACT_PROC_DESTRUCTIVE || \
	DTRACEACT_CLASS(x) == DTRACEACT_KERNEL_DESTRUCTIVE)

#define	DTRACEACT_ISSPECULATIVE(x)	\
	(DTRACEACT_CLASS(x) == DTRACEACT_SPECULATIVE)

#define	DTRACEACT_ISPRINTFLIKE(x)	\
	((x) == DTRACEACT_PRINTF || (x) == DTRACEACT_PRINTA || \
	(x) == DTRACEACT_SYSTEM || (x) == DTRACEACT_FREOPEN)

/*
 * DTrace Aggregating Actions
 *
 * These are functions f(x) for which the following is true:
 *
 *    f(f(x_0) U f(x_1) U ... U f(x_n)) = f(x_0 U x_1 U ... U x_n)
 *
 * where x_n is a set of arbitrary data.  Aggregating actions are in their own
 * DTrace action class, DTTRACEACT_AGGREGATION.  The macros provided here allow
 * for easier processing of the aggregation argument and data payload for a few
 * aggregating actions (notably:  quantize(), lquantize(), and ustack()).
 */
#define	DTRACEACT_AGGREGATION		0x0700
#define	DTRACEAGG_COUNT			(DTRACEACT_AGGREGATION + 1)
#define	DTRACEAGG_MIN			(DTRACEACT_AGGREGATION + 2)
#define	DTRACEAGG_MAX			(DTRACEACT_AGGREGATION + 3)
#define	DTRACEAGG_AVG			(DTRACEACT_AGGREGATION + 4)
#define	DTRACEAGG_SUM			(DTRACEACT_AGGREGATION + 5)
#define	DTRACEAGG_STDDEV		(DTRACEACT_AGGREGATION + 6)
#define	DTRACEAGG_QUANTIZE		(DTRACEACT_AGGREGATION + 7)
#define	DTRACEAGG_LQUANTIZE		(DTRACEACT_AGGREGATION + 8)
#define	DTRACEAGG_LLQUANTIZE		(DTRACEACT_AGGREGATION + 9)

#define	DTRACEACT_ISAGG(x)		\
	(DTRACEACT_CLASS(x) == DTRACEACT_AGGREGATION)

#define	DTRACE_QUANTIZE_NBUCKETS	\
	(((sizeof (uint64_t) * NBBY) - 1) * 2 + 1)

#define	DTRACE_QUANTIZE_ZEROBUCKET	((sizeof (uint64_t) * NBBY) - 1)

#define	DTRACE_QUANTIZE_BUCKETVAL(buck)					\
	(int64_t)((buck) < DTRACE_QUANTIZE_ZEROBUCKET ?			\
	-(1LL << (DTRACE_QUANTIZE_ZEROBUCKET - 1 - (buck))) :		\
	(buck) == DTRACE_QUANTIZE_ZEROBUCKET ? 0 :			\
	1LL << ((buck) - DTRACE_QUANTIZE_ZEROBUCKET - 1))

#define	DTRACE_LQUANTIZE_STEPSHIFT		48
#define	DTRACE_LQUANTIZE_STEPMASK		((uint64_t)UINT16_MAX << 48)
#define	DTRACE_LQUANTIZE_LEVELSHIFT		32
#define	DTRACE_LQUANTIZE_LEVELMASK		((uint64_t)UINT16_MAX << 32)
#define	DTRACE_LQUANTIZE_BASESHIFT		0
#define	DTRACE_LQUANTIZE_BASEMASK		UINT32_MAX

#define	DTRACE_LQUANTIZE_STEP(x)		\
	(uint16_t)(((x) & DTRACE_LQUANTIZE_STEPMASK) >> \
	DTRACE_LQUANTIZE_STEPSHIFT)

#define	DTRACE_LQUANTIZE_LEVELS(x)		\
	(uint16_t)(((x) & DTRACE_LQUANTIZE_LEVELMASK) >> \
	DTRACE_LQUANTIZE_LEVELSHIFT)

#define	DTRACE_LQUANTIZE_BASE(x)		\
	(int32_t)(((x) & DTRACE_LQUANTIZE_BASEMASK) >> \
	DTRACE_LQUANTIZE_BASESHIFT)

#define	DTRACE_LLQUANTIZE_FACTORSHIFT		48
#define	DTRACE_LLQUANTIZE_FACTORMASK		((uint64_t)UINT16_MAX << 48)
#define	DTRACE_LLQUANTIZE_LOWSHIFT		32
#define	DTRACE_LLQUANTIZE_LOWMASK		((uint64_t)UINT16_MAX << 32)
#define	DTRACE_LLQUANTIZE_HIGHSHIFT		16
#define	DTRACE_LLQUANTIZE_HIGHMASK		((uint64_t)UINT16_MAX << 16)
#define	DTRACE_LLQUANTIZE_NSTEPSHIFT		0
#define	DTRACE_LLQUANTIZE_NSTEPMASK		UINT16_MAX

#define	DTRACE_LLQUANTIZE_FACTOR(x)		\
	(uint16_t)(((x) & DTRACE_LLQUANTIZE_FACTORMASK) >> \
	DTRACE_LLQUANTIZE_FACTORSHIFT)

#define	DTRACE_LLQUANTIZE_LOW(x)		\
	(uint16_t)(((x) & DTRACE_LLQUANTIZE_LOWMASK) >> \
	DTRACE_LLQUANTIZE_LOWSHIFT)

#define	DTRACE_LLQUANTIZE_HIGH(x)		\
	(uint16_t)(((x) & DTRACE_LLQUANTIZE_HIGHMASK) >> \
	DTRACE_LLQUANTIZE_HIGHSHIFT)

#define	DTRACE_LLQUANTIZE_NSTEP(x)		\
	(uint16_t)(((x) & DTRACE_LLQUANTIZE_NSTEPMASK) >> \
	DTRACE_LLQUANTIZE_NSTEPSHIFT)

#define	DTRACE_USTACK_NFRAMES(x)	(uint32_t)((x) & UINT32_MAX)
#define	DTRACE_USTACK_STRSIZE(x)	(uint32_t)((x) >> 32)
#define	DTRACE_USTACK_ARG(x, y)		\
	((((uint64_t)(y)) << 32) | ((x) & UINT32_MAX))

#ifndef _LP64
#if BYTE_ORDER == _BIG_ENDIAN
#define	DTRACE_PTR(type, name)	uint32_t name##pad; type *name
#else
#define	DTRACE_PTR(type, name)	type *name; uint32_t name##pad
#endif
#else
#define	DTRACE_PTR(type, name)	type *name
#endif

/*
 * DTrace Object Format (DOF)
 *
 * DTrace programs can be persistently encoded in the DOF format so that they
 * may be embedded in other programs (for example, in an ELF file) or in the
 * dtrace driver configuration file for use in anonymous tracing.  The DOF
 * format is versioned and extensible so that it can be revised and so that
 * internal data structures can be modified or extended compatibly.  All DOF
 * structures use fixed-size types, so the 32-bit and 64-bit representations
 * are identical and consumers can use either data model transparently.
 *
 * The file layout is structured as follows:
 *
 * +---------------+-------------------+----- ... ----+---- ... ------+
 * |   dof_hdr_t   |  dof_sec_t[ ... ] |   loadable   | non-loadable  |
 * | (file header) | (section headers) | section data | section data  |
 * +---------------+-------------------+----- ... ----+---- ... ------+
 * |<------------ dof_hdr.dofh_loadsz --------------->|               |
 * |<------------ dof_hdr.dofh_filesz ------------------------------->|
 *
 * The file header stores meta-data including a magic number, data model for
 * the instrumentation, data encoding, and properties of the DIF code within.
 * The header describes its own size and the size of the section headers.  By
 * convention, an array of section headers follows the file header, and then
 * the data for all loadable sections and unloadable sections.  This permits
 * consumer code to easily download the headers and all loadable data into the
 * DTrace driver in one contiguous chunk, omitting other extraneous sections.
 *
 * The section headers describe the size, offset, alignment, and section type
 * for each section.  Sections are described using a set of #defines that tell
 * the consumer what kind of data is expected.  Sections can contain links to
 * other sections by storing a dof_secidx_t, an index into the section header
 * array, inside of the section data structures.  The section header includes
 * an entry size so that sections with data arrays can grow their structures.
 *
 * The DOF data itself can contain many snippets of DIF (i.e. >1 DIFOs), which
 * are represented themselves as a collection of related DOF sections.  This
 * permits us to change the set of sections associated with a DIFO over time,
 * and also permits us to encode DIFOs that contain different sets of sections.
 * When a DOF section wants to refer to a DIFO, it stores the dof_secidx_t of a
 * section of type DOF_SECT_DIFOHDR.  This section's data is then an array of
 * dof_secidx_t's which in turn denote the sections associated with this DIFO.
 *
 * This loose coupling of the file structure (header and sections) to the
 * structure of the DTrace program itself (ECB descriptions, action
 * descriptions, and DIFOs) permits activities such as relocation processing
 * to occur in a single pass without having to understand D program structure.
 *
 * Finally, strings are always stored in ELF-style string tables along with a
 * string table section index and string table offset.  Therefore strings in
 * DOF are always arbitrary-length and not bound to the current implementation.
 */

#define	DOF_ID_SIZE	16	/* total size of dofh_ident[] in bytes */

typedef struct dof_hdr {
	uint8_t dofh_ident[DOF_ID_SIZE]; /* identification bytes (see below) */
	uint32_t dofh_flags;		/* file attribute flags (if any) */
	uint32_t dofh_hdrsize;		/* size of file header in bytes */
	uint32_t dofh_secsize;		/* size of section header in bytes */
	uint32_t dofh_secnum;		/* number of section headers */
	uint64_t dofh_secoff;		/* file offset of section headers */
	uint64_t dofh_loadsz;		/* file size of loadable portion */
	uint64_t dofh_filesz;		/* file size of entire DOF file */
	uint64_t dofh_pad;		/* reserved for future use */
} dof_hdr_t;

#define	DOF_ID_MAG0	0	/* first byte of magic number */
#define	DOF_ID_MAG1	1	/* second byte of magic number */
#define	DOF_ID_MAG2	2	/* third byte of magic number */
#define	DOF_ID_MAG3	3	/* fourth byte of magic number */
#define	DOF_ID_MODEL	4	/* DOF data model (see below) */
#define	DOF_ID_ENCODING	5	/* DOF data encoding (see below) */
#define	DOF_ID_VERSION	6	/* DOF file format major version (see below) */
#define	DOF_ID_DIFVERS	7	/* DIF instruction set version */
#define	DOF_ID_DIFIREG	8	/* DIF integer registers used by compiler */
#define	DOF_ID_DIFTREG	9	/* DIF tuple registers used by compiler */
#define	DOF_ID_PAD	10	/* start of padding bytes (all zeroes) */

#define	DOF_MAG_MAG0	0x7F	/* DOF_ID_MAG[0-3] */
#define	DOF_MAG_MAG1	'D'
#define	DOF_MAG_MAG2	'O'
#define	DOF_MAG_MAG3	'F'

#define	DOF_MAG_STRING	"\177DOF"
#define	DOF_MAG_STRLEN	4

#define	DOF_MODEL_NONE	0	/* DOF_ID_MODEL */
#define	DOF_MODEL_ILP32	1
#define	DOF_MODEL_LP64	2

#ifdef _LP64
#define	DOF_MODEL_NATIVE	DOF_MODEL_LP64
#else
#define	DOF_MODEL_NATIVE	DOF_MODEL_ILP32
#endif

#define	DOF_ENCODE_NONE	0	/* DOF_ID_ENCODING */
#define	DOF_ENCODE_LSB	1
#define	DOF_ENCODE_MSB	2

#if BYTE_ORDER == _BIG_ENDIAN
#define	DOF_ENCODE_NATIVE	DOF_ENCODE_MSB
#else
#define	DOF_ENCODE_NATIVE	DOF_ENCODE_LSB
#endif

#define	DOF_VERSION_1	1	/* DOF version 1: Solaris 10 FCS */
#define	DOF_VERSION_2	2	/* DOF version 2: Solaris Express 6/06 */
#define	DOF_VERSION	DOF_VERSION_2	/* Latest DOF version */

#define	DOF_FL_VALID	0	/* mask of all valid dofh_flags bits */

typedef uint32_t dof_secidx_t;	/* section header table index type */
typedef uint32_t dof_stridx_t;	/* string table index type */

#define	DOF_SECIDX_NONE	(-1U)	/* null value for section indices */
#define	DOF_STRIDX_NONE	(-1U)	/* null value for string indices */

typedef struct dof_sec {
	uint32_t dofs_type;	/* section type (see below) */
	uint32_t dofs_align;	/* section data memory alignment */
	uint32_t dofs_flags;	/* section flags (if any) */
	uint32_t dofs_entsize;	/* size of section entry (if table) */
	uint64_t dofs_offset;	/* offset of section data within file */
	uint64_t dofs_size;	/* size of section data in bytes */
} dof_sec_t;

#define	DOF_SECT_NONE		0	/* null section */
#define	DOF_SECT_COMMENTS	1	/* compiler comments */
#define	DOF_SECT_SOURCE		2	/* D program source code */
#define	DOF_SECT_ECBDESC	3	/* dof_ecbdesc_t */
#define	DOF_SECT_PROBEDESC	4	/* dof_probedesc_t */
#define	DOF_SECT_ACTDESC	5	/* dof_actdesc_t array */
#define	DOF_SECT_DIFOHDR	6	/* dof_difohdr_t (variable length) */
#define	DOF_SECT_DIF		7	/* uint32_t array of byte code */
#define	DOF_SECT_STRTAB		8	/* string table */
#define	DOF_SECT_VARTAB		9	/* dtrace_difv_t array */
#define	DOF_SECT_RELTAB		10	/* dof_relodesc_t array */
#define	DOF_SECT_TYPTAB		11	/* dtrace_diftype_t array */
#define	DOF_SECT_URELHDR	12	/* dof_relohdr_t (user relocations) */
#define	DOF_SECT_KRELHDR	13	/* dof_relohdr_t (kernel relocations) */
#define	DOF_SECT_OPTDESC	14	/* dof_optdesc_t array */
#define	DOF_SECT_PROVIDER	15	/* dof_provider_t */
#define	DOF_SECT_PROBES		16	/* dof_probe_t array */
#define	DOF_SECT_PRARGS		17	/* uint8_t array (probe arg mappings) */
#define	DOF_SECT_PROFFS		18	/* uint32_t array (probe arg offsets) */
#define	DOF_SECT_INTTAB		19	/* uint64_t array */
#define	DOF_SECT_UTSNAME	20	/* struct utsname */
#define	DOF_SECT_XLTAB		21	/* dof_xlref_t array */
#define	DOF_SECT_XLMEMBERS	22	/* dof_xlmember_t array */
#define	DOF_SECT_XLIMPORT	23	/* dof_xlator_t */
#define	DOF_SECT_XLEXPORT	24	/* dof_xlator_t */
#define	DOF_SECT_PREXPORT	25	/* dof_secidx_t array (exported objs) */
#define	DOF_SECT_PRENOFFS	26	/* uint32_t array (enabled offsets) */

#define	DOF_SECF_LOAD		1	/* section should be loaded */

#define	DOF_SEC_ISLOADABLE(x)						\
	(((x) == DOF_SECT_ECBDESC) || ((x) == DOF_SECT_PROBEDESC) ||	\
	((x) == DOF_SECT_ACTDESC) || ((x) == DOF_SECT_DIFOHDR) ||	\
	((x) == DOF_SECT_DIF) || ((x) == DOF_SECT_STRTAB) ||		\
	((x) == DOF_SECT_VARTAB) || ((x) == DOF_SECT_RELTAB) ||		\
	((x) == DOF_SECT_TYPTAB) || ((x) == DOF_SECT_URELHDR) ||	\
	((x) == DOF_SECT_KRELHDR) || ((x) == DOF_SECT_OPTDESC) ||	\
	((x) == DOF_SECT_PROVIDER) || ((x) == DOF_SECT_PROBES) ||	\
	((x) == DOF_SECT_PRARGS) || ((x) == DOF_SECT_PROFFS) ||		\
	((x) == DOF_SECT_INTTAB) || ((x) == DOF_SECT_XLTAB) ||		\
	((x) == DOF_SECT_XLMEMBERS) || ((x) == DOF_SECT_XLIMPORT) ||	\
	((x) == DOF_SECT_XLEXPORT) ||  ((x) == DOF_SECT_PREXPORT) || 	\
	((x) == DOF_SECT_PRENOFFS))

typedef struct dof_ecbdesc {
	dof_secidx_t dofe_probes;	/* link to DOF_SECT_PROBEDESC */
	dof_secidx_t dofe_pred;		/* link to DOF_SECT_DIFOHDR */
	dof_secidx_t dofe_actions;	/* link to DOF_SECT_ACTDESC */
	uint32_t dofe_pad;		/* reserved for future use */
	uint64_t dofe_uarg;		/* user-supplied library argument */
} dof_ecbdesc_t;

typedef struct dof_probedesc {
	dof_secidx_t dofp_strtab;	/* link to DOF_SECT_STRTAB section */
	dof_stridx_t dofp_provider;	/* provider string */
	dof_stridx_t dofp_mod;		/* module string */
	dof_stridx_t dofp_func;		/* function string */
	dof_stridx_t dofp_name;		/* name string */
	uint32_t dofp_id;		/* probe identifier (or zero) */
} dof_probedesc_t;

typedef struct dof_actdesc {
	dof_secidx_t dofa_difo;		/* link to DOF_SECT_DIFOHDR */
	dof_secidx_t dofa_strtab;	/* link to DOF_SECT_STRTAB section */
	uint32_t dofa_kind;		/* action kind (DTRACEACT_* constant) */
	uint32_t dofa_ntuple;		/* number of subsequent tuple actions */
	uint64_t dofa_arg;		/* kind-specific argument */
	uint64_t dofa_uarg;		/* user-supplied argument */
} dof_actdesc_t;

typedef struct dof_difohdr {
	dtrace_diftype_t dofd_rtype;	/* return type for this fragment */
	dof_secidx_t dofd_links[1];	/* variable length array of indices */
} dof_difohdr_t;

typedef struct dof_relohdr {
	dof_secidx_t dofr_strtab;	/* link to DOF_SECT_STRTAB for names */
	dof_secidx_t dofr_relsec;	/* link to DOF_SECT_RELTAB for relos */
	dof_secidx_t dofr_tgtsec;	/* link to section we are relocating */
} dof_relohdr_t;

typedef struct dof_relodesc {
	dof_stridx_t dofr_name;		/* string name of relocation symbol */
	uint32_t dofr_type;		/* relo type (DOF_RELO_* constant) */
	uint64_t dofr_offset;		/* byte offset for relocation */
	uint64_t dofr_data;		/* additional type-specific data */
} dof_relodesc_t;

#define	DOF_RELO_NONE	0		/* empty relocation entry */
#define	DOF_RELO_SETX	1		/* relocate setx value */
#define	DOF_RELO_DOFREL	2		/* relocate DOF-relative value */

typedef struct dof_optdesc {
	uint32_t dofo_option;		/* option identifier */
	dof_secidx_t dofo_strtab;	/* string table, if string option */
	uint64_t dofo_value;		/* option value or string index */
} dof_optdesc_t;

typedef uint32_t dof_attr_t;		/* encoded stability attributes */

#define	DOF_ATTR(n, d, c)	(((n) << 24) | ((d) << 16) | ((c) << 8))
#define	DOF_ATTR_NAME(a)	(((a) >> 24) & 0xff)
#define	DOF_ATTR_DATA(a)	(((a) >> 16) & 0xff)
#define	DOF_ATTR_CLASS(a)	(((a) >>  8) & 0xff)

typedef struct dof_provider {
	dof_secidx_t dofpv_strtab;	/* link to DOF_SECT_STRTAB section */
	dof_secidx_t dofpv_probes;	/* link to DOF_SECT_PROBES section */
	dof_secidx_t dofpv_prargs;	/* link to DOF_SECT_PRARGS section */
	dof_secidx_t dofpv_proffs;	/* link to DOF_SECT_PROFFS section */
	dof_stridx_t dofpv_name;	/* provider name string */
	dof_attr_t dofpv_provattr;	/* provider attributes */
	dof_attr_t dofpv_modattr;	/* module attributes */
	dof_attr_t dofpv_funcattr;	/* function attributes */
	dof_attr_t dofpv_nameattr;	/* name attributes */
	dof_attr_t dofpv_argsattr;	/* args attributes */
	dof_secidx_t dofpv_prenoffs;	/* link to DOF_SECT_PRENOFFS section */
} dof_provider_t;

typedef struct dof_probe {
	uint64_t dofpr_addr;		/* probe base address or offset */
	dof_stridx_t dofpr_func;	/* probe function string */
	dof_stridx_t dofpr_name;	/* probe name string */
	dof_stridx_t dofpr_nargv;	/* native argument type strings */
	dof_stridx_t dofpr_xargv;	/* translated argument type strings */
	uint32_t dofpr_argidx;		/* index of first argument mapping */
	uint32_t dofpr_offidx;		/* index of first offset entry */
	uint8_t dofpr_nargc;		/* native argument count */
	uint8_t dofpr_xargc;		/* translated argument count */
	uint16_t dofpr_noffs;		/* number of offset entries for probe */
	uint32_t dofpr_enoffidx;	/* index of first is-enabled offset */
	uint16_t dofpr_nenoffs;		/* number of is-enabled offsets */
	uint16_t dofpr_pad1;		/* reserved for future use */
	uint32_t dofpr_pad2;		/* reserved for future use */
} dof_probe_t;

typedef struct dof_xlator {
	dof_secidx_t dofxl_members;	/* link to DOF_SECT_XLMEMBERS section */
	dof_secidx_t dofxl_strtab;	/* link to DOF_SECT_STRTAB section */
	dof_stridx_t dofxl_argv;	/* input parameter type strings */
	uint32_t dofxl_argc;		/* input parameter list length */
	dof_stridx_t dofxl_type;	/* output type string name */
	dof_attr_t dofxl_attr;		/* output stability attributes */
} dof_xlator_t;

typedef struct dof_xlmember {
	dof_secidx_t dofxm_difo;	/* member link to DOF_SECT_DIFOHDR */
	dof_stridx_t dofxm_name;	/* member name */
	dtrace_diftype_t dofxm_type;	/* member type */
} dof_xlmember_t;

typedef struct dof_xlref {
	dof_secidx_t dofxr_xlator;	/* link to DOF_SECT_XLATORS section */
	uint32_t dofxr_member;		/* index of referenced dof_xlmember */
	uint32_t dofxr_argn;		/* index of argument for DIF_OP_XLARG */
} dof_xlref_t;

/*
 * DTrace Intermediate Format Object (DIFO)
 *
 * A DIFO is used to store the compiled DIF for a D expression, its return
 * type, and its string and variable tables.  The string table is a single
 * buffer of character data into which sets instructions and variable
 * references can reference strings using a byte offset.  The variable table
 * is an array of dtrace_difv_t structures that describe the name and type of
 * each variable and the id used in the DIF code.  This structure is described
 * above in the DIF section of this header file.  The DIFO is used at both
 * user-level (in the library) and in the kernel, but the structure is never
 * passed between the two: the DOF structures form the only interface.  As a
 * result, the definition can change depending on the presence of _KERNEL.
 */
typedef struct dtrace_difo {
	dif_instr_t *dtdo_buf;		/* instruction buffer */
	uint64_t *dtdo_inttab;		/* integer table (optional) */
	char *dtdo_strtab;		/* string table (optional) */
	dtrace_difv_t *dtdo_vartab;	/* variable table (optional) */
	uint_t dtdo_len;		/* length of instruction buffer */
	uint_t dtdo_intlen;		/* length of integer table */
	uint_t dtdo_strlen;		/* length of string table */
	uint_t dtdo_varlen;		/* length of variable table */
	dtrace_diftype_t dtdo_rtype;	/* return type */
	uint_t dtdo_refcnt;		/* owner reference count */
	uint_t dtdo_destructive;	/* invokes destructive subroutines */
#ifndef _KERNEL
	dof_relodesc_t *dtdo_kreltab;	/* kernel relocations */
	dof_relodesc_t *dtdo_ureltab;	/* user relocations */
	struct dt_node **dtdo_xlmtab;	/* translator references */
	uint_t dtdo_krelen;		/* length of krelo table */
	uint_t dtdo_urelen;		/* length of urelo table */
	uint_t dtdo_xlmlen;		/* length of translator table */
#endif
} dtrace_difo_t;

/*
 * DTrace Enabling Description Structures
 *
 * When DTrace is tracking the description of a DTrace enabling entity (probe,
 * predicate, action, ECB, record, etc.), it does so in a description
 * structure.  These structures all end in "desc", and are used at both
 * user-level and in the kernel -- but (with the exception of
 * dtrace_probedesc_t) they are never passed between them.  Typically,
 * user-level will use the description structures when assembling an enabling.
 * It will then distill those description structures into a DOF object (see
 * above), and send it into the kernel.  The kernel will again use the
 * description structures to create a description of the enabling as it reads
 * the DOF.  When the description is complete, the enabling will be actually
 * created -- turning it into the structures that represent the enabling
 * instead of merely describing it.  Not surprisingly, the description
 * structures bear a strong resemblance to the DOF structures that act as their
 * conduit.
 */
struct dtrace_predicate;

typedef struct dtrace_probedesc {
	dtrace_id_t dtpd_id;			/* probe identifier */
	char dtpd_provider[DTRACE_PROVNAMELEN]; /* probe provider name */
	char dtpd_mod[DTRACE_MODNAMELEN];	/* probe module name */
	char dtpd_func[DTRACE_FUNCNAMELEN];	/* probe function name */
	char dtpd_name[DTRACE_NAMELEN];		/* probe name */
} dtrace_probedesc_t;

typedef struct dtrace_repldesc {
	dtrace_probedesc_t dtrpd_match;		/* probe descr. to match */
	dtrace_probedesc_t dtrpd_create;	/* probe descr. to create */
} dtrace_repldesc_t;

typedef struct dtrace_preddesc {
	dtrace_difo_t *dtpdd_difo;		/* pointer to DIF object */
	struct dtrace_predicate *dtpdd_predicate; /* pointer to predicate */
} dtrace_preddesc_t;

typedef struct dtrace_actdesc {
	dtrace_difo_t *dtad_difo;		/* pointer to DIF object */
	struct dtrace_actdesc *dtad_next;	/* next action */
	dtrace_actkind_t dtad_kind;		/* kind of action */
	uint32_t dtad_ntuple;			/* number in tuple */
	uint64_t dtad_arg;			/* action argument */
	uint64_t dtad_uarg;			/* user argument */
	int dtad_refcnt;			/* reference count */
} dtrace_actdesc_t;

typedef struct dtrace_ecbdesc {
	dtrace_actdesc_t *dted_action;		/* action description(s) */
	dtrace_preddesc_t dted_pred;		/* predicate description */
	dtrace_probedesc_t dted_probe;		/* probe description */
	uint64_t dted_uarg;			/* library argument */
	int dted_refcnt;			/* reference count */
} dtrace_ecbdesc_t;

/*
 * DTrace Metadata Description Structures
 *
 * DTrace separates the trace data stream from the metadata stream.  The only
 * metadata tokens placed in the data stream are the dtrace_rechdr_t (EPID +
 * timestamp) or (in the case of aggregations) aggregation identifiers.  To
 * determine the structure of the data, DTrace consumers pass the token to the
 * kernel, and receive in return a corresponding description of the enabled
 * probe (via the dtrace_eprobedesc structure) or the aggregation (via the
 * dtrace_aggdesc structure).  Both of these structures are expressed in terms
 * of record descriptions (via the dtrace_recdesc structure) that describe the
 * exact structure of the data.  Some record descriptions may also contain a
 * format identifier; this additional bit of metadata can be retrieved from the
 * kernel, for which a format description is returned via the dtrace_fmtdesc
 * structure.  Note that all four of these structures must be bitness-neutral
 * to allow for a 32-bit DTrace consumer on a 64-bit kernel.
 */
typedef struct dtrace_recdesc {
	dtrace_actkind_t dtrd_action;		/* kind of action */
	uint32_t dtrd_size;			/* size of record */
	uint32_t dtrd_offset;			/* offset in ECB's data */
	uint16_t dtrd_alignment;		/* required alignment */
	uint16_t dtrd_format;			/* format, if any */
	uint64_t dtrd_arg;			/* action argument */
	uint64_t dtrd_uarg;			/* user argument */
} dtrace_recdesc_t;

typedef struct dtrace_eprobedesc {
	dtrace_epid_t dtepd_epid;		/* enabled probe ID */
	dtrace_id_t dtepd_probeid;		/* probe ID */
	uint64_t dtepd_uarg;			/* library argument */
	uint32_t dtepd_size;			/* total size */
	int dtepd_nrecs;			/* number of records */
	dtrace_recdesc_t dtepd_rec[1];		/* records themselves */
} dtrace_eprobedesc_t;

typedef struct dtrace_aggdesc {
	DTRACE_PTR(char, dtagd_name);		/* not filled in by kernel */
	dtrace_aggvarid_t dtagd_varid;		/* not filled in by kernel */
	int dtagd_flags;			/* not filled in by kernel */
	dtrace_aggid_t dtagd_id;		/* aggregation ID */
	dtrace_epid_t dtagd_epid;		/* enabled probe ID */
	uint32_t dtagd_size;			/* size in bytes */
	int dtagd_nrecs;			/* number of records */
	uint32_t dtagd_pad;			/* explicit padding */
	dtrace_recdesc_t dtagd_rec[1];		/* record descriptions */
} dtrace_aggdesc_t;

typedef struct dtrace_fmtdesc {
	DTRACE_PTR(char, dtfd_string);		/* format string */
	int dtfd_length;			/* length of format string */
	uint16_t dtfd_format;			/* format identifier */
} dtrace_fmtdesc_t;

#define	DTRACE_SIZEOF_EPROBEDESC(desc)				\
	(sizeof (dtrace_eprobedesc_t) + ((desc)->dtepd_nrecs ?	\
	(((desc)->dtepd_nrecs - 1) * sizeof (dtrace_recdesc_t)) : 0))

#define	DTRACE_SIZEOF_AGGDESC(desc)				\
	(sizeof (dtrace_aggdesc_t) + ((desc)->dtagd_nrecs ?	\
	(((desc)->dtagd_nrecs - 1) * sizeof (dtrace_recdesc_t)) : 0))

/*
 * DTrace Option Interface
 *
 * Run-time DTrace options are set and retrieved via DOF_SECT_OPTDESC sections
 * in a DOF image.  The dof_optdesc structure contains an option identifier and
 * an option value.  The valid option identifiers are found below; the mapping
 * between option identifiers and option identifying strings is maintained at
 * user-level.  Note that the value of DTRACEOPT_UNSET is such that all of the
 * following are potentially valid option values:  all positive integers, zero
 * and negative one.  Some options (notably "bufpolicy" and "bufresize") take
 * predefined tokens as their values; these are defined with
 * DTRACEOPT_{option}_{token}.
 */
#define	DTRACEOPT_BUFSIZE	0	/* buffer size */
#define	DTRACEOPT_BUFPOLICY	1	/* buffer policy */
#define	DTRACEOPT_DYNVARSIZE	2	/* dynamic variable size */
#define	DTRACEOPT_AGGSIZE	3	/* aggregation size */
#define	DTRACEOPT_SPECSIZE	4	/* speculation size */
#define	DTRACEOPT_NSPEC		5	/* number of speculations */
#define	DTRACEOPT_STRSIZE	6	/* string size */
#define	DTRACEOPT_CLEANRATE	7	/* dynvar cleaning rate */
#define	DTRACEOPT_CPU		8	/* CPU to trace */
#define	DTRACEOPT_BUFRESIZE	9	/* buffer resizing policy */
#define	DTRACEOPT_GRABANON	10	/* grab anonymous state, if any */
#define	DTRACEOPT_FLOWINDENT	11	/* indent function entry/return */
#define	DTRACEOPT_QUIET		12	/* only output explicitly traced data */
#define	DTRACEOPT_STACKFRAMES	13	/* number of stack frames */
#define	DTRACEOPT_USTACKFRAMES	14	/* number of user stack frames */
#define	DTRACEOPT_AGGRATE	15	/* aggregation snapshot rate */
#define	DTRACEOPT_SWITCHRATE	16	/* buffer switching rate */
#define	DTRACEOPT_STATUSRATE	17	/* status rate */
#define	DTRACEOPT_DESTRUCTIVE	18	/* destructive actions allowed */
#define	DTRACEOPT_STACKINDENT	19	/* output indent for stack traces */
#define	DTRACEOPT_RAWBYTES	20	/* always print bytes in raw form */
#define	DTRACEOPT_JSTACKFRAMES	21	/* number of jstack() frames */
#define	DTRACEOPT_JSTACKSTRSIZE	22	/* size of jstack() string table */
#define	DTRACEOPT_AGGSORTKEY	23	/* sort aggregations by key */
#define	DTRACEOPT_AGGSORTREV	24	/* reverse-sort aggregations */
#define	DTRACEOPT_AGGSORTPOS	25	/* agg. position to sort on */
#define	DTRACEOPT_AGGSORTKEYPOS	26	/* agg. key position to sort on */
#define	DTRACEOPT_TEMPORAL	27	/* temporally ordered output */
#define	DTRACEOPT_AGGHIST	28	/* histogram aggregation output */
#define	DTRACEOPT_AGGPACK	29	/* packed aggregation output */
#define	DTRACEOPT_AGGZOOM	30	/* zoomed aggregation scaling */
#define	DTRACEOPT_ZONE		31	/* zone in which to enable probes */
#define	DTRACEOPT_MAX		32	/* number of options */

#define	DTRACEOPT_UNSET		(dtrace_optval_t)-2	/* unset option */

#define	DTRACEOPT_BUFPOLICY_RING	0	/* ring buffer */
#define	DTRACEOPT_BUFPOLICY_FILL	1	/* fill buffer, then stop */
#define	DTRACEOPT_BUFPOLICY_SWITCH	2	/* switch buffers */

#define	DTRACEOPT_BUFRESIZE_AUTO	0	/* automatic resizing */
#define	DTRACEOPT_BUFRESIZE_MANUAL	1	/* manual resizing */

/*
 * DTrace Buffer Interface
 *
 * In order to get a snapshot of the principal or aggregation buffer,
 * user-level passes a buffer description to the kernel with the dtrace_bufdesc
 * structure.  This describes which CPU user-level is interested in, and
 * where user-level wishes the kernel to snapshot the buffer to (the
 * dtbd_data field).  The kernel uses the same structure to pass back some
 * information regarding the buffer:  the size of data actually copied out, the
 * number of drops, the number of errors, the offset of the oldest record,
 * and the time of the snapshot.
 *
 * If the buffer policy is a "switch" policy, taking a snapshot of the
 * principal buffer has the additional effect of switching the active and
 * inactive buffers.  Taking a snapshot of the aggregation buffer _always_ has
 * the additional effect of switching the active and inactive buffers.
 */
typedef struct dtrace_bufdesc {
	uint64_t dtbd_size;			/* size of buffer */
	uint32_t dtbd_cpu;			/* CPU or DTRACE_CPUALL */
	uint32_t dtbd_errors;			/* number of errors */
	uint64_t dtbd_drops;			/* number of drops */
	DTRACE_PTR(char, dtbd_data);		/* data */
	uint64_t dtbd_oldest;			/* offset of oldest record */
	uint64_t dtbd_timestamp;		/* hrtime of snapshot */
} dtrace_bufdesc_t;

/*
 * Each record in the buffer (dtbd_data) begins with a header that includes
 * the epid and a timestamp.  The timestamp is split into two 4-byte parts
 * so that we do not require 8-byte alignment.
 */
typedef struct dtrace_rechdr {
	dtrace_epid_t dtrh_epid;		/* enabled probe id */
	uint32_t dtrh_timestamp_hi;		/* high bits of hrtime_t */
	uint32_t dtrh_timestamp_lo;		/* low bits of hrtime_t */
} dtrace_rechdr_t;

#define	DTRACE_RECORD_LOAD_TIMESTAMP(dtrh)			\
	((dtrh)->dtrh_timestamp_lo +				\
	((uint64_t)(dtrh)->dtrh_timestamp_hi << 32))

#define	DTRACE_RECORD_STORE_TIMESTAMP(dtrh, hrtime) {		\
	(dtrh)->dtrh_timestamp_lo = (uint32_t)hrtime;		\
	(dtrh)->dtrh_timestamp_hi = hrtime >> 32;		\
}

/*
 * DTrace Status
 *
 * The status of DTrace is relayed via the dtrace_status structure.  This
 * structure contains members to count drops other than the capacity drops
 * available via the buffer interface (see above).  This consists of dynamic
 * drops (including capacity dynamic drops, rinsing drops and dirty drops), and
 * speculative drops (including capacity speculative drops, drops due to busy
 * speculative buffers and drops due to unavailable speculative buffers).
 * Additionally, the status structure contains a field to indicate the number
 * of "fill"-policy buffers have been filled and a boolean field to indicate
 * that exit() has been called.  If the dtst_exiting field is non-zero, no
 * further data will be generated until tracing is stopped (at which time any
 * enablings of the END action will be processed); if user-level sees that
 * this field is non-zero, tracing should be stopped as soon as possible.
 */
typedef struct dtrace_status {
	uint64_t dtst_dyndrops;			/* dynamic drops */
	uint64_t dtst_dyndrops_rinsing;		/* dyn drops due to rinsing */
	uint64_t dtst_dyndrops_dirty;		/* dyn drops due to dirty */
	uint64_t dtst_specdrops;		/* speculative drops */
	uint64_t dtst_specdrops_busy;		/* spec drops due to busy */
	uint64_t dtst_specdrops_unavail;	/* spec drops due to unavail */
	uint64_t dtst_errors;			/* total errors */
	uint64_t dtst_filled;			/* number of filled bufs */
	uint64_t dtst_stkstroverflows;		/* stack string tab overflows */
	uint64_t dtst_dblerrors;		/* errors in ERROR probes */
	char dtst_killed;			/* non-zero if killed */
	char dtst_exiting;			/* non-zero if exit() called */
	char dtst_pad[6];			/* pad out to 64-bit align */
} dtrace_status_t;

/*
 * DTrace Configuration
 *
 * User-level may need to understand some elements of the kernel DTrace
 * configuration in order to generate correct DIF.  This information is
 * conveyed via the dtrace_conf structure.
 */
typedef struct dtrace_conf {
	uint_t dtc_difversion;			/* supported DIF version */
	uint_t dtc_difintregs;			/* # of DIF integer registers */
	uint_t dtc_diftupregs;			/* # of DIF tuple registers */
	uint_t dtc_ctfmodel;			/* CTF data model */
	uint_t dtc_pad[8];			/* reserved for future use */
} dtrace_conf_t;

/*
 * DTrace Faults
 *
 * The constants below DTRACEFLT_LIBRARY indicate probe processing faults;
 * constants at or above DTRACEFLT_LIBRARY indicate faults in probe
 * postprocessing at user-level.  Probe processing faults induce an ERROR
 * probe and are replicated in unistd.d to allow users' ERROR probes to decode
 * the error condition using thse symbolic labels.
 */
#define	DTRACEFLT_UNKNOWN		0	/* Unknown fault */
#define	DTRACEFLT_BADADDR		1	/* Bad address */
#define	DTRACEFLT_BADALIGN		2	/* Bad alignment */
#define	DTRACEFLT_ILLOP			3	/* Illegal operation */
#define	DTRACEFLT_DIVZERO		4	/* Divide-by-zero */
#define	DTRACEFLT_NOSCRATCH		5	/* Out of scratch space */
#define	DTRACEFLT_KPRIV			6	/* Illegal kernel access */
#define	DTRACEFLT_UPRIV			7	/* Illegal user access */
#define	DTRACEFLT_TUPOFLOW		8	/* Tuple stack overflow */
#define	DTRACEFLT_BADSTACK		9	/* Bad stack */

#define	DTRACEFLT_LIBRARY		1000	/* Library-level fault */

/*
 * DTrace Argument Types
 *
 * Because it would waste both space and time, argument types do not reside
 * with the probe.  In order to determine argument types for args[X]
 * variables, the D compiler queries for argument types on a probe-by-probe
 * basis.  (This optimizes for the common case that arguments are either not
 * used or used in an untyped fashion.)  Typed arguments are specified with a
 * string of the type name in the dtragd_native member of the argument
 * description structure.  Typed arguments may be further translated to types
 * of greater stability; the provider indicates such a translated argument by
 * filling in the dtargd_xlate member with the string of the translated type.
 * Finally, the provider may indicate which argument value a given argument
 * maps to by setting the dtargd_mapping member -- allowing a single argument
 * to map to multiple args[X] variables.
 */
typedef struct dtrace_argdesc {
	dtrace_id_t dtargd_id;			/* probe identifier */
	int dtargd_ndx;				/* arg number (-1 iff none) */
	int dtargd_mapping;			/* value mapping */
	char dtargd_native[DTRACE_ARGTYPELEN];	/* native type name */
	char dtargd_xlate[DTRACE_ARGTYPELEN];	/* translated type name */
} dtrace_argdesc_t;

/*
 * DTrace Stability Attributes
 *
 * Each DTrace provider advertises the name and data stability of each of its
 * probe description components, as well as its architectural dependencies.
 * The D compiler can query the provider attributes (dtrace_pattr_t below) in
 * order to compute the properties of an input program and report them.
 */
typedef uint8_t dtrace_stability_t;	/* stability code (see attributes(5)) */
typedef uint8_t dtrace_class_t;		/* architectural dependency class */

#define	DTRACE_STABILITY_INTERNAL	0	/* private to DTrace itself */
#define	DTRACE_STABILITY_PRIVATE	1	/* private to Sun (see docs) */
#define	DTRACE_STABILITY_OBSOLETE	2	/* scheduled for removal */
#define	DTRACE_STABILITY_EXTERNAL	3	/* not controlled by Sun */
#define	DTRACE_STABILITY_UNSTABLE	4	/* new or rapidly changing */
#define	DTRACE_STABILITY_EVOLVING	5	/* less rapidly changing */
#define	DTRACE_STABILITY_STABLE		6	/* mature interface from Sun */
#define	DTRACE_STABILITY_STANDARD	7	/* industry standard */
#define	DTRACE_STABILITY_MAX		7	/* maximum valid stability */

#define	DTRACE_CLASS_UNKNOWN	0	/* unknown architectural dependency */
#define	DTRACE_CLASS_CPU	1	/* CPU-module-specific */
#define	DTRACE_CLASS_PLATFORM	2	/* platform-specific (uname -i) */
#define	DTRACE_CLASS_GROUP	3	/* hardware-group-specific (uname -m) */
#define	DTRACE_CLASS_ISA	4	/* ISA-specific (uname -p) */
#define	DTRACE_CLASS_COMMON	5	/* common to all systems */
#define	DTRACE_CLASS_MAX	5	/* maximum valid class */

#define	DTRACE_PRIV_NONE	0x0000
#define	DTRACE_PRIV_KERNEL	0x0001
#define	DTRACE_PRIV_USER	0x0002
#define	DTRACE_PRIV_PROC	0x0004
#define	DTRACE_PRIV_OWNER	0x0008
#define	DTRACE_PRIV_ZONEOWNER	0x0010

#define	DTRACE_PRIV_ALL	\
	(DTRACE_PRIV_KERNEL | DTRACE_PRIV_USER | \
	DTRACE_PRIV_PROC | DTRACE_PRIV_OWNER | DTRACE_PRIV_ZONEOWNER)

typedef struct dtrace_ppriv {
	uint32_t dtpp_flags;			/* privilege flags */
	uid_t dtpp_uid;				/* user ID */
	zoneid_t dtpp_zoneid;			/* zone ID */
} dtrace_ppriv_t;

typedef struct dtrace_attribute {
	dtrace_stability_t dtat_name;		/* entity name stability */
	dtrace_stability_t dtat_data;		/* entity data stability */
	dtrace_class_t dtat_class;		/* entity data dependency */
} dtrace_attribute_t;

typedef struct dtrace_pattr {
	dtrace_attribute_t dtpa_provider;	/* provider attributes */
	dtrace_attribute_t dtpa_mod;		/* module attributes */
	dtrace_attribute_t dtpa_func;		/* function attributes */
	dtrace_attribute_t dtpa_name;		/* name attributes */
	dtrace_attribute_t dtpa_args;		/* args[] attributes */
} dtrace_pattr_t;

typedef struct dtrace_providerdesc {
	char dtvd_name[DTRACE_PROVNAMELEN];	/* provider name */
	dtrace_pattr_t dtvd_attr;		/* stability attributes */
	dtrace_ppriv_t dtvd_priv;		/* privileges required */
} dtrace_providerdesc_t;

/*
 * DTrace Pseudodevice Interface
 *
 * DTrace is controlled through ioctl(2)'s to the in-kernel dtrace:dtrace
 * pseudodevice driver.  These ioctls comprise the user-kernel interface to
 * DTrace.
 */
#ifdef illumos
#define	DTRACEIOC		(('d' << 24) | ('t' << 16) | ('r' << 8))
#define	DTRACEIOC_PROVIDER	(DTRACEIOC | 1)		/* provider query */
#define	DTRACEIOC_PROBES	(DTRACEIOC | 2)		/* probe query */
#define	DTRACEIOC_BUFSNAP	(DTRACEIOC | 4)		/* snapshot buffer */
#define	DTRACEIOC_PROBEMATCH	(DTRACEIOC | 5)		/* match probes */
#define	DTRACEIOC_ENABLE	(DTRACEIOC | 6)		/* enable probes */
#define	DTRACEIOC_AGGSNAP	(DTRACEIOC | 7)		/* snapshot agg. */
#define	DTRACEIOC_EPROBE	(DTRACEIOC | 8)		/* get eprobe desc. */
#define	DTRACEIOC_PROBEARG	(DTRACEIOC | 9)		/* get probe arg */
#define	DTRACEIOC_CONF		(DTRACEIOC | 10)	/* get config. */
#define	DTRACEIOC_STATUS	(DTRACEIOC | 11)	/* get status */
#define	DTRACEIOC_GO		(DTRACEIOC | 12)	/* start tracing */
#define	DTRACEIOC_STOP		(DTRACEIOC | 13)	/* stop tracing */
#define	DTRACEIOC_AGGDESC	(DTRACEIOC | 15)	/* get agg. desc. */
#define	DTRACEIOC_FORMAT	(DTRACEIOC | 16)	/* get format str */
#define	DTRACEIOC_DOFGET	(DTRACEIOC | 17)	/* get DOF */
#define	DTRACEIOC_REPLICATE	(DTRACEIOC | 18)	/* replicate enab */
#else
#define	DTRACEIOC_PROVIDER	_IOWR('x',1,dtrace_providerdesc_t)
							/* provider query */
#define	DTRACEIOC_PROBES	_IOWR('x',2,dtrace_probedesc_t)
							/* probe query */
#define	DTRACEIOC_BUFSNAP	_IOW('x',4,dtrace_bufdesc_t *)	
							/* snapshot buffer */
#define	DTRACEIOC_PROBEMATCH	_IOWR('x',5,dtrace_probedesc_t)
							/* match probes */
typedef struct {
	void	*dof;		/* DOF userland address written to driver. */
	int	n_matched;	/* # matches returned by driver. */
} dtrace_enable_io_t;
#define	DTRACEIOC_ENABLE	_IOWR('x',6,dtrace_enable_io_t)
							/* enable probes */
#define	DTRACEIOC_AGGSNAP	_IOW('x',7,dtrace_bufdesc_t *)
							/* snapshot agg. */
#define	DTRACEIOC_EPROBE	_IOW('x',8,dtrace_eprobedesc_t)
							/* get eprobe desc. */
#define	DTRACEIOC_PROBEARG	_IOWR('x',9,dtrace_argdesc_t)
							/* get probe arg */
#define	DTRACEIOC_CONF		_IOR('x',10,dtrace_conf_t)
							/* get config. */
#define	DTRACEIOC_STATUS	_IOR('x',11,dtrace_status_t)
							/* get status */
#define	DTRACEIOC_GO		_IOR('x',12,processorid_t)
							/* start tracing */
#define	DTRACEIOC_STOP		_IOWR('x',13,processorid_t)
							/* stop tracing */
#define	DTRACEIOC_AGGDESC	_IOW('x',15,dtrace_aggdesc_t *)	
							/* get agg. desc. */
#define	DTRACEIOC_FORMAT	_IOWR('x',16,dtrace_fmtdesc_t)	
							/* get format str */
#define	DTRACEIOC_DOFGET	_IOW('x',17,dof_hdr_t *)
							/* get DOF */
#define	DTRACEIOC_REPLICATE	_IOW('x',18,dtrace_repldesc_t)	
							/* replicate enab */
#endif

/*
 * DTrace Helpers
 *
 * In general, DTrace establishes probes in processes and takes actions on
 * processes without knowing their specific user-level structures.  Instead of
 * existing in the framework, process-specific knowledge is contained by the
 * enabling D program -- which can apply process-specific knowledge by making
 * appropriate use of DTrace primitives like copyin() and copyinstr() to
 * operate on user-level data.  However, there may exist some specific probes
 * of particular semantic relevance that the application developer may wish to
 * explicitly export.  For example, an application may wish to export a probe
 * at the point that it begins and ends certain well-defined transactions.  In
 * addition to providing probes, programs may wish to offer assistance for
 * certain actions.  For example, in highly dynamic environments (e.g., Java),
 * it may be difficult to obtain a stack trace in terms of meaningful symbol
 * names (the translation from instruction addresses to corresponding symbol
 * names may only be possible in situ); these environments may wish to define
 * a series of actions to be applied in situ to obtain a meaningful stack
 * trace.
 *
 * These two mechanisms -- user-level statically defined tracing and assisting
 * DTrace actions -- are provided via DTrace _helpers_.  Helpers are specified
 * via DOF, but unlike enabling DOF, helper DOF may contain definitions of
 * providers, probes and their arguments.  If a helper wishes to provide
 * action assistance, probe descriptions and corresponding DIF actions may be
 * specified in the helper DOF.  For such helper actions, however, the probe
 * description describes the specific helper:  all DTrace helpers have the
 * provider name "dtrace" and the module name "helper", and the name of the
 * helper is contained in the function name (for example, the ustack() helper
 * is named "ustack").  Any helper-specific name may be contained in the name
 * (for example, if a helper were to have a constructor, it might be named
 * "dtrace:helper:<helper>:init").  Helper actions are only called when the
 * action that they are helping is taken.  Helper actions may only return DIF
 * expressions, and may only call the following subroutines:
 *
 *    alloca()      <= Allocates memory out of the consumer's scratch space
 *    bcopy()       <= Copies memory to scratch space
 *    copyin()      <= Copies memory from user-level into consumer's scratch
 *    copyinto()    <= Copies memory into a specific location in scratch
 *    copyinstr()   <= Copies a string into a specific location in scratch
 *
 * Helper actions may only access the following built-in variables:
 *
 *    curthread     <= Current kthread_t pointer
 *    tid           <= Current thread identifier
 *    pid           <= Current process identifier
 *    ppid          <= Parent process identifier
 *    uid           <= Current user ID
 *    gid           <= Current group ID
 *    execname      <= Current executable name
 *    zonename      <= Current zone name
 *
 * Helper actions may not manipulate or allocate dynamic variables, but they
 * may have clause-local and statically-allocated global variables.  The
 * helper action variable state is specific to the helper action -- variables
 * used by the helper action may not be accessed outside of the helper
 * action, and the helper action may not access variables that like outside
 * of it.  Helper actions may not load from kernel memory at-large; they are
 * restricting to loading current user state (via copyin() and variants) and
 * scratch space.  As with probe enablings, helper actions are executed in
 * program order.  The result of the helper action is the result of the last
 * executing helper expression.
 *
 * Helpers -- composed of either providers/probes or probes/actions (or both)
 * -- are added by opening the "helper" minor node, and issuing an ioctl(2)
 * (DTRACEHIOC_ADDDOF) that specifies the dof_helper_t structure. This
 * encapsulates the name and base address of the user-level library or
 * executable publishing the helpers and probes as well as the DOF that
 * contains the definitions of those helpers and probes.
 *
 * The DTRACEHIOC_ADD and DTRACEHIOC_REMOVE are left in place for legacy
 * helpers and should no longer be used.  No other ioctls are valid on the
 * helper minor node.
 */
#ifdef illumos
#define	DTRACEHIOC		(('d' << 24) | ('t' << 16) | ('h' << 8))
#define	DTRACEHIOC_ADD		(DTRACEHIOC | 1)	/* add helper */
#define	DTRACEHIOC_REMOVE	(DTRACEHIOC | 2)	/* remove helper */
#define	DTRACEHIOC_ADDDOF	(DTRACEHIOC | 3)	/* add helper DOF */
#else
#define	DTRACEHIOC_REMOVE	_IOW('z', 2, int)	/* remove helper */
#define	DTRACEHIOC_ADDDOF	_IOWR('z', 3, dof_helper_t)/* add helper DOF */
#endif

typedef struct dof_helper {
	char dofhp_mod[DTRACE_MODNAMELEN];	/* executable or library name */
	uint64_t dofhp_addr;			/* base address of object */
	uint64_t dofhp_dof;			/* address of helper DOF */
#ifdef __FreeBSD__
	pid_t dofhp_pid;			/* target process ID */
	int dofhp_gen;
#endif
} dof_helper_t;

#define	DTRACEMNR_DTRACE	"dtrace"	/* node for DTrace ops */
#define	DTRACEMNR_HELPER	"helper"	/* node for helpers */
#define	DTRACEMNRN_DTRACE	0		/* minor for DTrace ops */
#define	DTRACEMNRN_HELPER	1		/* minor for helpers */
#define	DTRACEMNRN_CLONE	2		/* first clone minor */

#ifdef _KERNEL

/*
 * DTrace Provider API
 *
 * The following functions are implemented by the DTrace framework and are
 * used to implement separate in-kernel DTrace providers.  Common functions
 * are provided in uts/common/os/dtrace.c.  ISA-dependent subroutines are
 * defined in uts/<isa>/dtrace/dtrace_asm.s or uts/<isa>/dtrace/dtrace_isa.c.
 *
 * The provider API has two halves:  the API that the providers consume from
 * DTrace, and the API that providers make available to DTrace.
 *
 * 1 Framework-to-Provider API
 *
 * 1.1  Overview
 *
 * The Framework-to-Provider API is represented by the dtrace_pops structure
 * that the provider passes to the framework when registering itself.  This
 * structure consists of the following members:
 *
 *   dtps_provide()          <-- Provide all probes, all modules
 *   dtps_provide_module()   <-- Provide all probes in specified module
 *   dtps_enable()           <-- Enable specified probe
 *   dtps_disable()          <-- Disable specified probe
 *   dtps_suspend()          <-- Suspend specified probe
 *   dtps_resume()           <-- Resume specified probe
 *   dtps_getargdesc()       <-- Get the argument description for args[X]
 *   dtps_getargval()        <-- Get the value for an argX or args[X] variable
 *   dtps_usermode()         <-- Find out if the probe was fired in user mode
 *   dtps_destroy()          <-- Destroy all state associated with this probe
 *
 * 1.2  void dtps_provide(void *arg, const dtrace_probedesc_t *spec)
 *
 * 1.2.1  Overview
 *
 *   Called to indicate that the provider should provide all probes.  If the
 *   specified description is non-NULL, dtps_provide() is being called because
 *   no probe matched a specified probe -- if the provider has the ability to
 *   create custom probes, it may wish to create a probe that matches the
 *   specified description.
 *
 * 1.2.2  Arguments and notes
 *
 *   The first argument is the cookie as passed to dtrace_register().  The
 *   second argument is a pointer to a probe description that the provider may
 *   wish to consider when creating custom probes.  The provider is expected to
 *   call back into the DTrace framework via dtrace_probe_create() to create
 *   any necessary probes.  dtps_provide() may be called even if the provider
 *   has made available all probes; the provider should check the return value
 *   of dtrace_probe_create() to handle this case.  Note that the provider need
 *   not implement both dtps_provide() and dtps_provide_module(); see
 *   "Arguments and Notes" for dtrace_register(), below.
 *
 * 1.2.3  Return value
 *
 *   None.
 *
 * 1.2.4  Caller's context
 *
 *   dtps_provide() is typically called from open() or ioctl() context, but may
 *   be called from other contexts as well.  The DTrace framework is locked in
 *   such a way that providers may not register or unregister.  This means that
 *   the provider may not call any DTrace API that affects its registration with
 *   the framework, including dtrace_register(), dtrace_unregister(),
 *   dtrace_invalidate(), and dtrace_condense().  However, the context is such
 *   that the provider may (and indeed, is expected to) call probe-related
 *   DTrace routines, including dtrace_probe_create(), dtrace_probe_lookup(),
 *   and dtrace_probe_arg().
 *
 * 1.3  void dtps_provide_module(void *arg, modctl_t *mp)
 *
 * 1.3.1  Overview
 *
 *   Called to indicate that the provider should provide all probes in the
 *   specified module.
 *
 * 1.3.2  Arguments and notes
 *
 *   The first argument is the cookie as passed to dtrace_register().  The
 *   second argument is a pointer to a modctl structure that indicates the
 *   module for which probes should be created.
 *
 * 1.3.3  Return value
 *
 *   None.
 *
 * 1.3.4  Caller's context
 *
 *   dtps_provide_module() may be called from open() or ioctl() context, but
 *   may also be called from a module loading context.  mod_lock is held, and
 *   the DTrace framework is locked in such a way that providers may not
 *   register or unregister.  This means that the provider may not call any
 *   DTrace API that affects its registration with the framework, including
 *   dtrace_register(), dtrace_unregister(), dtrace_invalidate(), and
 *   dtrace_condense().  However, the context is such that the provider may (and
 *   indeed, is expected to) call probe-related DTrace routines, including
 *   dtrace_probe_create(), dtrace_probe_lookup(), and dtrace_probe_arg().  Note
 *   that the provider need not implement both dtps_provide() and
 *   dtps_provide_module(); see "Arguments and Notes" for dtrace_register(),
 *   below.
 *
 * 1.4  void dtps_enable(void *arg, dtrace_id_t id, void *parg)
 *
 * 1.4.1  Overview
 *
 *   Called to enable the specified probe.
 *
 * 1.4.2  Arguments and notes
 *
 *   The first argument is the cookie as passed to dtrace_register().  The
 *   second argument is the identifier of the probe to be enabled.  The third
 *   argument is the probe argument as passed to dtrace_probe_create().
 *   dtps_enable() will be called when a probe transitions from not being
 *   enabled at all to having one or more ECB.  The number of ECBs associated
 *   with the probe may change without subsequent calls into the provider.
 *   When the number of ECBs drops to zero, the provider will be explicitly
 *   told to disable the probe via dtps_disable().  dtrace_probe() should never
 *   be called for a probe identifier that hasn't been explicitly enabled via
 *   dtps_enable().
 *
 * 1.4.3  Return value
 *
 *   None.
 *
 * 1.4.4  Caller's context
 *
 *   The DTrace framework is locked in such a way that it may not be called
 *   back into at all.  cpu_lock is held.  mod_lock is not held and may not
 *   be acquired.
 *
 * 1.5  void dtps_disable(void *arg, dtrace_id_t id, void *parg)
 *
 * 1.5.1  Overview
 *
 *   Called to disable the specified probe.
 *
 * 1.5.2  Arguments and notes
 *
 *   The first argument is the cookie as passed to dtrace_register().  The
 *   second argument is the identifier of the probe to be disabled.  The third
 *   argument is the probe argument as passed to dtrace_probe_create().
 *   dtps_disable() will be called when a probe transitions from being enabled
 *   to having zero ECBs.  dtrace_probe() should never be called for a probe
 *   identifier that has been explicitly enabled via dtps_disable().
 *
 * 1.5.3  Return value
 *
 *   None.
 *
 * 1.5.4  Caller's context
 *
 *   The DTrace framework is locked in such a way that it may not be called
 *   back into at all.  cpu_lock is held.  mod_lock is not held and may not
 *   be acquired.
 *
 * 1.6  void dtps_suspend(void *arg, dtrace_id_t id, void *parg)
 *
 * 1.6.1  Overview
 *
 *   Called to suspend the specified enabled probe.  This entry point is for
 *   providers that may need to suspend some or all of their probes when CPUs
 *   are being powered on or when the boot monitor is being entered for a
 *   prolonged period of time.
 *
 * 1.6.2  Arguments and notes
 *
 *   The first argument is the cookie as passed to dtrace_register().  The
 *   second argument is the identifier of the probe to be suspended.  The
 *   third argument is the probe argument as passed to dtrace_probe_create().
 *   dtps_suspend will only be called on an enabled probe.  Providers that
 *   provide a dtps_suspend entry point will want to take roughly the action
 *   that it takes for dtps_disable.
 *
 * 1.6.3  Return value
 *
 *   None.
 *
 * 1.6.4  Caller's context
 *
 *   Interrupts are disabled.  The DTrace framework is in a state such that the
 *   specified probe cannot be disabled or destroyed for the duration of
 *   dtps_suspend().  As interrupts are disabled, the provider is afforded
 *   little latitude; the provider is expected to do no more than a store to
 *   memory.
 *
 * 1.7  void dtps_resume(void *arg, dtrace_id_t id, void *parg)
 *
 * 1.7.1  Overview
 *
 *   Called to resume the specified enabled probe.  This entry point is for
 *   providers that may need to resume some or all of their probes after the
 *   completion of an event that induced a call to dtps_suspend().
 *
 * 1.7.2  Arguments and notes
 *
 *   The first argument is the cookie as passed to dtrace_register().  The
 *   second argument is the identifier of the probe to be resumed.  The
 *   third argument is the probe argument as passed to dtrace_probe_create().
 *   dtps_resume will only be called on an enabled probe.  Providers that
 *   provide a dtps_resume entry point will want to take roughly the action
 *   that it takes for dtps_enable.
 *
 * 1.7.3  Return value
 *
 *   None.
 *
 * 1.7.4  Caller's context
 *
 *   Interrupts are disabled.  The DTrace framework is in a state such that the
 *   specified probe cannot be disabled or destroyed for the duration of
 *   dtps_resume().  As interrupts are disabled, the provider is afforded
 *   little latitude; the provider is expected to do no more than a store to
 *   memory.
 *
 * 1.8  void dtps_getargdesc(void *arg, dtrace_id_t id, void *parg,
 *           dtrace_argdesc_t *desc)
 *
 * 1.8.1  Overview
 *
 *   Called to retrieve the argument description for an args[X] variable.
 *
 * 1.8.2  Arguments and notes
 *
 *   The first argument is the cookie as passed to dtrace_register(). The
 *   second argument is the identifier of the current probe. The third
 *   argument is the probe argument as passed to dtrace_probe_create(). The
 *   fourth argument is a pointer to the argument description.  This
 *   description is both an input and output parameter:  it contains the
 *   index of the desired argument in the dtargd_ndx field, and expects
 *   the other fields to be filled in upon return.  If there is no argument
 *   corresponding to the specified index, the dtargd_ndx field should be set
 *   to DTRACE_ARGNONE.
 *
 * 1.8.3  Return value
 *
 *   None.  The dtargd_ndx, dtargd_native, dtargd_xlate and dtargd_mapping
 *   members of the dtrace_argdesc_t structure are all output values.
 *
 * 1.8.4  Caller's context
 *
 *   dtps_getargdesc() is called from ioctl() context. mod_lock is held, and
 *   the DTrace framework is locked in such a way that providers may not
 *   register or unregister.  This means that the provider may not call any
 *   DTrace API that affects its registration with the framework, including
 *   dtrace_register(), dtrace_unregister(), dtrace_invalidate(), and
 *   dtrace_condense().
 *
 * 1.9  uint64_t dtps_getargval(void *arg, dtrace_id_t id, void *parg,
 *               int argno, int aframes)
 *
 * 1.9.1  Overview
 *
 *   Called to retrieve a value for an argX or args[X] variable.
 *
 * 1.9.2  Arguments and notes
 *
 *   The first argument is the cookie as passed to dtrace_register(). The
 *   second argument is the identifier of the current probe. The third
 *   argument is the probe argument as passed to dtrace_probe_create(). The
 *   fourth argument is the number of the argument (the X in the example in
 *   1.9.1). The fifth argument is the number of stack frames that were used
 *   to get from the actual place in the code that fired the probe to
 *   dtrace_probe() itself, the so-called artificial frames. This argument may
 *   be used to descend an appropriate number of frames to find the correct
 *   values. If this entry point is left NULL, the dtrace_getarg() built-in
 *   function is used.
 *
 * 1.9.3  Return value
 *
 *   The value of the argument.
 *
 * 1.9.4  Caller's context
 *
 *   This is called from within dtrace_probe() meaning that interrupts
 *   are disabled. No locks should be taken within this entry point.
 *
 * 1.10  int dtps_usermode(void *arg, dtrace_id_t id, void *parg)
 *
 * 1.10.1  Overview
 *
 *   Called to determine if the probe was fired in a user context.
 *
 * 1.10.2  Arguments and notes
 *
 *   The first argument is the cookie as passed to dtrace_register(). The
 *   second argument is the identifier of the current probe. The third
 *   argument is the probe argument as passed to dtrace_probe_create().  This
 *   entry point must not be left NULL for providers whose probes allow for
 *   mixed mode tracing, that is to say those probes that can fire during
 *   kernel- _or_ user-mode execution
 *
 * 1.10.3  Return value
 *
 *   A bitwise OR that encapsulates both the mode (either DTRACE_MODE_KERNEL
 *   or DTRACE_MODE_USER) and the policy when the privilege of the enabling
 *   is insufficient for that mode (a combination of DTRACE_MODE_NOPRIV_DROP,
 *   DTRACE_MODE_NOPRIV_RESTRICT, and DTRACE_MODE_LIMITEDPRIV_RESTRICT).  If
 *   DTRACE_MODE_NOPRIV_DROP bit is set, insufficient privilege will result
 *   in the probe firing being silently ignored for the enabling; if the
 *   DTRACE_NODE_NOPRIV_RESTRICT bit is set, insufficient privilege will not
 *   prevent probe processing for the enabling, but restrictions will be in
 *   place that induce a UPRIV fault upon attempt to examine probe arguments
 *   or current process state.  If the DTRACE_MODE_LIMITEDPRIV_RESTRICT bit
 *   is set, similar restrictions will be placed upon operation if the
 *   privilege is sufficient to process the enabling, but does not otherwise
 *   entitle the enabling to all zones.  The DTRACE_MODE_NOPRIV_DROP and
 *   DTRACE_MODE_NOPRIV_RESTRICT are mutually exclusive (and one of these
 *   two policies must be specified), but either may be combined (or not)
 *   with DTRACE_MODE_LIMITEDPRIV_RESTRICT.
 *
 * 1.10.4  Caller's context
 *
 *   This is called from within dtrace_probe() meaning that interrupts
 *   are disabled. No locks should be taken within this entry point.
 *
 * 1.11 void dtps_destroy(void *arg, dtrace_id_t id, void *parg)
 *
 * 1.11.1 Overview
 *
 *   Called to destroy the specified probe.
 *
 * 1.11.2 Arguments and notes
 *
 *   The first argument is the cookie as passed to dtrace_register().  The
 *   second argument is the identifier of the probe to be destroyed.  The third
 *   argument is the probe argument as passed to dtrace_probe_create().  The
 *   provider should free all state associated with the probe.  The framework
 *   guarantees that dtps_destroy() is only called for probes that have either
 *   been disabled via dtps_disable() or were never enabled via dtps_enable().
 *   Once dtps_disable() has been called for a probe, no further call will be
 *   made specifying the probe.
 *
 * 1.11.3 Return value
 *
 *   None.
 *
 * 1.11.4 Caller's context
 *
 *   The DTrace framework is locked in such a way that it may not be called
 *   back into at all.  mod_lock is held.  cpu_lock is not held, and may not be
 *   acquired.
 *
 *
 * 2 Provider-to-Framework API
 *
 * 2.1  Overview
 *
 * The Provider-to-Framework API provides the mechanism for the provider to
 * register itself with the DTrace framework, to create probes, to lookup
 * probes and (most importantly) to fire probes.  The Provider-to-Framework
 * consists of:
 *
 *   dtrace_register()       <-- Register a provider with the DTrace framework
 *   dtrace_unregister()     <-- Remove a provider's DTrace registration
 *   dtrace_invalidate()     <-- Invalidate the specified provider
 *   dtrace_condense()       <-- Remove a provider's unenabled probes
 *   dtrace_attached()       <-- Indicates whether or not DTrace has attached
 *   dtrace_probe_create()   <-- Create a DTrace probe
 *   dtrace_probe_lookup()   <-- Lookup a DTrace probe based on its name
 *   dtrace_probe_arg()      <-- Return the probe argument for a specific probe
 *   dtrace_probe()          <-- Fire the specified probe
 *
 * 2.2  int dtrace_register(const char *name, const dtrace_pattr_t *pap,
 *          uint32_t priv, cred_t *cr, const dtrace_pops_t *pops, void *arg,
 *          dtrace_provider_id_t *idp)
 *
 * 2.2.1  Overview
 *
 *   dtrace_register() registers the calling provider with the DTrace
 *   framework.  It should generally be called by DTrace providers in their
 *   attach(9E) entry point.
 *
 * 2.2.2  Arguments and Notes
 *
 *   The first argument is the name of the provider.  The second argument is a
 *   pointer to the stability attributes for the provider.  The third argument
 *   is the privilege flags for the provider, and must be some combination of:
 *
 *     DTRACE_PRIV_NONE     <= All users may enable probes from this provider
 *
 *     DTRACE_PRIV_PROC     <= Any user with privilege of PRIV_DTRACE_PROC may
 *                             enable probes from this provider
 *
 *     DTRACE_PRIV_USER     <= Any user with privilege of PRIV_DTRACE_USER may
 *                             enable probes from this provider
 *
 *     DTRACE_PRIV_KERNEL   <= Any user with privilege of PRIV_DTRACE_KERNEL
 *                             may enable probes from this provider
 *
 *     DTRACE_PRIV_OWNER    <= This flag places an additional constraint on
 *                             the privilege requirements above. These probes
 *                             require either (a) a user ID matching the user
 *                             ID of the cred passed in the fourth argument
 *                             or (b) the PRIV_PROC_OWNER privilege.
 *
 *     DTRACE_PRIV_ZONEOWNER<= This flag places an additional constraint on
 *                             the privilege requirements above. These probes
 *                             require either (a) a zone ID matching the zone
 *                             ID of the cred passed in the fourth argument
 *                             or (b) the PRIV_PROC_ZONE privilege.
 *
 *   Note that these flags designate the _visibility_ of the probes, not
 *   the conditions under which they may or may not fire.
 *
 *   The fourth argument is the credential that is associated with the
 *   provider.  This argument should be NULL if the privilege flags don't
 *   include DTRACE_PRIV_OWNER or DTRACE_PRIV_ZONEOWNER.  If non-NULL, the
 *   framework stashes the uid and zoneid represented by this credential
 *   for use at probe-time, in implicit predicates.  These limit visibility
 *   of the probes to users and/or zones which have sufficient privilege to
 *   access them.
 *
 *   The fifth argument is a DTrace provider operations vector, which provides
 *   the implementation for the Framework-to-Provider API.  (See Section 1,
 *   above.)  This must be non-NULL, and each member must be non-NULL.  The
 *   exceptions to this are (1) the dtps_provide() and dtps_provide_module()
 *   members (if the provider so desires, _one_ of these members may be left
 *   NULL -- denoting that the provider only implements the other) and (2)
 *   the dtps_suspend() and dtps_resume() members, which must either both be
 *   NULL or both be non-NULL.
 *
 *   The sixth argument is a cookie to be specified as the first argument for
 *   each function in the Framework-to-Provider API.  This argument may have
 *   any value.
 *
 *   The final argument is a pointer to dtrace_provider_id_t.  If
 *   dtrace_register() successfully completes, the provider identifier will be
 *   stored in the memory pointed to be this argument.  This argument must be
 *   non-NULL.
 *
 * 2.2.3  Return value
 *
 *   On success, dtrace_register() returns 0 and stores the new provider's
 *   identifier into the memory pointed to by the idp argument.  On failure,
 *   dtrace_register() returns an errno:
 *
 *     EINVAL   The arguments passed to dtrace_register() were somehow invalid.
 *              This may because a parameter that must be non-NULL was NULL,
 *              because the name was invalid (either empty or an illegal
 *              provider name) or because the attributes were invalid.
 *
 *   No other failure code is returned.
 *
 * 2.2.4  Caller's context
 *
 *   dtrace_register() may induce calls to dtrace_provide(); the provider must
 *   hold no locks across dtrace_register() that may also be acquired by
 *   dtrace_provide().  cpu_lock and mod_lock must not be held.
 *
 * 2.3  int dtrace_unregister(dtrace_provider_t id)
 *
 * 2.3.1  Overview
 *
 *   Unregisters the specified provider from the DTrace framework.  It should
 *   generally be called by DTrace providers in their detach(9E) entry point.
 *
 * 2.3.2  Arguments and Notes
 *
 *   The only argument is the provider identifier, as returned from a
 *   successful call to dtrace_register().  As a result of calling
 *   dtrace_unregister(), the DTrace framework will call back into the provider
 *   via the dtps_destroy() entry point.  Once dtrace_unregister() successfully
 *   completes, however, the DTrace framework will no longer make calls through
 *   the Framework-to-Provider API.
 *
 * 2.3.3  Return value
 *
 *   On success, dtrace_unregister returns 0.  On failure, dtrace_unregister()
 *   returns an errno:
 *
 *     EBUSY    There are currently processes that have the DTrace pseudodevice
 *              open, or there exists an anonymous enabling that hasn't yet
 *              been claimed.
 *
 *   No other failure code is returned.
 *
 * 2.3.4  Caller's context
 *
 *   Because a call to dtrace_unregister() may induce calls through the
 *   Framework-to-Provider API, the caller may not hold any lock across
 *   dtrace_register() that is also acquired in any of the Framework-to-
 *   Provider API functions.  Additionally, mod_lock may not be held.
 *
 * 2.4  void dtrace_invalidate(dtrace_provider_id_t id)
 *
 * 2.4.1  Overview
 *
 *   Invalidates the specified provider.  All subsequent probe lookups for the
 *   specified provider will fail, but its probes will not be removed.
 *
 * 2.4.2  Arguments and note
 *
 *   The only argument is the provider identifier, as returned from a
 *   successful call to dtrace_register().  In general, a provider's probes
 *   always remain valid; dtrace_invalidate() is a mechanism for invalidating
 *   an entire provider, regardless of whether or not probes are enabled or
 *   not.  Note that dtrace_invalidate() will _not_ prevent already enabled
 *   probes from firing -- it will merely prevent any new enablings of the
 *   provider's probes.
 *
 * 2.5 int dtrace_condense(dtrace_provider_id_t id)
 *
 * 2.5.1  Overview
 *
 *   Removes all the unenabled probes for the given provider. This function is
 *   not unlike dtrace_unregister(), except that it doesn't remove the
 *   provider just as many of its associated probes as it can.
 *
 * 2.5.2  Arguments and Notes
 *
 *   As with dtrace_unregister(), the sole argument is the provider identifier
 *   as returned from a successful call to dtrace_register().  As a result of
 *   calling dtrace_condense(), the DTrace framework will call back into the
 *   given provider's dtps_destroy() entry point for each of the provider's
 *   unenabled probes.
 *
 * 2.5.3  Return value
 *
 *   Currently, dtrace_condense() always returns 0.  However, consumers of this
 *   function should check the return value as appropriate; its behavior may
 *   change in the future.
 *
 * 2.5.4  Caller's context
 *
 *   As with dtrace_unregister(), the caller may not hold any lock across
 *   dtrace_condense() that is also acquired in the provider's entry points.
 *   Also, mod_lock may not be held.
 *
 * 2.6 int dtrace_attached()
 *
 * 2.6.1  Overview
 *
 *   Indicates whether or not DTrace has attached.
 *
 * 2.6.2  Arguments and Notes
 *
 *   For most providers, DTrace makes initial contact beyond registration.
 *   That is, once a provider has registered with DTrace, it waits to hear
 *   from DTrace to create probes.  However, some providers may wish to
 *   proactively create probes without first being told by DTrace to do so.
 *   If providers wish to do this, they must first call dtrace_attached() to
 *   determine if DTrace itself has attached.  If dtrace_attached() returns 0,
 *   the provider must not make any other Provider-to-Framework API call.
 *
 * 2.6.3  Return value
 *
 *   dtrace_attached() returns 1 if DTrace has attached, 0 otherwise.
 *
 * 2.7  int dtrace_probe_create(dtrace_provider_t id, const char *mod,
 *	    const char *func, const char *name, int aframes, void *arg)
 *
 * 2.7.1  Overview
 *
 *   Creates a probe with specified module name, function name, and name.
 *
 * 2.7.2  Arguments and Notes
 *
 *   The first argument is the provider identifier, as returned from a
 *   successful call to dtrace_register().  The second, third, and fourth
 *   arguments are the module name, function name, and probe name,
 *   respectively.  Of these, module name and function name may both be NULL
 *   (in which case the probe is considered to be unanchored), or they may both
 *   be non-NULL.  The name must be non-NULL, and must point to a non-empty
 *   string.
 *
 *   The fifth argument is the number of artificial stack frames that will be
 *   found on the stack when dtrace_probe() is called for the new probe.  These
 *   artificial frames will be automatically be pruned should the stack() or
 *   stackdepth() functions be called as part of one of the probe's ECBs.  If
 *   the parameter doesn't add an artificial frame, this parameter should be
 *   zero.
 *
 *   The final argument is a probe argument that will be passed back to the
 *   provider when a probe-specific operation is called.  (e.g., via
 *   dtps_enable(), dtps_disable(), etc.)
 *
 *   Note that it is up to the provider to be sure that the probe that it
 *   creates does not already exist -- if the provider is unsure of the probe's
 *   existence, it should assure its absence with dtrace_probe_lookup() before
 *   calling dtrace_probe_create().
 *
 * 2.7.3  Return value
 *
 *   dtrace_probe_create() always succeeds, and always returns the identifier
 *   of the newly-created probe.
 *
 * 2.7.4  Caller's context
 *
 *   While dtrace_probe_create() is generally expected to be called from
 *   dtps_provide() and/or dtps_provide_module(), it may be called from other
 *   non-DTrace contexts.  Neither cpu_lock nor mod_lock may be held.
 *
 * 2.8  dtrace_id_t dtrace_probe_lookup(dtrace_provider_t id, const char *mod,
 *	    const char *func, const char *name)
 *
 * 2.8.1  Overview
 *
 *   Looks up a probe based on provdider and one or more of module name,
 *   function name and probe name.
 *
 * 2.8.2  Arguments and Notes
 *
 *   The first argument is the provider identifier, as returned from a
 *   successful call to dtrace_register().  The second, third, and fourth
 *   arguments are the module name, function name, and probe name,
 *   respectively.  Any of these may be NULL; dtrace_probe_lookup() will return
 *   the identifier of the first probe that is provided by the specified
 *   provider and matches all of the non-NULL matching criteria.
 *   dtrace_probe_lookup() is generally used by a provider to be check the
 *   existence of a probe before creating it with dtrace_probe_create().
 *
 * 2.8.3  Return value
 *
 *   If the probe exists, returns its identifier.  If the probe does not exist,
 *   return DTRACE_IDNONE.
 *
 * 2.8.4  Caller's context
 *
 *   While dtrace_probe_lookup() is generally expected to be called from
 *   dtps_provide() and/or dtps_provide_module(), it may also be called from
 *   other non-DTrace contexts.  Neither cpu_lock nor mod_lock may be held.
 *
 * 2.9  void *dtrace_probe_arg(dtrace_provider_t id, dtrace_id_t probe)
 *
 * 2.9.1  Overview
 *
 *   Returns the probe argument associated with the specified probe.
 *
 * 2.9.2  Arguments and Notes
 *
 *   The first argument is the provider identifier, as returned from a
 *   successful call to dtrace_register().  The second argument is a probe
 *   identifier, as returned from dtrace_probe_lookup() or
 *   dtrace_probe_create().  This is useful if a probe has multiple
 *   provider-specific components to it:  the provider can create the probe
 *   once with provider-specific state, and then add to the state by looking
 *   up the probe based on probe identifier.
 *
 * 2.9.3  Return value
 *
 *   Returns the argument associated with the specified probe.  If the
 *   specified probe does not exist, or if the specified probe is not provided
 *   by the specified provider, NULL is returned.
 *
 * 2.9.4  Caller's context
 *
 *   While dtrace_probe_arg() is generally expected to be called from
 *   dtps_provide() and/or dtps_provide_module(), it may also be called from
 *   other non-DTrace contexts.  Neither cpu_lock nor mod_lock may be held.
 *
 * 2.10  void dtrace_probe(dtrace_id_t probe, uintptr_t arg0, uintptr_t arg1,
 *		uintptr_t arg2, uintptr_t arg3, uintptr_t arg4)
 *
 * 2.10.1  Overview
 *
 *   The epicenter of DTrace:  fires the specified probes with the specified
 *   arguments.
 *
 * 2.10.2  Arguments and Notes
 *
 *   The first argument is a probe identifier as returned by
 *   dtrace_probe_create() or dtrace_probe_lookup().  The second through sixth
 *   arguments are the values to which the D variables "arg0" through "arg4"
 *   will be mapped.
 *
 *   dtrace_probe() should be called whenever the specified probe has fired --
 *   however the provider defines it.
 *
 * 2.10.3  Return value
 *
 *   None.
 *
 * 2.10.4  Caller's context
 *
 *   dtrace_probe() may be called in virtually any context:  kernel, user,
 *   interrupt, high-level interrupt, with arbitrary adaptive locks held, with
 *   dispatcher locks held, with interrupts disabled, etc.  The only latitude
 *   that must be afforded to DTrace is the ability to make calls within
 *   itself (and to its in-kernel subroutines) and the ability to access
 *   arbitrary (but mapped) memory.  On some platforms, this constrains
 *   context.  For example, on UltraSPARC, dtrace_probe() cannot be called
 *   from any context in which TL is greater than zero.  dtrace_probe() may
 *   also not be called from any routine which may be called by dtrace_probe()
 *   -- which includes functions in the DTrace framework and some in-kernel
 *   DTrace subroutines.  All such functions "dtrace_"; providers that
 *   instrument the kernel arbitrarily should be sure to not instrument these
 *   routines.
 */
typedef struct dtrace_pops {
	void (*dtps_provide)(void *arg, dtrace_probedesc_t *spec);
	void (*dtps_provide_module)(void *arg, modctl_t *mp);
	void (*dtps_enable)(void *arg, dtrace_id_t id, void *parg);
	void (*dtps_disable)(void *arg, dtrace_id_t id, void *parg);
	void (*dtps_suspend)(void *arg, dtrace_id_t id, void *parg);
	void (*dtps_resume)(void *arg, dtrace_id_t id, void *parg);
	void (*dtps_getargdesc)(void *arg, dtrace_id_t id, void *parg,
	    dtrace_argdesc_t *desc);
	uint64_t (*dtps_getargval)(void *arg, dtrace_id_t id, void *parg,
	    int argno, int aframes);
	int (*dtps_usermode)(void *arg, dtrace_id_t id, void *parg);
	void (*dtps_destroy)(void *arg, dtrace_id_t id, void *parg);
} dtrace_pops_t;

#define	DTRACE_MODE_KERNEL			0x01
#define	DTRACE_MODE_USER			0x02
#define	DTRACE_MODE_NOPRIV_DROP			0x10
#define	DTRACE_MODE_NOPRIV_RESTRICT		0x20
#define	DTRACE_MODE_LIMITEDPRIV_RESTRICT	0x40

typedef uintptr_t	dtrace_provider_id_t;

extern int dtrace_register(const char *, const dtrace_pattr_t *, uint32_t,
    cred_t *, const dtrace_pops_t *, void *, dtrace_provider_id_t *);
extern int dtrace_unregister(dtrace_provider_id_t);
extern int dtrace_condense(dtrace_provider_id_t);
extern void dtrace_invalidate(dtrace_provider_id_t);
extern dtrace_id_t dtrace_probe_lookup(dtrace_provider_id_t, char *,
    char *, char *);
extern dtrace_id_t dtrace_probe_create(dtrace_provider_id_t, const char *,
    const char *, const char *, int, void *);
extern void *dtrace_probe_arg(dtrace_provider_id_t, dtrace_id_t);
extern void dtrace_probe(dtrace_id_t, uintptr_t arg0, uintptr_t arg1,
    uintptr_t arg2, uintptr_t arg3, uintptr_t arg4);

/*
 * DTrace Meta Provider API
 *
 * The following functions are implemented by the DTrace framework and are
 * used to implement meta providers. Meta providers plug into the DTrace
 * framework and are used to instantiate new providers on the fly. At
 * present, there is only one type of meta provider and only one meta
 * provider may be registered with the DTrace framework at a time. The
 * sole meta provider type provides user-land static tracing facilities
 * by taking meta probe descriptions and adding a corresponding provider
 * into the DTrace framework.
 *
 * 1 Framework-to-Provider
 *
 * 1.1 Overview
 *
 * The Framework-to-Provider API is represented by the dtrace_mops structure
 * that the meta provider passes to the framework when registering itself as
 * a meta provider. This structure consists of the following members:
 *
 *   dtms_create_probe()	<-- Add a new probe to a created provider
 *   dtms_provide_pid()		<-- Create a new provider for a given process
 *   dtms_remove_pid()		<-- Remove a previously created provider
 *
 * 1.2  void dtms_create_probe(void *arg, void *parg,
 *           dtrace_helper_probedesc_t *probedesc);
 *
 * 1.2.1  Overview
 *
 *   Called by the DTrace framework to create a new probe in a provider
 *   created by this meta provider.
 *
 * 1.2.2  Arguments and notes
 *
 *   The first argument is the cookie as passed to dtrace_meta_register().
 *   The second argument is the provider cookie for the associated provider;
 *   this is obtained from the return value of dtms_provide_pid(). The third
 *   argument is the helper probe description.
 *
 * 1.2.3  Return value
 *
 *   None
 *
 * 1.2.4  Caller's context
 *
 *   dtms_create_probe() is called from either ioctl() or module load context
 *   in the context of a newly-created provider (that is, a provider that
 *   is a result of a call to dtms_provide_pid()). The DTrace framework is
 *   locked in such a way that meta providers may not register or unregister,
 *   such that no other thread can call into a meta provider operation and that
 *   atomicity is assured with respect to meta provider operations across
 *   dtms_provide_pid() and subsequent calls to dtms_create_probe().
 *   The context is thus effectively single-threaded with respect to the meta
 *   provider, and that the meta provider cannot call dtrace_meta_register()
 *   or dtrace_meta_unregister(). However, the context is such that the
 *   provider may (and is expected to) call provider-related DTrace provider
 *   APIs including dtrace_probe_create().
 *
 * 1.3  void *dtms_provide_pid(void *arg, dtrace_meta_provider_t *mprov,
 *	      pid_t pid)
 *
 * 1.3.1  Overview
 *
 *   Called by the DTrace framework to instantiate a new provider given the
 *   description of the provider and probes in the mprov argument. The
 *   meta provider should call dtrace_register() to insert the new provider
 *   into the DTrace framework.
 *
 * 1.3.2  Arguments and notes
 *
 *   The first argument is the cookie as passed to dtrace_meta_register().
 *   The second argument is a pointer to a structure describing the new
 *   helper provider. The third argument is the process identifier for
 *   process associated with this new provider. Note that the name of the
 *   provider as passed to dtrace_register() should be the contatenation of
 *   the dtmpb_provname member of the mprov argument and the processs
 *   identifier as a string.
 *
 * 1.3.3  Return value
 *
 *   The cookie for the provider that the meta provider creates. This is
 *   the same value that it passed to dtrace_register().
 *
 * 1.3.4  Caller's context
 *
 *   dtms_provide_pid() is called from either ioctl() or module load context.
 *   The DTrace framework is locked in such a way that meta providers may not
 *   register or unregister. This means that the meta provider cannot call
 *   dtrace_meta_register() or dtrace_meta_unregister(). However, the context
 *   is such that the provider may -- and is expected to --  call
 *   provider-related DTrace provider APIs including dtrace_register().
 *
 * 1.4  void dtms_remove_pid(void *arg, dtrace_meta_provider_t *mprov,
 *	     pid_t pid)
 *
 * 1.4.1  Overview
 *
 *   Called by the DTrace framework to remove a provider that had previously
 *   been instantiated via the dtms_provide_pid() entry point. The meta
 *   provider need not remove the provider immediately, but this entry
 *   point indicates that the provider should be removed as soon as possible
 *   using the dtrace_unregister() API.
 *
 * 1.4.2  Arguments and notes
 *
 *   The first argument is the cookie as passed to dtrace_meta_register().
 *   The second argument is a pointer to a structure describing the helper
 *   provider. The third argument is the process identifier for process
 *   associated with this new provider.
 *
 * 1.4.3  Return value
 *
 *   None
 *
 * 1.4.4  Caller's context
 *
 *   dtms_remove_pid() is called from either ioctl() or exit() context.
 *   The DTrace framework is locked in such a way that meta providers may not
 *   register or unregister. This means that the meta provider cannot call
 *   dtrace_meta_register() or dtrace_meta_unregister(). However, the context
 *   is such that the provider may -- and is expected to -- call
 *   provider-related DTrace provider APIs including dtrace_unregister().
 */
typedef struct dtrace_helper_probedesc {
	char *dthpb_mod;			/* probe module */
	char *dthpb_func; 			/* probe function */
	char *dthpb_name; 			/* probe name */
	uint64_t dthpb_base;			/* base address */
	uint32_t *dthpb_offs;			/* offsets array */
	uint32_t *dthpb_enoffs;			/* is-enabled offsets array */
	uint32_t dthpb_noffs;			/* offsets count */
	uint32_t dthpb_nenoffs;			/* is-enabled offsets count */
	uint8_t *dthpb_args;			/* argument mapping array */
	uint8_t dthpb_xargc;			/* translated argument count */
	uint8_t dthpb_nargc;			/* native argument count */
	char *dthpb_xtypes;			/* translated types strings */
	char *dthpb_ntypes;			/* native types strings */
} dtrace_helper_probedesc_t;

typedef struct dtrace_helper_provdesc {
	char *dthpv_provname;			/* provider name */
	dtrace_pattr_t dthpv_pattr;		/* stability attributes */
} dtrace_helper_provdesc_t;

typedef struct dtrace_mops {
	void (*dtms_create_probe)(void *, void *, dtrace_helper_probedesc_t *);
	void *(*dtms_provide_pid)(void *, dtrace_helper_provdesc_t *, pid_t);
	void (*dtms_remove_pid)(void *, dtrace_helper_provdesc_t *, pid_t);
} dtrace_mops_t;

typedef uintptr_t	dtrace_meta_provider_id_t;

extern int dtrace_meta_register(const char *, const dtrace_mops_t *, void *,
    dtrace_meta_provider_id_t *);
extern int dtrace_meta_unregister(dtrace_meta_provider_id_t);

/*
 * DTrace Kernel Hooks
 *
 * The following functions are implemented by the base kernel and form a set of
 * hooks used by the DTrace framework.  DTrace hooks are implemented in either
 * uts/common/os/dtrace_subr.c, an ISA-specific assembly file, or in a
 * uts/<platform>/os/dtrace_subr.c corresponding to each hardware platform.
 */

typedef enum dtrace_vtime_state {
	DTRACE_VTIME_INACTIVE = 0,	/* No DTrace, no TNF */
	DTRACE_VTIME_ACTIVE,		/* DTrace virtual time, no TNF */
	DTRACE_VTIME_INACTIVE_TNF,	/* No DTrace, TNF active */
	DTRACE_VTIME_ACTIVE_TNF		/* DTrace virtual time _and_ TNF */
} dtrace_vtime_state_t;

#ifdef illumos
extern dtrace_vtime_state_t dtrace_vtime_active;
#endif
extern void dtrace_vtime_switch(kthread_t *next);
extern void dtrace_vtime_enable_tnf(void);
extern void dtrace_vtime_disable_tnf(void);
extern void dtrace_vtime_enable(void);
extern void dtrace_vtime_disable(void);

struct regs;
struct reg;

#ifdef illumos
extern int (*dtrace_pid_probe_ptr)(struct reg *);
extern int (*dtrace_return_probe_ptr)(struct reg *);
extern void (*dtrace_fasttrap_fork_ptr)(proc_t *, proc_t *);
extern void (*dtrace_fasttrap_exec_ptr)(proc_t *);
extern void (*dtrace_fasttrap_exit_ptr)(proc_t *);
extern void dtrace_fasttrap_fork(proc_t *, proc_t *);
#endif

typedef uintptr_t dtrace_icookie_t;
typedef void (*dtrace_xcall_t)(void *);

extern dtrace_icookie_t dtrace_interrupt_disable(void);
extern void dtrace_interrupt_enable(dtrace_icookie_t);

extern void dtrace_membar_producer(void);
extern void dtrace_membar_consumer(void);

extern void (*dtrace_cpu_init)(processorid_t);
#ifdef illumos
extern void (*dtrace_modload)(modctl_t *);
extern void (*dtrace_modunload)(modctl_t *);
#endif
extern void (*dtrace_helpers_cleanup)(void);
extern void (*dtrace_helpers_fork)(proc_t *parent, proc_t *child);
extern void (*dtrace_cpustart_init)(void);
extern void (*dtrace_cpustart_fini)(void);
extern void (*dtrace_closef)(void);

extern void (*dtrace_debugger_init)(void);
extern void (*dtrace_debugger_fini)(void);
extern dtrace_cacheid_t dtrace_predcache_id;

#ifdef illumos
extern hrtime_t dtrace_gethrtime(void);
#else
void dtrace_debug_printf(const char *, ...) __printflike(1, 2);
#endif
extern void dtrace_sync(void);
extern void dtrace_toxic_ranges(void (*)(uintptr_t, uintptr_t));
extern void dtrace_xcall(processorid_t, dtrace_xcall_t, void *);
extern void dtrace_vpanic(const char *, __va_list);
extern void dtrace_panic(const char *, ...);

extern int dtrace_safe_defer_signal(void);
extern void dtrace_safe_synchronous_signal(void);

extern int dtrace_mach_aframes(void);

#if defined(__i386) || defined(__amd64)
extern int dtrace_instr_size(uchar_t *instr);
extern int dtrace_instr_size_isa(uchar_t *, model_t, int *);
extern void dtrace_invop_callsite(void);
#endif
extern void dtrace_invop_add(int (*)(uintptr_t, struct trapframe *, uintptr_t));
extern void dtrace_invop_remove(int (*)(uintptr_t, struct trapframe *,
    uintptr_t));

#ifdef __sparc
extern int dtrace_blksuword32(uintptr_t, uint32_t *, int);
extern void dtrace_getfsr(uint64_t *);
#endif

#ifndef illumos
extern void dtrace_helpers_duplicate(proc_t *, proc_t *);
extern void dtrace_helpers_destroy(proc_t *);
#endif

#define	DTRACE_CPUFLAG_ISSET(flag) \
	(cpu_core[curcpu].cpuc_dtrace_flags & (flag))

#define	DTRACE_CPUFLAG_SET(flag) \
	(cpu_core[curcpu].cpuc_dtrace_flags |= (flag))

#define	DTRACE_CPUFLAG_CLEAR(flag) \
	(cpu_core[curcpu].cpuc_dtrace_flags &= ~(flag))

#endif /* _KERNEL */

#endif	/* _ASM */

#if defined(__i386) || defined(__amd64)

#define	DTRACE_INVOP_PUSHL_EBP		1
#define	DTRACE_INVOP_PUSHQ_RBP		DTRACE_INVOP_PUSHL_EBP
#define	DTRACE_INVOP_POPL_EBP		2
#define	DTRACE_INVOP_POPQ_RBP		DTRACE_INVOP_POPL_EBP
#define	DTRACE_INVOP_LEAVE		3
#define	DTRACE_INVOP_NOP		4
#define	DTRACE_INVOP_RET		5

#elif defined(__powerpc__)

#define DTRACE_INVOP_BCTR	1
#define DTRACE_INVOP_BLR	2
#define DTRACE_INVOP_JUMP	3
#define DTRACE_INVOP_MFLR_R0	4
#define DTRACE_INVOP_NOP	5

#elif defined(__arm__)

#define	DTRACE_INVOP_SHIFT	4
#define	DTRACE_INVOP_MASK	((1 << DTRACE_INVOP_SHIFT) - 1)
#define	DTRACE_INVOP_DATA(x)	((x) >> DTRACE_INVOP_SHIFT)

#define DTRACE_INVOP_PUSHM	1
#define DTRACE_INVOP_POPM	2
#define DTRACE_INVOP_B		3

#elif defined(__aarch64__)

#define	INSN_SIZE	4

#define	B_MASK		0xff000000
#define	B_DATA_MASK	0x00ffffff
#define	B_INSTR		0x14000000

#define	RET_INSTR	0xd65f03c0

#define	LDP_STP_MASK	0xffc00000
#define	STP_32		0x29800000
#define	STP_64		0xa9800000
#define	LDP_32		0x28c00000
#define	LDP_64		0xa8c00000
#define	LDP_STP_PREIND	(1 << 24)
#define	LDP_STP_DIR	(1 << 22) /* Load instruction */
#define	ARG1_SHIFT	0
#define	ARG1_MASK	0x1f
#define	ARG2_SHIFT	10
#define	ARG2_MASK	0x1f
#define	OFFSET_SHIFT	15
#define	OFFSET_SIZE	7
#define	OFFSET_MASK	((1 << OFFSET_SIZE) - 1)

#define	DTRACE_INVOP_PUSHM	1
#define	DTRACE_INVOP_RET	2
#define	DTRACE_INVOP_B		3

#elif defined(__mips__)

#define	INSN_SIZE		4

/* Load/Store double RA to/from SP */
#define	LDSD_RA_SP_MASK		0xffff0000
#define	LDSD_DATA_MASK		0x0000ffff
#define	SD_RA_SP		0xffbf0000
#define	LD_RA_SP		0xdfbf0000

#define	DTRACE_INVOP_SD		1
#define	DTRACE_INVOP_LD		2

#elif defined(__riscv)

#define	DTRACE_INVOP_SD		1
#define	DTRACE_INVOP_C_SDSP	2
#define	DTRACE_INVOP_RET	3
#define	DTRACE_INVOP_C_RET	4
#define	DTRACE_INVOP_NOP	5

#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DTRACE_H */
