/*-
 * Copyright 1996-1998 John D. Polstra.
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2016 Yukishige Shibata <y-shibat@mtd.biglobe.ne.jp>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/linker.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/imgact_elf.h>
#include <sys/syscall.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>

#include <machine/elf.h>
#include <machine/md_var.h>

struct sysentvec elf64_freebsd_sysvec = {
	.sv_size	= SYS_MAXSYSCALL,
	.sv_table	= sysent,
	.sv_errsize	= 0,
	.sv_errtbl	= NULL,
	.sv_transtrap	= NULL,
	.sv_fixup	= __elfN(freebsd_fixup),
	.sv_sendsig	= sendsig,
	.sv_sigcode	= sigcode,
	.sv_szsigcode	= &szsigcode,
	.sv_name	= "FreeBSD ELF64",
	.sv_coredump	= __elfN(coredump),
	.sv_imgact_try	= NULL,
	.sv_minsigstksz	= MINSIGSTKSZ,
	.sv_minuser	= VM_MIN_ADDRESS,
	.sv_maxuser	= VM_MAXUSER_ADDRESS,
	.sv_usrstack	= USRSTACK,
	.sv_psstrings	= PS_STRINGS,
	.sv_stackprot	= VM_PROT_READ | VM_PROT_WRITE,
	.sv_copyout_strings	= exec_copyout_strings,
	.sv_setregs	= exec_setregs,
	.sv_fixlimit	= NULL,
	.sv_maxssiz	= NULL,
	.sv_flags	= SV_ABI_FREEBSD | SV_LP64 | SV_SHP | SV_ASLR,
	.sv_set_syscall_retval = cpu_set_syscall_retval,
	.sv_fetch_syscall_args = cpu_fetch_syscall_args,
	.sv_syscallnames = syscallnames,
	.sv_shared_page_base = SHAREDPAGE,
	.sv_shared_page_len = PAGE_SIZE,
	.sv_schedtail	= NULL,
	.sv_thread_detach = NULL,
	.sv_trap	= NULL,
};
INIT_SYSENTVEC(elf64_sysvec, &elf64_freebsd_sysvec);

static Elf64_Brandinfo freebsd_brand_info = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_RISCV,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/libexec/ld-elf.so.1",
	.sysvec		= &elf64_freebsd_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &elf64_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

SYSINIT(elf64, SI_SUB_EXEC, SI_ORDER_FIRST,
	(sysinit_cfunc_t) elf64_insert_brand_entry,
	&freebsd_brand_info);

static int debug_kld;
SYSCTL_INT(_kern, OID_AUTO, debug_kld,
	   CTLFLAG_RW, &debug_kld, 0,
	   "Activate debug prints in elf_reloc_internal()");

struct type2str_ent {
	int type;
	const char *str;
};

void
elf64_dump_thread(struct thread *td, void *dst, size_t *off)
{

}

/*
 * Following 4 functions are used to manupilate bits on 32bit interger value.
 * FIXME: I implemetend for ease-to-understand rather than for well-optimized.
 */
static uint32_t
gen_bitmask(int msb, int lsb)
{
	uint32_t mask;

	if (msb == sizeof(mask) * 8 - 1)
		mask = ~0;
	else
		mask = (1U << (msb + 1)) - 1;

	if (lsb > 0)
		mask &= ~((1U << lsb) - 1);

	return (mask);
}

static uint32_t
extract_bits(uint32_t x, int msb, int lsb)
{
	uint32_t mask;

	mask = gen_bitmask(msb, lsb);

	x &= mask;
	x >>= lsb;

	return (x);
}

static uint32_t
insert_bits(uint32_t d, uint32_t s, int msb, int lsb)
{
	uint32_t mask;

	mask = gen_bitmask(msb, lsb);

	d &= ~mask;

	s <<= lsb;
	s &= mask;

	return (d | s);
}

static uint32_t
insert_imm(uint32_t insn, uint32_t imm, int imm_msb, int imm_lsb,
    int insn_lsb)
{
	int insn_msb;
	uint32_t v;

	v = extract_bits(imm, imm_msb, imm_lsb);
	insn_msb = (imm_msb - imm_lsb) + insn_lsb;

	return (insert_bits(insn, v, insn_msb, insn_lsb));
}

/*
 * The RISC-V ISA is designed so that all of immediate values are
 * sign-extended.
 * An immediate value is sometimes generated at runtime by adding
 * 12bit sign integer and 20bit signed integer. This requests 20bit
 * immediate value to be ajusted if the MSB of the 12bit immediate
 * value is asserted (sign-extended value is treated as negative value).
 *
 * For example, 0x123800 can be calculated by adding upper 20 bit of
 * 0x124000 and sign-extended 12bit immediate whose bit pattern is
 * 0x800 as follows:
 *   0x123800
 *     = 0x123000 + 0x800
 *     = (0x123000 + 0x1000) + (-0x1000 + 0x800)
 *     = (0x123000 + 0x1000) + (0xff...ff800)
 *     = 0x124000            + sign-extention(0x800)
 */
static uint32_t
calc_hi20_imm(uint32_t value)
{
	/*
	 * There is the arithmetical hack that can remove conditional
	 * statement. But I implement it in straightforward way.
	 */
	if ((value & 0x800) != 0)
		value += 0x1000;
	return (value & ~0xfff);
}

static const struct type2str_ent t2s[] = {
	{ R_RISCV_NONE,		"R_RISCV_NONE"		},
	{ R_RISCV_64,		"R_RISCV_64"		},
	{ R_RISCV_JUMP_SLOT,	"R_RISCV_JUMP_SLOT"	},
	{ R_RISCV_RELATIVE,	"R_RISCV_RELATIVE"	},
	{ R_RISCV_JAL,		"R_RISCV_JAL"		},
	{ R_RISCV_CALL,		"R_RISCV_CALL"		},
	{ R_RISCV_PCREL_HI20,	"R_RISCV_PCREL_HI20"	},
	{ R_RISCV_PCREL_LO12_I,	"R_RISCV_PCREL_LO12_I"	},
	{ R_RISCV_PCREL_LO12_S,	"R_RISCV_PCREL_LO12_S"	},
	{ R_RISCV_HI20,		"R_RISCV_HI20"		},
	{ R_RISCV_LO12_I,	"R_RISCV_LO12_I"	},
	{ R_RISCV_LO12_S,	"R_RISCV_LO12_S"	},
};

static const char *
reloctype_to_str(int type)
{
	int i;

	for (i = 0; i < sizeof(t2s) / sizeof(t2s[0]); ++i) {
		if (type == t2s[i].type)
			return t2s[i].str;
	}

	return "*unknown*";
}

bool
elf_is_ifunc_reloc(Elf_Size r_info __unused)
{

	return (false);
}

/*
 * Currently kernel loadable module for RISCV is compiled with -fPIC option.
 * (see also additional CFLAGS definition for RISCV in sys/conf/kmod.mk)
 * Only R_RISCV_64, R_RISCV_JUMP_SLOT and RISCV_RELATIVE are emitted in
 * the module. Other relocations will be processed when kernel loadable
 * modules are built in non-PIC.
 *
 * FIXME: only RISCV64 is supported.
 */
static int
elf_reloc_internal(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, int local, elf_lookup_fn lookup)
{
	Elf_Size rtype, symidx;
	const Elf_Rela *rela;
	Elf_Addr val, addr;
	Elf64_Addr *where;
	Elf_Addr addend;
	uint32_t before32_1;
	uint32_t before32;
	uint64_t before64;
	uint32_t* insn32p;
	uint32_t imm20;
	int error;

	switch (type) {
	case ELF_RELOC_RELA:
		rela = (const Elf_Rela *)data;
		where = (Elf_Addr *)(relocbase + rela->r_offset);
		insn32p = (uint32_t*)where;
		addend = rela->r_addend;
		rtype = ELF_R_TYPE(rela->r_info);
		symidx = ELF_R_SYM(rela->r_info);
		break;
	default:
		printf("%s:%d unknown reloc type %d\n",
		       __FUNCTION__, __LINE__, type);
		return -1;
	}

	switch (rtype) {
	case R_RISCV_NONE:
		break;

	case R_RISCV_64:
	case R_RISCV_JUMP_SLOT:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return -1;

		val = addr;
		before64 = *where;
		if (*where != val)
			*where = val;

		if (debug_kld)
			printf("%p %c %-24s %016lx -> %016lx\n",
			       where,
			       (local? 'l': 'g'),
			       reloctype_to_str(rtype),
			       before64, *where);
		break;

	case R_RISCV_RELATIVE:
		before64 = *where;

		*where = elf_relocaddr(lf, relocbase + addend);

		if (debug_kld)
			printf("%p %c %-24s %016lx -> %016lx\n",
			       where,
			       (local? 'l': 'g'),
			       reloctype_to_str(rtype),
			       before64, *where);
		break;

	case R_RISCV_JAL:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return -1;

		val = addr - (Elf_Addr)where;
		if ((val <= -(1UL << 20) || (1UL << 20) <= val)) {
			printf("kldload: huge offset against R_RISCV_JAL\n");
			return -1;
		}

		before32 = *insn32p;
		*insn32p = insert_imm(*insn32p, val, 20, 20, 31);
		*insn32p = insert_imm(*insn32p, val, 10,  1, 21);
		*insn32p = insert_imm(*insn32p, val, 11, 11, 20);
		*insn32p = insert_imm(*insn32p, val, 19, 12, 12);

		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n",
			       where,
			       (local? 'l': 'g'),
			       reloctype_to_str(rtype),
			       before32, *insn32p);
		break;

	case R_RISCV_CALL:
		/*
		 * R_RISCV_CALL relocates 8-byte region that consists
		 * of the sequence of AUIPC and JALR.
		 */
		/* calculate and check the pc relative offset. */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return -1;
		val = addr - (Elf_Addr)where;
		if ((val <= -(1UL << 32) || (1UL << 32) <= val)) {
			printf("kldload: huge offset against R_RISCV_CALL\n");
			return -1;
		}

		/* Relocate AUIPC. */
		before32 = insn32p[0];
		imm20 = calc_hi20_imm(val);
		insn32p[0] = insert_imm(insn32p[0], imm20, 31, 12, 12);

		/* Relocate JALR. */
		before32_1 = insn32p[1];
		insn32p[1] = insert_imm(insn32p[1], val, 11,  0, 20);

		if (debug_kld)
			printf("%p %c %-24s %08x %08x -> %08x %08x\n",
			       where,
			       (local? 'l': 'g'),
			       reloctype_to_str(rtype),
			       before32,   insn32p[0],
			       before32_1, insn32p[1]);
		break;

	case R_RISCV_PCREL_HI20:
		val = addr - (Elf_Addr)where;
		insn32p = (uint32_t*)where;
		before32 = *insn32p;
		imm20 = calc_hi20_imm(val);
		*insn32p = insert_imm(*insn32p, imm20, 31, 12, 12);

		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n",
			       where,
			       (local? 'l': 'g'),
			       reloctype_to_str(rtype),
			       before32, *insn32p);
		break;

	case R_RISCV_PCREL_LO12_I:
		val = addr - (Elf_Addr)where;
		insn32p = (uint32_t*)where;
		before32 = *insn32p;
		*insn32p = insert_imm(*insn32p, addr, 11,  0, 20);

		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n",
			       where,
			       (local? 'l': 'g'),
			       reloctype_to_str(rtype),
			       before32, *insn32p);
		break;

	case R_RISCV_PCREL_LO12_S:
		val = addr - (Elf_Addr)where;
		insn32p = (uint32_t*)where;
		before32 = *insn32p;
		*insn32p = insert_imm(*insn32p, addr, 11,  5, 25);
		*insn32p = insert_imm(*insn32p, addr,  4,  0,  7);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n",
			       where,
			       (local? 'l': 'g'),
			       reloctype_to_str(rtype),
			       before32, *insn32p);
		break;

	case R_RISCV_HI20:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return -1;

		insn32p = (uint32_t*)where;
		before32 = *insn32p;
		imm20 = calc_hi20_imm(val);
		*insn32p = insert_imm(*insn32p, imm20, 31, 12, 12);

		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n",
			       where,
			       (local? 'l': 'g'),
			       reloctype_to_str(rtype),
			       before32, *insn32p);
		break;

	case R_RISCV_LO12_I:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return -1;

		val = addr;
		insn32p = (uint32_t*)where;
		before32 = *insn32p;
		*insn32p = insert_imm(*insn32p, addr, 11,  0, 20);

		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n",
			       where,
			       (local? 'l': 'g'),
			       reloctype_to_str(rtype),
			       before32, *insn32p);
		break;

	case R_RISCV_LO12_S:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return -1;

		val = addr;
		insn32p = (uint32_t*)where;
		before32 = *insn32p;
		*insn32p = insert_imm(*insn32p, addr, 11,  5, 25);
		*insn32p = insert_imm(*insn32p, addr,  4,  0,  7);

		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n",
			       where,
			       (local? 'l': 'g'),
			       reloctype_to_str(rtype),
			       before32, *insn32p);
		break;

	default:
		printf("kldload: unexpected relocation type %ld\n", rtype);
		return (-1);
	}

	return (0);
}

int
elf_reloc(linker_file_t lf, Elf_Addr relocbase, const void *data, int type,
    elf_lookup_fn lookup)
{

	return (elf_reloc_internal(lf, relocbase, data, type, 0, lookup));
}

int
elf_reloc_local(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, elf_lookup_fn lookup)
{

	return (elf_reloc_internal(lf, relocbase, data, type, 1, lookup));
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
