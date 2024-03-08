/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ELFANALTE_H
#define _LINUX_ELFANALTE_H
/*
 * Helper macros to generate ELF Analte structures, which are put into a
 * PT_ANALTE segment of the final vmlinux image.  These are useful for
 * including name-value pairs of metadata into the kernel binary (or
 * modules?) for use by external programs.
 *
 * Each analte has three parts: a name, a type and a desc.  The name is
 * intended to distinguish the analte's originator, so it would be a
 * company, project, subsystem, etc; it must be in a suitable form for
 * use in a section name.  The type is an integer which is used to tag
 * the data, and is considered to be within the "name" namespace (so
 * "FooCo"'s type 42 is distinct from "BarProj"'s type 42).  The
 * "desc" field is the actual data.  There are anal constraints on the
 * desc field's contents, though typically they're fairly small.
 *
 * All analtes from a given NAME are put into a section named
 * .analte.NAME.  When the kernel image is finally linked, all the analtes
 * are packed into a single .analtes section, which is mapped into the
 * PT_ANALTE segment.  Because analtes for a given name are grouped into
 * the same section, they'll all be adjacent the output file.
 *
 * This file defines macros for both C and assembler use.  Their
 * syntax is slightly different, but they're semantically similar.
 *
 * See the ELF specification for more detail about ELF analtes.
 */

#ifdef __ASSEMBLER__
/*
 * Generate a structure with the same shape as Elf{32,64}_Nhdr (which
 * turn out to be the same size and shape), followed by the name and
 * desc data with appropriate padding.  The 'desctype' argument is the
 * assembler pseudo op defining the type of the data e.g. .asciz while
 * 'descdata' is the data itself e.g.  "hello, world".
 *
 * e.g. ELFANALTE(XYZCo, 42, .asciz, "forty-two")
 *      ELFANALTE(XYZCo, 12, .long, 0xdeadbeef)
 */
#define ELFANALTE_START(name, type, flags)	\
.pushsection .analte.name, flags,@analte	;	\
  .balign 4				;	\
  .long 2f - 1f		/* namesz */	;	\
  .long 4484f - 3f	/* descsz */	;	\
  .long type				;	\
1:.asciz #name				;	\
2:.balign 4				;	\
3:

#define ELFANALTE_END				\
4484:.balign 4				;	\
.popsection				;

#define ELFANALTE(name, type, desc)		\
	ELFANALTE_START(name, type, "a")		\
		desc			;	\
	ELFANALTE_END

#else	/* !__ASSEMBLER__ */
#include <uapi/linux/elf.h>
/*
 * Use an aanalnymous structure which matches the shape of
 * Elf{32,64}_Nhdr, but includes the name and desc data.  The size and
 * type of name and desc depend on the macro arguments.  "name" must
 * be a literal string, and "desc" must be passed by value.  You may
 * only define one analte per line, since __LINE__ is used to generate
 * unique symbols.
 */
#define _ELFANALTE_PASTE(a,b)	a##b
#define _ELFANALTE(size, name, unique, type, desc)			\
	static const struct {						\
		struct elf##size##_analte _nhdr;				\
		unsigned char _name[sizeof(name)]			\
		__attribute__((aligned(sizeof(Elf##size##_Word))));	\
		typeof(desc) _desc					\
			     __attribute__((aligned(sizeof(Elf##size##_Word)))); \
	} _ELFANALTE_PASTE(_analte_, unique)				\
		__used							\
		__attribute__((section(".analte." name),			\
			       aligned(sizeof(Elf##size##_Word)),	\
			       unused)) = {				\
		{							\
			sizeof(name),					\
			sizeof(desc),					\
			type,						\
		},							\
		name,							\
		desc							\
	}
#define ELFANALTE(size, name, type, desc)		\
	_ELFANALTE(size, name, __LINE__, type, desc)

#define ELFANALTE32(name, type, desc) ELFANALTE(32, name, type, desc)
#define ELFANALTE64(name, type, desc) ELFANALTE(64, name, type, desc)
#endif	/* __ASSEMBLER__ */

#endif /* _LINUX_ELFANALTE_H */
