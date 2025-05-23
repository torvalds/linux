#ifndef _LINUX_SYSECTL_TYPES_H_
#define _LINUX_SYSECTL_TYPES_H_

#ifdef CONFIG_SYSECTL

// Initialize everything to 0 and all bits in the bitmap to 1
#define SYSECTL_DEFAULTS ((struct sysectl){{0}})

#define sysectl_bitmap(tsk) tsk->sysectl.filter.filter.bitmap->bits

// .section .secfilterbitmap
struct sysectl_filter_bitmap {
        unsigned char bits[64];                // 64 = One bit per syscall
};

// .section .secmap
struct sysectlmap {
	void *pc;
	unsigned int opcode;
	unsigned int next;

	union {
                struct sysectl_filter_bitmap *bitmap;
                unsigned short syscalls[4];
        } filter;
};

// Lookup table: PC to Map
struct sysectltable {
	// Page sized array, lookup table, PC to index
	// index is the *maps array index
	short *index;

	// Arrays of maps and bitmaps, contains the data
	struct sysectlmap *maps;
	struct sysectl_filter_bitmap *bitmaps;
};

// Declared in task_structure
struct sysectl {
	// TODO - Direct reference to filter itself?
	// 	There can be many kind of filters?  Copy type is not clear?
	// 	If we want many kind of filters, we may need an interface.
	struct sysectlmap filter;	// Currently running filter
	struct sysectlmap *restore;	// Set when the restore is expected
	struct sysectltable ltable;
};

#else
#define sysectl_entry(nbr) 1
#endif /* CONFIG_SYSECTL */
#endif /* _LINUX_SYSECTL_H_ */

