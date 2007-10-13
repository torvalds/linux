#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "ioctls_32.h"
# else
#  include "ioctls_64.h"
# endif
#else
# ifdef __i386__
#  include "ioctls_32.h"
# else
#  include "ioctls_64.h"
# endif
#endif
