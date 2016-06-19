
#define DEFINE_PER_CPU(type, val) type val

#define __get_cpu_var(var)	var
#define this_cpu_ptr(var)	var
#define per_cpu_ptr(ptr, cpu)   ({ (void)(cpu); (ptr); })
#define per_cpu(var, cpu)	(*per_cpu_ptr(&(var), cpu))
