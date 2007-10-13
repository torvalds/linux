#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "types_32.h"
# else
#  include "types_64.h"
# endif
#else
# ifdef __i386__
#  include "types_32.h"
# else
#  include "types_64.h"
# endif
#endif
