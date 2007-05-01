/*
 * Definitions for talking to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */
#ifdef __KERNEL__
#ifndef _PPC_PROM_H
#define _PPC_PROM_H

/* This is used in arch/ppc/mm/mem_pieces.h */
struct reg_property {
	unsigned int address;
	unsigned int size;
};

/*
 * These macros assist in performing the address calculations that we
 * need to do to access data when the kernel is running at an address
 * that is different from the address that the kernel is linked at.
 * The reloc_offset() function returns the difference between these
 * two addresses and the macros simplify the process of adding or
 * subtracting this offset to/from pointer values.
 */
extern unsigned long reloc_offset(void);
extern unsigned long add_reloc_offset(unsigned long);
extern unsigned long sub_reloc_offset(unsigned long);

#define PTRRELOC(x)	((typeof(x))add_reloc_offset((unsigned long)(x)))
#define PTRUNRELOC(x)	((typeof(x))sub_reloc_offset((unsigned long)(x)))

/*
 * Fallback definitions since we don't support OF in arch/ppc any more.
 */
#define machine_is_compatible(x)		0
#define of_find_compatible_node(f, t, c)	NULL
#define of_get_property(p, n, l)		NULL
#define get_property(a, b, c)			of_get_property((a), (b), (c))

#endif /* _PPC_PROM_H */
#endif /* __KERNEL__ */
