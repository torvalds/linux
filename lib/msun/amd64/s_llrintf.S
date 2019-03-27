#include <machine/asm.h>
__FBSDID("$FreeBSD$")
	
/* sizeof(long) == sizeof(long long) */
#define	fn	llrintf
#include "s_lrintf.S"
