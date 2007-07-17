#ifndef _ASM_FB_H_
#define _ASM_FB_H_

/* Caching is off in the I/O space quadrant by design.  */
#define fb_pgprotect(...) do {} while (0)

#endif /* _ASM_FB_H_ */
