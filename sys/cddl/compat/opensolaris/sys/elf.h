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
 *
 * $FreeBSD$
 *
 * ELF compatibility definitions for OpenSolaris source.
 *
 */

#ifndef	_SYS__ELF_SOLARIS_H_
#define	_SYS__ELF_SOLARIS_H_

#include_next <sys/elf.h>

#define __sElfN(x)       typedef __CONCAT(__CONCAT(__CONCAT(Elf,__ELF_WORD_SIZE),_),x) x

__sElfN(Addr);
__sElfN(Cap);
__sElfN(Dyn);
__sElfN(Ehdr);
__sElfN(Move);
__sElfN(Off);
__sElfN(Phdr);
__sElfN(Rel);
__sElfN(Rela);
__sElfN(Shdr);
__sElfN(Sym);
__sElfN(Syminfo);
__sElfN(Verdaux);
__sElfN(Verdef);
__sElfN(Vernaux);
__sElfN(Verneed);
__sElfN(Versym);

__sElfN(Half);
__sElfN(Sword);
__sElfN(Word);

#if __ELF_WORD_SIZE == 32
typedef	Elf32_Word	Xword;	/* Xword/Sxword are 32-bits in Elf32 */
typedef	Elf32_Sword	Sxword;
#else
typedef	Elf64_Xword	Xword;
typedef	Elf64_Sxword	Sxword;
#endif

#define ELF_M_INFO	__ELFN(M_INFO)
#define ELF_M_SIZE	__ELFN(M_SIZE)
#define ELF_M_SYM	__ELFN(M_SYM)

/*
 * Elf `printf' type-cast macros.  These force arguments to be a fixed size
 * so that Elf32 and Elf64 can share common format strings.
 */
#define	EC_ADDR(a)	((Elf64_Addr)(a))		/* "ull" */
#define	EC_OFF(a)	((Elf64_Off)(a))		/* "ull"  */
#define	EC_HALF(a)	((Elf64_Half)(a))		/* "d"   */
#define	EC_WORD(a)	((Elf64_Word)(a))		/* "u"   */
#define	EC_SWORD(a)	((Elf64_Sword)(a))		/* "d"   */
#define	EC_XWORD(a)	((Elf64_Xword)(a))		/* "ull" */
#define	EC_SXWORD(a)	((Elf64_Sxword)(a))		/* "ll"  */
#define	EC_LWORD(a)	((Elf64_Lword)(a))		/* "ull" */

#define	elf_checksum		__elfN(checksum)
#define	elf_fsize		__elfN(fsize)
#define	elf_getehdr		__elfN(getehdr)
#define	elf_getphdr		__elfN(getphdr)
#define	elf_newehdr		__elfN(newehdr)
#define	elf_newphdr		__elfN(newphdr)
#define	elf_getshdr		__elfN(getshdr)
#define	elf_xlatetof		__elfN(xlatetof)
#define	elf_xlatetom		__elfN(xlatetom)

#define	Elf_cap_entry		__ElfN(cap_entry)
#define	Elf_cap_title		__ElfN(cap_title)
#define	Elf_demangle_name	__ElfN(demangle_name)
#define	Elf_dyn_entry		__ElfN(dyn_entry)
#define	Elf_dyn_title		__ElfN(dyn_title)
#define	Elf_ehdr		__ElfN(ehdr)
#define	Elf_got_entry		__ElfN(got_entry)
#define	Elf_got_title		__ElfN(got_title)
#define	Elf_reloc_apply_reg	__ElfN(reloc_apply_reg)
#define	Elf_reloc_apply_val	__ElfN(reloc_apply_val)
#define	Elf_reloc_entry_1	__ElfN(reloc_entry_1)
#define	Elf_reloc_entry_2	__ElfN(reloc_entry_2)
#define	Elf_reloc_title		__ElfN(reloc_title)
#define	Elf_phdr		__ElfN(phdr)
#define	Elf_shdr		__ElfN(shdr)
#define	Elf_syms_table_entry	__ElfN(syms_table_entry)
#define	Elf_syms_table_title	__ElfN(syms_table_title)
#define	Elf_ver_def_title	__ElfN(ver_def_title)
#define	Elf_ver_line_1		__ElfN(ver_line_1)
#define	Elf_ver_line_2		__ElfN(ver_line_2)
#define	Elf_ver_line_3		__ElfN(ver_line_3)
#define	Elf_ver_line_4		__ElfN(ver_line_4)
#define	Elf_ver_line_5		__ElfN(ver_line_5)
#define	Elf_ver_need_title	__ElfN(ver_need_title)

#endif /* !_SYS__ELF_SOLARIS_H_ */
