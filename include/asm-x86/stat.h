#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "stat_32.h"
# else
#  include "stat_64.h"
# endif
#else
# ifdef __i386__
#  include "stat_32.h"
# else
#  include "stat_64.h"
# endif
#endif
