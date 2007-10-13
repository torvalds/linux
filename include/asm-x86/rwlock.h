#ifdef CONFIG_X86_32
# include "rwlock_32.h"
#else
# include "rwlock_64.h"
#endif
