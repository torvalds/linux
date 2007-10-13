#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "siginfo_32.h"
# else
#  include "siginfo_64.h"
# endif
#else
# ifdef __i386__
#  include "siginfo_32.h"
# else
#  include "siginfo_64.h"
# endif
#endif
