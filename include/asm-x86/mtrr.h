#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "mtrr_32.h"
# else
#  include "mtrr_64.h"
# endif
#else
# ifdef __i386__
#  include "mtrr_32.h"
# else
#  include "mtrr_64.h"
# endif
#endif
