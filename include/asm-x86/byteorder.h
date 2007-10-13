#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "byteorder_32.h"
# else
#  include "byteorder_64.h"
# endif
#else
# ifdef __i386__
#  include "byteorder_32.h"
# else
#  include "byteorder_64.h"
# endif
#endif
