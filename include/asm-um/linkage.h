#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#define FASTCALL(x)	x __attribute__((regparm(3)))
#define fastcall        __attribute__((regparm(3)))

#endif
