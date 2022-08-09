/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_INSTRUCTION_POINTER_H
#define _LINUX_INSTRUCTION_POINTER_H

#define _RET_IP_		(unsigned long)__builtin_return_address(0)
#define _THIS_IP_  ({ __label__ __here; __here: (unsigned long)&&__here; })

#endif /* _LINUX_INSTRUCTION_POINTER_H */
