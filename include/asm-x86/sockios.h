#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "sockios_32.h"
# else
#  include "sockios_64.h"
# endif
#else
# ifdef __i386__
#  include "sockios_32.h"
# else
#  include "sockios_64.h"
# endif
#endif
