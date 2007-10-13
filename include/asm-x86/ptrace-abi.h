#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "ptrace-abi_32.h"
# else
#  include "ptrace-abi_64.h"
# endif
#else
# ifdef __i386__
#  include "ptrace-abi_32.h"
# else
#  include "ptrace-abi_64.h"
# endif
#endif
