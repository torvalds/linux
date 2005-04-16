#include <linux/config.h>

/* This handles the memory map.. */
#ifndef CONFIG_SUN3
#define PAGE_OFFSET_RAW		0x00000000
#else
#define PAGE_OFFSET_RAW		0x0E000000
#endif

