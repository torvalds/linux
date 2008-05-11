#ifndef __ASM_E820_H
#define __ASM_E820_H
#define E820MAP	0x2d0		/* our map */
#define E820MAX	128		/* number of entries in E820MAP */
#define E820NR	0x1e8		/* # entries in E820MAP */

#define E820_RAM	1
#define E820_RESERVED	2
#define E820_ACPI	3
#define E820_NVS	4

#ifndef __ASSEMBLY__
struct e820entry {
	__u64 addr;	/* start of memory segment */
	__u64 size;	/* size of memory segment */
	__u32 type;	/* type of memory segment */
} __attribute__((packed));

struct e820map {
	__u32 nr_map;
	struct e820entry map[E820MAX];
};

extern struct e820map e820;

extern int e820_any_mapped(u64 start, u64 end, unsigned type);
extern int e820_all_mapped(u64 start, u64 end, unsigned type);
extern void add_memory_region(u64 start, u64 size, int type);
extern void e820_print_map(char *who);
extern int sanitize_e820_map(struct e820entry *biosmap, char *pnr_map);
extern int copy_e820_map(struct e820entry *biosmap, int nr_map);
extern u64 update_memory_range(u64 start, u64 size, unsigned old_type,
			       unsigned new_type);
extern void update_e820(void);
extern void e820_setup_gap(void);

#endif /* __ASSEMBLY__ */

#define ISA_START_ADDRESS	0xa0000
#define ISA_END_ADDRESS		0x100000

#define BIOS_BEGIN		0x000a0000
#define BIOS_END		0x00100000

#ifdef __KERNEL__
#ifdef CONFIG_X86_32
# include "e820_32.h"
#else
# include "e820_64.h"
#endif
#endif /* __KERNEL__ */

#endif  /* __ASM_E820_H */
