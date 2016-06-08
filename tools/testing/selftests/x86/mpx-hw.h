#ifndef _MPX_HW_H
#define _MPX_HW_H

#include <assert.h>

/* Describe the MPX Hardware Layout in here */

#define NR_MPX_BOUNDS_REGISTERS 4

#ifdef __i386__

#define MPX_BOUNDS_TABLE_ENTRY_SIZE_BYTES	16 /* 4 * 32-bits */
#define MPX_BOUNDS_TABLE_SIZE_BYTES		(1ULL << 14) /* 16k */
#define MPX_BOUNDS_DIR_ENTRY_SIZE_BYTES		4
#define MPX_BOUNDS_DIR_SIZE_BYTES		(1ULL << 22) /* 4MB */

#define MPX_BOUNDS_TABLE_BOTTOM_BIT		2
#define MPX_BOUNDS_TABLE_TOP_BIT		11
#define MPX_BOUNDS_DIR_BOTTOM_BIT		12
#define MPX_BOUNDS_DIR_TOP_BIT			31

#else

/*
 * Linear Address of "pointer" (LAp)
 *   0 ->  2: ignored
 *   3 -> 19: index in to bounds table
 *  20 -> 47: index in to bounds directory
 *  48 -> 63: ignored
 */

#define MPX_BOUNDS_TABLE_ENTRY_SIZE_BYTES	32
#define MPX_BOUNDS_TABLE_SIZE_BYTES		(1ULL << 22) /* 4MB */
#define MPX_BOUNDS_DIR_ENTRY_SIZE_BYTES		8
#define MPX_BOUNDS_DIR_SIZE_BYTES		(1ULL << 31) /* 2GB */

#define MPX_BOUNDS_TABLE_BOTTOM_BIT		3
#define MPX_BOUNDS_TABLE_TOP_BIT		19
#define MPX_BOUNDS_DIR_BOTTOM_BIT		20
#define MPX_BOUNDS_DIR_TOP_BIT			47

#endif

#define MPX_BOUNDS_DIR_NR_ENTRIES	\
	(MPX_BOUNDS_DIR_SIZE_BYTES/MPX_BOUNDS_DIR_ENTRY_SIZE_BYTES)
#define MPX_BOUNDS_TABLE_NR_ENTRIES	\
	(MPX_BOUNDS_TABLE_SIZE_BYTES/MPX_BOUNDS_TABLE_ENTRY_SIZE_BYTES)

#define MPX_BOUNDS_TABLE_ENTRY_VALID_BIT	0x1

struct mpx_bd_entry {
	union {
		char x[MPX_BOUNDS_DIR_ENTRY_SIZE_BYTES];
		void *contents[1];
	};
} __attribute__((packed));

struct mpx_bt_entry {
	union {
		char x[MPX_BOUNDS_TABLE_ENTRY_SIZE_BYTES];
		unsigned long contents[1];
	};
} __attribute__((packed));

struct mpx_bounds_dir {
	struct mpx_bd_entry entries[MPX_BOUNDS_DIR_NR_ENTRIES];
} __attribute__((packed));

struct mpx_bounds_table {
	struct mpx_bt_entry entries[MPX_BOUNDS_TABLE_NR_ENTRIES];
} __attribute__((packed));

static inline unsigned long GET_BITS(unsigned long val, int bottombit, int topbit)
{
	int total_nr_bits = topbit - bottombit;
	unsigned long mask = (1UL << total_nr_bits)-1;
	return (val >> bottombit) & mask;
}

static inline unsigned long __vaddr_bounds_table_index(void *vaddr)
{
	return GET_BITS((unsigned long)vaddr, MPX_BOUNDS_TABLE_BOTTOM_BIT,
					      MPX_BOUNDS_TABLE_TOP_BIT);
}

static inline unsigned long __vaddr_bounds_directory_index(void *vaddr)
{
	return GET_BITS((unsigned long)vaddr, MPX_BOUNDS_DIR_BOTTOM_BIT,
					      MPX_BOUNDS_DIR_TOP_BIT);
}

static inline struct mpx_bd_entry *mpx_vaddr_to_bd_entry(void *vaddr,
		struct mpx_bounds_dir *bounds_dir)
{
	unsigned long index = __vaddr_bounds_directory_index(vaddr);
	return &bounds_dir->entries[index];
}

static inline int bd_entry_valid(struct mpx_bd_entry *bounds_dir_entry)
{
	unsigned long __bd_entry = (unsigned long)bounds_dir_entry->contents;
	return (__bd_entry & MPX_BOUNDS_TABLE_ENTRY_VALID_BIT);
}

static inline struct mpx_bounds_table *
__bd_entry_to_bounds_table(struct mpx_bd_entry *bounds_dir_entry)
{
	unsigned long __bd_entry = (unsigned long)bounds_dir_entry->contents;
	assert(__bd_entry & MPX_BOUNDS_TABLE_ENTRY_VALID_BIT);
	__bd_entry &= ~MPX_BOUNDS_TABLE_ENTRY_VALID_BIT;
	return (struct mpx_bounds_table *)__bd_entry;
}

static inline struct mpx_bt_entry *
mpx_vaddr_to_bt_entry(void *vaddr, struct mpx_bounds_dir *bounds_dir)
{
	struct mpx_bd_entry *bde = mpx_vaddr_to_bd_entry(vaddr, bounds_dir);
	struct mpx_bounds_table *bt = __bd_entry_to_bounds_table(bde);
	unsigned long index = __vaddr_bounds_table_index(vaddr);
	return &bt->entries[index];
}

#endif /* _MPX_HW_H */
