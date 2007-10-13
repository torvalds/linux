#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "user_32.h"
# else
#  include "user_64.h"
# endif
#else
# ifdef __i386__
#  include "user_32.h"
# else
#  include "user_64.h"
# endif
#endif
