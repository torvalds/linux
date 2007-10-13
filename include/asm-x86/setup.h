#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "setup_32.h"
# else
#  include "setup_64.h"
# endif
#else
# ifdef __i386__
#  include "setup_32.h"
# else
#  include "setup_64.h"
# endif
#endif
