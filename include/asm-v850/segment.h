#ifndef __V850_SEGMENT_H__
#define __V850_SEGMENT_H__


#ifndef __ASSEMBLY__

typedef unsigned long mm_segment_t;	/* domain register */

#endif /* !__ASSEMBLY__ */


#define __KERNEL_CS	0x0
#define __KERNEL_DS	0x0

#define __USER_CS	0x1
#define __USER_DS	0x1

#define KERNEL_DS	__KERNEL_DS
#define KERNEL_CS	__KERNEL_CS
#define USER_DS		__USER_DS
#define USER_CS		__USER_CS

#define segment_eq(a,b)	((a) == (b))

#define get_ds()	(KERNEL_DS)
#define get_fs()	(USER_DS)

#define set_fs(seg)	((void)(seg))


#define copy_segments(task, mm)	((void)((void)(task), (mm)))
#define release_segments(mm)	((void)(mm))
#define forget_segments()	((void)0)


#endif /* __V850_SEGMENT_H__ */
