/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
 * Copyright (c) 2000 Eduardo Horvath.
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: NetBSD: mdreloc.c,v 1.42 2008/04/28 20:23:04 martin Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/linker.h>
#include <sys/proc.h>
#include <sys/sysent.h>
#include <sys/imgact_elf.h>
#include <sys/syscall.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/vm_param.h>

#include <machine/elf.h>

#include "linker_if.h"

static struct sysentvec elf64_freebsd_sysvec = {
	.sv_size	= SYS_MAXSYSCALL,
	.sv_table	= sysent,
	.sv_errsize	= 0,
	.sv_errtbl	= NULL,
	.sv_transtrap	= NULL,
	.sv_fixup	= __elfN(freebsd_fixup),
	.sv_sendsig	= sendsig,
	.sv_sigcode	= NULL,
	.sv_szsigcode	= NULL,
	.sv_name	= "FreeBSD ELF64",
	.sv_coredump	= __elfN(coredump),
	.sv_imgact_try	= NULL,
	.sv_minsigstksz	= MINSIGSTKSZ,
	.sv_minuser	= VM_MIN_ADDRESS,
	.sv_maxuser	= VM_MAXUSER_ADDRESS,
	.sv_usrstack	= USRSTACK,
	.sv_psstrings	= PS_STRINGS,
	.sv_stackprot	= VM_PROT_READ | VM_PROT_WRITE,
	.sv_copyout_strings = exec_copyout_strings,
	.sv_setregs	= exec_setregs,
	.sv_fixlimit	= NULL,
	.sv_maxssiz	= NULL,
	.sv_flags	= SV_ABI_FREEBSD | SV_LP64 | SV_ASLR,
	.sv_set_syscall_retval = cpu_set_syscall_retval,
	.sv_fetch_syscall_args = cpu_fetch_syscall_args,
	.sv_syscallnames = syscallnames,
	.sv_schedtail	= NULL,
	.sv_thread_detach = NULL,
	.sv_trap	= NULL,
};

static Elf64_Brandinfo freebsd_brand_info = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_SPARCV9,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/libexec/ld-elf.so.1",
	.sysvec		= &elf64_freebsd_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &elf64_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

SYSINIT(elf64, SI_SUB_EXEC, SI_ORDER_FIRST,
    (sysinit_cfunc_t)elf64_insert_brand_entry, &freebsd_brand_info);

static Elf64_Brandinfo freebsd_brand_oinfo = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_SPARCV9,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/usr/libexec/ld-elf.so.1",
	.sysvec		= &elf64_freebsd_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &elf64_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

SYSINIT(oelf64, SI_SUB_EXEC, SI_ORDER_ANY,
    (sysinit_cfunc_t)elf64_insert_brand_entry, &freebsd_brand_oinfo);

void
elf64_dump_thread(struct thread *td __unused, void *dst __unused,
    size_t *off __unused)
{

}

/*
 * The following table holds for each relocation type:
 *	- the width in bits of the memory location the relocation
 *	  applies to (not currently used)
 *	- the number of bits the relocation value must be shifted to the
 *	  right (i.e. discard least significant bits) to fit into
 *	  the appropriate field in the instruction word.
 *	- flags indicating whether
 *		* the relocation involves a symbol
 *		* the relocation is relative to the current position
 *		* the relocation is for a GOT entry
 *		* the relocation is relative to the load address
 *
 */
#define	_RF_S		0x80000000		/* Resolve symbol */
#define	_RF_A		0x40000000		/* Use addend */
#define	_RF_P		0x20000000		/* Location relative */
#define	_RF_G		0x10000000		/* GOT offset */
#define	_RF_B		0x08000000		/* Load address relative */
#define	_RF_U		0x04000000		/* Unaligned */
#define	_RF_X		0x02000000		/* Bare symbols, needs proc */
#define	_RF_D		0x01000000		/* Use dynamic TLS offset */
#define	_RF_O		0x00800000		/* Use static TLS offset */
#define	_RF_I		0x00400000		/* Use TLS object ID */
#define	_RF_SZ(s)	(((s) & 0xff) << 8)	/* memory target size */
#define	_RF_RS(s)	( (s) & 0xff)		/* right shift */
static const int reloc_target_flags[] = {
	0,							/* NONE */
	_RF_S|_RF_A|		_RF_SZ(8)  | _RF_RS(0),		/* 8 */
	_RF_S|_RF_A|		_RF_SZ(16) | _RF_RS(0),		/* 16 */
	_RF_S|_RF_A|		_RF_SZ(32) | _RF_RS(0),		/* 32 */
	_RF_S|_RF_A|_RF_P|	_RF_SZ(8)  | _RF_RS(0),		/* DISP_8 */
	_RF_S|_RF_A|_RF_P|	_RF_SZ(16) | _RF_RS(0),		/* DISP_16 */
	_RF_S|_RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(0),		/* DISP_32 */
	_RF_S|_RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(2),		/* WDISP_30 */
	_RF_S|_RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(2),		/* WDISP_22 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(10),	/* HI22 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(0),		/* 22 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(0),		/* 13 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(0),		/* LO10 */
	_RF_G|			_RF_SZ(32) | _RF_RS(0),		/* GOT10 */
	_RF_G|			_RF_SZ(32) | _RF_RS(0),		/* GOT13 */
	_RF_G|			_RF_SZ(32) | _RF_RS(10),	/* GOT22 */
	_RF_S|_RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(0),		/* PC10 */
	_RF_S|_RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(10),	/* PC22 */
	      _RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(2),		/* WPLT30 */
				_RF_SZ(32) | _RF_RS(0),		/* COPY */
	_RF_S|_RF_A|		_RF_SZ(64) | _RF_RS(0),		/* GLOB_DAT */
				_RF_SZ(32) | _RF_RS(0),		/* JMP_SLOT */
	      _RF_A|	_RF_B|	_RF_SZ(64) | _RF_RS(0),		/* RELATIVE */
	_RF_S|_RF_A|	_RF_U|	_RF_SZ(32) | _RF_RS(0),		/* UA_32 */

	      _RF_A|		_RF_SZ(32) | _RF_RS(0),		/* PLT32 */
	      _RF_A|		_RF_SZ(32) | _RF_RS(10),	/* HIPLT22 */
	      _RF_A|		_RF_SZ(32) | _RF_RS(0),		/* LOPLT10 */
	      _RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(0),		/* PCPLT32 */
	      _RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(10),	/* PCPLT22 */
	      _RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(0),		/* PCPLT10 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(0),		/* 10 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(0),		/* 11 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(64) | _RF_RS(0),		/* 64 */
	_RF_S|_RF_A|/*extra*/	_RF_SZ(32) | _RF_RS(0),		/* OLO10 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(42),	/* HH22 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(32),	/* HM10 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(10),	/* LM22 */
	_RF_S|_RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(42),	/* PC_HH22 */
	_RF_S|_RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(32),	/* PC_HM10 */
	_RF_S|_RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(10),	/* PC_LM22 */
	_RF_S|_RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(2),		/* WDISP16 */
	_RF_S|_RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(2),		/* WDISP19 */
	_RF_S|_RF_A|		_RF_SZ(32) | _RF_RS(0),		/* GLOB_JMP */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(0),		/* 7 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(0),		/* 5 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(0),		/* 6 */
	_RF_S|_RF_A|_RF_P|	_RF_SZ(64) | _RF_RS(0),		/* DISP64 */
	      _RF_A|		_RF_SZ(64) | _RF_RS(0),		/* PLT64 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(10),	/* HIX22 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(0),		/* LOX10 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(22),	/* H44 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(12),	/* M44 */
	_RF_S|_RF_A|_RF_X|	_RF_SZ(32) | _RF_RS(0),		/* L44 */
	_RF_S|_RF_A|		_RF_SZ(64) | _RF_RS(0),		/* REGISTER */
	_RF_S|_RF_A|	_RF_U|	_RF_SZ(64) | _RF_RS(0),		/* UA64 */
	_RF_S|_RF_A|	_RF_U|	_RF_SZ(16) | _RF_RS(0),		/* UA16 */

#if 0
	/* TLS */
	_RF_S|_RF_A|		_RF_SZ(32) | _RF_RS(10),	/* GD_HI22 */
	_RF_S|_RF_A|		_RF_SZ(32) | _RF_RS(0),		/* GD_LO10 */
	0,							/* GD_ADD */
	      _RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(2),		/* GD_CALL */
	_RF_S|_RF_A|		_RF_SZ(32) | _RF_RS(10),	/* LDM_HI22 */
	_RF_S|_RF_A|		_RF_SZ(32) | _RF_RS(0),		/* LDM_LO10 */
	0,							/* LDM_ADD */
	      _RF_A|_RF_P|	_RF_SZ(32) | _RF_RS(2),		/* LDM_CALL */
	_RF_S|_RF_A|		_RF_SZ(32) | _RF_RS(10),	/* LDO_HIX22 */
	_RF_S|_RF_A|		_RF_SZ(32) | _RF_RS(0),		/* LDO_LOX10 */
	0,							/* LDO_ADD */
	_RF_S|_RF_A|		_RF_SZ(32) | _RF_RS(10),	/* IE_HI22 */
	_RF_S|_RF_A|		_RF_SZ(32) | _RF_RS(0),		/* IE_LO10 */
	0,							/* IE_LD */
	0,							/* IE_LDX */
	0,							/* IE_ADD */
	_RF_S|_RF_A|	_RF_O|	_RF_SZ(32) | _RF_RS(10),	/* LE_HIX22 */
	_RF_S|_RF_A|	_RF_O|	_RF_SZ(32) | _RF_RS(0),		/* LE_LOX10 */
	_RF_S|		_RF_I|	_RF_SZ(32) | _RF_RS(0),		/* DTPMOD32 */
	_RF_S|		_RF_I|	_RF_SZ(64) | _RF_RS(0),		/* DTPMOD64 */
	_RF_S|_RF_A|	_RF_D|	_RF_SZ(32) | _RF_RS(0),		/* DTPOFF32 */
	_RF_S|_RF_A|	_RF_D|	_RF_SZ(64) | _RF_RS(0),		/* DTPOFF64 */
	_RF_S|_RF_A|	_RF_O|	_RF_SZ(32) | _RF_RS(0),		/* TPOFF32 */
	_RF_S|_RF_A|	_RF_O|	_RF_SZ(64) | _RF_RS(0)		/* TPOFF64 */
#endif
};

#if 0
static const char *const reloc_names[] = {
	"NONE", "8", "16", "32", "DISP_8", "DISP_16", "DISP_32", "WDISP_30",
	"WDISP_22", "HI22", "22", "13", "LO10", "GOT10", "GOT13", "GOT22",
	"PC10", "PC22", "WPLT30", "COPY", "GLOB_DAT", "JMP_SLOT", "RELATIVE",
	"UA_32", "PLT32", "HIPLT22", "LOPLT10", "LOPLT10", "PCPLT22",
	"PCPLT32", "10", "11", "64", "OLO10", "HH22", "HM10", "LM22",
	"PC_HH22", "PC_HM10", "PC_LM22", "WDISP16", "WDISP19", "GLOB_JMP",
	"7", "5", "6", "DISP64", "PLT64", "HIX22", "LOX10", "H44", "M44",
	"L44", "REGISTER", "UA64", "UA16", "GD_HI22", "GD_LO10", "GD_ADD",
	"GD_CALL", "LDM_HI22", "LDMO10", "LDM_ADD", "LDM_CALL", "LDO_HIX22",
	"LDO_LOX10", "LDO_ADD", "IE_HI22", "IE_LO10", "IE_LD", "IE_LDX",
	"IE_ADD", "LE_HIX22", "LE_LOX10", "DTPMOD32", "DTPMOD64", "DTPOFF32",
	"DTPOFF64", "TPOFF32", "TPOFF64"
};
#endif

#define	RELOC_RESOLVE_SYMBOL(t)		((reloc_target_flags[t] & _RF_S) != 0)
#define	RELOC_PC_RELATIVE(t)		((reloc_target_flags[t] & _RF_P) != 0)
#define	RELOC_BASE_RELATIVE(t)		((reloc_target_flags[t] & _RF_B) != 0)
#define	RELOC_UNALIGNED(t)		((reloc_target_flags[t] & _RF_U) != 0)
#define	RELOC_USE_ADDEND(t)		((reloc_target_flags[t] & _RF_A) != 0)
#define	RELOC_BARE_SYMBOL(t)		((reloc_target_flags[t] & _RF_X) != 0)
#define	RELOC_USE_TLS_DOFF(t)		((reloc_target_flags[t] & _RF_D) != 0)
#define	RELOC_USE_TLS_OFF(t)		((reloc_target_flags[t] & _RF_O) != 0)
#define	RELOC_USE_TLS_ID(t)		((reloc_target_flags[t] & _RF_I) != 0)
#define	RELOC_TARGET_SIZE(t)		((reloc_target_flags[t] >> 8) & 0xff)
#define	RELOC_VALUE_RIGHTSHIFT(t)	(reloc_target_flags[t] & 0xff)

static const long reloc_target_bitmask[] = {
#define	_BM(x)	(~(-(1ULL << (x))))
	0,				/* NONE */
	_BM(8), _BM(16), _BM(32),	/* 8, 16, 32 */
	_BM(8), _BM(16), _BM(32),	/* DISP8, DISP16, DISP32 */
	_BM(30), _BM(22),		/* WDISP30, WDISP22 */
	_BM(22), _BM(22),		/* HI22, 22 */
	_BM(13), _BM(10),		/* 13, LO10 */
	_BM(10), _BM(13), _BM(22),	/* GOT10, GOT13, GOT22 */
	_BM(10), _BM(22),		/* PC10, PC22 */
	_BM(30), 0,			/* WPLT30, COPY */
	_BM(32), _BM(32), _BM(32),	/* GLOB_DAT, JMP_SLOT, RELATIVE */
	_BM(32), _BM(32),		/* UA32, PLT32 */
	_BM(22), _BM(10),		/* HIPLT22, LOPLT10 */
	_BM(32), _BM(22), _BM(10),	/* PCPLT32, PCPLT22, PCPLT10 */
	_BM(10), _BM(11), -1,		/* 10, 11, 64 */
	_BM(13), _BM(22),		/* OLO10, HH22 */
	_BM(10), _BM(22),		/* HM10, LM22 */
	_BM(22), _BM(10), _BM(22),	/* PC_HH22, PC_HM10, PC_LM22 */
	_BM(16), _BM(19),		/* WDISP16, WDISP19 */
	-1,				/* GLOB_JMP */
	_BM(7), _BM(5), _BM(6),		/* 7, 5, 6 */
	-1, -1,				/* DISP64, PLT64 */
	_BM(22), _BM(13),		/* HIX22, LOX10 */
	_BM(22), _BM(10), _BM(13),	/* H44, M44, L44 */
	-1, -1, _BM(16),		/* REGISTER, UA64, UA16 */
#if 0
	_BM(22), _BM(10), 0, _BM(30),	/* GD_HI22, GD_LO10, GD_ADD, GD_CALL */
	_BM(22), _BM(10), 0,		/* LDM_HI22, LDMO10, LDM_ADD */
	_BM(30),			/* LDM_CALL */
	_BM(22), _BM(10), 0,		/* LDO_HIX22, LDO_LOX10, LDO_ADD */
	_BM(22), _BM(10), 0, 0,		/* IE_HI22, IE_LO10, IE_LD, IE_LDX */
	0,				/* IE_ADD */
	_BM(22), _BM(13),		/* LE_HIX22, LE_LOX10 */
	_BM(32), -1,			/* DTPMOD32, DTPMOD64 */
	_BM(32), -1,			/* DTPOFF32, DTPOFF64 */
	_BM(32), -1			/* TPOFF32, TPOFF64 */
#endif
#undef _BM
};
#define	RELOC_VALUE_BITMASK(t)	(reloc_target_bitmask[t])

bool
elf_is_ifunc_reloc(Elf_Size r_info __unused)
{

	return (false);
}

int
elf_reloc_local(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, elf_lookup_fn lookup __unused)
{
	const Elf_Rela *rela;
	Elf_Addr *where;

	if (type != ELF_RELOC_RELA)
		return (-1);

	rela = (const Elf_Rela *)data;
	if (ELF64_R_TYPE_ID(rela->r_info) != R_SPARC_RELATIVE)
		return (-1);

	where = (Elf_Addr *)(relocbase + rela->r_offset);
	*where = elf_relocaddr(lf, rela->r_addend + relocbase);

	return (0);
}

/* Process one elf relocation with addend. */
int
elf_reloc(linker_file_t lf, Elf_Addr relocbase, const void *data, int type,
    elf_lookup_fn lookup)
{
	const Elf_Rela *rela;
	Elf_Word *where32;
	Elf_Addr *where;
	Elf_Size rtype, symidx;
	Elf_Addr value;
	Elf_Addr mask;
	Elf_Addr addr;
	int error;

	if (type != ELF_RELOC_RELA)
		return (-1);

	rela = (const Elf_Rela *)data;
	where = (Elf_Addr *)(relocbase + rela->r_offset);
	where32 = (Elf_Word *)where;
	rtype = ELF64_R_TYPE_ID(rela->r_info);
	symidx = ELF_R_SYM(rela->r_info);

	if (rtype == R_SPARC_NONE || rtype == R_SPARC_RELATIVE)
		return (0);

	if (rtype == R_SPARC_JMP_SLOT || rtype == R_SPARC_COPY ||
	    rtype >= nitems(reloc_target_bitmask)) {
		printf("kldload: unexpected relocation type %ld\n", rtype);
		return (-1);
	}

	if (RELOC_UNALIGNED(rtype)) {
		printf("kldload: unaligned relocation type %ld\n", rtype);
		return (-1);
	}

	value = rela->r_addend;

	if (RELOC_RESOLVE_SYMBOL(rtype)) {
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		value += addr;
		if (RELOC_BARE_SYMBOL(rtype))
			value = elf_relocaddr(lf, value);
	}

	if (rtype == R_SPARC_OLO10)
		value = (value & 0x3ff) + ELF64_R_TYPE_DATA(rela->r_info);

	if (rtype == R_SPARC_HIX22)
		value ^= 0xffffffffffffffff;

	if (RELOC_PC_RELATIVE(rtype))
		value -= (Elf_Addr)where;

	if (RELOC_BASE_RELATIVE(rtype))
		value = elf_relocaddr(lf, value + relocbase);

	mask = RELOC_VALUE_BITMASK(rtype);
	value >>= RELOC_VALUE_RIGHTSHIFT(rtype);
	value &= mask;

	if (rtype == R_SPARC_LOX10)
		value |= 0x1c00;

	if (RELOC_TARGET_SIZE(rtype) > 32) {
		*where &= ~mask;
		*where |= value;
	} else {
		*where32 &= ~mask;
		*where32 |= value;
	}

	return (0);
}

int
elf_cpu_load_file(linker_file_t lf __unused)
{

	return (0);
}

int
elf_cpu_unload_file(linker_file_t lf __unused)
{

	return (0);
}
