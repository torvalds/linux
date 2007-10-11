#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "resource_32.h"
# else
#  include "resource_64.h"
# endif
#else
# ifdef __i386__
#  include "resource_32.h"
# else
#  include "resource_64.h"
# endif
#endif
