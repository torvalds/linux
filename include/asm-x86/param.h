#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "param_32.h"
# else
#  include "param_64.h"
# endif
#else
# ifdef __i386__
#  include "param_32.h"
# else
#  include "param_64.h"
# endif
#endif
