#ifndef _ASM_GENERIC_KMAP_TYPES_H
#define _ASM_GENERIC_KMAP_TYPES_H

#ifdef CONFIG_DEBUG_HIGHMEM
# define D(n) __KM_FENCE_##n ,
#else
# define D(n)
#endif

enum km_type {
D(0)	KM_BOUNCE_READ,
D(1)	KM_SKB_SUNRPC_DATA,
D(2)	KM_SKB_DATA_SOFTIRQ,
D(3)	KM_USER0,
D(4)	KM_USER1,
D(5)	KM_BIO_SRC_IRQ,
D(6)	KM_BIO_DST_IRQ,
D(7)	KM_PTE0,
D(8)	KM_PTE1,
D(9)	KM_IRQ0,
D(10)	KM_IRQ1,
D(11)	KM_SOFTIRQ0,
D(12)	KM_SOFTIRQ1,
D(13)	KM_SYNC_ICACHE,
D(14)	KM_SYNC_DCACHE,
D(15)	KM_UML_USERCOPY, /* UML specific, for copy_*_user - used in do_op_one_page */
D(16)	KM_TYPE_NR
};

#undef D

#endif
