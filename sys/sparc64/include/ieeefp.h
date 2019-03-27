/*-
 * Written by J.T. Conklin, Apr 6, 1995
 * Public domain.
 * $FreeBSD$
 */

#ifndef _MACHINE_IEEEFP_H_
#define _MACHINE_IEEEFP_H_

/* Deprecated FPU control interface */

#include <machine/fsr.h>

typedef int fp_except_t;
#define FP_X_IMP	FSR_NX	/* imprecise (loss of precision) */
#define FP_X_DZ		FSR_DZ	/* divide-by-zero exception */
#define FP_X_UFL	FSR_UF	/* underflow exception */
#define FP_X_OFL	FSR_OF	/* overflow exception */
#define FP_X_INV	FSR_NV	/* invalid operation exception */

typedef enum {
	FP_RN = FSR_RD_N,	/* round to nearest representable number */
	FP_RZ = FSR_RD_Z,	/* round to zero (truncate) */
	FP_RP = FSR_RD_PINF,	/* round toward positive infinity */
	FP_RM = FSR_RD_NINF	/* round toward negative infinity */
} fp_rnd_t;

__BEGIN_DECLS
extern fp_rnd_t    fpgetround(void);
extern fp_rnd_t    fpsetround(fp_rnd_t);
extern fp_except_t fpgetmask(void);
extern fp_except_t fpsetmask(fp_except_t);
extern fp_except_t fpgetsticky(void);
__END_DECLS


#endif /* _MACHINE_IEEEFP_H_ */
