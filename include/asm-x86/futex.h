#ifdef CONFIG_X86_32
# include "futex_32.h"
#else
# include "futex_64.h"
#endif
