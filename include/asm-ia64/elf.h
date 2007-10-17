#ifndef _ASM_IA64_ELF_H
#define _ASM_IA64_ELF_H

/*
 * ELF-specific definitions.
 *
 * Copyright (C) 1998-1999, 2002-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */


#include <asm/fpu.h>
#include <asm/page.h>
#include <asm/auxvec.h>

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x)->e_machine == EM_IA_64)

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS64
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_IA_64

#define USE_ELF_CORE_DUMP

/* Least-significant four bits of ELF header's e_flags are OS-specific.  The bits are
   interpreted as follows by Linux: */
#define EF_IA_64_LINUX_EXECUTABLE_STACK	0x1	/* is stack (& heap) executable by default? */

#define ELF_EXEC_PAGESIZE	PAGE_SIZE

/*
 * This is the location that an ET_DYN program is loaded if exec'ed.
 * Typical use of this is to invoke "./ld.so someprog" to test out a
 * new version of the loader.  We need to make sure that it is out of
 * the way of the program that it will "exec", and that there is
 * sufficient room for the brk.
 */
#define ELF_ET_DYN_BASE		(TASK_UNMAPPED_BASE + 0x800000000UL)

#define PT_IA_64_UNWIND		0x70000001

/* IA-64 relocations: */
#define R_IA64_NONE		0x00	/* none */
#define R_IA64_IMM14		0x21	/* symbol + addend, add imm14 */
#define R_IA64_IMM22		0x22	/* symbol + addend, add imm22 */
#define R_IA64_IMM64		0x23	/* symbol + addend, mov imm64 */
#define R_IA64_DIR32MSB		0x24	/* symbol + addend, data4 MSB */
#define R_IA64_DIR32LSB		0x25	/* symbol + addend, data4 LSB */
#define R_IA64_DIR64MSB		0x26	/* symbol + addend, data8 MSB */
#define R_IA64_DIR64LSB		0x27	/* symbol + addend, data8 LSB */
#define R_IA64_GPREL22		0x2a	/* @gprel(sym+add), add imm22 */
#define R_IA64_GPREL64I		0x2b	/* @gprel(sym+add), mov imm64 */
#define R_IA64_GPREL32MSB	0x2c	/* @gprel(sym+add), data4 MSB */
#define R_IA64_GPREL32LSB	0x2d	/* @gprel(sym+add), data4 LSB */
#define R_IA64_GPREL64MSB	0x2e	/* @gprel(sym+add), data8 MSB */
#define R_IA64_GPREL64LSB	0x2f	/* @gprel(sym+add), data8 LSB */
#define R_IA64_LTOFF22		0x32	/* @ltoff(sym+add), add imm22 */
#define R_IA64_LTOFF64I		0x33	/* @ltoff(sym+add), mov imm64 */
#define R_IA64_PLTOFF22		0x3a	/* @pltoff(sym+add), add imm22 */
#define R_IA64_PLTOFF64I	0x3b	/* @pltoff(sym+add), mov imm64 */
#define R_IA64_PLTOFF64MSB	0x3e	/* @pltoff(sym+add), data8 MSB */
#define R_IA64_PLTOFF64LSB	0x3f	/* @pltoff(sym+add), data8 LSB */
#define R_IA64_FPTR64I		0x43	/* @fptr(sym+add), mov imm64 */
#define R_IA64_FPTR32MSB	0x44	/* @fptr(sym+add), data4 MSB */
#define R_IA64_FPTR32LSB	0x45	/* @fptr(sym+add), data4 LSB */
#define R_IA64_FPTR64MSB	0x46	/* @fptr(sym+add), data8 MSB */
#define R_IA64_FPTR64LSB	0x47	/* @fptr(sym+add), data8 LSB */
#define R_IA64_PCREL60B		0x48	/* @pcrel(sym+add), brl */
#define R_IA64_PCREL21B		0x49	/* @pcrel(sym+add), ptb, call */
#define R_IA64_PCREL21M		0x4a	/* @pcrel(sym+add), chk.s */
#define R_IA64_PCREL21F		0x4b	/* @pcrel(sym+add), fchkf */
#define R_IA64_PCREL32MSB	0x4c	/* @pcrel(sym+add), data4 MSB */
#define R_IA64_PCREL32LSB	0x4d	/* @pcrel(sym+add), data4 LSB */
#define R_IA64_PCREL64MSB	0x4e	/* @pcrel(sym+add), data8 MSB */
#define R_IA64_PCREL64LSB	0x4f	/* @pcrel(sym+add), data8 LSB */
#define R_IA64_LTOFF_FPTR22	0x52	/* @ltoff(@fptr(s+a)), imm22 */
#define R_IA64_LTOFF_FPTR64I	0x53	/* @ltoff(@fptr(s+a)), imm64 */
#define R_IA64_LTOFF_FPTR32MSB	0x54	/* @ltoff(@fptr(s+a)), 4 MSB */
#define R_IA64_LTOFF_FPTR32LSB	0x55	/* @ltoff(@fptr(s+a)), 4 LSB */
#define R_IA64_LTOFF_FPTR64MSB	0x56	/* @ltoff(@fptr(s+a)), 8 MSB */
#define R_IA64_LTOFF_FPTR64LSB	0x57	/* @ltoff(@fptr(s+a)), 8 LSB */
#define R_IA64_SEGREL32MSB	0x5c	/* @segrel(sym+add), data4 MSB */
#define R_IA64_SEGREL32LSB	0x5d	/* @segrel(sym+add), data4 LSB */
#define R_IA64_SEGREL64MSB	0x5e	/* @segrel(sym+add), data8 MSB */
#define R_IA64_SEGREL64LSB	0x5f	/* @segrel(sym+add), data8 LSB */
#define R_IA64_SECREL32MSB	0x64	/* @secrel(sym+add), data4 MSB */
#define R_IA64_SECREL32LSB	0x65	/* @secrel(sym+add), data4 LSB */
#define R_IA64_SECREL64MSB	0x66	/* @secrel(sym+add), data8 MSB */
#define R_IA64_SECREL64LSB	0x67	/* @secrel(sym+add), data8 LSB */
#define R_IA64_REL32MSB		0x6c	/* data 4 + REL */
#define R_IA64_REL32LSB		0x6d	/* data 4 + REL */
#define R_IA64_REL64MSB		0x6e	/* data 8 + REL */
#define R_IA64_REL64LSB		0x6f	/* data 8 + REL */
#define R_IA64_LTV32MSB		0x74	/* symbol + addend, data4 MSB */
#define R_IA64_LTV32LSB		0x75	/* symbol + addend, data4 LSB */
#define R_IA64_LTV64MSB		0x76	/* symbol + addend, data8 MSB */
#define R_IA64_LTV64LSB		0x77	/* symbol + addend, data8 LSB */
#define R_IA64_PCREL21BI	0x79	/* @pcrel(sym+add), ptb, call */
#define R_IA64_PCREL22		0x7a	/* @pcrel(sym+add), imm22 */
#define R_IA64_PCREL64I		0x7b	/* @pcrel(sym+add), imm64 */
#define R_IA64_IPLTMSB		0x80	/* dynamic reloc, imported PLT, MSB */
#define R_IA64_IPLTLSB		0x81	/* dynamic reloc, imported PLT, LSB */
#define R_IA64_COPY		0x84	/* dynamic reloc, data copy */
#define R_IA64_SUB		0x85	/* -symbol + addend, add imm22 */
#define R_IA64_LTOFF22X		0x86	/* LTOFF22, relaxable.  */
#define R_IA64_LDXMOV		0x87	/* Use of LTOFF22X.  */
#define R_IA64_TPREL14		0x91	/* @tprel(sym+add), add imm14 */
#define R_IA64_TPREL22		0x92	/* @tprel(sym+add), add imm22 */
#define R_IA64_TPREL64I		0x93	/* @tprel(sym+add), add imm64 */
#define R_IA64_TPREL64MSB	0x96	/* @tprel(sym+add), data8 MSB */
#define R_IA64_TPREL64LSB	0x97	/* @tprel(sym+add), data8 LSB */
#define R_IA64_LTOFF_TPREL22	0x9a	/* @ltoff(@tprel(s+a)), add imm22 */
#define R_IA64_DTPMOD64MSB	0xa6	/* @dtpmod(sym+add), data8 MSB */
#define R_IA64_DTPMOD64LSB	0xa7	/* @dtpmod(sym+add), data8 LSB */
#define R_IA64_LTOFF_DTPMOD22	0xaa	/* @ltoff(@dtpmod(s+a)), imm22 */
#define R_IA64_DTPREL14		0xb1	/* @dtprel(sym+add), imm14 */
#define R_IA64_DTPREL22		0xb2	/* @dtprel(sym+add), imm22 */
#define R_IA64_DTPREL64I	0xb3	/* @dtprel(sym+add), imm64 */
#define R_IA64_DTPREL32MSB	0xb4	/* @dtprel(sym+add), data4 MSB */
#define R_IA64_DTPREL32LSB	0xb5	/* @dtprel(sym+add), data4 LSB */
#define R_IA64_DTPREL64MSB	0xb6	/* @dtprel(sym+add), data8 MSB */
#define R_IA64_DTPREL64LSB	0xb7	/* @dtprel(sym+add), data8 LSB */
#define R_IA64_LTOFF_DTPREL22	0xba	/* @ltoff(@dtprel(s+a)), imm22 */

/* IA-64 specific section flags: */
#define SHF_IA_64_SHORT		0x10000000	/* section near gp */

/*
 * We use (abuse?) this macro to insert the (empty) vm_area that is
 * used to map the register backing store.  I don't see any better
 * place to do this, but we should discuss this with Linus once we can
 * talk to him...
 */
extern void ia64_init_addr_space (void);
#define ELF_PLAT_INIT(_r, load_addr)	ia64_init_addr_space()

/* ELF register definitions.  This is needed for core dump support.  */

/*
 * elf_gregset_t contains the application-level state in the following order:
 *	r0-r31
 *	NaT bits (for r0-r31; bit N == 1 iff rN is a NaT)
 *	predicate registers (p0-p63)
 *	b0-b7
 *	ip cfm psr
 *	ar.rsc ar.bsp ar.bspstore ar.rnat
 *	ar.ccv ar.unat ar.fpsr ar.pfs ar.lc ar.ec ar.csd ar.ssd
 */
#define ELF_NGREG	128	/* we really need just 72 but let's leave some headroom... */
#define ELF_NFPREG	128	/* f0 and f1 could be omitted, but so what... */

typedef unsigned long elf_fpxregset_t;

typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct ia64_fpreg elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];



struct pt_regs;	/* forward declaration... */
extern void ia64_elf_core_copy_regs (struct pt_regs *src, elf_gregset_t dst);
#define ELF_CORE_COPY_REGS(_dest,_regs)	ia64_elf_core_copy_regs(_regs, _dest);

/* This macro yields a bitmask that programs can use to figure out
   what instruction set this CPU supports.  */
#define ELF_HWCAP 	0

/* This macro yields a string that ld.so will use to load
   implementation specific libraries for optimization.  Not terribly
   relevant until we have real hardware to play with... */
#define ELF_PLATFORM	NULL

#ifdef __KERNEL__
#define SET_PERSONALITY(ex, ibcs2)	set_personality(PER_LINUX)
#define elf_read_implies_exec(ex, executable_stack)					\
	((executable_stack!=EXSTACK_DISABLE_X) && ((ex).e_flags & EF_IA_64_LINUX_EXECUTABLE_STACK) != 0)

struct task_struct;

extern int dump_task_regs(struct task_struct *, elf_gregset_t *);
extern int dump_task_fpu (struct task_struct *, elf_fpregset_t *);

#define ELF_CORE_COPY_TASK_REGS(tsk, elf_gregs) dump_task_regs(tsk, elf_gregs)
#define ELF_CORE_COPY_FPREGS(tsk, elf_fpregs) dump_task_fpu(tsk, elf_fpregs)

#define GATE_EHDR	((const struct elfhdr *) GATE_ADDR)

/* update AT_VECTOR_SIZE_ARCH if the number of NEW_AUX_ENT entries changes */
#define ARCH_DLINFO								\
do {										\
	extern char __kernel_syscall_via_epc[];					\
	NEW_AUX_ENT(AT_SYSINFO, (unsigned long) __kernel_syscall_via_epc);	\
	NEW_AUX_ENT(AT_SYSINFO_EHDR, (unsigned long) GATE_EHDR);		\
} while (0)


/*
 * These macros parameterize elf_core_dump in fs/binfmt_elf.c to write out
 * extra segments containing the gate DSO contents.  Dumping its
 * contents makes post-mortem fully interpretable later without matching up
 * the same kernel and hardware config to see what PC values meant.
 * Dumping its extra ELF program headers includes all the other information
 * a debugger needs to easily find how the gate DSO was being used.
 */
#define ELF_CORE_EXTRA_PHDRS		(GATE_EHDR->e_phnum)
#define ELF_CORE_WRITE_EXTRA_PHDRS						\
do {										\
	const struct elf_phdr *const gate_phdrs =			      \
		(const struct elf_phdr *) (GATE_ADDR + GATE_EHDR->e_phoff);   \
	int i;									\
	Elf64_Off ofs = 0;						      \
	for (i = 0; i < GATE_EHDR->e_phnum; ++i) {				\
		struct elf_phdr phdr = gate_phdrs[i];			      \
		if (phdr.p_type == PT_LOAD) {					\
			phdr.p_memsz = PAGE_ALIGN(phdr.p_memsz);	      \
			phdr.p_filesz = phdr.p_memsz;			      \
			if (ofs == 0) {					      \
				ofs = phdr.p_offset = offset;		      \
			offset += phdr.p_filesz;				\
		}							      \
		else							      \
				phdr.p_offset = ofs;			      \
		}							      \
		else							      \
			phdr.p_offset += ofs;					\
		phdr.p_paddr = 0; /* match other core phdrs */			\
		DUMP_WRITE(&phdr, sizeof(phdr));				\
	}									\
} while (0)
#define ELF_CORE_WRITE_EXTRA_DATA					\
do {									\
	const struct elf_phdr *const gate_phdrs =			      \
		(const struct elf_phdr *) (GATE_ADDR + GATE_EHDR->e_phoff);   \
	int i;								\
	for (i = 0; i < GATE_EHDR->e_phnum; ++i) {			\
		if (gate_phdrs[i].p_type == PT_LOAD) {			      \
			DUMP_WRITE((void *) gate_phdrs[i].p_vaddr,	      \
				   PAGE_ALIGN(gate_phdrs[i].p_memsz));	      \
			break;						      \
		}							      \
	}								\
} while (0)

#endif /* __KERNEL__ */

#endif /* _ASM_IA64_ELF_H */
