#ifndef _LINUX_LINKAGE_H
#define _LINUX_LINKAGE_H

#include <asm/linkage.h>

#ifdef __cplusplus
#define CPP_ASMLINKAGE extern "C"
#else
#define CPP_ASMLINKAGE
#endif

#ifndef asmlinkage
#define asmlinkage CPP_ASMLINKAGE
#endif

#ifndef asmregparm
# define asmregparm
#endif

#ifndef asmlinkage_protect
# define asmlinkage_protect(n, ret, args...)	do { } while (0)
#endif

#ifndef __ALIGN
#define __ALIGN		.align 4,0x90
#define __ALIGN_STR	".align 4,0x90"
#endif

#ifdef __ASSEMBLY__

#define ALIGN __ALIGN
#define ALIGN_STR __ALIGN_STR

#ifndef ENTRY
#define ENTRY(name) \
  .globl name; \
  ALIGN; \
  name:
#endif

#ifndef WEAK
#define WEAK(name)	   \
	.weak name;	   \
	name:
#endif

#define KPROBE_ENTRY(name) \
  .pushsection .kprobes.text, "ax"; \
  ENTRY(name)

#define KPROBE_END(name) \
  END(name);		 \
  .popsection

#ifndef END
#define END(name) \
  .size name, .-name
#endif

/* If symbol 'name' is treated as a subroutine (gets called, and returns)
 * then please use ENDPROC to mark 'name' as STT_FUNC for the benefit of
 * static analysis tools such as stack depth analyzer.
 */
#ifndef ENDPROC
#define ENDPROC(name) \
  .type name, @function; \
  END(name)
#endif

#endif

#define NORET_TYPE    /**/
#define ATTRIB_NORET  __attribute__((noreturn))
#define NORET_AND     noreturn,

#endif
