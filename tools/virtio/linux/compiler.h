/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_COMPILER_H
#define LINUX_COMPILER_H

#define WRITE_ONCE(var, val) \
	(*((volatile typeof(val) *)(&(var))) = (val))

#define READ_ONCE(var) (*((volatile typeof(var) *)(&(var))))

#endif
