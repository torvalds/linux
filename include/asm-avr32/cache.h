#ifndef __ASM_AVR32_CACHE_H
#define __ASM_AVR32_CACHE_H

#define L1_CACHE_SHIFT 5
#define L1_CACHE_BYTES (1 << L1_CACHE_SHIFT)

#ifndef __ASSEMBLER__
struct cache_info {
	unsigned int ways;
	unsigned int sets;
	unsigned int linesz;
};
#endif /* __ASSEMBLER */

/* Cache operation constants */
#define ICACHE_FLUSH		0x00
#define ICACHE_INVALIDATE	0x01
#define ICACHE_LOCK		0x02
#define ICACHE_UNLOCK		0x03
#define ICACHE_PREFETCH		0x04

#define DCACHE_FLUSH		0x08
#define DCACHE_LOCK		0x09
#define DCACHE_UNLOCK		0x0a
#define DCACHE_INVALIDATE	0x0b
#define DCACHE_CLEAN		0x0c
#define DCACHE_CLEAN_INVAL	0x0d

#endif /* __ASM_AVR32_CACHE_H */
