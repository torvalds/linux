#ifndef _MACH_IPI_H
#define _MACH_IPI_H 1

#include <asm/genapic.h>

#define send_IPI_mask (genapic->send_IPI_mask)
#define send_IPI_allbutself (genapic->send_IPI_allbutself)
#define send_IPI_all (genapic->send_IPI_all)

#endif
