#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "debugreg_32.h"
# else
#  include "debugreg_64.h"
# endif
#else
# ifdef __i386__
#  include "debugreg_32.h"
# else
#  include "debugreg_64.h"
# endif
#endif
