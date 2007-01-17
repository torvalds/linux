/* Written 2000 by Andi Kleen */
#ifndef __ARCH_DESC_DEFS_H
#define __ARCH_DESC_DEFS_H

/*
 * Segment descriptor structure definitions, usable from both x86_64 and i386
 * archs.
 */

#ifndef __ASSEMBLY__

#include <linux/types.h>

// 8 byte segment descriptor
struct desc_struct {
	u16 limit0;
	u16 base0;
	unsigned base1 : 8, type : 4, s : 1, dpl : 2, p : 1;
	unsigned limit : 4, avl : 1, l : 1, d : 1, g : 1, base2 : 8;
} __attribute__((packed));

struct n_desc_struct {
	unsigned int a,b;
};

enum {
	GATE_INTERRUPT = 0xE,
	GATE_TRAP = 0xF,
	GATE_CALL = 0xC,
};

// 16byte gate
struct gate_struct {
	u16 offset_low;
	u16 segment;
	unsigned ist : 3, zero0 : 5, type : 5, dpl : 2, p : 1;
	u16 offset_middle;
	u32 offset_high;
	u32 zero1;
} __attribute__((packed));

#define PTR_LOW(x) ((unsigned long)(x) & 0xFFFF)
#define PTR_MIDDLE(x) (((unsigned long)(x) >> 16) & 0xFFFF)
#define PTR_HIGH(x) ((unsigned long)(x) >> 32)

enum {
	DESC_TSS = 0x9,
	DESC_LDT = 0x2,
};

// LDT or TSS descriptor in the GDT. 16 bytes.
struct ldttss_desc {
	u16 limit0;
	u16 base0;
	unsigned base1 : 8, type : 5, dpl : 2, p : 1;
	unsigned limit1 : 4, zero0 : 3, g : 1, base2 : 8;
	u32 base3;
	u32 zero1;
} __attribute__((packed));

struct desc_ptr {
	unsigned short size;
	unsigned long address;
} __attribute__((packed)) ;


#endif /* !__ASSEMBLY__ */

#endif
