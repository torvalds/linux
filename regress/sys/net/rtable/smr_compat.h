
#ifndef _SMR_COMPAT_H_
#define _SMR_COMPAT_H_

#include <sys/smr.h>

void smr_read_enter(void);
void smr_read_leave(void);

void SMR_ASSERT_CRITICAL(void);

#define smr_barrier() do { } while (0)

#endif /* _SMR_COMPAT_H_ */
