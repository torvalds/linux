#ifndef __PPC64_VDSO_H__
#define __PPC64_VDSO_H__

#ifdef __KERNEL__

/* Default link addresses for the vDSOs */
#define VDSO32_LBASE	0x100000
#define VDSO64_LBASE	0x100000

/* Default map addresses */
#define VDSO32_MBASE	VDSO32_LBASE
#define VDSO64_MBASE	VDSO64_LBASE

#define VDSO_VERSION_STRING	LINUX_2.6.12

/* Define if 64 bits VDSO has procedure descriptors */
#undef VDS64_HAS_DESCRIPTORS

#ifndef __ASSEMBLY__

extern unsigned int vdso64_pages;
extern unsigned int vdso32_pages;

/* Offsets relative to thread->vdso_base */
extern unsigned long vdso64_rt_sigtramp;
extern unsigned long vdso32_sigtramp;
extern unsigned long vdso32_rt_sigtramp;

extern void vdso_init(void);

#else /* __ASSEMBLY__ */

#ifdef __VDSO64__
#ifdef VDS64_HAS_DESCRIPTORS
#define V_FUNCTION_BEGIN(name)		\
	.globl name;			\
        .section ".opd","a";		\
        .align 3;			\
	name:				\
	.quad .name,.TOC.@tocbase,0;	\
	.previous;			\
	.globl .name;			\
	.type .name,@function; 		\
	.name:				\

#define V_FUNCTION_END(name)		\
	.size .name,.-.name;

#define V_LOCAL_FUNC(name) (.name)

#else /* VDS64_HAS_DESCRIPTORS */

#define V_FUNCTION_BEGIN(name)		\
	.globl name;			\
	name:				\

#define V_FUNCTION_END(name)		\
	.size name,.-name;

#define V_LOCAL_FUNC(name) (name)

#endif /* VDS64_HAS_DESCRIPTORS */
#endif /* __VDSO64__ */

#ifdef __VDSO32__

#define V_FUNCTION_BEGIN(name)		\
	.globl name;			\
	.type name,@function; 		\
	name:				\

#define V_FUNCTION_END(name)		\
	.size name,.-name;

#define V_LOCAL_FUNC(name) (name)

#endif /* __VDSO32__ */

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* __PPC64_VDSO_H__ */
