#ifndef _ASM_MAX_NUMNODES_H
#define _ASM_MAX_NUMNODES_H

#include <linux/config.h>

#ifdef CONFIG_X86_NUMAQ

/* Max 16 Nodes */
#define NODES_SHIFT	4

#elif defined(CONFIG_ACPI_SRAT)

/* Max 8 Nodes */
#define NODES_SHIFT	3

#endif /* CONFIG_X86_NUMAQ */

#endif /* _ASM_MAX_NUMNODES_H */
