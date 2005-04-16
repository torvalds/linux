#ifndef __ARM_MMU_H
#define __ARM_MMU_H

typedef struct {
#if __LINUX_ARM_ARCH__ >= 6
	unsigned int id;
#endif
} mm_context_t;

#if __LINUX_ARM_ARCH__ >= 6
#define ASID(mm)	((mm)->context.id & 255)
#else
#define ASID(mm)	(0)
#endif

#endif
