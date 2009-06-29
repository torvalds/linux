#ifndef _PERF_TYPES_H
#define _PERF_TYPES_H

/*
 * We define u64 as unsigned long long for every architecture
 * so that we can print it with %Lx without getting warnings.
 */
typedef unsigned long long u64;
typedef signed long long   s64;
typedef unsigned int	   u32;
typedef signed int	   s32;
typedef unsigned short	   u16;
typedef signed short	   s16;
typedef unsigned char	   u8;
typedef signed char	   s8;

#endif /* _PERF_TYPES_H */
