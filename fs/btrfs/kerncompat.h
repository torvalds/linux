#ifndef __KERNCOMPAT
#define __KERNCOMPAT
#define gfp_t int
#define get_cpu_var(p) (p)
#define __get_cpu_var(p) (p)
#define BITS_PER_LONG 64
#define __GFP_BITS_SHIFT 20
#define __GFP_BITS_MASK ((int)((1 << __GFP_BITS_SHIFT) - 1))
#define __read_mostly
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define __force
#define PAGE_SHIFT 12
#define ULONG_MAX       (~0UL)
#define BUG() abort()

typedef unsigned int u32;
typedef unsigned long u64;
typedef unsigned char u8;
typedef unsigned short u16;

typedef unsigned long pgoff_t;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct vma_shared { int prio_tree_node; };
struct vm_area_struct {
	unsigned long vm_pgoff;
	unsigned long vm_start;
	unsigned long vm_end;
	struct vma_shared shared;
};

struct page {
	unsigned long index;
};

static inline void preempt_enable(void) { do {; } while(0);}
static inline void preempt_disable(void) { do {; } while(0);}

static inline void __set_bit(int bit, unsigned long *map) {
	unsigned long *p = map + bit / BITS_PER_LONG;
	bit = bit & (BITS_PER_LONG -1);
	*p |= 1UL << bit;
}

static inline int test_bit(int bit, unsigned long *map) {
	unsigned long *p = map + bit / BITS_PER_LONG;
	bit = bit & (BITS_PER_LONG -1);
	return *p & (1UL << bit) ? 1 : 0;
}

static inline void __clear_bit(int bit, unsigned long *map) {
	unsigned long *p = map + bit / BITS_PER_LONG;
	bit = bit & (BITS_PER_LONG -1);
	*p &= ~(1UL << bit);
}
#define BUG_ON(c) do { if (c) abort(); } while (0)

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
	        (type *)( (char *)__mptr - __builtin_offsetof(type,member) );})

#endif

#define ENOMEM 5
#define EEXIST 6
