/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * This file contains routines for relocating and dynamically linking
 * executable object code files in the Windows(r) PE (Portable Executable)
 * format. In Windows, anything with a .EXE, .DLL or .SYS extension is
 * considered an executable, and all such files have some structures in
 * common. The PE format was apparently based largely on COFF but has
 * mutated significantly over time. We are mainly concerned with .SYS files,
 * so this module implements only enough routines to be able to parse the
 * headers and sections of a .SYS object file and perform the necessary
 * relocations and jump table patching to allow us to call into it
 * (and to have it call back to us). Note that while this module
 * can handle fixups for imported symbols, it knows nothing about
 * exporting them.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/errno.h>
#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#endif

#include <compat/ndis/pe_var.h>

static vm_offset_t pe_functbl_match(image_patch_table *, char *);

/*
 * Check for an MS-DOS executable header. All Windows binaries
 * have a small MS-DOS executable prepended to them to print out
 * the "This program requires Windows" message. Even .SYS files
 * have this header, in spite of the fact that you're can't actually
 * run them directly.
 */

int
pe_get_dos_header(imgbase, hdr)
	vm_offset_t		imgbase;
	image_dos_header	*hdr;
{
	uint16_t		signature;

	if (imgbase == 0 || hdr == NULL)
		return (EINVAL);

	signature = *(uint16_t *)imgbase;
	if (signature != IMAGE_DOS_SIGNATURE)
		return (ENOEXEC);

	bcopy ((char *)imgbase, (char *)hdr, sizeof(image_dos_header));

	return (0);
}

/*
 * Verify that this image has a Windows NT PE signature.
 */

int
pe_is_nt_image(imgbase)
	vm_offset_t		imgbase;
{
	uint32_t		signature;
	image_dos_header	*dos_hdr;

	if (imgbase == 0)
		return (EINVAL);

	signature = *(uint16_t *)imgbase;
	if (signature == IMAGE_DOS_SIGNATURE) {
		dos_hdr = (image_dos_header *)imgbase;
		signature = *(uint32_t *)(imgbase + dos_hdr->idh_lfanew);
		if (signature == IMAGE_NT_SIGNATURE)
			return (0);
	}

	return (ENOEXEC);
}

/*
 * Return a copy of the optional header. This contains the
 * executable entry point and the directory listing which we
 * need to find the relocations and imports later.
 */

int
pe_get_optional_header(imgbase, hdr)
	vm_offset_t		imgbase;
	image_optional_header	*hdr;
{
	image_dos_header	*dos_hdr;
	image_nt_header		*nt_hdr;

	if (imgbase == 0 || hdr == NULL)
		return (EINVAL);

	if (pe_is_nt_image(imgbase))
		return (EINVAL);

	dos_hdr = (image_dos_header *)(imgbase);
	nt_hdr = (image_nt_header *)(imgbase + dos_hdr->idh_lfanew);

	bcopy ((char *)&nt_hdr->inh_optionalhdr, (char *)hdr,
	    nt_hdr->inh_filehdr.ifh_optionalhdrlen);

	return (0);
}

/*
 * Return a copy of the file header. Contains the number of
 * sections in this image.
 */

int
pe_get_file_header(imgbase, hdr)
	vm_offset_t		imgbase;
	image_file_header	*hdr;
{
	image_dos_header	*dos_hdr;
	image_nt_header		*nt_hdr;

	if (imgbase == 0 || hdr == NULL)
		return (EINVAL);

	if (pe_is_nt_image(imgbase))
		return (EINVAL);

	dos_hdr = (image_dos_header *)imgbase;
	nt_hdr = (image_nt_header *)(imgbase + dos_hdr->idh_lfanew);

	/*
	 * Note: the size of the nt_header is variable since it
	 * can contain optional fields, as indicated by ifh_optionalhdrlen.
	 * However it happens we're only interested in fields in the
	 * non-variant portion of the nt_header structure, so we don't
	 * bother copying the optional parts here.
	 */

	bcopy ((char *)&nt_hdr->inh_filehdr, (char *)hdr,
	    sizeof(image_file_header));

	return (0);
}

/*
 * Return the header of the first section in this image (usually
 * .text).
 */

int
pe_get_section_header(imgbase, hdr)
	vm_offset_t		imgbase;
	image_section_header	*hdr;
{
	image_dos_header	*dos_hdr;
	image_nt_header		*nt_hdr;
	image_section_header	*sect_hdr;

	if (imgbase == 0 || hdr == NULL)
		return (EINVAL);

	if (pe_is_nt_image(imgbase))
		return (EINVAL);

	dos_hdr = (image_dos_header *)imgbase;
	nt_hdr = (image_nt_header *)(imgbase + dos_hdr->idh_lfanew);
	sect_hdr = IMAGE_FIRST_SECTION(nt_hdr);

	bcopy ((char *)sect_hdr, (char *)hdr, sizeof(image_section_header));

	return (0);
}

/*
 * Return the number of sections in this executable, or 0 on error.
 */

int
pe_numsections(imgbase)
	vm_offset_t		imgbase;
{
	image_file_header	file_hdr;

	if (pe_get_file_header(imgbase, &file_hdr))
		return (0);

	return (file_hdr.ifh_numsections);
}

/*
 * Return the base address that this image was linked for.
 * This helps us calculate relocation addresses later.
 */

vm_offset_t
pe_imagebase(imgbase)
	vm_offset_t		imgbase;
{
	image_optional_header	optional_hdr;

	if (pe_get_optional_header(imgbase, &optional_hdr))
		return (0);

	return (optional_hdr.ioh_imagebase);
}

/*
 * Return the offset of a given directory structure within the
 * image. Directories reside within sections.
 */

vm_offset_t
pe_directory_offset(imgbase, diridx)
	vm_offset_t		imgbase;
	uint32_t		diridx;
{
	image_optional_header	opt_hdr;
	vm_offset_t		dir;

	if (pe_get_optional_header(imgbase, &opt_hdr))
		return (0);

	if (diridx >= opt_hdr.ioh_rva_size_cnt)
		return (0);

	dir = opt_hdr.ioh_datadir[diridx].idd_vaddr;

	return (pe_translate_addr(imgbase, dir));
}

vm_offset_t
pe_translate_addr(imgbase, rva)
	vm_offset_t		imgbase;
	vm_offset_t		rva;
{
	image_optional_header	opt_hdr;
	image_section_header	*sect_hdr;
	image_dos_header	*dos_hdr;
	image_nt_header		*nt_hdr;
	int			i = 0, sections, fixedlen;

	if (pe_get_optional_header(imgbase, &opt_hdr))
		return (0);

	sections = pe_numsections(imgbase);

	dos_hdr = (image_dos_header *)imgbase;
	nt_hdr = (image_nt_header *)(imgbase + dos_hdr->idh_lfanew);
	sect_hdr = IMAGE_FIRST_SECTION(nt_hdr);

	/*
	 * The test here is to see if the RVA falls somewhere
	 * inside the section, based on the section's start RVA
	 * and its length. However it seems sometimes the
	 * virtual length isn't enough to cover the entire
	 * area of the section. We fudge by taking into account
	 * the section alignment and rounding the section length
	 * up to a page boundary.
	 */
	while (i++ < sections) {
		fixedlen = sect_hdr->ish_misc.ish_vsize;
		fixedlen += ((opt_hdr.ioh_sectalign - 1) -
		    sect_hdr->ish_misc.ish_vsize) &
		    (opt_hdr.ioh_sectalign - 1);
		if (sect_hdr->ish_vaddr <= (uint32_t)rva &&
		    (sect_hdr->ish_vaddr + fixedlen) >
		    (uint32_t)rva)
			break;
		sect_hdr++;
	}

	if (i > sections)
		return (0);

	return ((vm_offset_t)(imgbase + rva - sect_hdr->ish_vaddr +
	    sect_hdr->ish_rawdataaddr));
}

/*
 * Get the section header for a particular section. Note that
 * section names can be anything, but there are some standard
 * ones (.text, .data, .rdata, .reloc).
 */

int
pe_get_section(imgbase, hdr, name)
	vm_offset_t		imgbase;
	image_section_header	*hdr;
	const char		*name;
{
	image_dos_header	*dos_hdr;
	image_nt_header		*nt_hdr;
	image_section_header	*sect_hdr;

	int			i, sections;

	if (imgbase == 0 || hdr == NULL)
		return (EINVAL);

	if (pe_is_nt_image(imgbase))
		return (EINVAL);

	sections = pe_numsections(imgbase);

	dos_hdr = (image_dos_header *)imgbase;
	nt_hdr = (image_nt_header *)(imgbase + dos_hdr->idh_lfanew);
	sect_hdr = IMAGE_FIRST_SECTION(nt_hdr);

	for (i = 0; i < sections; i++) {
		if (!strcmp ((char *)&sect_hdr->ish_name, name)) {
			bcopy((char *)sect_hdr, (char *)hdr,
			    sizeof(image_section_header));
			return (0);
		} else
			sect_hdr++;
	}

	return (ENOEXEC);
}

/*
 * Apply the base relocations to this image. The relocation table
 * resides within the .reloc section. Relocations are specified in
 * blocks which refer to a particular page. We apply the relocations
 * one page block at a time.
 */

int
pe_relocate(imgbase)
	vm_offset_t		imgbase;
{
	image_section_header	sect;
	image_base_reloc	*relhdr;
	uint16_t		rel, *sloc;
	vm_offset_t		base;
	vm_size_t		delta;
	uint32_t		*lloc;
	uint64_t		*qloc;
	int			i, count;
	vm_offset_t		txt;

	base = pe_imagebase(imgbase);
	pe_get_section(imgbase, &sect, ".text");
	txt = pe_translate_addr(imgbase, sect.ish_vaddr);
	delta = (uint32_t)(txt) - base - sect.ish_vaddr;

	pe_get_section(imgbase, &sect, ".reloc");

	relhdr = (image_base_reloc *)(imgbase + sect.ish_rawdataaddr);

	do {
		count = (relhdr->ibr_blocksize -
		    (sizeof(uint32_t) * 2)) / sizeof(uint16_t);
		for (i = 0; i < count; i++) {
			rel = relhdr->ibr_rel[i];
			switch (IMR_RELTYPE(rel)) {
			case IMAGE_REL_BASED_ABSOLUTE:
				break;
			case IMAGE_REL_BASED_HIGHLOW:
				lloc = (uint32_t *)pe_translate_addr(imgbase,
				    relhdr->ibr_vaddr + IMR_RELOFFSET(rel));
				*lloc = pe_translate_addr(imgbase,
				    (*lloc - base));
				break;
			case IMAGE_REL_BASED_HIGH:
				sloc = (uint16_t *)pe_translate_addr(imgbase,
				    relhdr->ibr_vaddr + IMR_RELOFFSET(rel));
				*sloc += (delta & 0xFFFF0000) >> 16;
				break;
			case IMAGE_REL_BASED_LOW:
				sloc = (uint16_t *)pe_translate_addr(imgbase,
				    relhdr->ibr_vaddr + IMR_RELOFFSET(rel));
				*sloc += (delta & 0xFFFF);
				break;
			case IMAGE_REL_BASED_DIR64:
				qloc = (uint64_t *)pe_translate_addr(imgbase,
				    relhdr->ibr_vaddr + IMR_RELOFFSET(rel));
				*qloc = pe_translate_addr(imgbase,
				    (*qloc - base));
				break;

			default:
				printf("[%d]reloc type: %d\n",i,
				    IMR_RELTYPE(rel));
				break;
			}
		}
		relhdr = (image_base_reloc *)((vm_offset_t)relhdr +
		    relhdr->ibr_blocksize);
	} while (relhdr->ibr_blocksize);

	return (0);
}

/*
 * Return the import descriptor for a particular module. An image
 * may be linked against several modules, typically HAL.dll, ntoskrnl.exe
 * and NDIS.SYS. For each module, there is a list of imported function
 * names and their addresses.
 *
 * Note: module names are case insensitive!
 */

int
pe_get_import_descriptor(imgbase, desc, module)
	vm_offset_t		imgbase;
	image_import_descriptor	*desc;
	char			*module;
{
	vm_offset_t		offset;
	image_import_descriptor	*imp_desc;
	char			*modname;

	if (imgbase == 0 || module == NULL || desc == NULL)
		return (EINVAL);

	offset = pe_directory_offset(imgbase, IMAGE_DIRECTORY_ENTRY_IMPORT);
	if (offset == 0)
		return (ENOENT);

	imp_desc = (void *)offset;

	while (imp_desc->iid_nameaddr) {
		modname = (char *)pe_translate_addr(imgbase,
		    imp_desc->iid_nameaddr);
		if (!strncasecmp(module, modname, strlen(module))) {
			bcopy((char *)imp_desc, (char *)desc,
			    sizeof(image_import_descriptor));
			return (0);
		}
		imp_desc++;
	}

	return (ENOENT);
}

int
pe_get_messagetable(imgbase, md)
	vm_offset_t		imgbase;
	message_resource_data	**md;
{
	image_resource_directory	*rdir, *rtype;
	image_resource_directory_entry	*dent, *dent2;
	image_resource_data_entry	*rent;
	vm_offset_t		offset;
	int			i;

	if (imgbase == 0)
		return (EINVAL);

	offset = pe_directory_offset(imgbase, IMAGE_DIRECTORY_ENTRY_RESOURCE);
	if (offset == 0)
		return (ENOENT);

	rdir = (image_resource_directory *)offset;

	dent = (image_resource_directory_entry *)(offset +
	    sizeof(image_resource_directory));

	for (i = 0; i < rdir->ird_id_entries; i++){
		if (dent->irde_name != RT_MESSAGETABLE)	{
			dent++;
			continue;
		}
		dent2 = dent;
		while (dent2->irde_dataoff & RESOURCE_DIR_FLAG) {
			rtype = (image_resource_directory *)(offset +
			    (dent2->irde_dataoff & ~RESOURCE_DIR_FLAG));
			dent2 = (image_resource_directory_entry *)
			    ((uintptr_t)rtype +
			     sizeof(image_resource_directory));
		}
		rent = (image_resource_data_entry *)(offset +
		    dent2->irde_dataoff);
		*md = (message_resource_data *)pe_translate_addr(imgbase,
		    rent->irde_offset);
		return (0);
	}

	return (ENOENT);
}

int
pe_get_message(imgbase, id, str, len, flags)
	vm_offset_t		imgbase;
	uint32_t		id;
	char			**str;
	int			*len;
	uint16_t		*flags;
{
	message_resource_data	*md = NULL;
	message_resource_block	*mb;
	message_resource_entry	*me;
	uint32_t		i;

	pe_get_messagetable(imgbase, &md);

	if (md == NULL)
		return (ENOENT);

	mb = (message_resource_block *)((uintptr_t)md +
	    sizeof(message_resource_data));

	for (i = 0; i < md->mrd_numblocks; i++) {
		if (id >= mb->mrb_lowid && id <= mb->mrb_highid) {
			me = (message_resource_entry *)((uintptr_t)md +
			    mb->mrb_entryoff);
			for (i = id - mb->mrb_lowid; i > 0; i--)
				me = (message_resource_entry *)((uintptr_t)me +
				    me->mre_len);
			*str = me->mre_text;
			*len = me->mre_len;
			*flags = me->mre_flags;
			return (0);
		}
		mb++;
	}

	return (ENOENT);
}

/*
 * Find the function that matches a particular name. This doesn't
 * need to be particularly speedy since it's only run when loading
 * a module for the first time.
 */

static vm_offset_t
pe_functbl_match(functbl, name)
	image_patch_table	*functbl;
	char			*name;
{
	image_patch_table	*p;

	if (functbl == NULL || name == NULL)
		return (0);

	p = functbl;

	while (p->ipt_name != NULL) {
		if (!strcmp(p->ipt_name, name))
			return ((vm_offset_t)p->ipt_wrap);
		p++;
	}
	printf("no match for %s\n", name);

	/*
	 * Return the wrapper pointer for this routine.
	 * For x86, this is the same as the funcptr.
	 * For amd64, this points to a wrapper routine
	 * that does calling convention translation and
	 * then invokes the underlying routine.
	 */
	return ((vm_offset_t)p->ipt_wrap);
}

/*
 * Patch the imported function addresses for a given module.
 * The caller must specify the module name and provide a table
 * of function pointers that will be patched into the jump table.
 * Note that there are actually two copies of the jump table: one
 * copy is left alone. In a .SYS file, the jump tables are usually
 * merged into the INIT segment.
 */

int
pe_patch_imports(imgbase, module, functbl)
	vm_offset_t		imgbase;
	char			*module;
	image_patch_table	*functbl;
{
	image_import_descriptor	imp_desc;
	char			*fname;
	vm_offset_t		*nptr, *fptr;
	vm_offset_t		func;

	if (imgbase == 0 || module == NULL || functbl == NULL)
		return (EINVAL);

	if (pe_get_import_descriptor(imgbase, &imp_desc, module))
		return (ENOEXEC);

	nptr = (vm_offset_t *)pe_translate_addr(imgbase,
	    imp_desc.iid_import_name_table_addr);
	fptr = (vm_offset_t *)pe_translate_addr(imgbase,
	    imp_desc.iid_import_address_table_addr);

	while (nptr != NULL && pe_translate_addr(imgbase, *nptr)) {
		fname = (char *)pe_translate_addr(imgbase, (*nptr) + 2);
		func = pe_functbl_match(functbl, fname);
		if (func)
			*fptr = func;
#ifdef notdef
		if (*fptr == 0)
			return (ENOENT);
#endif
		nptr++;
		fptr++;
	}

	return (0);
}
