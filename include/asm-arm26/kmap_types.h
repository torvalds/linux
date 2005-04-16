#ifndef __ARM_KMAP_TYPES_H
#define __ARM_KMAP_TYPES_H

/*
 * This is the "bare minimum".  AIO seems to require this.
 */
enum km_type {
        KM_IRQ0,
        KM_USER1
};

#endif
