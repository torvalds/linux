#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "mman_32.h"
# else
#  include "mman_64.h"
# endif
#else
# ifdef __i386__
#  include "mman_32.h"
# else
#  include "mman_64.h"
# endif
#endif
