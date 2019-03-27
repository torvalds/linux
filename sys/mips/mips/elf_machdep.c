/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 1996-1998 John D. Polstra.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: src/sys/i386/i386/elf_machdep.c,v 1.20 2004/08/11 02:35:05 marcel
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/linker.h>
#include <sys/sysent.h>
#include <sys/imgact_elf.h>
#include <sys/proc.h>
#include <sys/syscall.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>

#include <machine/elf.h>
#include <machine/md_var.h>
#include <machine/cache.h>

#ifdef __mips_n64
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
	.sv_stackprot	= VM_PROT_ALL,
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
	.machine	= EM_MIPS,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/libexec/ld-elf.so.1",
	.sysvec		= &elf64_freebsd_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &elf64_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

SYSINIT(elf64, SI_SUB_EXEC, SI_ORDER_ANY,
    (sysinit_cfunc_t) elf64_insert_brand_entry,
    &freebsd_brand_info);

void
elf64_dump_thread(struct thread *td __unused, void *dst __unused,
    size_t *off __unused)
{
}
#else
struct sysentvec elf32_freebsd_sysvec = {
	.sv_size	= SYS_MAXSYSCALL,
	.sv_table	= sysent,
	.sv_errsize	= 0,
	.sv_errtbl	= NULL,
	.sv_transtrap	= NULL,
	.sv_fixup	= __elfN(freebsd_fixup),
	.sv_sendsig	= sendsig,
	.sv_sigcode	= sigcode,
	.sv_szsigcode	= &szsigcode,
	.sv_name	= "FreeBSD ELF32",
	.sv_coredump	= __elfN(coredump),
	.sv_imgact_try	= NULL,
	.sv_minsigstksz	= MINSIGSTKSZ,
	.sv_minuser	= VM_MIN_ADDRESS,
	.sv_maxuser	= VM_MAXUSER_ADDRESS,
	.sv_usrstack	= USRSTACK,
	.sv_psstrings	= PS_STRINGS,
	.sv_stackprot	= VM_PROT_ALL,
	.sv_copyout_strings = exec_copyout_strings,
	.sv_setregs	= exec_setregs,
	.sv_fixlimit	= NULL,
	.sv_maxssiz	= NULL,
	.sv_flags	= SV_ABI_FREEBSD | SV_ILP32 | SV_ASLR,
	.sv_set_syscall_retval = cpu_set_syscall_retval,
	.sv_fetch_syscall_args = cpu_fetch_syscall_args,
	.sv_syscallnames = syscallnames,
	.sv_schedtail	= NULL,
	.sv_thread_detach = NULL,
	.sv_trap	= NULL,
};

static Elf32_Brandinfo freebsd_brand_info = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_MIPS,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/libexec/ld-elf.so.1",
	.sysvec		= &elf32_freebsd_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &elf32_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

SYSINIT(elf32, SI_SUB_EXEC, SI_ORDER_FIRST,
    (sysinit_cfunc_t) elf32_insert_brand_entry,
    &freebsd_brand_info);

void
elf32_dump_thread(struct thread *td __unused, void *dst __unused,
    size_t *off __unused)
{
}
#endif

/*
 * The following MIPS relocation code for tracking multiple
 * consecutive HI32/LO32 entries is because of the following:
 *
 * https://dmz-portal.mips.com/wiki/MIPS_relocation_types
 *
 * ===
 *
 * + R_MIPS_HI16
 *
 * An R_MIPS_HI16 must be followed eventually by an associated R_MIPS_LO16
 * relocation record in the same SHT_REL section. The contents of the two
 * fields to be relocated are combined to form a full 32-bit addend AHL.
 * An R_MIPS_LO16 entry which does not immediately follow a R_MIPS_HI16 is
 * combined with the most recent one encountered, i.e. multiple R_MIPS_LO16
 * entries may be associated with a single R_MIPS_HI16. Use of these
 * relocation types in a SHT_REL section is discouraged and may be
 * forbidden to avoid this complication.
 *
 * A GNU extension allows multiple R_MIPS_HI16 records to share the same
 * R_MIPS_LO16 relocation record(s). The association works like this within
 * a single relocation section:
 *
 * + From the beginning of the section moving to the end of the section,
 *   until R_MIPS_LO16 is not found each found R_MIPS_HI16 relocation will
 *   be associated with the first R_MIPS_LO16.
 *
 * + Until another R_MIPS_HI16 record is found all found R_MIPS_LO16
 *   relocations found are associated with the last R_MIPS_HI16.
 *
 * ===
 *
 * This is so gcc can do dead code detection/removal without having to
 * generate HI/LO pairs even if one of them would be deleted.
 *
 * So, the summary is:
 *
 * + A HI16 entry must occur before any LO16 entries;
 * + Multiple consecutive HI16 RELA entries need to be buffered until the
 *   first LO16 RELA entry occurs - and then all HI16 RELA relocations use
 *   the offset in the LOW16 RELA for calculating their offsets;
 * + The last HI16 RELA entry before a LO16 RELA entry is used (the AHL)
 *   for the first subsequent LO16 calculation;
 * + If multiple consecutive LO16 RELA entries occur, only the first
 *   LO16 RELA entry triggers an update of buffered HI16 RELA entries;
 *   any subsequent LO16 RELA entry before another HI16 RELA entry will
 *   not cause any further updates to the HI16 RELA entries.
 *
 * Additionally, flush out any outstanding HI16 entries that don't have
 * a LO16 entry in case some garbage entries are left in the file.
 */

struct mips_tmp_reloc;
struct mips_tmp_reloc {
	struct mips_tmp_reloc *next;

	Elf_Addr ahl;
	Elf32_Addr *where_hi16;
};

static struct mips_tmp_reloc *ml = NULL;

/*
 * Add a temporary relocation (ie, a HI16 reloc type.)
 */
static int
mips_tmp_reloc_add(Elf_Addr ahl, Elf32_Addr *where_hi16)
{
	struct mips_tmp_reloc *r;

	r = malloc(sizeof(struct mips_tmp_reloc), M_TEMP, M_NOWAIT);
	if (r == NULL) {
		printf("%s: failed to malloc\n", __func__);
		return (0);
	}

	r->ahl = ahl;
	r->where_hi16 = where_hi16;
	r->next = ml;
	ml = r;

	return (1);
}

/*
 * Flush the temporary relocation list.
 *
 * This should be done after a file is completely loaded
 * so no stale relocations exist to confuse the next
 * load.
 */
static void
mips_tmp_reloc_flush(void)
{
	struct mips_tmp_reloc *r, *rn;

	r = ml;
	ml = NULL;
	while (r != NULL) {
		rn = r->next;
		free(r, M_TEMP);
		r = rn;
	}
}

/*
 * Get an entry from the reloc list; or NULL if we've run out.
 */
static struct mips_tmp_reloc *
mips_tmp_reloc_get(void)
{
	struct mips_tmp_reloc *r;

	r = ml;
	if (r == NULL)
		return (NULL);
	ml = ml->next;
	return (r);
}

/*
 * Free a relocation entry.
 */
static void
mips_tmp_reloc_free(struct mips_tmp_reloc *r)
{

	free(r, M_TEMP);
}

bool
elf_is_ifunc_reloc(Elf_Size r_info __unused)
{

	return (false);
}

/* Process one elf relocation with addend. */
static int
elf_reloc_internal(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, int local, elf_lookup_fn lookup)
{
	Elf32_Addr *where = (Elf32_Addr *)NULL;
	Elf_Addr addr;
	Elf_Addr addend = (Elf_Addr)0;
	Elf_Word rtype = (Elf_Word)0, symidx;
	struct mips_tmp_reloc *r;
	const Elf_Rel *rel = NULL;
	const Elf_Rela *rela = NULL;
	int error;

	/* Store the last seen ahl from a HI16 for LO16 processing */
	static Elf_Addr last_ahl;

	switch (type) {
	case ELF_RELOC_REL:
		rel = (const Elf_Rel *)data;
		where = (Elf32_Addr *) (relocbase + rel->r_offset);
		rtype = ELF_R_TYPE(rel->r_info);
		symidx = ELF_R_SYM(rel->r_info);
		switch (rtype) {
		case R_MIPS_64:
			addend = *(Elf64_Addr *)where;
			break;
		default:
			addend = *where;
			break;
		}

		break;
	case ELF_RELOC_RELA:
		rela = (const Elf_Rela *)data;
		where = (Elf32_Addr *) (relocbase + rela->r_offset);
		addend = rela->r_addend;
		rtype = ELF_R_TYPE(rela->r_info);
		symidx = ELF_R_SYM(rela->r_info);
		break;
	default:
		panic("unknown reloc type %d\n", type);
	}

	switch (rtype) {
	case R_MIPS_NONE:	/* none */
		break;

	case R_MIPS_32:		/* S + A */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		addr += addend;
		if (*where != addr)
			*where = (Elf32_Addr)addr;
		break;

	case R_MIPS_26:		/* ((A << 2) | (P & 0xf0000000) + S) >> 2 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);

		addend &= 0x03ffffff;
		/*
		 * Addendum for .rela R_MIPS_26 is not shifted right
		 */
		if (rela == NULL)
			addend <<= 2;

		addr += ((Elf_Addr)where & 0xf0000000) | addend;
		addr >>= 2;

		*where &= ~0x03ffffff;
		*where |= addr & 0x03ffffff;
		break;

	case R_MIPS_64:		/* S + A */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		addr += addend;
		if (*(Elf64_Addr*)where != addr)
			*(Elf64_Addr*)where = addr;
		break;

	/*
	 * Handle the two GNU extension cases:
	 *
	 * + Multiple HI16s followed by a LO16, and
	 * + A HI16 followed by multiple LO16s.
	 *
	 * The former is tricky - the HI16 relocations need
	 * to be buffered until a LO16 occurs, at which point
	 * each HI16 is replayed against the LO16 relocation entry
	 * (with the relevant overflow information.)
	 *
	 * The latter should be easy to handle - when the
	 * first LO16 is seen, write out and flush the
	 * HI16 buffer.  Any subsequent LO16 entries will
	 * find a blank relocation buffer.
	 *
	 */

	case R_MIPS_HI16:	/* ((AHL + S) - ((short)(AHL + S)) >> 16 */
		if (rela != NULL) {
			error = lookup(lf, symidx, 1, &addr);
			if (error != 0)
				return (-1);
			addr += addend;
			*where &= 0xffff0000;
			*where |= ((((long long) addr + 0x8000LL) >> 16) & 0xffff);
		} else {
			/*
			 * Add a temporary relocation to the list;
			 * will pop it off / free the list when
			 * we've found a suitable HI16.
			 */
			if (mips_tmp_reloc_add(addend << 16, where) == 0)
				return (-1);
			/*
			 * Track the last seen HI16 AHL for use by
			 * the first LO16 AHL calculation.
			 *
			 * The assumption is any intermediary deleted
			 * LO16's were optimised out, so the last
			 * HI16 before the LO16 is the "true" relocation
			 * entry to use for that LO16 write.
			 */
			last_ahl = addend << 16;
		}
		break;

	case R_MIPS_LO16:	/* AHL + S */
		if (rela != NULL) {
			error = lookup(lf, symidx, 1, &addr);
			if (error != 0)
				return (-1);
			addr += addend;
			*where &= 0xffff0000;
			*where |= addr & 0xffff;
		} else {
			Elf_Addr tmp_ahl;
			Elf_Addr tmp_addend;

			tmp_ahl = last_ahl + (int16_t) addend;
			error = lookup(lf, symidx, 1, &addr);
			if (error != 0)
				return (-1);

			tmp_addend = addend & 0xffff0000;

			/* Use the last seen ahl for calculating addend */
			tmp_addend |= (uint16_t)(tmp_ahl + addr);
			*where = tmp_addend;

			/*
			 * This logic implements the "we saw multiple HI16
			 * before a LO16" assignment /and/ "we saw multiple
			 * LO16s".
			 *
			 * Multiple LO16s will be handled as a blank
			 * relocation list.
			 *
			 * Multple HI16's are iterated over here.
			 */
			while ((r = mips_tmp_reloc_get()) != NULL) {
				Elf_Addr rahl;

				/*
				 * We have the ahl from the HI16 entry, so
				 * offset it by the 16 bits of the low ahl.
				 */
				rahl = r->ahl;
				rahl += (int16_t) addend;

				tmp_addend = *(r->where_hi16);
				tmp_addend &= 0xffff0000;
				tmp_addend |= ((rahl + addr) -
				    (int16_t)(rahl + addr)) >> 16;
				*(r->where_hi16) = tmp_addend;
				mips_tmp_reloc_free(r);
			}
		}

		break;

	case R_MIPS_HIGHER:	/* %higher(A+S) */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		addr += addend;
		*where &= 0xffff0000;
		*where |= (((long long)addr + 0x80008000LL) >> 32) & 0xffff;
		break;

	case R_MIPS_HIGHEST:	/* %highest(A+S) */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		addr += addend;
		*where &= 0xffff0000;
		*where |= (((long long)addr + 0x800080008000LL) >> 48) & 0xffff;
		break;

	default:
		printf("kldload: unexpected relocation type %d\n",
			rtype);
		return (-1);
	}

	return(0);
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

	/*
	 * Sync the I and D caches to make sure our relocations are visible.
	 */
	mips_icache_sync_all();

	/* Flush outstanding relocations */
	mips_tmp_reloc_flush();

	return (0);
}

int
elf_cpu_unload_file(linker_file_t lf __unused)
{

	return (0);
}
