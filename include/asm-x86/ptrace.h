#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "ptrace_32.h"
# else
#  include "ptrace_64.h"
# endif
#else
# ifdef __i386__
#  include "ptrace_32.h"
# else
#  include "ptrace_64.h"
# endif
#endif
