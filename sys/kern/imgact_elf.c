/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2017 Dell EMC
 * Copyright (c) 2000-2001, 2003 David O'Brien
 * Copyright (c) 1995-1996 Søren Schmidt
 * Copyright (c) 1996 Peter Wemm
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_capsicum.h"

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/compressor.h>
#include <sys/exec.h>
#include <sys/fcntl.h>
#include <sys/imgact.h>
#include <sys/imgact_elf.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include <sys/namei.h>
#include <sys/pioctl.h>
#include <sys/proc.h>
#include <sys/procfs.h>
#include <sys/ptrace.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/sbuf.h>
#include <sys/sf_buf.h>
#include <sys/smp.h>
#include <sys/systm.h>
#include <sys/signalvar.h>
#include <sys/stat.h>
#include <sys/sx.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/vnode.h>
#include <sys/syslog.h>
#include <sys/eventhandler.h>
#include <sys/user.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>

#include <machine/elf.h>
#include <machine/md_var.h>

#define ELF_NOTE_ROUNDSIZE	4
#define OLD_EI_BRAND	8

static int __elfN(check_header)(const Elf_Ehdr *hdr);
static Elf_Brandinfo *__elfN(get_brandinfo)(struct image_params *imgp,
    const char *interp, int interp_name_len, int32_t *osrel, uint32_t *fctl0);
static int __elfN(load_file)(struct proc *p, const char *file, u_long *addr,
    u_long *entry);
static int __elfN(load_section)(struct image_params *imgp, vm_ooffset_t offset,
    caddr_t vmaddr, size_t memsz, size_t filsz, vm_prot_t prot);
static int __CONCAT(exec_, __elfN(imgact))(struct image_params *imgp);
static bool __elfN(freebsd_trans_osrel)(const Elf_Note *note,
    int32_t *osrel);
static bool kfreebsd_trans_osrel(const Elf_Note *note, int32_t *osrel);
static boolean_t __elfN(check_note)(struct image_params *imgp,
    Elf_Brandnote *checknote, int32_t *osrel, uint32_t *fctl0);
static vm_prot_t __elfN(trans_prot)(Elf_Word);
static Elf_Word __elfN(untrans_prot)(vm_prot_t);

SYSCTL_NODE(_kern, OID_AUTO, __CONCAT(elf, __ELF_WORD_SIZE), CTLFLAG_RW, 0,
    "");

#define	CORE_BUF_SIZE	(16 * 1024)

int __elfN(fallback_brand) = -1;
SYSCTL_INT(__CONCAT(_kern_elf, __ELF_WORD_SIZE), OID_AUTO,
    fallback_brand, CTLFLAG_RWTUN, &__elfN(fallback_brand), 0,
    __XSTRING(__CONCAT(ELF, __ELF_WORD_SIZE)) " brand of last resort");

static int elf_legacy_coredump = 0;
SYSCTL_INT(_debug, OID_AUTO, __elfN(legacy_coredump), CTLFLAG_RW, 
    &elf_legacy_coredump, 0,
    "include all and only RW pages in core dumps");

int __elfN(nxstack) =
#if defined(__amd64__) || defined(__powerpc64__) /* both 64 and 32 bit */ || \
    (defined(__arm__) && __ARM_ARCH >= 7) || defined(__aarch64__) || \
    defined(__riscv)
	1;
#else
	0;
#endif
SYSCTL_INT(__CONCAT(_kern_elf, __ELF_WORD_SIZE), OID_AUTO,
    nxstack, CTLFLAG_RW, &__elfN(nxstack), 0,
    __XSTRING(__CONCAT(ELF, __ELF_WORD_SIZE)) ": enable non-executable stack");

#if __ELF_WORD_SIZE == 32 && (defined(__amd64__) || defined(__i386__))
int i386_read_exec = 0;
SYSCTL_INT(_kern_elf32, OID_AUTO, read_exec, CTLFLAG_RW, &i386_read_exec, 0,
    "enable execution from readable segments");
#endif

SYSCTL_NODE(__CONCAT(_kern_elf, __ELF_WORD_SIZE), OID_AUTO, aslr, CTLFLAG_RW, 0,
    "");
#define	ASLR_NODE_OID	__CONCAT(__CONCAT(_kern_elf, __ELF_WORD_SIZE), _aslr)

static int __elfN(aslr_enabled) = 0;
SYSCTL_INT(ASLR_NODE_OID, OID_AUTO, enable, CTLFLAG_RWTUN,
    &__elfN(aslr_enabled), 0,
    __XSTRING(__CONCAT(ELF, __ELF_WORD_SIZE))
    ": enable address map randomization");

static int __elfN(pie_aslr_enabled) = 0;
SYSCTL_INT(ASLR_NODE_OID, OID_AUTO, pie_enable, CTLFLAG_RWTUN,
    &__elfN(pie_aslr_enabled), 0,
    __XSTRING(__CONCAT(ELF, __ELF_WORD_SIZE))
    ": enable address map randomization for PIE binaries");

static int __elfN(aslr_honor_sbrk) = 1;
SYSCTL_INT(ASLR_NODE_OID, OID_AUTO, honor_sbrk, CTLFLAG_RW,
    &__elfN(aslr_honor_sbrk), 0,
    __XSTRING(__CONCAT(ELF, __ELF_WORD_SIZE)) ": assume sbrk is used");

static Elf_Brandinfo *elf_brand_list[MAX_BRANDS];

#define	aligned(a, t)	(rounddown2((u_long)(a), sizeof(t)) == (u_long)(a))

static const char FREEBSD_ABI_VENDOR[] = "FreeBSD";

Elf_Brandnote __elfN(freebsd_brandnote) = {
	.hdr.n_namesz	= sizeof(FREEBSD_ABI_VENDOR),
	.hdr.n_descsz	= sizeof(int32_t),
	.hdr.n_type	= NT_FREEBSD_ABI_TAG,
	.vendor		= FREEBSD_ABI_VENDOR,
	.flags		= BN_TRANSLATE_OSREL,
	.trans_osrel	= __elfN(freebsd_trans_osrel)
};

static bool
__elfN(freebsd_trans_osrel)(const Elf_Note *note, int32_t *osrel)
{
	uintptr_t p;

	p = (uintptr_t)(note + 1);
	p += roundup2(note->n_namesz, ELF_NOTE_ROUNDSIZE);
	*osrel = *(const int32_t *)(p);

	return (true);
}

static const char GNU_ABI_VENDOR[] = "GNU";
static int GNU_KFREEBSD_ABI_DESC = 3;

Elf_Brandnote __elfN(kfreebsd_brandnote) = {
	.hdr.n_namesz	= sizeof(GNU_ABI_VENDOR),
	.hdr.n_descsz	= 16,	/* XXX at least 16 */
	.hdr.n_type	= 1,
	.vendor		= GNU_ABI_VENDOR,
	.flags		= BN_TRANSLATE_OSREL,
	.trans_osrel	= kfreebsd_trans_osrel
};

static bool
kfreebsd_trans_osrel(const Elf_Note *note, int32_t *osrel)
{
	const Elf32_Word *desc;
	uintptr_t p;

	p = (uintptr_t)(note + 1);
	p += roundup2(note->n_namesz, ELF_NOTE_ROUNDSIZE);

	desc = (const Elf32_Word *)p;
	if (desc[0] != GNU_KFREEBSD_ABI_DESC)
		return (false);

	/*
	 * Debian GNU/kFreeBSD embed the earliest compatible kernel version
	 * (__FreeBSD_version: <major><two digit minor>Rxx) in the LSB way.
	 */
	*osrel = desc[1] * 100000 + desc[2] * 1000 + desc[3];

	return (true);
}

int
__elfN(insert_brand_entry)(Elf_Brandinfo *entry)
{
	int i;

	for (i = 0; i < MAX_BRANDS; i++) {
		if (elf_brand_list[i] == NULL) {
			elf_brand_list[i] = entry;
			break;
		}
	}
	if (i == MAX_BRANDS) {
		printf("WARNING: %s: could not insert brandinfo entry: %p\n",
			__func__, entry);
		return (-1);
	}
	return (0);
}

int
__elfN(remove_brand_entry)(Elf_Brandinfo *entry)
{
	int i;

	for (i = 0; i < MAX_BRANDS; i++) {
		if (elf_brand_list[i] == entry) {
			elf_brand_list[i] = NULL;
			break;
		}
	}
	if (i == MAX_BRANDS)
		return (-1);
	return (0);
}

int
__elfN(brand_inuse)(Elf_Brandinfo *entry)
{
	struct proc *p;
	int rval = FALSE;

	sx_slock(&allproc_lock);
	FOREACH_PROC_IN_SYSTEM(p) {
		if (p->p_sysent == entry->sysvec) {
			rval = TRUE;
			break;
		}
	}
	sx_sunlock(&allproc_lock);

	return (rval);
}

static Elf_Brandinfo *
__elfN(get_brandinfo)(struct image_params *imgp, const char *interp,
    int interp_name_len, int32_t *osrel, uint32_t *fctl0)
{
	const Elf_Ehdr *hdr = (const Elf_Ehdr *)imgp->image_header;
	Elf_Brandinfo *bi, *bi_m;
	boolean_t ret;
	int i;

	/*
	 * We support four types of branding -- (1) the ELF EI_OSABI field
	 * that SCO added to the ELF spec, (2) FreeBSD 3.x's traditional string
	 * branding w/in the ELF header, (3) path of the `interp_path'
	 * field, and (4) the ".note.ABI-tag" ELF section.
	 */

	/* Look for an ".note.ABI-tag" ELF section */
	bi_m = NULL;
	for (i = 0; i < MAX_BRANDS; i++) {
		bi = elf_brand_list[i];
		if (bi == NULL)
			continue;
		if (interp != NULL && (bi->flags & BI_BRAND_ONLY_STATIC) != 0)
			continue;
		if (hdr->e_machine == bi->machine && (bi->flags &
		    (BI_BRAND_NOTE|BI_BRAND_NOTE_MANDATORY)) != 0) {
			ret = __elfN(check_note)(imgp, bi->brand_note, osrel,
			    fctl0);
			/* Give brand a chance to veto check_note's guess */
			if (ret && bi->header_supported)
				ret = bi->header_supported(imgp);
			/*
			 * If note checker claimed the binary, but the
			 * interpreter path in the image does not
			 * match default one for the brand, try to
			 * search for other brands with the same
			 * interpreter.  Either there is better brand
			 * with the right interpreter, or, failing
			 * this, we return first brand which accepted
			 * our note and, optionally, header.
			 */
			if (ret && bi_m == NULL && interp != NULL &&
			    (bi->interp_path == NULL ||
			    (strlen(bi->interp_path) + 1 != interp_name_len ||
			    strncmp(interp, bi->interp_path, interp_name_len)
			    != 0))) {
				bi_m = bi;
				ret = 0;
			}
			if (ret)
				return (bi);
		}
	}
	if (bi_m != NULL)
		return (bi_m);

	/* If the executable has a brand, search for it in the brand list. */
	for (i = 0; i < MAX_BRANDS; i++) {
		bi = elf_brand_list[i];
		if (bi == NULL || (bi->flags & BI_BRAND_NOTE_MANDATORY) != 0 ||
		    (interp != NULL && (bi->flags & BI_BRAND_ONLY_STATIC) != 0))
			continue;
		if (hdr->e_machine == bi->machine &&
		    (hdr->e_ident[EI_OSABI] == bi->brand ||
		    (bi->compat_3_brand != NULL &&
		    strcmp((const char *)&hdr->e_ident[OLD_EI_BRAND],
		    bi->compat_3_brand) == 0))) {
			/* Looks good, but give brand a chance to veto */
			if (bi->header_supported == NULL ||
			    bi->header_supported(imgp)) {
				/*
				 * Again, prefer strictly matching
				 * interpreter path.
				 */
				if (interp_name_len == 0 &&
				    bi->interp_path == NULL)
					return (bi);
				if (bi->interp_path != NULL &&
				    strlen(bi->interp_path) + 1 ==
				    interp_name_len && strncmp(interp,
				    bi->interp_path, interp_name_len) == 0)
					return (bi);
				if (bi_m == NULL)
					bi_m = bi;
			}
		}
	}
	if (bi_m != NULL)
		return (bi_m);

	/* No known brand, see if the header is recognized by any brand */
	for (i = 0; i < MAX_BRANDS; i++) {
		bi = elf_brand_list[i];
		if (bi == NULL || bi->flags & BI_BRAND_NOTE_MANDATORY ||
		    bi->header_supported == NULL)
			continue;
		if (hdr->e_machine == bi->machine) {
			ret = bi->header_supported(imgp);
			if (ret)
				return (bi);
		}
	}

	/* Lacking a known brand, search for a recognized interpreter. */
	if (interp != NULL) {
		for (i = 0; i < MAX_BRANDS; i++) {
			bi = elf_brand_list[i];
			if (bi == NULL || (bi->flags &
			    (BI_BRAND_NOTE_MANDATORY | BI_BRAND_ONLY_STATIC))
			    != 0)
				continue;
			if (hdr->e_machine == bi->machine &&
			    bi->interp_path != NULL &&
			    /* ELF image p_filesz includes terminating zero */
			    strlen(bi->interp_path) + 1 == interp_name_len &&
			    strncmp(interp, bi->interp_path, interp_name_len)
			    == 0 && (bi->header_supported == NULL ||
			    bi->header_supported(imgp)))
				return (bi);
		}
	}

	/* Lacking a recognized interpreter, try the default brand */
	for (i = 0; i < MAX_BRANDS; i++) {
		bi = elf_brand_list[i];
		if (bi == NULL || (bi->flags & BI_BRAND_NOTE_MANDATORY) != 0 ||
		    (interp != NULL && (bi->flags & BI_BRAND_ONLY_STATIC) != 0))
			continue;
		if (hdr->e_machine == bi->machine &&
		    __elfN(fallback_brand) == bi->brand &&
		    (bi->header_supported == NULL ||
		    bi->header_supported(imgp)))
			return (bi);
	}
	return (NULL);
}

static int
__elfN(check_header)(const Elf_Ehdr *hdr)
{
	Elf_Brandinfo *bi;
	int i;

	if (!IS_ELF(*hdr) ||
	    hdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||
	    hdr->e_ident[EI_DATA] != ELF_TARG_DATA ||
	    hdr->e_ident[EI_VERSION] != EV_CURRENT ||
	    hdr->e_phentsize != sizeof(Elf_Phdr) ||
	    hdr->e_version != ELF_TARG_VER)
		return (ENOEXEC);

	/*
	 * Make sure we have at least one brand for this machine.
	 */

	for (i = 0; i < MAX_BRANDS; i++) {
		bi = elf_brand_list[i];
		if (bi != NULL && bi->machine == hdr->e_machine)
			break;
	}
	if (i == MAX_BRANDS)
		return (ENOEXEC);

	return (0);
}

static int
__elfN(map_partial)(vm_map_t map, vm_object_t object, vm_ooffset_t offset,
    vm_offset_t start, vm_offset_t end, vm_prot_t prot)
{
	struct sf_buf *sf;
	int error;
	vm_offset_t off;

	/*
	 * Create the page if it doesn't exist yet. Ignore errors.
	 */
	vm_map_fixed(map, NULL, 0, trunc_page(start), round_page(end) -
	    trunc_page(start), VM_PROT_ALL, VM_PROT_ALL, MAP_CHECK_EXCL);

	/*
	 * Find the page from the underlying object.
	 */
	if (object != NULL) {
		sf = vm_imgact_map_page(object, offset);
		if (sf == NULL)
			return (KERN_FAILURE);
		off = offset - trunc_page(offset);
		error = copyout((caddr_t)sf_buf_kva(sf) + off, (caddr_t)start,
		    end - start);
		vm_imgact_unmap_page(sf);
		if (error != 0)
			return (KERN_FAILURE);
	}

	return (KERN_SUCCESS);
}

static int
__elfN(map_insert)(struct image_params *imgp, vm_map_t map, vm_object_t object,
    vm_ooffset_t offset, vm_offset_t start, vm_offset_t end, vm_prot_t prot,
    int cow)
{
	struct sf_buf *sf;
	vm_offset_t off;
	vm_size_t sz;
	int error, locked, rv;

	if (start != trunc_page(start)) {
		rv = __elfN(map_partial)(map, object, offset, start,
		    round_page(start), prot);
		if (rv != KERN_SUCCESS)
			return (rv);
		offset += round_page(start) - start;
		start = round_page(start);
	}
	if (end != round_page(end)) {
		rv = __elfN(map_partial)(map, object, offset +
		    trunc_page(end) - start, trunc_page(end), end, prot);
		if (rv != KERN_SUCCESS)
			return (rv);
		end = trunc_page(end);
	}
	if (start >= end)
		return (KERN_SUCCESS);
	if ((offset & PAGE_MASK) != 0) {
		/*
		 * The mapping is not page aligned.  This means that we have
		 * to copy the data.
		 */
		rv = vm_map_fixed(map, NULL, 0, start, end - start,
		    prot | VM_PROT_WRITE, VM_PROT_ALL, MAP_CHECK_EXCL);
		if (rv != KERN_SUCCESS)
			return (rv);
		if (object == NULL)
			return (KERN_SUCCESS);
		for (; start < end; start += sz) {
			sf = vm_imgact_map_page(object, offset);
			if (sf == NULL)
				return (KERN_FAILURE);
			off = offset - trunc_page(offset);
			sz = end - start;
			if (sz > PAGE_SIZE - off)
				sz = PAGE_SIZE - off;
			error = copyout((caddr_t)sf_buf_kva(sf) + off,
			    (caddr_t)start, sz);
			vm_imgact_unmap_page(sf);
			if (error != 0)
				return (KERN_FAILURE);
			offset += sz;
		}
	} else {
		vm_object_reference(object);
		rv = vm_map_fixed(map, object, offset, start, end - start,
		    prot, VM_PROT_ALL, cow | MAP_CHECK_EXCL);
		if (rv != KERN_SUCCESS) {
			locked = VOP_ISLOCKED(imgp->vp);
			VOP_UNLOCK(imgp->vp, 0);
			vm_object_deallocate(object);
			vn_lock(imgp->vp, locked | LK_RETRY);
			return (rv);
		}
	}
	return (KERN_SUCCESS);
}

static int
__elfN(load_section)(struct image_params *imgp, vm_ooffset_t offset,
    caddr_t vmaddr, size_t memsz, size_t filsz, vm_prot_t prot)
{
	struct sf_buf *sf;
	size_t map_len;
	vm_map_t map;
	vm_object_t object;
	vm_offset_t off, map_addr;
	int error, rv, cow;
	size_t copy_len;
	vm_ooffset_t file_addr;

	/*
	 * It's necessary to fail if the filsz + offset taken from the
	 * header is greater than the actual file pager object's size.
	 * If we were to allow this, then the vm_map_find() below would
	 * walk right off the end of the file object and into the ether.
	 *
	 * While I'm here, might as well check for something else that
	 * is invalid: filsz cannot be greater than memsz.
	 */
	if ((filsz != 0 && (off_t)filsz + offset > imgp->attr->va_size) ||
	    filsz > memsz) {
		uprintf("elf_load_section: truncated ELF file\n");
		return (ENOEXEC);
	}

	object = imgp->object;
	map = &imgp->proc->p_vmspace->vm_map;
	map_addr = trunc_page((vm_offset_t)vmaddr);
	file_addr = trunc_page(offset);

	/*
	 * We have two choices.  We can either clear the data in the last page
	 * of an oversized mapping, or we can start the anon mapping a page
	 * early and copy the initialized data into that first page.  We
	 * choose the second.
	 */
	if (filsz == 0)
		map_len = 0;
	else if (memsz > filsz)
		map_len = trunc_page(offset + filsz) - file_addr;
	else
		map_len = round_page(offset + filsz) - file_addr;

	if (map_len != 0) {
		/* cow flags: don't dump readonly sections in core */
		cow = MAP_COPY_ON_WRITE | MAP_PREFAULT |
		    (prot & VM_PROT_WRITE ? 0 : MAP_DISABLE_COREDUMP);

		rv = __elfN(map_insert)(imgp, map,
				      object,
				      file_addr,	/* file offset */
				      map_addr,		/* virtual start */
				      map_addr + map_len,/* virtual end */
				      prot,
				      cow);
		if (rv != KERN_SUCCESS)
			return (EINVAL);

		/* we can stop now if we've covered it all */
		if (memsz == filsz)
			return (0);
	}


	/*
	 * We have to get the remaining bit of the file into the first part
	 * of the oversized map segment.  This is normally because the .data
	 * segment in the file is extended to provide bss.  It's a neat idea
	 * to try and save a page, but it's a pain in the behind to implement.
	 */
	copy_len = filsz == 0 ? 0 : (offset + filsz) - trunc_page(offset +
	    filsz);
	map_addr = trunc_page((vm_offset_t)vmaddr + filsz);
	map_len = round_page((vm_offset_t)vmaddr + memsz) - map_addr;

	/* This had damn well better be true! */
	if (map_len != 0) {
		rv = __elfN(map_insert)(imgp, map, NULL, 0, map_addr,
		    map_addr + map_len, prot, 0);
		if (rv != KERN_SUCCESS)
			return (EINVAL);
	}

	if (copy_len != 0) {
		sf = vm_imgact_map_page(object, offset + filsz);
		if (sf == NULL)
			return (EIO);

		/* send the page fragment to user space */
		off = trunc_page(offset + filsz) - trunc_page(offset + filsz);
		error = copyout((caddr_t)sf_buf_kva(sf) + off,
		    (caddr_t)map_addr, copy_len);
		vm_imgact_unmap_page(sf);
		if (error != 0)
			return (error);
	}

	/*
	 * Remove write access to the page if it was only granted by map_insert
	 * to allow copyout.
	 */
	if ((prot & VM_PROT_WRITE) == 0)
		vm_map_protect(map, trunc_page(map_addr), round_page(map_addr +
		    map_len), prot, FALSE);

	return (0);
}

/*
 * Load the file "file" into memory.  It may be either a shared object
 * or an executable.
 *
 * The "addr" reference parameter is in/out.  On entry, it specifies
 * the address where a shared object should be loaded.  If the file is
 * an executable, this value is ignored.  On exit, "addr" specifies
 * where the file was actually loaded.
 *
 * The "entry" reference parameter is out only.  On exit, it specifies
 * the entry point for the loaded file.
 */
static int
__elfN(load_file)(struct proc *p, const char *file, u_long *addr,
	u_long *entry)
{
	struct {
		struct nameidata nd;
		struct vattr attr;
		struct image_params image_params;
	} *tempdata;
	const Elf_Ehdr *hdr = NULL;
	const Elf_Phdr *phdr = NULL;
	struct nameidata *nd;
	struct vattr *attr;
	struct image_params *imgp;
	vm_prot_t prot;
	u_long rbase;
	u_long base_addr = 0;
	int error, i, numsegs;

#ifdef CAPABILITY_MODE
	/*
	 * XXXJA: This check can go away once we are sufficiently confident
	 * that the checks in namei() are correct.
	 */
	if (IN_CAPABILITY_MODE(curthread))
		return (ECAPMODE);
#endif

	tempdata = malloc(sizeof(*tempdata), M_TEMP, M_WAITOK);
	nd = &tempdata->nd;
	attr = &tempdata->attr;
	imgp = &tempdata->image_params;

	/*
	 * Initialize part of the common data
	 */
	imgp->proc = p;
	imgp->attr = attr;
	imgp->firstpage = NULL;
	imgp->image_header = NULL;
	imgp->object = NULL;
	imgp->execlabel = NULL;

	NDINIT(nd, LOOKUP, LOCKLEAF | FOLLOW, UIO_SYSSPACE, file, curthread);
	if ((error = namei(nd)) != 0) {
		nd->ni_vp = NULL;
		goto fail;
	}
	NDFREE(nd, NDF_ONLY_PNBUF);
	imgp->vp = nd->ni_vp;

	/*
	 * Check permissions, modes, uid, etc on the file, and "open" it.
	 */
	error = exec_check_permissions(imgp);
	if (error)
		goto fail;

	error = exec_map_first_page(imgp);
	if (error)
		goto fail;

	/*
	 * Also make certain that the interpreter stays the same, so set
	 * its VV_TEXT flag, too.
	 */
	VOP_SET_TEXT(nd->ni_vp);

	imgp->object = nd->ni_vp->v_object;

	hdr = (const Elf_Ehdr *)imgp->image_header;
	if ((error = __elfN(check_header)(hdr)) != 0)
		goto fail;
	if (hdr->e_type == ET_DYN)
		rbase = *addr;
	else if (hdr->e_type == ET_EXEC)
		rbase = 0;
	else {
		error = ENOEXEC;
		goto fail;
	}

	/* Only support headers that fit within first page for now      */
	if ((hdr->e_phoff > PAGE_SIZE) ||
	    (u_int)hdr->e_phentsize * hdr->e_phnum > PAGE_SIZE - hdr->e_phoff) {
		error = ENOEXEC;
		goto fail;
	}

	phdr = (const Elf_Phdr *)(imgp->image_header + hdr->e_phoff);
	if (!aligned(phdr, Elf_Addr)) {
		error = ENOEXEC;
		goto fail;
	}

	for (i = 0, numsegs = 0; i < hdr->e_phnum; i++) {
		if (phdr[i].p_type == PT_LOAD && phdr[i].p_memsz != 0) {
			/* Loadable segment */
			prot = __elfN(trans_prot)(phdr[i].p_flags);
			error = __elfN(load_section)(imgp, phdr[i].p_offset,
			    (caddr_t)(uintptr_t)phdr[i].p_vaddr + rbase,
			    phdr[i].p_memsz, phdr[i].p_filesz, prot);
			if (error != 0)
				goto fail;
			/*
			 * Establish the base address if this is the
			 * first segment.
			 */
			if (numsegs == 0)
  				base_addr = trunc_page(phdr[i].p_vaddr +
				    rbase);
			numsegs++;
		}
	}
	*addr = base_addr;
	*entry = (unsigned long)hdr->e_entry + rbase;

fail:
	if (imgp->firstpage)
		exec_unmap_first_page(imgp);

	if (nd->ni_vp)
		vput(nd->ni_vp);

	free(tempdata, M_TEMP);

	return (error);
}

static u_long
__CONCAT(rnd_, __elfN(base))(vm_map_t map __unused, u_long minv, u_long maxv,
    u_int align)
{
	u_long rbase, res;

	MPASS(vm_map_min(map) <= minv);
	MPASS(maxv <= vm_map_max(map));
	MPASS(minv < maxv);
	MPASS(minv + align < maxv);
	arc4rand(&rbase, sizeof(rbase), 0);
	res = roundup(minv, (u_long)align) + rbase % (maxv - minv);
	res &= ~((u_long)align - 1);
	if (res >= maxv)
		res -= align;
	KASSERT(res >= minv,
	    ("res %#lx < minv %#lx, maxv %#lx rbase %#lx",
	    res, minv, maxv, rbase));
	KASSERT(res < maxv,
	    ("res %#lx > maxv %#lx, minv %#lx rbase %#lx",
	    res, maxv, minv, rbase));
	return (res);
}

static int
__elfN(enforce_limits)(struct image_params *imgp, const Elf_Ehdr *hdr,
    const Elf_Phdr *phdr, u_long et_dyn_addr)
{
	struct vmspace *vmspace;
	const char *err_str;
	u_long text_size, data_size, total_size, text_addr, data_addr;
	u_long seg_size, seg_addr;
	int i;

	err_str = NULL;
	text_size = data_size = total_size = text_addr = data_addr = 0;

	for (i = 0; i < hdr->e_phnum; i++) {
		if (phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0)
			continue;

		seg_addr = trunc_page(phdr[i].p_vaddr + et_dyn_addr);
		seg_size = round_page(phdr[i].p_memsz +
		    phdr[i].p_vaddr + et_dyn_addr - seg_addr);

		/*
		 * Make the largest executable segment the official
		 * text segment and all others data.
		 *
		 * Note that obreak() assumes that data_addr + data_size == end
		 * of data load area, and the ELF file format expects segments
		 * to be sorted by address.  If multiple data segments exist,
		 * the last one will be used.
		 */

		if ((phdr[i].p_flags & PF_X) != 0 && text_size < seg_size) {
			text_size = seg_size;
			text_addr = seg_addr;
		} else {
			data_size = seg_size;
			data_addr = seg_addr;
		}
		total_size += seg_size;
	}
	
	if (data_addr == 0 && data_size == 0) {
		data_addr = text_addr;
		data_size = text_size;
	}

	/*
	 * Check limits.  It should be safe to check the
	 * limits after loading the segments since we do
	 * not actually fault in all the segments pages.
	 */
	PROC_LOCK(imgp->proc);
	if (data_size > lim_cur_proc(imgp->proc, RLIMIT_DATA))
		err_str = "Data segment size exceeds process limit";
	else if (text_size > maxtsiz)
		err_str = "Text segment size exceeds system limit";
	else if (total_size > lim_cur_proc(imgp->proc, RLIMIT_VMEM))
		err_str = "Total segment size exceeds process limit";
	else if (racct_set(imgp->proc, RACCT_DATA, data_size) != 0)
		err_str = "Data segment size exceeds resource limit";
	else if (racct_set(imgp->proc, RACCT_VMEM, total_size) != 0)
		err_str = "Total segment size exceeds resource limit";
	PROC_UNLOCK(imgp->proc);
	if (err_str != NULL) {
		uprintf("%s\n", err_str);
		return (ENOMEM);
	}

	vmspace = imgp->proc->p_vmspace;
	vmspace->vm_tsize = text_size >> PAGE_SHIFT;
	vmspace->vm_taddr = (caddr_t)(uintptr_t)text_addr;
	vmspace->vm_dsize = data_size >> PAGE_SHIFT;
	vmspace->vm_daddr = (caddr_t)(uintptr_t)data_addr;

	return (0);
}

/*
 * Impossible et_dyn_addr initial value indicating that the real base
 * must be calculated later with some randomization applied.
 */
#define	ET_DYN_ADDR_RAND	1

static int
__CONCAT(exec_, __elfN(imgact))(struct image_params *imgp)
{
	struct thread *td;
	const Elf_Ehdr *hdr;
	const Elf_Phdr *phdr;
	Elf_Auxargs *elf_auxargs;
	struct vmspace *vmspace;
	vm_map_t map;
	const char *newinterp;
	char *interp, *interp_buf, *path;
	Elf_Brandinfo *brand_info;
	struct sysentvec *sv;
	vm_prot_t prot;
	u_long addr, baddr, et_dyn_addr, entry, proghdr;
	u_long maxalign, mapsz, maxv, maxv1;
	uint32_t fctl0;
	int32_t osrel;
	int error, i, n, interp_name_len, have_interp;

	hdr = (const Elf_Ehdr *)imgp->image_header;

	/*
	 * Do we have a valid ELF header ?
	 *
	 * Only allow ET_EXEC & ET_DYN here, reject ET_DYN later
	 * if particular brand doesn't support it.
	 */
	if (__elfN(check_header)(hdr) != 0 ||
	    (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN))
		return (-1);

	/*
	 * From here on down, we return an errno, not -1, as we've
	 * detected an ELF file.
	 */

	if ((hdr->e_phoff > PAGE_SIZE) ||
	    (u_int)hdr->e_phentsize * hdr->e_phnum > PAGE_SIZE - hdr->e_phoff) {
		/* Only support headers in first page for now */
		uprintf("Program headers not in the first page\n");
		return (ENOEXEC);
	}
	phdr = (const Elf_Phdr *)(imgp->image_header + hdr->e_phoff); 
	if (!aligned(phdr, Elf_Addr)) {
		uprintf("Unaligned program headers\n");
		return (ENOEXEC);
	}

	n = error = 0;
	baddr = 0;
	osrel = 0;
	fctl0 = 0;
	entry = proghdr = 0;
	interp_name_len = 0;
	newinterp = NULL;
	interp = interp_buf = NULL;
	td = curthread;
	maxalign = PAGE_SIZE;
	mapsz = 0;

	for (i = 0; i < hdr->e_phnum; i++) {
		switch (phdr[i].p_type) {
		case PT_LOAD:
			if (n == 0)
				baddr = phdr[i].p_vaddr;
			if (phdr[i].p_align > maxalign)
				maxalign = phdr[i].p_align;
			mapsz += phdr[i].p_memsz;
			n++;
			break;
		case PT_INTERP:
			/* Path to interpreter */
			if (phdr[i].p_filesz < 2 ||
			    phdr[i].p_filesz > MAXPATHLEN) {
				uprintf("Invalid PT_INTERP\n");
				error = ENOEXEC;
				goto ret;
			}
			if (interp != NULL) {
				uprintf("Multiple PT_INTERP headers\n");
				error = ENOEXEC;
				goto ret;
			}
			interp_name_len = phdr[i].p_filesz;
			if (phdr[i].p_offset > PAGE_SIZE ||
			    interp_name_len > PAGE_SIZE - phdr[i].p_offset) {
				VOP_UNLOCK(imgp->vp, 0);
				interp_buf = malloc(interp_name_len + 1, M_TEMP,
				    M_WAITOK);
				vn_lock(imgp->vp, LK_EXCLUSIVE | LK_RETRY);
				error = vn_rdwr(UIO_READ, imgp->vp, interp_buf,
				    interp_name_len, phdr[i].p_offset,
				    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred,
				    NOCRED, NULL, td);
				if (error != 0) {
					uprintf("i/o error PT_INTERP %d\n",
					    error);
					goto ret;
				}
				interp_buf[interp_name_len] = '\0';
				interp = interp_buf;
			} else {
				interp = __DECONST(char *, imgp->image_header) +
				    phdr[i].p_offset;
				if (interp[interp_name_len - 1] != '\0') {
					uprintf("Invalid PT_INTERP\n");
					error = ENOEXEC;
					goto ret;
				}
			}
			break;
		case PT_GNU_STACK:
			if (__elfN(nxstack))
				imgp->stack_prot =
				    __elfN(trans_prot)(phdr[i].p_flags);
			imgp->stack_sz = phdr[i].p_memsz;
			break;
		}
	}

	brand_info = __elfN(get_brandinfo)(imgp, interp, interp_name_len,
	    &osrel, &fctl0);
	if (brand_info == NULL) {
		uprintf("ELF binary type \"%u\" not known.\n",
		    hdr->e_ident[EI_OSABI]);
		error = ENOEXEC;
		goto ret;
	}
	sv = brand_info->sysvec;
	et_dyn_addr = 0;
	if (hdr->e_type == ET_DYN) {
		if ((brand_info->flags & BI_CAN_EXEC_DYN) == 0) {
			uprintf("Cannot execute shared object\n");
			error = ENOEXEC;
			goto ret;
		}
		/*
		 * Honour the base load address from the dso if it is
		 * non-zero for some reason.
		 */
		if (baddr == 0) {
			if ((sv->sv_flags & SV_ASLR) == 0 ||
			    (fctl0 & NT_FREEBSD_FCTL_ASLR_DISABLE) != 0)
				et_dyn_addr = ET_DYN_LOAD_ADDR;
			else if ((__elfN(pie_aslr_enabled) &&
			    (imgp->proc->p_flag2 & P2_ASLR_DISABLE) == 0) ||
			    (imgp->proc->p_flag2 & P2_ASLR_ENABLE) != 0)
				et_dyn_addr = ET_DYN_ADDR_RAND;
			else
				et_dyn_addr = ET_DYN_LOAD_ADDR;
		}
	}
	if (interp != NULL && brand_info->interp_newpath != NULL)
		newinterp = brand_info->interp_newpath;

	/*
	 * Avoid a possible deadlock if the current address space is destroyed
	 * and that address space maps the locked vnode.  In the common case,
	 * the locked vnode's v_usecount is decremented but remains greater
	 * than zero.  Consequently, the vnode lock is not needed by vrele().
	 * However, in cases where the vnode lock is external, such as nullfs,
	 * v_usecount may become zero.
	 *
	 * The VV_TEXT flag prevents modifications to the executable while
	 * the vnode is unlocked.
	 */
	VOP_UNLOCK(imgp->vp, 0);

	/*
	 * Decide whether to enable randomization of user mappings.
	 * First, reset user preferences for the setid binaries.
	 * Then, account for the support of the randomization by the
	 * ABI, by user preferences, and make special treatment for
	 * PIE binaries.
	 */
	if (imgp->credential_setid) {
		PROC_LOCK(imgp->proc);
		imgp->proc->p_flag2 &= ~(P2_ASLR_ENABLE | P2_ASLR_DISABLE);
		PROC_UNLOCK(imgp->proc);
	}
	if ((sv->sv_flags & SV_ASLR) == 0 ||
	    (imgp->proc->p_flag2 & P2_ASLR_DISABLE) != 0 ||
	    (fctl0 & NT_FREEBSD_FCTL_ASLR_DISABLE) != 0) {
		KASSERT(et_dyn_addr != ET_DYN_ADDR_RAND,
		    ("et_dyn_addr == RAND and !ASLR"));
	} else if ((imgp->proc->p_flag2 & P2_ASLR_ENABLE) != 0 ||
	    (__elfN(aslr_enabled) && hdr->e_type == ET_EXEC) ||
	    et_dyn_addr == ET_DYN_ADDR_RAND) {
		imgp->map_flags |= MAP_ASLR;
		/*
		 * If user does not care about sbrk, utilize the bss
		 * grow region for mappings as well.  We can select
		 * the base for the image anywere and still not suffer
		 * from the fragmentation.
		 */
		if (!__elfN(aslr_honor_sbrk) ||
		    (imgp->proc->p_flag2 & P2_ASLR_IGNSTART) != 0)
			imgp->map_flags |= MAP_ASLR_IGNSTART;
	}

	error = exec_new_vmspace(imgp, sv);
	vmspace = imgp->proc->p_vmspace;
	map = &vmspace->vm_map;

	imgp->proc->p_sysent = sv;

	maxv = vm_map_max(map) - lim_max(td, RLIMIT_STACK);
	if (et_dyn_addr == ET_DYN_ADDR_RAND) {
		KASSERT((map->flags & MAP_ASLR) != 0,
		    ("ET_DYN_ADDR_RAND but !MAP_ASLR"));
		et_dyn_addr = __CONCAT(rnd_, __elfN(base))(map,
		    vm_map_min(map) + mapsz + lim_max(td, RLIMIT_DATA),
		    /* reserve half of the address space to interpreter */
		    maxv / 2, 1UL << flsl(maxalign));
	}

	vn_lock(imgp->vp, LK_EXCLUSIVE | LK_RETRY);
	if (error != 0)
		goto ret;

	for (i = 0; i < hdr->e_phnum; i++) {
		switch (phdr[i].p_type) {
		case PT_LOAD:	/* Loadable segment */
			if (phdr[i].p_memsz == 0)
				break;
			prot = __elfN(trans_prot)(phdr[i].p_flags);
			error = __elfN(load_section)(imgp, phdr[i].p_offset,
			    (caddr_t)(uintptr_t)phdr[i].p_vaddr + et_dyn_addr,
			    phdr[i].p_memsz, phdr[i].p_filesz, prot);
			if (error != 0)
				goto ret;

			/*
			 * If this segment contains the program headers,
			 * remember their virtual address for the AT_PHDR
			 * aux entry. Static binaries don't usually include
			 * a PT_PHDR entry.
			 */
			if (phdr[i].p_offset == 0 &&
			    hdr->e_phoff + hdr->e_phnum * hdr->e_phentsize
				<= phdr[i].p_filesz)
				proghdr = phdr[i].p_vaddr + hdr->e_phoff +
				    et_dyn_addr;
			break;
		case PT_PHDR: 	/* Program header table info */
			proghdr = phdr[i].p_vaddr + et_dyn_addr;
			break;
		default:
			break;
		}
	}

	error = __elfN(enforce_limits)(imgp, hdr, phdr, et_dyn_addr);
	if (error != 0)
		goto ret;

	entry = (u_long)hdr->e_entry + et_dyn_addr;

	/*
	 * We load the dynamic linker where a userland call
	 * to mmap(0, ...) would put it.  The rationale behind this
	 * calculation is that it leaves room for the heap to grow to
	 * its maximum allowed size.
	 */
	addr = round_page((vm_offset_t)vmspace->vm_daddr + lim_max(td,
	    RLIMIT_DATA));
	if ((map->flags & MAP_ASLR) != 0) {
		maxv1 = maxv / 2 + addr / 2;
		MPASS(maxv1 >= addr);	/* No overflow */
		map->anon_loc = __CONCAT(rnd_, __elfN(base))(map, addr, maxv1,
		    MAXPAGESIZES > 1 ? pagesizes[1] : pagesizes[0]);
	} else {
		map->anon_loc = addr;
	}

	imgp->entry_addr = entry;

	if (interp != NULL) {
		have_interp = FALSE;
		VOP_UNLOCK(imgp->vp, 0);
		if ((map->flags & MAP_ASLR) != 0) {
			/* Assume that interpeter fits into 1/4 of AS */
			maxv1 = maxv / 2 + addr / 2;
			MPASS(maxv1 >= addr);	/* No overflow */
			addr = __CONCAT(rnd_, __elfN(base))(map, addr,
			    maxv1, PAGE_SIZE);
		}
		if (brand_info->emul_path != NULL &&
		    brand_info->emul_path[0] != '\0') {
			path = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
			snprintf(path, MAXPATHLEN, "%s%s",
			    brand_info->emul_path, interp);
			error = __elfN(load_file)(imgp->proc, path, &addr,
			    &imgp->entry_addr);
			free(path, M_TEMP);
			if (error == 0)
				have_interp = TRUE;
		}
		if (!have_interp && newinterp != NULL &&
		    (brand_info->interp_path == NULL ||
		    strcmp(interp, brand_info->interp_path) == 0)) {
			error = __elfN(load_file)(imgp->proc, newinterp, &addr,
			    &imgp->entry_addr);
			if (error == 0)
				have_interp = TRUE;
		}
		if (!have_interp) {
			error = __elfN(load_file)(imgp->proc, interp, &addr,
			    &imgp->entry_addr);
		}
		vn_lock(imgp->vp, LK_EXCLUSIVE | LK_RETRY);
		if (error != 0) {
			uprintf("ELF interpreter %s not found, error %d\n",
			    interp, error);
			goto ret;
		}
	} else
		addr = et_dyn_addr;

	/*
	 * Construct auxargs table (used by the fixup routine)
	 */
	elf_auxargs = malloc(sizeof(Elf_Auxargs), M_TEMP, M_WAITOK);
	elf_auxargs->execfd = -1;
	elf_auxargs->phdr = proghdr;
	elf_auxargs->phent = hdr->e_phentsize;
	elf_auxargs->phnum = hdr->e_phnum;
	elf_auxargs->pagesz = PAGE_SIZE;
	elf_auxargs->base = addr;
	elf_auxargs->flags = 0;
	elf_auxargs->entry = entry;
	elf_auxargs->hdr_eflags = hdr->e_flags;

	imgp->auxargs = elf_auxargs;
	imgp->interpreted = 0;
	imgp->reloc_base = addr;
	imgp->proc->p_osrel = osrel;
	imgp->proc->p_fctl0 = fctl0;
	imgp->proc->p_elf_machine = hdr->e_machine;
	imgp->proc->p_elf_flags = hdr->e_flags;

ret:
	free(interp_buf, M_TEMP);
	return (error);
}

#define	suword __CONCAT(suword, __ELF_WORD_SIZE)

int
__elfN(freebsd_fixup)(register_t **stack_base, struct image_params *imgp)
{
	Elf_Auxargs *args = (Elf_Auxargs *)imgp->auxargs;
	Elf_Auxinfo *argarray, *pos;
	Elf_Addr *base, *auxbase;
	int error;

	base = (Elf_Addr *)*stack_base;
	auxbase = base + imgp->args->argc + 1 + imgp->args->envc + 1;
	argarray = pos = malloc(AT_COUNT * sizeof(*pos), M_TEMP,
	    M_WAITOK | M_ZERO);

	if (args->execfd != -1)
		AUXARGS_ENTRY(pos, AT_EXECFD, args->execfd);
	AUXARGS_ENTRY(pos, AT_PHDR, args->phdr);
	AUXARGS_ENTRY(pos, AT_PHENT, args->phent);
	AUXARGS_ENTRY(pos, AT_PHNUM, args->phnum);
	AUXARGS_ENTRY(pos, AT_PAGESZ, args->pagesz);
	AUXARGS_ENTRY(pos, AT_FLAGS, args->flags);
	AUXARGS_ENTRY(pos, AT_ENTRY, args->entry);
	AUXARGS_ENTRY(pos, AT_BASE, args->base);
	AUXARGS_ENTRY(pos, AT_EHDRFLAGS, args->hdr_eflags);
	if (imgp->execpathp != 0)
		AUXARGS_ENTRY(pos, AT_EXECPATH, imgp->execpathp);
	AUXARGS_ENTRY(pos, AT_OSRELDATE,
	    imgp->proc->p_ucred->cr_prison->pr_osreldate);
	if (imgp->canary != 0) {
		AUXARGS_ENTRY(pos, AT_CANARY, imgp->canary);
		AUXARGS_ENTRY(pos, AT_CANARYLEN, imgp->canarylen);
	}
	AUXARGS_ENTRY(pos, AT_NCPUS, mp_ncpus);
	if (imgp->pagesizes != 0) {
		AUXARGS_ENTRY(pos, AT_PAGESIZES, imgp->pagesizes);
		AUXARGS_ENTRY(pos, AT_PAGESIZESLEN, imgp->pagesizeslen);
	}
	if (imgp->sysent->sv_timekeep_base != 0) {
		AUXARGS_ENTRY(pos, AT_TIMEKEEP,
		    imgp->sysent->sv_timekeep_base);
	}
	AUXARGS_ENTRY(pos, AT_STACKPROT, imgp->sysent->sv_shared_page_obj
	    != NULL && imgp->stack_prot != 0 ? imgp->stack_prot :
	    imgp->sysent->sv_stackprot);
	if (imgp->sysent->sv_hwcap != NULL)
		AUXARGS_ENTRY(pos, AT_HWCAP, *imgp->sysent->sv_hwcap);
	if (imgp->sysent->sv_hwcap2 != NULL)
		AUXARGS_ENTRY(pos, AT_HWCAP2, *imgp->sysent->sv_hwcap2);
	AUXARGS_ENTRY(pos, AT_NULL, 0);

	free(imgp->auxargs, M_TEMP);
	imgp->auxargs = NULL;
	KASSERT(pos - argarray <= AT_COUNT, ("Too many auxargs"));

	error = copyout(argarray, auxbase, sizeof(*argarray) * AT_COUNT);
	free(argarray, M_TEMP);
	if (error != 0)
		return (error);

	base--;
	if (suword(base, imgp->args->argc) == -1)
		return (EFAULT);
	*stack_base = (register_t *)base;
	return (0);
}

/*
 * Code for generating ELF core dumps.
 */

typedef void (*segment_callback)(vm_map_entry_t, void *);

/* Closure for cb_put_phdr(). */
struct phdr_closure {
	Elf_Phdr *phdr;		/* Program header to fill in */
	Elf_Off offset;		/* Offset of segment in core file */
};

/* Closure for cb_size_segment(). */
struct sseg_closure {
	int count;		/* Count of writable segments. */
	size_t size;		/* Total size of all writable segments. */
};

typedef void (*outfunc_t)(void *, struct sbuf *, size_t *);

struct note_info {
	int		type;		/* Note type. */
	outfunc_t 	outfunc; 	/* Output function. */
	void		*outarg;	/* Argument for the output function. */
	size_t		outsize;	/* Output size. */
	TAILQ_ENTRY(note_info) link;	/* Link to the next note info. */
};

TAILQ_HEAD(note_info_list, note_info);

/* Coredump output parameters. */
struct coredump_params {
	off_t		offset;
	struct ucred	*active_cred;
	struct ucred	*file_cred;
	struct thread	*td;
	struct vnode	*vp;
	struct compressor *comp;
};

extern int compress_user_cores;
extern int compress_user_cores_level;

static void cb_put_phdr(vm_map_entry_t, void *);
static void cb_size_segment(vm_map_entry_t, void *);
static int core_write(struct coredump_params *, const void *, size_t, off_t,
    enum uio_seg);
static void each_dumpable_segment(struct thread *, segment_callback, void *);
static int __elfN(corehdr)(struct coredump_params *, int, void *, size_t,
    struct note_info_list *, size_t);
static void __elfN(prepare_notes)(struct thread *, struct note_info_list *,
    size_t *);
static void __elfN(puthdr)(struct thread *, void *, size_t, int, size_t);
static void __elfN(putnote)(struct note_info *, struct sbuf *);
static size_t register_note(struct note_info_list *, int, outfunc_t, void *);
static int sbuf_drain_core_output(void *, const char *, int);
static int sbuf_drain_count(void *arg, const char *data, int len);

static void __elfN(note_fpregset)(void *, struct sbuf *, size_t *);
static void __elfN(note_prpsinfo)(void *, struct sbuf *, size_t *);
static void __elfN(note_prstatus)(void *, struct sbuf *, size_t *);
static void __elfN(note_threadmd)(void *, struct sbuf *, size_t *);
static void __elfN(note_thrmisc)(void *, struct sbuf *, size_t *);
static void __elfN(note_ptlwpinfo)(void *, struct sbuf *, size_t *);
static void __elfN(note_procstat_auxv)(void *, struct sbuf *, size_t *);
static void __elfN(note_procstat_proc)(void *, struct sbuf *, size_t *);
static void __elfN(note_procstat_psstrings)(void *, struct sbuf *, size_t *);
static void note_procstat_files(void *, struct sbuf *, size_t *);
static void note_procstat_groups(void *, struct sbuf *, size_t *);
static void note_procstat_osrel(void *, struct sbuf *, size_t *);
static void note_procstat_rlimit(void *, struct sbuf *, size_t *);
static void note_procstat_umask(void *, struct sbuf *, size_t *);
static void note_procstat_vmmap(void *, struct sbuf *, size_t *);

/*
 * Write out a core segment to the compression stream.
 */
static int
compress_chunk(struct coredump_params *p, char *base, char *buf, u_int len)
{
	u_int chunk_len;
	int error;

	while (len > 0) {
		chunk_len = MIN(len, CORE_BUF_SIZE);

		/*
		 * We can get EFAULT error here.
		 * In that case zero out the current chunk of the segment.
		 */
		error = copyin(base, buf, chunk_len);
		if (error != 0)
			bzero(buf, chunk_len);
		error = compressor_write(p->comp, buf, chunk_len);
		if (error != 0)
			break;
		base += chunk_len;
		len -= chunk_len;
	}
	return (error);
}

static int
core_compressed_write(void *base, size_t len, off_t offset, void *arg)
{

	return (core_write((struct coredump_params *)arg, base, len, offset,
	    UIO_SYSSPACE));
}

static int
core_write(struct coredump_params *p, const void *base, size_t len,
    off_t offset, enum uio_seg seg)
{

	return (vn_rdwr_inchunks(UIO_WRITE, p->vp, __DECONST(void *, base),
	    len, offset, seg, IO_UNIT | IO_DIRECT | IO_RANGELOCKED,
	    p->active_cred, p->file_cred, NULL, p->td));
}

static int
core_output(void *base, size_t len, off_t offset, struct coredump_params *p,
    void *tmpbuf)
{
	int error;

	if (p->comp != NULL)
		return (compress_chunk(p, base, tmpbuf, len));

	/*
	 * EFAULT is a non-fatal error that we can get, for example,
	 * if the segment is backed by a file but extends beyond its
	 * end.
	 */
	error = core_write(p, base, len, offset, UIO_USERSPACE);
	if (error == EFAULT) {
		log(LOG_WARNING, "Failed to fully fault in a core file segment "
		    "at VA %p with size 0x%zx to be written at offset 0x%jx "
		    "for process %s\n", base, len, offset, curproc->p_comm);

		/*
		 * Write a "real" zero byte at the end of the target region
		 * in the case this is the last segment.
		 * The intermediate space will be implicitly zero-filled.
		 */
		error = core_write(p, zero_region, 1, offset + len - 1,
		    UIO_SYSSPACE);
	}
	return (error);
}

/*
 * Drain into a core file.
 */
static int
sbuf_drain_core_output(void *arg, const char *data, int len)
{
	struct coredump_params *p;
	int error, locked;

	p = (struct coredump_params *)arg;

	/*
	 * Some kern_proc out routines that print to this sbuf may
	 * call us with the process lock held. Draining with the
	 * non-sleepable lock held is unsafe. The lock is needed for
	 * those routines when dumping a live process. In our case we
	 * can safely release the lock before draining and acquire
	 * again after.
	 */
	locked = PROC_LOCKED(p->td->td_proc);
	if (locked)
		PROC_UNLOCK(p->td->td_proc);
	if (p->comp != NULL)
		error = compressor_write(p->comp, __DECONST(char *, data), len);
	else
		error = core_write(p, __DECONST(void *, data), len, p->offset,
		    UIO_SYSSPACE);
	if (locked)
		PROC_LOCK(p->td->td_proc);
	if (error != 0)
		return (-error);
	p->offset += len;
	return (len);
}

/*
 * Drain into a counter.
 */
static int
sbuf_drain_count(void *arg, const char *data __unused, int len)
{
	size_t *sizep;

	sizep = (size_t *)arg;
	*sizep += len;
	return (len);
}

int
__elfN(coredump)(struct thread *td, struct vnode *vp, off_t limit, int flags)
{
	struct ucred *cred = td->td_ucred;
	int error = 0;
	struct sseg_closure seginfo;
	struct note_info_list notelst;
	struct coredump_params params;
	struct note_info *ninfo;
	void *hdr, *tmpbuf;
	size_t hdrsize, notesz, coresize;

	hdr = NULL;
	tmpbuf = NULL;
	TAILQ_INIT(&notelst);

	/* Size the program segments. */
	seginfo.count = 0;
	seginfo.size = 0;
	each_dumpable_segment(td, cb_size_segment, &seginfo);

	/*
	 * Collect info about the core file header area.
	 */
	hdrsize = sizeof(Elf_Ehdr) + sizeof(Elf_Phdr) * (1 + seginfo.count);
	if (seginfo.count + 1 >= PN_XNUM)
		hdrsize += sizeof(Elf_Shdr);
	__elfN(prepare_notes)(td, &notelst, &notesz);
	coresize = round_page(hdrsize + notesz) + seginfo.size;

	/* Set up core dump parameters. */
	params.offset = 0;
	params.active_cred = cred;
	params.file_cred = NOCRED;
	params.td = td;
	params.vp = vp;
	params.comp = NULL;

#ifdef RACCT
	if (racct_enable) {
		PROC_LOCK(td->td_proc);
		error = racct_add(td->td_proc, RACCT_CORE, coresize);
		PROC_UNLOCK(td->td_proc);
		if (error != 0) {
			error = EFAULT;
			goto done;
		}
	}
#endif
	if (coresize >= limit) {
		error = EFAULT;
		goto done;
	}

	/* Create a compression stream if necessary. */
	if (compress_user_cores != 0) {
		params.comp = compressor_init(core_compressed_write,
		    compress_user_cores, CORE_BUF_SIZE,
		    compress_user_cores_level, &params);
		if (params.comp == NULL) {
			error = EFAULT;
			goto done;
		}
		tmpbuf = malloc(CORE_BUF_SIZE, M_TEMP, M_WAITOK | M_ZERO);
        }

	/*
	 * Allocate memory for building the header, fill it up,
	 * and write it out following the notes.
	 */
	hdr = malloc(hdrsize, M_TEMP, M_WAITOK);
	error = __elfN(corehdr)(&params, seginfo.count, hdr, hdrsize, &notelst,
	    notesz);

	/* Write the contents of all of the writable segments. */
	if (error == 0) {
		Elf_Phdr *php;
		off_t offset;
		int i;

		php = (Elf_Phdr *)((char *)hdr + sizeof(Elf_Ehdr)) + 1;
		offset = round_page(hdrsize + notesz);
		for (i = 0; i < seginfo.count; i++) {
			error = core_output((caddr_t)(uintptr_t)php->p_vaddr,
			    php->p_filesz, offset, &params, tmpbuf);
			if (error != 0)
				break;
			offset += php->p_filesz;
			php++;
		}
		if (error == 0 && params.comp != NULL)
			error = compressor_flush(params.comp);
	}
	if (error) {
		log(LOG_WARNING,
		    "Failed to write core file for process %s (error %d)\n",
		    curproc->p_comm, error);
	}

done:
	free(tmpbuf, M_TEMP);
	if (params.comp != NULL)
		compressor_fini(params.comp);
	while ((ninfo = TAILQ_FIRST(&notelst)) != NULL) {
		TAILQ_REMOVE(&notelst, ninfo, link);
		free(ninfo, M_TEMP);
	}
	if (hdr != NULL)
		free(hdr, M_TEMP);

	return (error);
}

/*
 * A callback for each_dumpable_segment() to write out the segment's
 * program header entry.
 */
static void
cb_put_phdr(vm_map_entry_t entry, void *closure)
{
	struct phdr_closure *phc = (struct phdr_closure *)closure;
	Elf_Phdr *phdr = phc->phdr;

	phc->offset = round_page(phc->offset);

	phdr->p_type = PT_LOAD;
	phdr->p_offset = phc->offset;
	phdr->p_vaddr = entry->start;
	phdr->p_paddr = 0;
	phdr->p_filesz = phdr->p_memsz = entry->end - entry->start;
	phdr->p_align = PAGE_SIZE;
	phdr->p_flags = __elfN(untrans_prot)(entry->protection);

	phc->offset += phdr->p_filesz;
	phc->phdr++;
}

/*
 * A callback for each_dumpable_segment() to gather information about
 * the number of segments and their total size.
 */
static void
cb_size_segment(vm_map_entry_t entry, void *closure)
{
	struct sseg_closure *ssc = (struct sseg_closure *)closure;

	ssc->count++;
	ssc->size += entry->end - entry->start;
}

/*
 * For each writable segment in the process's memory map, call the given
 * function with a pointer to the map entry and some arbitrary
 * caller-supplied data.
 */
static void
each_dumpable_segment(struct thread *td, segment_callback func, void *closure)
{
	struct proc *p = td->td_proc;
	vm_map_t map = &p->p_vmspace->vm_map;
	vm_map_entry_t entry;
	vm_object_t backing_object, object;
	boolean_t ignore_entry;

	vm_map_lock_read(map);
	for (entry = map->header.next; entry != &map->header;
	    entry = entry->next) {
		/*
		 * Don't dump inaccessible mappings, deal with legacy
		 * coredump mode.
		 *
		 * Note that read-only segments related to the elf binary
		 * are marked MAP_ENTRY_NOCOREDUMP now so we no longer
		 * need to arbitrarily ignore such segments.
		 */
		if (elf_legacy_coredump) {
			if ((entry->protection & VM_PROT_RW) != VM_PROT_RW)
				continue;
		} else {
			if ((entry->protection & VM_PROT_ALL) == 0)
				continue;
		}

		/*
		 * Dont include memory segment in the coredump if
		 * MAP_NOCORE is set in mmap(2) or MADV_NOCORE in
		 * madvise(2).  Do not dump submaps (i.e. parts of the
		 * kernel map).
		 */
		if (entry->eflags & (MAP_ENTRY_NOCOREDUMP|MAP_ENTRY_IS_SUB_MAP))
			continue;

		if ((object = entry->object.vm_object) == NULL)
			continue;

		/* Ignore memory-mapped devices and such things. */
		VM_OBJECT_RLOCK(object);
		while ((backing_object = object->backing_object) != NULL) {
			VM_OBJECT_RLOCK(backing_object);
			VM_OBJECT_RUNLOCK(object);
			object = backing_object;
		}
		ignore_entry = object->type != OBJT_DEFAULT &&
		    object->type != OBJT_SWAP && object->type != OBJT_VNODE &&
		    object->type != OBJT_PHYS;
		VM_OBJECT_RUNLOCK(object);
		if (ignore_entry)
			continue;

		(*func)(entry, closure);
	}
	vm_map_unlock_read(map);
}

/*
 * Write the core file header to the file, including padding up to
 * the page boundary.
 */
static int
__elfN(corehdr)(struct coredump_params *p, int numsegs, void *hdr,
    size_t hdrsize, struct note_info_list *notelst, size_t notesz)
{
	struct note_info *ninfo;
	struct sbuf *sb;
	int error;

	/* Fill in the header. */
	bzero(hdr, hdrsize);
	__elfN(puthdr)(p->td, hdr, hdrsize, numsegs, notesz);

	sb = sbuf_new(NULL, NULL, CORE_BUF_SIZE, SBUF_FIXEDLEN);
	sbuf_set_drain(sb, sbuf_drain_core_output, p);
	sbuf_start_section(sb, NULL);
	sbuf_bcat(sb, hdr, hdrsize);
	TAILQ_FOREACH(ninfo, notelst, link)
	    __elfN(putnote)(ninfo, sb);
	/* Align up to a page boundary for the program segments. */
	sbuf_end_section(sb, -1, PAGE_SIZE, 0);
	error = sbuf_finish(sb);
	sbuf_delete(sb);

	return (error);
}

static void
__elfN(prepare_notes)(struct thread *td, struct note_info_list *list,
    size_t *sizep)
{
	struct proc *p;
	struct thread *thr;
	size_t size;

	p = td->td_proc;
	size = 0;

	size += register_note(list, NT_PRPSINFO, __elfN(note_prpsinfo), p);

	/*
	 * To have the debugger select the right thread (LWP) as the initial
	 * thread, we dump the state of the thread passed to us in td first.
	 * This is the thread that causes the core dump and thus likely to
	 * be the right thread one wants to have selected in the debugger.
	 */
	thr = td;
	while (thr != NULL) {
		size += register_note(list, NT_PRSTATUS,
		    __elfN(note_prstatus), thr);
		size += register_note(list, NT_FPREGSET,
		    __elfN(note_fpregset), thr);
		size += register_note(list, NT_THRMISC,
		    __elfN(note_thrmisc), thr);
		size += register_note(list, NT_PTLWPINFO,
		    __elfN(note_ptlwpinfo), thr);
		size += register_note(list, -1,
		    __elfN(note_threadmd), thr);

		thr = (thr == td) ? TAILQ_FIRST(&p->p_threads) :
		    TAILQ_NEXT(thr, td_plist);
		if (thr == td)
			thr = TAILQ_NEXT(thr, td_plist);
	}

	size += register_note(list, NT_PROCSTAT_PROC,
	    __elfN(note_procstat_proc), p);
	size += register_note(list, NT_PROCSTAT_FILES,
	    note_procstat_files, p);
	size += register_note(list, NT_PROCSTAT_VMMAP,
	    note_procstat_vmmap, p);
	size += register_note(list, NT_PROCSTAT_GROUPS,
	    note_procstat_groups, p);
	size += register_note(list, NT_PROCSTAT_UMASK,
	    note_procstat_umask, p);
	size += register_note(list, NT_PROCSTAT_RLIMIT,
	    note_procstat_rlimit, p);
	size += register_note(list, NT_PROCSTAT_OSREL,
	    note_procstat_osrel, p);
	size += register_note(list, NT_PROCSTAT_PSSTRINGS,
	    __elfN(note_procstat_psstrings), p);
	size += register_note(list, NT_PROCSTAT_AUXV,
	    __elfN(note_procstat_auxv), p);

	*sizep = size;
}

static void
__elfN(puthdr)(struct thread *td, void *hdr, size_t hdrsize, int numsegs,
    size_t notesz)
{
	Elf_Ehdr *ehdr;
	Elf_Phdr *phdr;
	Elf_Shdr *shdr;
	struct phdr_closure phc;

	ehdr = (Elf_Ehdr *)hdr;

	ehdr->e_ident[EI_MAG0] = ELFMAG0;
	ehdr->e_ident[EI_MAG1] = ELFMAG1;
	ehdr->e_ident[EI_MAG2] = ELFMAG2;
	ehdr->e_ident[EI_MAG3] = ELFMAG3;
	ehdr->e_ident[EI_CLASS] = ELF_CLASS;
	ehdr->e_ident[EI_DATA] = ELF_DATA;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELFOSABI_FREEBSD;
	ehdr->e_ident[EI_ABIVERSION] = 0;
	ehdr->e_ident[EI_PAD] = 0;
	ehdr->e_type = ET_CORE;
	ehdr->e_machine = td->td_proc->p_elf_machine;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_entry = 0;
	ehdr->e_phoff = sizeof(Elf_Ehdr);
	ehdr->e_flags = td->td_proc->p_elf_flags;
	ehdr->e_ehsize = sizeof(Elf_Ehdr);
	ehdr->e_phentsize = sizeof(Elf_Phdr);
	ehdr->e_shentsize = sizeof(Elf_Shdr);
	ehdr->e_shstrndx = SHN_UNDEF;
	if (numsegs + 1 < PN_XNUM) {
		ehdr->e_phnum = numsegs + 1;
		ehdr->e_shnum = 0;
	} else {
		ehdr->e_phnum = PN_XNUM;
		ehdr->e_shnum = 1;

		ehdr->e_shoff = ehdr->e_phoff +
		    (numsegs + 1) * ehdr->e_phentsize;
		KASSERT(ehdr->e_shoff == hdrsize - sizeof(Elf_Shdr),
		    ("e_shoff: %zu, hdrsize - shdr: %zu",
		     (size_t)ehdr->e_shoff, hdrsize - sizeof(Elf_Shdr)));

		shdr = (Elf_Shdr *)((char *)hdr + ehdr->e_shoff);
		memset(shdr, 0, sizeof(*shdr));
		/*
		 * A special first section is used to hold large segment and
		 * section counts.  This was proposed by Sun Microsystems in
		 * Solaris and has been adopted by Linux; the standard ELF
		 * tools are already familiar with the technique.
		 *
		 * See table 7-7 of the Solaris "Linker and Libraries Guide"
		 * (or 12-7 depending on the version of the document) for more
		 * details.
		 */
		shdr->sh_type = SHT_NULL;
		shdr->sh_size = ehdr->e_shnum;
		shdr->sh_link = ehdr->e_shstrndx;
		shdr->sh_info = numsegs + 1;
	}

	/*
	 * Fill in the program header entries.
	 */
	phdr = (Elf_Phdr *)((char *)hdr + ehdr->e_phoff);

	/* The note segement. */
	phdr->p_type = PT_NOTE;
	phdr->p_offset = hdrsize;
	phdr->p_vaddr = 0;
	phdr->p_paddr = 0;
	phdr->p_filesz = notesz;
	phdr->p_memsz = 0;
	phdr->p_flags = PF_R;
	phdr->p_align = ELF_NOTE_ROUNDSIZE;
	phdr++;

	/* All the writable segments from the program. */
	phc.phdr = phdr;
	phc.offset = round_page(hdrsize + notesz);
	each_dumpable_segment(td, cb_put_phdr, &phc);
}

static size_t
register_note(struct note_info_list *list, int type, outfunc_t out, void *arg)
{
	struct note_info *ninfo;
	size_t size, notesize;

	size = 0;
	out(arg, NULL, &size);
	ninfo = malloc(sizeof(*ninfo), M_TEMP, M_ZERO | M_WAITOK);
	ninfo->type = type;
	ninfo->outfunc = out;
	ninfo->outarg = arg;
	ninfo->outsize = size;
	TAILQ_INSERT_TAIL(list, ninfo, link);

	if (type == -1)
		return (size);

	notesize = sizeof(Elf_Note) +		/* note header */
	    roundup2(sizeof(FREEBSD_ABI_VENDOR), ELF_NOTE_ROUNDSIZE) +
						/* note name */
	    roundup2(size, ELF_NOTE_ROUNDSIZE);	/* note description */

	return (notesize);
}

static size_t
append_note_data(const void *src, void *dst, size_t len)
{
	size_t padded_len;

	padded_len = roundup2(len, ELF_NOTE_ROUNDSIZE);
	if (dst != NULL) {
		bcopy(src, dst, len);
		bzero((char *)dst + len, padded_len - len);
	}
	return (padded_len);
}

size_t
__elfN(populate_note)(int type, void *src, void *dst, size_t size, void **descp)
{
	Elf_Note *note;
	char *buf;
	size_t notesize;

	buf = dst;
	if (buf != NULL) {
		note = (Elf_Note *)buf;
		note->n_namesz = sizeof(FREEBSD_ABI_VENDOR);
		note->n_descsz = size;
		note->n_type = type;
		buf += sizeof(*note);
		buf += append_note_data(FREEBSD_ABI_VENDOR, buf,
		    sizeof(FREEBSD_ABI_VENDOR));
		append_note_data(src, buf, size);
		if (descp != NULL)
			*descp = buf;
	}

	notesize = sizeof(Elf_Note) +		/* note header */
	    roundup2(sizeof(FREEBSD_ABI_VENDOR), ELF_NOTE_ROUNDSIZE) +
						/* note name */
	    roundup2(size, ELF_NOTE_ROUNDSIZE);	/* note description */

	return (notesize);
}

static void
__elfN(putnote)(struct note_info *ninfo, struct sbuf *sb)
{
	Elf_Note note;
	ssize_t old_len, sect_len;
	size_t new_len, descsz, i;

	if (ninfo->type == -1) {
		ninfo->outfunc(ninfo->outarg, sb, &ninfo->outsize);
		return;
	}

	note.n_namesz = sizeof(FREEBSD_ABI_VENDOR);
	note.n_descsz = ninfo->outsize;
	note.n_type = ninfo->type;

	sbuf_bcat(sb, &note, sizeof(note));
	sbuf_start_section(sb, &old_len);
	sbuf_bcat(sb, FREEBSD_ABI_VENDOR, sizeof(FREEBSD_ABI_VENDOR));
	sbuf_end_section(sb, old_len, ELF_NOTE_ROUNDSIZE, 0);
	if (note.n_descsz == 0)
		return;
	sbuf_start_section(sb, &old_len);
	ninfo->outfunc(ninfo->outarg, sb, &ninfo->outsize);
	sect_len = sbuf_end_section(sb, old_len, ELF_NOTE_ROUNDSIZE, 0);
	if (sect_len < 0)
		return;

	new_len = (size_t)sect_len;
	descsz = roundup(note.n_descsz, ELF_NOTE_ROUNDSIZE);
	if (new_len < descsz) {
		/*
		 * It is expected that individual note emitters will correctly
		 * predict their expected output size and fill up to that size
		 * themselves, padding in a format-specific way if needed.
		 * However, in case they don't, just do it here with zeros.
		 */
		for (i = 0; i < descsz - new_len; i++)
			sbuf_putc(sb, 0);
	} else if (new_len > descsz) {
		/*
		 * We can't always truncate sb -- we may have drained some
		 * of it already.
		 */
		KASSERT(new_len == descsz, ("%s: Note type %u changed as we "
		    "read it (%zu > %zu).  Since it is longer than "
		    "expected, this coredump's notes are corrupt.  THIS "
		    "IS A BUG in the note_procstat routine for type %u.\n",
		    __func__, (unsigned)note.n_type, new_len, descsz,
		    (unsigned)note.n_type));
	}
}

/*
 * Miscellaneous note out functions.
 */

#if defined(COMPAT_FREEBSD32) && __ELF_WORD_SIZE == 32
#include <compat/freebsd32/freebsd32.h>
#include <compat/freebsd32/freebsd32_signal.h>

typedef struct prstatus32 elf_prstatus_t;
typedef struct prpsinfo32 elf_prpsinfo_t;
typedef struct fpreg32 elf_prfpregset_t;
typedef struct fpreg32 elf_fpregset_t;
typedef struct reg32 elf_gregset_t;
typedef struct thrmisc32 elf_thrmisc_t;
#define ELF_KERN_PROC_MASK	KERN_PROC_MASK32
typedef struct kinfo_proc32 elf_kinfo_proc_t;
typedef uint32_t elf_ps_strings_t;
#else
typedef prstatus_t elf_prstatus_t;
typedef prpsinfo_t elf_prpsinfo_t;
typedef prfpregset_t elf_prfpregset_t;
typedef prfpregset_t elf_fpregset_t;
typedef gregset_t elf_gregset_t;
typedef thrmisc_t elf_thrmisc_t;
#define ELF_KERN_PROC_MASK	0
typedef struct kinfo_proc elf_kinfo_proc_t;
typedef vm_offset_t elf_ps_strings_t;
#endif

static void
__elfN(note_prpsinfo)(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct sbuf sbarg;
	size_t len;
	char *cp, *end;
	struct proc *p;
	elf_prpsinfo_t *psinfo;
	int error;

	p = (struct proc *)arg;
	if (sb != NULL) {
		KASSERT(*sizep == sizeof(*psinfo), ("invalid size"));
		psinfo = malloc(sizeof(*psinfo), M_TEMP, M_ZERO | M_WAITOK);
		psinfo->pr_version = PRPSINFO_VERSION;
		psinfo->pr_psinfosz = sizeof(elf_prpsinfo_t);
		strlcpy(psinfo->pr_fname, p->p_comm, sizeof(psinfo->pr_fname));
		PROC_LOCK(p);
		if (p->p_args != NULL) {
			len = sizeof(psinfo->pr_psargs) - 1;
			if (len > p->p_args->ar_length)
				len = p->p_args->ar_length;
			memcpy(psinfo->pr_psargs, p->p_args->ar_args, len);
			PROC_UNLOCK(p);
			error = 0;
		} else {
			_PHOLD(p);
			PROC_UNLOCK(p);
			sbuf_new(&sbarg, psinfo->pr_psargs,
			    sizeof(psinfo->pr_psargs), SBUF_FIXEDLEN);
			error = proc_getargv(curthread, p, &sbarg);
			PRELE(p);
			if (sbuf_finish(&sbarg) == 0)
				len = sbuf_len(&sbarg) - 1;
			else
				len = sizeof(psinfo->pr_psargs) - 1;
			sbuf_delete(&sbarg);
		}
		if (error || len == 0)
			strlcpy(psinfo->pr_psargs, p->p_comm,
			    sizeof(psinfo->pr_psargs));
		else {
			KASSERT(len < sizeof(psinfo->pr_psargs),
			    ("len is too long: %zu vs %zu", len,
			    sizeof(psinfo->pr_psargs)));
			cp = psinfo->pr_psargs;
			end = cp + len - 1;
			for (;;) {
				cp = memchr(cp, '\0', end - cp);
				if (cp == NULL)
					break;
				*cp = ' ';
			}
		}
		psinfo->pr_pid = p->p_pid;
		sbuf_bcat(sb, psinfo, sizeof(*psinfo));
		free(psinfo, M_TEMP);
	}
	*sizep = sizeof(*psinfo);
}

static void
__elfN(note_prstatus)(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct thread *td;
	elf_prstatus_t *status;

	td = (struct thread *)arg;
	if (sb != NULL) {
		KASSERT(*sizep == sizeof(*status), ("invalid size"));
		status = malloc(sizeof(*status), M_TEMP, M_ZERO | M_WAITOK);
		status->pr_version = PRSTATUS_VERSION;
		status->pr_statussz = sizeof(elf_prstatus_t);
		status->pr_gregsetsz = sizeof(elf_gregset_t);
		status->pr_fpregsetsz = sizeof(elf_fpregset_t);
		status->pr_osreldate = osreldate;
		status->pr_cursig = td->td_proc->p_sig;
		status->pr_pid = td->td_tid;
#if defined(COMPAT_FREEBSD32) && __ELF_WORD_SIZE == 32
		fill_regs32(td, &status->pr_reg);
#else
		fill_regs(td, &status->pr_reg);
#endif
		sbuf_bcat(sb, status, sizeof(*status));
		free(status, M_TEMP);
	}
	*sizep = sizeof(*status);
}

static void
__elfN(note_fpregset)(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct thread *td;
	elf_prfpregset_t *fpregset;

	td = (struct thread *)arg;
	if (sb != NULL) {
		KASSERT(*sizep == sizeof(*fpregset), ("invalid size"));
		fpregset = malloc(sizeof(*fpregset), M_TEMP, M_ZERO | M_WAITOK);
#if defined(COMPAT_FREEBSD32) && __ELF_WORD_SIZE == 32
		fill_fpregs32(td, fpregset);
#else
		fill_fpregs(td, fpregset);
#endif
		sbuf_bcat(sb, fpregset, sizeof(*fpregset));
		free(fpregset, M_TEMP);
	}
	*sizep = sizeof(*fpregset);
}

static void
__elfN(note_thrmisc)(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct thread *td;
	elf_thrmisc_t thrmisc;

	td = (struct thread *)arg;
	if (sb != NULL) {
		KASSERT(*sizep == sizeof(thrmisc), ("invalid size"));
		bzero(&thrmisc._pad, sizeof(thrmisc._pad));
		strcpy(thrmisc.pr_tname, td->td_name);
		sbuf_bcat(sb, &thrmisc, sizeof(thrmisc));
	}
	*sizep = sizeof(thrmisc);
}

static void
__elfN(note_ptlwpinfo)(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct thread *td;
	size_t size;
	int structsize;
#if defined(COMPAT_FREEBSD32) && __ELF_WORD_SIZE == 32
	struct ptrace_lwpinfo32 pl;
#else
	struct ptrace_lwpinfo pl;
#endif

	td = (struct thread *)arg;
	size = sizeof(structsize) + sizeof(pl);
	if (sb != NULL) {
		KASSERT(*sizep == size, ("invalid size"));
		structsize = sizeof(pl);
		sbuf_bcat(sb, &structsize, sizeof(structsize));
		bzero(&pl, sizeof(pl));
		pl.pl_lwpid = td->td_tid;
		pl.pl_event = PL_EVENT_NONE;
		pl.pl_sigmask = td->td_sigmask;
		pl.pl_siglist = td->td_siglist;
		if (td->td_si.si_signo != 0) {
			pl.pl_event = PL_EVENT_SIGNAL;
			pl.pl_flags |= PL_FLAG_SI;
#if defined(COMPAT_FREEBSD32) && __ELF_WORD_SIZE == 32
			siginfo_to_siginfo32(&td->td_si, &pl.pl_siginfo);
#else
			pl.pl_siginfo = td->td_si;
#endif
		}
		strcpy(pl.pl_tdname, td->td_name);
		/* XXX TODO: supply more information in struct ptrace_lwpinfo*/
		sbuf_bcat(sb, &pl, sizeof(pl));
	}
	*sizep = size;
}

/*
 * Allow for MD specific notes, as well as any MD
 * specific preparations for writing MI notes.
 */
static void
__elfN(note_threadmd)(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct thread *td;
	void *buf;
	size_t size;

	td = (struct thread *)arg;
	size = *sizep;
	if (size != 0 && sb != NULL)
		buf = malloc(size, M_TEMP, M_ZERO | M_WAITOK);
	else
		buf = NULL;
	size = 0;
	__elfN(dump_thread)(td, buf, &size);
	KASSERT(sb == NULL || *sizep == size, ("invalid size"));
	if (size != 0 && sb != NULL)
		sbuf_bcat(sb, buf, size);
	free(buf, M_TEMP);
	*sizep = size;
}

#ifdef KINFO_PROC_SIZE
CTASSERT(sizeof(struct kinfo_proc) == KINFO_PROC_SIZE);
#endif

static void
__elfN(note_procstat_proc)(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct proc *p;
	size_t size;
	int structsize;

	p = (struct proc *)arg;
	size = sizeof(structsize) + p->p_numthreads *
	    sizeof(elf_kinfo_proc_t);

	if (sb != NULL) {
		KASSERT(*sizep == size, ("invalid size"));
		structsize = sizeof(elf_kinfo_proc_t);
		sbuf_bcat(sb, &structsize, sizeof(structsize));
		PROC_LOCK(p);
		kern_proc_out(p, sb, ELF_KERN_PROC_MASK);
	}
	*sizep = size;
}

#ifdef KINFO_FILE_SIZE
CTASSERT(sizeof(struct kinfo_file) == KINFO_FILE_SIZE);
#endif

static void
note_procstat_files(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct proc *p;
	size_t size, sect_sz, i;
	ssize_t start_len, sect_len;
	int structsize, filedesc_flags;

	if (coredump_pack_fileinfo)
		filedesc_flags = KERN_FILEDESC_PACK_KINFO;
	else
		filedesc_flags = 0;

	p = (struct proc *)arg;
	structsize = sizeof(struct kinfo_file);
	if (sb == NULL) {
		size = 0;
		sb = sbuf_new(NULL, NULL, 128, SBUF_FIXEDLEN);
		sbuf_set_drain(sb, sbuf_drain_count, &size);
		sbuf_bcat(sb, &structsize, sizeof(structsize));
		PROC_LOCK(p);
		kern_proc_filedesc_out(p, sb, -1, filedesc_flags);
		sbuf_finish(sb);
		sbuf_delete(sb);
		*sizep = size;
	} else {
		sbuf_start_section(sb, &start_len);

		sbuf_bcat(sb, &structsize, sizeof(structsize));
		PROC_LOCK(p);
		kern_proc_filedesc_out(p, sb, *sizep - sizeof(structsize),
		    filedesc_flags);

		sect_len = sbuf_end_section(sb, start_len, 0, 0);
		if (sect_len < 0)
			return;
		sect_sz = sect_len;

		KASSERT(sect_sz <= *sizep,
		    ("kern_proc_filedesc_out did not respect maxlen; "
		     "requested %zu, got %zu", *sizep - sizeof(structsize),
		     sect_sz - sizeof(structsize)));

		for (i = 0; i < *sizep - sect_sz && sb->s_error == 0; i++)
			sbuf_putc(sb, 0);
	}
}

#ifdef KINFO_VMENTRY_SIZE
CTASSERT(sizeof(struct kinfo_vmentry) == KINFO_VMENTRY_SIZE);
#endif

static void
note_procstat_vmmap(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct proc *p;
	size_t size;
	int structsize, vmmap_flags;

	if (coredump_pack_vmmapinfo)
		vmmap_flags = KERN_VMMAP_PACK_KINFO;
	else
		vmmap_flags = 0;

	p = (struct proc *)arg;
	structsize = sizeof(struct kinfo_vmentry);
	if (sb == NULL) {
		size = 0;
		sb = sbuf_new(NULL, NULL, 128, SBUF_FIXEDLEN);
		sbuf_set_drain(sb, sbuf_drain_count, &size);
		sbuf_bcat(sb, &structsize, sizeof(structsize));
		PROC_LOCK(p);
		kern_proc_vmmap_out(p, sb, -1, vmmap_flags);
		sbuf_finish(sb);
		sbuf_delete(sb);
		*sizep = size;
	} else {
		sbuf_bcat(sb, &structsize, sizeof(structsize));
		PROC_LOCK(p);
		kern_proc_vmmap_out(p, sb, *sizep - sizeof(structsize),
		    vmmap_flags);
	}
}

static void
note_procstat_groups(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct proc *p;
	size_t size;
	int structsize;

	p = (struct proc *)arg;
	size = sizeof(structsize) + p->p_ucred->cr_ngroups * sizeof(gid_t);
	if (sb != NULL) {
		KASSERT(*sizep == size, ("invalid size"));
		structsize = sizeof(gid_t);
		sbuf_bcat(sb, &structsize, sizeof(structsize));
		sbuf_bcat(sb, p->p_ucred->cr_groups, p->p_ucred->cr_ngroups *
		    sizeof(gid_t));
	}
	*sizep = size;
}

static void
note_procstat_umask(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct proc *p;
	size_t size;
	int structsize;

	p = (struct proc *)arg;
	size = sizeof(structsize) + sizeof(p->p_fd->fd_cmask);
	if (sb != NULL) {
		KASSERT(*sizep == size, ("invalid size"));
		structsize = sizeof(p->p_fd->fd_cmask);
		sbuf_bcat(sb, &structsize, sizeof(structsize));
		sbuf_bcat(sb, &p->p_fd->fd_cmask, sizeof(p->p_fd->fd_cmask));
	}
	*sizep = size;
}

static void
note_procstat_rlimit(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct proc *p;
	struct rlimit rlim[RLIM_NLIMITS];
	size_t size;
	int structsize, i;

	p = (struct proc *)arg;
	size = sizeof(structsize) + sizeof(rlim);
	if (sb != NULL) {
		KASSERT(*sizep == size, ("invalid size"));
		structsize = sizeof(rlim);
		sbuf_bcat(sb, &structsize, sizeof(structsize));
		PROC_LOCK(p);
		for (i = 0; i < RLIM_NLIMITS; i++)
			lim_rlimit_proc(p, i, &rlim[i]);
		PROC_UNLOCK(p);
		sbuf_bcat(sb, rlim, sizeof(rlim));
	}
	*sizep = size;
}

static void
note_procstat_osrel(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct proc *p;
	size_t size;
	int structsize;

	p = (struct proc *)arg;
	size = sizeof(structsize) + sizeof(p->p_osrel);
	if (sb != NULL) {
		KASSERT(*sizep == size, ("invalid size"));
		structsize = sizeof(p->p_osrel);
		sbuf_bcat(sb, &structsize, sizeof(structsize));
		sbuf_bcat(sb, &p->p_osrel, sizeof(p->p_osrel));
	}
	*sizep = size;
}

static void
__elfN(note_procstat_psstrings)(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct proc *p;
	elf_ps_strings_t ps_strings;
	size_t size;
	int structsize;

	p = (struct proc *)arg;
	size = sizeof(structsize) + sizeof(ps_strings);
	if (sb != NULL) {
		KASSERT(*sizep == size, ("invalid size"));
		structsize = sizeof(ps_strings);
#if defined(COMPAT_FREEBSD32) && __ELF_WORD_SIZE == 32
		ps_strings = PTROUT(p->p_sysent->sv_psstrings);
#else
		ps_strings = p->p_sysent->sv_psstrings;
#endif
		sbuf_bcat(sb, &structsize, sizeof(structsize));
		sbuf_bcat(sb, &ps_strings, sizeof(ps_strings));
	}
	*sizep = size;
}

static void
__elfN(note_procstat_auxv)(void *arg, struct sbuf *sb, size_t *sizep)
{
	struct proc *p;
	size_t size;
	int structsize;

	p = (struct proc *)arg;
	if (sb == NULL) {
		size = 0;
		sb = sbuf_new(NULL, NULL, 128, SBUF_FIXEDLEN);
		sbuf_set_drain(sb, sbuf_drain_count, &size);
		sbuf_bcat(sb, &structsize, sizeof(structsize));
		PHOLD(p);
		proc_getauxv(curthread, p, sb);
		PRELE(p);
		sbuf_finish(sb);
		sbuf_delete(sb);
		*sizep = size;
	} else {
		structsize = sizeof(Elf_Auxinfo);
		sbuf_bcat(sb, &structsize, sizeof(structsize));
		PHOLD(p);
		proc_getauxv(curthread, p, sb);
		PRELE(p);
	}
}

static boolean_t
__elfN(parse_notes)(struct image_params *imgp, Elf_Note *checknote,
    const char *note_vendor, const Elf_Phdr *pnote,
    boolean_t (*cb)(const Elf_Note *, void *, boolean_t *), void *cb_arg)
{
	const Elf_Note *note, *note0, *note_end;
	const char *note_name;
	char *buf;
	int i, error;
	boolean_t res;

	/* We need some limit, might as well use PAGE_SIZE. */
	if (pnote == NULL || pnote->p_filesz > PAGE_SIZE)
		return (FALSE);
	ASSERT_VOP_LOCKED(imgp->vp, "parse_notes");
	if (pnote->p_offset > PAGE_SIZE ||
	    pnote->p_filesz > PAGE_SIZE - pnote->p_offset) {
		VOP_UNLOCK(imgp->vp, 0);
		buf = malloc(pnote->p_filesz, M_TEMP, M_WAITOK);
		vn_lock(imgp->vp, LK_EXCLUSIVE | LK_RETRY);
		error = vn_rdwr(UIO_READ, imgp->vp, buf, pnote->p_filesz,
		    pnote->p_offset, UIO_SYSSPACE, IO_NODELOCKED,
		    curthread->td_ucred, NOCRED, NULL, curthread);
		if (error != 0) {
			uprintf("i/o error PT_NOTE\n");
			goto retf;
		}
		note = note0 = (const Elf_Note *)buf;
		note_end = (const Elf_Note *)(buf + pnote->p_filesz);
	} else {
		note = note0 = (const Elf_Note *)(imgp->image_header +
		    pnote->p_offset);
		note_end = (const Elf_Note *)(imgp->image_header +
		    pnote->p_offset + pnote->p_filesz);
		buf = NULL;
	}
	for (i = 0; i < 100 && note >= note0 && note < note_end; i++) {
		if (!aligned(note, Elf32_Addr) || (const char *)note_end -
		    (const char *)note < sizeof(Elf_Note)) {
			goto retf;
		}
		if (note->n_namesz != checknote->n_namesz ||
		    note->n_descsz != checknote->n_descsz ||
		    note->n_type != checknote->n_type)
			goto nextnote;
		note_name = (const char *)(note + 1);
		if (note_name + checknote->n_namesz >=
		    (const char *)note_end || strncmp(note_vendor,
		    note_name, checknote->n_namesz) != 0)
			goto nextnote;

		if (cb(note, cb_arg, &res))
			goto ret;
nextnote:
		note = (const Elf_Note *)((const char *)(note + 1) +
		    roundup2(note->n_namesz, ELF_NOTE_ROUNDSIZE) +
		    roundup2(note->n_descsz, ELF_NOTE_ROUNDSIZE));
	}
retf:
	res = FALSE;
ret:
	free(buf, M_TEMP);
	return (res);
}

struct brandnote_cb_arg {
	Elf_Brandnote *brandnote;
	int32_t *osrel;
};

static boolean_t
brandnote_cb(const Elf_Note *note, void *arg0, boolean_t *res)
{
	struct brandnote_cb_arg *arg;

	arg = arg0;

	/*
	 * Fetch the osreldate for binary from the ELF OSABI-note if
	 * necessary.
	 */
	*res = (arg->brandnote->flags & BN_TRANSLATE_OSREL) != 0 &&
	    arg->brandnote->trans_osrel != NULL ?
	    arg->brandnote->trans_osrel(note, arg->osrel) : TRUE;

	return (TRUE);
}

static Elf_Note fctl_note = {
	.n_namesz = sizeof(FREEBSD_ABI_VENDOR),
	.n_descsz = sizeof(uint32_t),
	.n_type = NT_FREEBSD_FEATURE_CTL,
};

struct fctl_cb_arg {
	uint32_t *fctl0;
};

static boolean_t
note_fctl_cb(const Elf_Note *note, void *arg0, boolean_t *res)
{
	struct fctl_cb_arg *arg;
	const Elf32_Word *desc;
	uintptr_t p;

	arg = arg0;
	p = (uintptr_t)(note + 1);
	p += roundup2(note->n_namesz, ELF_NOTE_ROUNDSIZE);
	desc = (const Elf32_Word *)p;
	*arg->fctl0 = desc[0];
	return (TRUE);
}

/*
 * Try to find the appropriate ABI-note section for checknote, fetch
 * the osreldate and feature control flags for binary from the ELF
 * OSABI-note.  Only the first page of the image is searched, the same
 * as for headers.
 */
static boolean_t
__elfN(check_note)(struct image_params *imgp, Elf_Brandnote *brandnote,
    int32_t *osrel, uint32_t *fctl0)
{
	const Elf_Phdr *phdr;
	const Elf_Ehdr *hdr;
	struct brandnote_cb_arg b_arg;
	struct fctl_cb_arg f_arg;
	int i, j;

	hdr = (const Elf_Ehdr *)imgp->image_header;
	phdr = (const Elf_Phdr *)(imgp->image_header + hdr->e_phoff);
	b_arg.brandnote = brandnote;
	b_arg.osrel = osrel;
	f_arg.fctl0 = fctl0;

	for (i = 0; i < hdr->e_phnum; i++) {
		if (phdr[i].p_type == PT_NOTE && __elfN(parse_notes)(imgp,
		    &brandnote->hdr, brandnote->vendor, &phdr[i], brandnote_cb,
		    &b_arg)) {
			for (j = 0; j < hdr->e_phnum; j++) {
				if (phdr[j].p_type == PT_NOTE &&
				    __elfN(parse_notes)(imgp, &fctl_note,
				    FREEBSD_ABI_VENDOR, &phdr[j],
				    note_fctl_cb, &f_arg))
					break;
			}
			return (TRUE);
		}
	}
	return (FALSE);

}

/*
 * Tell kern_execve.c about it, with a little help from the linker.
 */
static struct execsw __elfN(execsw) = {
	.ex_imgact = __CONCAT(exec_, __elfN(imgact)),
	.ex_name = __XSTRING(__CONCAT(ELF, __ELF_WORD_SIZE))
};
EXEC_SET(__CONCAT(elf, __ELF_WORD_SIZE), __elfN(execsw));

static vm_prot_t
__elfN(trans_prot)(Elf_Word flags)
{
	vm_prot_t prot;

	prot = 0;
	if (flags & PF_X)
		prot |= VM_PROT_EXECUTE;
	if (flags & PF_W)
		prot |= VM_PROT_WRITE;
	if (flags & PF_R)
		prot |= VM_PROT_READ;
#if __ELF_WORD_SIZE == 32 && (defined(__amd64__) || defined(__i386__))
	if (i386_read_exec && (flags & PF_R))
		prot |= VM_PROT_EXECUTE;
#endif
	return (prot);
}

static Elf_Word
__elfN(untrans_prot)(vm_prot_t prot)
{
	Elf_Word flags;

	flags = 0;
	if (prot & VM_PROT_EXECUTE)
		flags |= PF_X;
	if (prot & VM_PROT_READ)
		flags |= PF_R;
	if (prot & VM_PROT_WRITE)
		flags |= PF_W;
	return (flags);
}
