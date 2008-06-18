#ifndef ASM_X86__E820_H
#define ASM_X86__E820_H
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

/* reserved RAM used by kernel itself */
#define E820_RESERVED_KERN        128

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

#ifdef __KERNEL__
/* see comment in arch/x86/kernel/e820.c */
extern struct e820map e820;
extern struct e820map e820_saved;

extern int e820_any_mapped(u64 start, u64 end, unsigned type);
extern int e820_all_mapped(u64 start, u64 end, unsigned type);
extern void e820_add_region(u64 start, u64 size, int type);
extern void e820_print_map(char *who);
extern int
sanitize_e820_map(struct e820entry *biosmap, int max_nr_map, int *pnr_map);
extern u64 e820_update_range(u64 start, u64 size, unsigned old_type,
			       unsigned new_type);
extern u64 e820_remove_range(u64 start, u64 size, unsigned old_type,
			     int checktype);
extern void update_e820(void);
extern void e820_setup_gap(void);
extern int e820_search_gap(unsigned long *gapstart, unsigned long *gapsize,
			unsigned long start_addr, unsigned long long end_addr);
struct setup_data;
extern void parse_e820_ext(struct setup_data *data, unsigned long pa_data);

#if defined(CONFIG_X86_64) || \
	(defined(CONFIG_X86_32) && defined(CONFIG_HIBERNATION))
extern void e820_mark_nosave_regions(unsigned long limit_pfn);
#else
static inline void e820_mark_nosave_regions(unsigned long limit_pfn)
{
}
#endif

#ifdef CONFIG_MEMTEST
extern void early_memtest(unsigned long start, unsigned long end);
#else
static inline void early_memtest(unsigned long start, unsigned long end)
{
}
#endif

extern unsigned long end_user_pfn;

extern u64 find_e820_area(u64 start, u64 end, u64 size, u64 align);
extern u64 find_e820_area_size(u64 start, u64 *sizep, u64 align);
extern void reserve_early(u64 start, u64 end, char *name);
extern void reserve_early_overlap_ok(u64 start, u64 end, char *name);
extern void free_early(u64 start, u64 end);
extern void early_res_to_bootmem(u64 start, u64 end);
extern u64 early_reserve_e820(u64 startt, u64 sizet, u64 align);

extern unsigned long e820_end_of_ram_pfn(void);
extern unsigned long e820_end_of_low_ram_pfn(void);
extern int e820_find_active_region(const struct e820entry *ei,
				  unsigned long start_pfn,
				  unsigned long last_pfn,
				  unsigned long *ei_startpfn,
				  unsigned long *ei_endpfn);
extern void e820_register_active_regions(int nid, unsigned long start_pfn,
					 unsigned long end_pfn);
extern u64 e820_hole_size(u64 start, u64 end);
extern void finish_e820_parsing(void);
extern void e820_reserve_resources(void);
extern void setup_memory_map(void);
extern char *default_machine_specific_memory_setup(void);
extern char *machine_specific_memory_setup(void);
extern char *memory_setup(void);
#endif /* __KERNEL__ */
#endif /* __ASSEMBLY__ */

#define ISA_START_ADDRESS	0xa0000
#define ISA_END_ADDRESS		0x100000
#define is_ISA_range(s, e) ((s) >= ISA_START_ADDRESS && (e) < ISA_END_ADDRESS)

#define BIOS_BEGIN		0x000a0000
#define BIOS_END		0x00100000

#ifdef __KERNEL__
#include <linux/ioport.h>

#define HIGH_MEMORY	(1024*1024)
#endif /* __KERNEL__ */

#endif /* ASM_X86__E820_H */
