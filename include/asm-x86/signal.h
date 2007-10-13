#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "signal_32.h"
# else
#  include "signal_64.h"
# endif
#else
# ifdef __i386__
#  include "signal_32.h"
# else
#  include "signal_64.h"
# endif
#endif
