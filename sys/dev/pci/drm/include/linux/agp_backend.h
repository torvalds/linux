/* Public domain. */

#ifndef _LINUX_AGP_BACKEND_H
#define _LINUX_AGP_BACKEND_H

#include <sys/param.h> /* for sparc64 bus.h */
#include <machine/bus.h>

#if defined(__amd64__) || defined(__i386__)
#define AGP_USER_MEMORY			0
#define AGP_USER_CACHED_MEMORY		BUS_DMA_COHERENT
#endif

struct agp_bridge_data;

#endif
