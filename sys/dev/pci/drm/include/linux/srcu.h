/* Public domain. */

#ifndef _LINUX_SRCU_H
#define _LINUX_SRCU_H

#define init_srcu_struct(x)
#define cleanup_srcu_struct(x)

#define srcu_read_lock(x)			0
#define srcu_read_unlock(x, y)

#define synchronize_srcu_expedited(x)

#endif
