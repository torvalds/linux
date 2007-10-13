#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "ldt_32.h"
# else
#  include "ldt_64.h"
# endif
#else
# ifdef __i386__
#  include "ldt_32.h"
# else
#  include "ldt_64.h"
# endif
#endif
