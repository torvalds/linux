#ifndef _ASM_DMI_H
#define _ASM_DMI_H 1

#include <asm/io.h>

/* Use early IO mappings for DMI because it's initialized early */
#define dmi_ioremap bt_ioremap
#define dmi_iounmap bt_iounmap
#define dmi_alloc alloc_bootmem

#endif
