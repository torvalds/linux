#ifndef __CRIS_A_OUT_H__
#define __CRIS_A_OUT_H__

/* we don't support a.out binaries on Linux/CRIS anyway, so this is
 * not really used but still needed because binfmt_elf.c for some reason
 * wants to know about a.out even if there is no interpreter available...
 */

struct exec
{
  unsigned long a_info;		/* Use macros N_MAGIC, etc for access */
  unsigned a_text;		/* length of text, in bytes */
  unsigned a_data;		/* length of data, in bytes */
  unsigned a_bss;		/* length of uninitialized data area for file, in bytes */
  unsigned a_syms;		/* length of symbol table data in file, in bytes */
  unsigned a_entry;		/* start address */
  unsigned a_trsize;		/* length of relocation info for text, in bytes */
  unsigned a_drsize;		/* length of relocation info for data, in bytes */
};


#define N_TRSIZE(a)	((a).a_trsize)
#define N_DRSIZE(a)	((a).a_drsize)
#define N_SYMSIZE(a)	((a).a_syms)

#endif
