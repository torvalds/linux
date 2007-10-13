#ifdef CONFIG_X86_32
# include "spinlock_32.h"
#else
# include "spinlock_64.h"
#endif
