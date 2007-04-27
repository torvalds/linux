#ifndef _ASM_POWERPC_STRING_H
#define _ASM_POWERPC_STRING_H

#ifdef __KERNEL__

#define __HAVE_ARCH_STRCPY
#define __HAVE_ARCH_STRNCPY
#define __HAVE_ARCH_STRLEN
#define __HAVE_ARCH_STRCMP
#define __HAVE_ARCH_STRCAT
#define __HAVE_ARCH_MEMSET
#define __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_MEMMOVE
#define __HAVE_ARCH_MEMCMP
#define __HAVE_ARCH_MEMCHR

extern char * strcpy(char *,const char *);
extern char * strncpy(char *,const char *, __kernel_size_t);
extern __kernel_size_t strlen(const char *);
extern int strcmp(const char *,const char *);
extern char * strcat(char *, const char *);
extern void * memset(void *,int,__kernel_size_t);
extern void * memcpy(void *,const void *,__kernel_size_t);
extern void * memmove(void *,const void *,__kernel_size_t);
extern int memcmp(const void *,const void *,__kernel_size_t);
extern void * memchr(const void *,int,__kernel_size_t);

#endif /* __KERNEL__ */

#endif	/* _ASM_POWERPC_STRING_H */
