#ifndef _ASM_IRQ_VECTORS_LIMITS_H
#define _ASM_IRQ_VECTORS_LIMITS_H

/*
 * For Summit or generic (i.e. installer) kernels, we have lots of I/O APICs,
 * even with uni-proc kernels, so use a big array.
 *
 * This value should be the same in both the generic and summit subarches.
 * Change one, change 'em both.
 */
#define NR_IRQS	224
#define NR_IRQ_VECTORS	1024

#endif /* _ASM_IRQ_VECTORS_LIMITS_H */
