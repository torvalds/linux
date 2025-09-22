/*	$OpenBSD: reloc.h,v 1.3 2011/03/23 16:54:34 pirofti Exp $	*/

#ifndef	_MACHINE_RELOC_H_
#define	_MACHINE_RELOC_H_

#define RELOC_NONE            0       /* No reloc */
#define RELOC_REFLONG         1       /* Direct 32 bit */
#define RELOC_REFQUAD         2       /* Direct 64 bit */
#define RELOC_GPREL32         3       /* GP relative 32 bit */
#define RELOC_LITERAL         4       /* GP relative 16 bit w/optimization */
#define RELOC_LITUSE          5       /* Optimization hint for LITERAL */
#define RELOC_GPDISP          6       /* Add displacement to GP */
#define RELOC_BRADDR          7       /* PC+4 relative 23 bit shifted */
#define RELOC_HINT            8       /* PC+4 relative 16 bit shifted */
#define RELOC_SREL16          9       /* PC relative 16 bit */
#define RELOC_SREL32          10      /* PC relative 32 bit */
#define RELOC_SREL64          11      /* PC relative 64 bit */
#define RELOC_OP_PUSH         12      /* OP stack push */
#define RELOC_OP_STORE        13      /* OP stack pop and store */
#define RELOC_OP_PSUB         14      /* OP stack subtract */
#define RELOC_OP_PRSHIFT      15      /* OP stack right shift */
#define RELOC_GPVALUE         16
#define RELOC_GPRELHIGH       17
#define RELOC_GPRELLOW        18
#define RELOC_IMMED_GP_16     19
#define RELOC_IMMED_GP_HI32   20
#define RELOC_IMMED_SCN_HI32  21
#define RELOC_IMMED_BRELOC_HI32   22
#define RELOC_IMMED_LO32      23
#define RELOC_COPY            24      /* Copy symbol at runtime */
#define RELOC_GLOB_DAT        25      /* Create GOT entry */
#define RELOC_JMP_SLOT        26      /* Create PLT entry */
#define RELOC_RELATIVE        27      /* Adjust by program base */

#define R_TYPE(X)	__CONCAT(RELOC_,X)

#endif	/* _MACHINE_RELOC_H_ */
