#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "shmparam_32.h"
# else
#  include "shmparam_64.h"
# endif
#else
# ifdef __i386__
#  include "shmparam_32.h"
# else
#  include "shmparam_64.h"
# endif
#endif
