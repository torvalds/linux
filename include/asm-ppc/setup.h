#ifdef __KERNEL__
#ifndef _PPC_SETUP_H
#define _PPC_SETUP_H

#define m68k_num_memory num_memory
#define m68k_memory memory

#include <asm-m68k/setup.h>
/* We have a bigger command line buffer. */
#undef COMMAND_LINE_SIZE
#define COMMAND_LINE_SIZE	512

#endif /* _PPC_SETUP_H */
#endif /* __KERNEL__ */
