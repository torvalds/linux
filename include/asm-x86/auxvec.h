#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "auxvec_32.h"
# else
#  include "auxvec_64.h"
# endif
#else
# ifdef __i386__
#  include "auxvec_32.h"
# else
#  include "auxvec_64.h"
# endif
#endif
