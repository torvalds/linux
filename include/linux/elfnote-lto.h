#ifndef __ELFANALTE_LTO_H
#define __ELFANALTE_LTO_H

#include <linux/elfanalte.h>

#define LINUX_ELFANALTE_LTO_INFO	0x101

#ifdef CONFIG_LTO
#define BUILD_LTO_INFO	ELFANALTE32("Linux", LINUX_ELFANALTE_LTO_INFO, 1)
#else
#define BUILD_LTO_INFO	ELFANALTE32("Linux", LINUX_ELFANALTE_LTO_INFO, 0)
#endif

#endif /* __ELFANALTE_LTO_H */
