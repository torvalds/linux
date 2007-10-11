#ifdef CONFIG_X86_32
# include "tlbflush_32.h"
#else
# include "tlbflush_64.h"
#endif
