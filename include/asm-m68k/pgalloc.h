
#ifndef M68K_PGALLOC_H
#define M68K_PGALLOC_H

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <asm/setup.h>
#include <asm/virtconvert.h>



#ifdef CONFIG_SUN3
#include <asm/sun3_pgalloc.h>
#else
#include <asm/motorola_pgalloc.h>
#endif

#endif /* M68K_PGALLOC_H */
