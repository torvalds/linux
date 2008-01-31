#ifndef _LINUX_ELFNOTE_H
#define _LINUX_ELFNOTE_H
/*
 * Helper macros to generate ELF Note structures, which are put into a
 * PT_NOTE segment of the final vmlinux image.  These are useful for
 * including name-value pairs of metadata into the kernel binary (or
 * modules?) for use by external programs.
 *
 * Each note has three parts: a name, a type and a desc.  The name is
 * intended to distinguish the note's originator, so it would be a
 * company, project, subsystem, etc; it must be in a suitable form for
 * use in a section name.  The type is an integer which is used to tag
 * the data, and is considered to be within the "name" namespace (so
 * "FooCo"'s type 42 is distinct from "BarProj"'s type 42).  The
 * "desc" field is the actual data.  There are no constraints on the
 * desc field's contents, though typically they're fairly small.
 *
 * All notes from a given NAME are put into a section named
 * .note.NAME.  When the kernel image is finally linked, all the notes
 * are packed into a single .notes section, which is mapped into the
 * PT_NOTE segment.  Because notes for a given name are grouped into
 * the same section, they'll all be adjacent the output file.
 *
 * This file defines macros for both C and assembler use.  Their
 * syntax is slightly different, but they're semantically similar.
 *
 * See the ELF specification for more detail about ELF notes.
 */

#ifdef __ASSEMBLER__
/*
 * Generate a structure with the same shape as Elf{32,64}_Nhdr (which
 * turn out to be the same size and shape), followed by the name and
 * desc data with appropriate padding.  The 'desctype' argument is the
 * assembler pseudo op defining the type of the data e.g. .asciz while
 * 'descdata' is the data itself e.g.  "hello, world".
 *
 * e.g. ELFNOTE(XYZCo, 42, .asciz, "forty-two")
 *      ELFNOTE(XYZCo, 12, .long, 0xdeadbeef)
 */
#define ELFNOTE_START(name, type, flags)	\
.pushsection .note.name, flags,@note	;	\
  .balign 4				;	\
  .long 2f - 1f		/* namesz */	;	\
  .long 4484f - 3f	/* descsz */	;	\
  .long type				;	\
1:.asciz #name				;	\
2:.balign 4				;	\
3:

#define ELFNOTE_END				\
4484:.balign 4				;	\
.popsection				;

#define ELFNOTE(name, type, desc)		\
	ELFNOTE_START(name, type, "")		\
		desc			;	\
	ELFNOTE_END

#else	/* !__ASSEMBLER__ */
#include <linux/elf.h>
/*
 * Use an anonymous structure which matches the shape of
 * Elf{32,64}_Nhdr, but includes the name and desc data.  The size and
 * type of name and desc depend on the macro arguments.  "name" must
 * be a literal string, and "desc" must be passed by value.  You may
 * only define one note per line, since __LINE__ is used to generate
 * unique symbols.
 */
#define _ELFNOTE_PASTE(a,b)	a##b
#define _ELFNOTE(size, name, unique, type, desc)			\
	static const struct {						\
		struct elf##size##_note _nhdr;				\
		unsigned char _name[sizeof(name)]			\
		__attribute__((aligned(sizeof(Elf##size##_Word))));	\
		typeof(desc) _desc					\
			     __attribute__((aligned(sizeof(Elf##size##_Word)))); \
	} _ELFNOTE_PASTE(_note_, unique)				\
		__used							\
		__attribute__((section(".note." name),			\
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
#define ELFNOTE(size, name, type, desc)		\
	_ELFNOTE(size, name, __LINE__, type, desc)

#define ELFNOTE32(name, type, desc) ELFNOTE(32, name, type, desc)
#define ELFNOTE64(name, type, desc) ELFNOTE(64, name, type, desc)
#endif	/* __ASSEMBLER__ */

#endif /* _LINUX_ELFNOTE_H */
