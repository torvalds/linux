/*	$OpenBSD: spkrio.h,v 1.1 1999/01/02 00:02:43 niklas Exp $	*/
/*	$NetBSD: spkrio.h,v 1.1 1998/04/15 20:26:19 drochner Exp $	*/

/*
 * spkr.h -- interface definitions for speaker ioctl()
 */

#ifndef _DEV_ISA_SPKR_H_
#define _DEV_ISA_SPKR_H_

#define SPKRTONE        _IOW('S', 1, tone_t)    /* emit tone */
#define SPKRTUNE        _IO('S', 2)             /* emit tone sequence */

typedef struct {
	int	frequency;	/* in hertz */
	int	duration;	/* in 1/100ths of a second */
} tone_t;

#endif /* _DEV_ISA_SPKR_H_ */
