#include <asm-m68k/setup.h>

/* We have a bigger command line buffer. */
#undef COMMAND_LINE_SIZE
#define COMMAND_LINE_SIZE	512
