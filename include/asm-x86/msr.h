#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "msr_32.h"
# else
#  include "msr_64.h"
# endif
#else
# ifdef __i386__
#  include "msr_32.h"
# else
#  include "msr_64.h"
# endif
#endif
