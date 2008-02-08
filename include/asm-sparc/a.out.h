/* $Id: a.out.h,v 1.13 2000/01/09 10:46:53 anton Exp $ */
#ifndef __SPARC_A_OUT_H__
#define __SPARC_A_OUT_H__

#define SPARC_PGSIZE    0x2000        /* Thanks to the sun4 architecture... */
#define SEGMENT_SIZE    SPARC_PGSIZE  /* whee... */

struct exec {
	unsigned char a_dynamic:1;      /* A __DYNAMIC is in this image */
	unsigned char a_toolversion:7;
	unsigned char a_machtype;
	unsigned short a_info;
	unsigned long a_text;		/* length of text, in bytes */
	unsigned long a_data;		/* length of data, in bytes */
	unsigned long a_bss;		/* length of bss, in bytes */
	unsigned long a_syms;		/* length of symbol table, in bytes */
	unsigned long a_entry;		/* where program begins */
	unsigned long a_trsize;
	unsigned long a_drsize;
};

/* Where in the file does the text information begin? */
#define N_TXTOFF(x)     (N_MAGIC(x) == ZMAGIC ? 0 : sizeof (struct exec))

/* Where do the Symbols start? */
#define N_SYMOFF(x)     (N_TXTOFF(x) + (x).a_text +   \
                         (x).a_data + (x).a_trsize +  \
                         (x).a_drsize)

/* Where does text segment go in memory after being loaded? */
#define N_TXTADDR(x)    (((N_MAGIC(x) == ZMAGIC) &&        \
	                 ((x).a_entry < SPARC_PGSIZE)) ?   \
                          0 : SPARC_PGSIZE)

/* And same for the data segment.. */
#define N_DATADDR(x) (N_MAGIC(x)==OMAGIC ?         \
                      (N_TXTADDR(x) + (x).a_text)  \
                       : (_N_SEGMENT_ROUND (_N_TXTENDADDR(x))))

#define N_TRSIZE(a)	((a).a_trsize)
#define N_DRSIZE(a)	((a).a_drsize)
#define N_SYMSIZE(a)	((a).a_syms)

/*
 * Sparc relocation types
 */
enum reloc_type
{
	RELOC_8,
	RELOC_16,
	RELOC_32,	/* simplest relocs */
	RELOC_DISP8,
	RELOC_DISP16,
	RELOC_DISP32,	/* Disp's (pc-rel) */
	RELOC_WDISP30,
	RELOC_WDISP22,  /* SR word disp's */
	RELOC_HI22,
	RELOC_22,	/* SR 22-bit relocs */
	RELOC_13,
	RELOC_LO10,	/* SR 13&10-bit relocs */
	RELOC_SFA_BASE,
	RELOC_SFA_OFF13, /* SR S.F.A. relocs */
	RELOC_BASE10,
	RELOC_BASE13,
	RELOC_BASE22,	/* base_relative pic */
	RELOC_PC10,
	RELOC_PC22,	/* special pc-rel pic */
	RELOC_JMP_TBL,	/* jmp_tbl_rel in pic */
	RELOC_SEGOFF16,	/* ShLib offset-in-seg */
	RELOC_GLOB_DAT,
	RELOC_JMP_SLOT,
	RELOC_RELATIVE 	/* rtld relocs */
};

/*
 * Format of a relocation datum.
 */
struct relocation_info /* used when header.a_machtype == M_SPARC */
{
        unsigned long   r_address;  /* relocation addr */
        unsigned int    r_index:24; /* segment index or symbol index */
        unsigned int    r_extern:1; /* if F, r_index==SEG#; if T, SYM idx */
        unsigned int    r_pad:2;    /* <unused> */
        enum reloc_type r_type:5;   /* type of relocation to perform */
        long            r_addend;   /* addend for relocation value */
};

#define N_RELOCATION_INFO_DECLARED 1

#endif /* __SPARC_A_OUT_H__ */
