#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "errno_32.h"
# else
#  include "errno_64.h"
# endif
#else
# ifdef __i386__
#  include "errno_32.h"
# else
#  include "errno_64.h"
# endif
#endif
