#ifndef __ASM_E820_H
#define __ASM_E820_H
#define E820MAP	0x2d0		/* our map */
#define E820MAX	128		/* number of entries in E820MAP */

/*
 * Legacy E820 BIOS limits us to 128 (E820MAX) nodes due to the
 * constrained space in the zeropage.  If we have more nodes than
 * that, and if we've booted off EFI firmware, then the EFI tables
 * passed us from the EFI firmware can list more nodes.  Size our
 * internal memory map tables to have room for these additional
 * nodes, based on up to three entries per node for which the
 * kernel was built: MAX_NUMNODES == (1 << CONFIG_NODES_SHIFT),
 * plus E820MAX, allowing space for the possible duplicate E820
 * entries that might need room in the same arrays, prior to the
 * call to sanitize_e820_map() to remove duplicates.  The allowance
 * of three memory map entries per node is "enough" entries for
 * the initial hardware platform motivating this mechanism to make
 * use of additional EFI map entries.  Future platforms may want
 * to allow more than three entries per node or otherwise refine
 * this size.
 */

/*
 * Odd: 'make headers_check' complains about numa.h if I try
 * to collapse the next two #ifdef lines to a single line:
 *	#if defined(__KERNEL__) && defined(CONFIG_EFI)
 */
#ifdef __KERNEL__
#ifdef CONFIG_EFI
#include <linux/numa.h>
#define E820_X_MAX (E820MAX + 3 * MAX_NUMNODES)
#else	/* ! CONFIG_EFI */
#define E820_X_MAX E820MAX
#endif
#else	/* ! __KERNEL__ */
#define E820_X_MAX E820MAX
#endif

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
	struct e820entry map[E820_X_MAX];
};

extern struct e820map e820;

extern int e820_any_mapped(u64 start, u64 end, unsigned type);
extern int e820_all_mapped(u64 start, u64 end, unsigned type);
extern void add_memory_region(u64 start, u64 size, int type);
extern void e820_print_map(char *who);
extern int
sanitize_e820_map(struct e820entry *biosmap, int max_nr_map, int *pnr_map);
extern int copy_e820_map(struct e820entry *biosmap, int nr_map);
extern u64 update_memory_range(u64 start, u64 size, unsigned old_type,
			       unsigned new_type);
extern void update_e820(void);
extern void e820_setup_gap(void);

extern u64 find_e820_area(u64 start, u64 end, u64 size, u64 align);
extern u64 find_e820_area_size(u64 start, u64 *sizep, u64 align);
extern void reserve_early(u64 start, u64 end, char *name);
extern void free_early(u64 start, u64 end);
extern void early_res_to_bootmem(u64 start, u64 end);

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
