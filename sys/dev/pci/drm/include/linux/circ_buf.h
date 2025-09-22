/* Public domain. */

#ifndef _LINUX_CIRC_BUF_H
#define _LINUX_CIRC_BUF_H

#define CIRC_SPACE(h,t,s)	(((t) - ((h)+1)) & ((s)-1))

#endif
