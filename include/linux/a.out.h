#ifndef __A_OUT_GNU_H__
#define __A_OUT_GNU_H__

#include <uapi/linux/a.out.h>

#ifndef __ASSEMBLY__
#if defined (M_OLDSUN2)
#else
#endif
#if defined (M_68010)
#else
#endif
#if defined (M_68020)
#else
#endif
#if defined (M_SPARC)
#else
#endif
#if !defined (N_MAGIC)
#endif
#if !defined (N_BADMAG)
#endif
#if !defined (N_TXTOFF)
#endif
#if !defined (N_DATOFF)
#endif
#if !defined (N_TRELOFF)
#endif
#if !defined (N_DRELOFF)
#endif
#if !defined (N_SYMOFF)
#endif
#if !defined (N_STROFF)
#endif
#if !defined (N_TXTADDR)
#endif
#if defined(vax) || defined(hp300) || defined(pyr)
#endif
#ifdef	sony
#endif	/* Sony.  */
#ifdef is68k
#endif
#if defined(m68k) && defined(PORTAR)
#endif
#ifdef linux
#include <asm/page.h>
#if defined(__i386__) || defined(__mc68000__)
#else
#ifndef SEGMENT_SIZE
#define SEGMENT_SIZE	PAGE_SIZE
#endif
#endif
#endif
#ifndef N_DATADDR
#endif
#if !defined (N_BSSADDR)
#endif
#if !defined (N_NLIST_DECLARED)
#endif /* no N_NLIST_DECLARED.  */
#if !defined (N_UNDF)
#endif
#if !defined (N_ABS)
#endif
#if !defined (N_TEXT)
#endif
#if !defined (N_DATA)
#endif
#if !defined (N_BSS)
#endif
#if !defined (N_FN)
#endif
#if !defined (N_EXT)
#endif
#if !defined (N_TYPE)
#endif
#if !defined (N_STAB)
#endif
#if !defined (N_RELOCATION_INFO_DECLARED)
#ifdef NS32K
#else
#endif
#endif /* no N_RELOCATION_INFO_DECLARED.  */
#endif /*__ASSEMBLY__ */
#endif /* __A_OUT_GNU_H__ */
