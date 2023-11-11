#ifndef __ELFNOTE_LTO_H
#define __ELFNOTE_LTO_H

#include <linux/elfnote.h>

#define LINUX_ELFNOTE_LTO_INFO	0x101

#ifdef CONFIG_LTO
#define BUILD_LTO_INFO	ELFNOTE32("Linux", LINUX_ELFNOTE_LTO_INFO, 1)
#else
#define BUILD_LTO_INFO	ELFNOTE32("Linux", LINUX_ELFNOTE_LTO_INFO, 0)
#endif

#endif /* __ELFNOTE_LTO_H */
