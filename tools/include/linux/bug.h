#ifndef _TOOLS_PERF_LINUX_BUG_H
#define _TOOLS_PERF_LINUX_BUG_H

/* Force a compilation error if condition is true, but also produce a
   result (of value 0 and type size_t), so the expression can be used
   e.g. in a structure initializer (or where-ever else comma expressions
   aren't permitted). */
#define BUILD_BUG_ON_ZERO(e) (sizeof(struct { int:-!!(e); }))

#endif	/* _TOOLS_PERF_LINUX_BUG_H */
