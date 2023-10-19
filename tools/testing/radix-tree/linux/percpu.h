/* SPDX-License-Identifier: GPL-2.0 */
#define DECLARE_PER_CPU(type, val) extern type val
#define DEFINE_PER_CPU(type, val) type val

#define __get_cpu_var(var)	var
#define this_cpu_ptr(var)	var
#define this_cpu_read(var)	var
#define this_cpu_xchg(var, val)		uatomic_xchg(&var, val)
#define this_cpu_cmpxchg(var, old, new)	uatomic_cmpxchg(&var, old, new)
#define per_cpu_ptr(ptr, cpu)   ({ (void)(cpu); (ptr); })
#define per_cpu(var, cpu)	(*per_cpu_ptr(&(var), cpu))
