#ifdef __KERNEL__
# if defined(CONFIG_X86_32) || defined(__i386__)
#  include "posix_types_32.h"
# else
#  include "posix_types_64.h"
# endif
#endif
