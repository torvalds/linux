#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "sigcontext_32.h"
# else
#  include "sigcontext_64.h"
# endif
#else
# ifdef __i386__
#  include "sigcontext_32.h"
# else
#  include "sigcontext_64.h"
# endif
#endif
