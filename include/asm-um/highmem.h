#ifndef __UM_HIGHMEM_H
#define __UM_HIGHMEM_H

#include "asm/page.h"
#include "asm/fixmap.h"
#include "asm/arch/highmem.h"

#undef PKMAP_BASE

#define PKMAP_BASE ((FIXADDR_START - LAST_PKMAP * PAGE_SIZE) & PMD_MASK)

#endif
