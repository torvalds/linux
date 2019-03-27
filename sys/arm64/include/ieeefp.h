/*-
 * Based on sys/sparc64/include/ieeefp.h
 * Public domain.
 * $FreeBSD$
 */

#ifndef _MACHINE_IEEEFP_H_
#define	_MACHINE_IEEEFP_H_

/* Deprecated FPU control interface */

/* FP exception codes */
#define	FP_EXCEPT_INV	8
#define	FP_EXCEPT_DZ	9
#define	FP_EXCEPT_OFL	10
#define	FP_EXCEPT_UFL	11
#define	FP_EXCEPT_IMP	12
#define	FP_EXCEPT_DNML	15

typedef int fp_except_t;

#define	FP_X_INV	(1 << FP_EXCEPT_INV)	/* invalid operation exception */
#define	FP_X_DZ		(1 << FP_EXCEPT_DZ)	/* divide-by-zero exception */
#define	FP_X_OFL	(1 << FP_EXCEPT_OFL)	/* overflow exception */
#define	FP_X_UFL	(1 << FP_EXCEPT_UFL)	/* underflow exception */
#define	FP_X_IMP	(1 << FP_EXCEPT_IMP)	/* imprecise (loss of precision) */
#define	FP_X_DNML	(1 << FP_EXCEPT_DNML)	/* denormal exception */

typedef enum {
	FP_RN = (0 << 22),	/* round to nearest representable number */
	FP_RP = (1 << 22),	/* round toward positive infinity */
	FP_RM = (2 << 22),	/* round toward negative infinity */
	FP_RZ = (3 << 22)	/* round to zero (truncate) */
} fp_rnd_t;

__BEGIN_DECLS
extern fp_rnd_t    fpgetround(void);
extern fp_rnd_t    fpsetround(fp_rnd_t);
extern fp_except_t fpgetmask(void);
extern fp_except_t fpsetmask(fp_except_t);
__END_DECLS


#endif /* _MACHINE_IEEEFP_H_ */
