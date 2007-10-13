#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "ucontext_32.h"
# else
#  include "ucontext_64.h"
# endif
#else
# ifdef __i386__
#  include "ucontext_32.h"
# else
#  include "ucontext_64.h"
# endif
#endif
