#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "statfs_32.h"
# else
#  include "statfs_64.h"
# endif
#else
# ifdef __i386__
#  include "statfs_32.h"
# else
#  include "statfs_64.h"
# endif
#endif
