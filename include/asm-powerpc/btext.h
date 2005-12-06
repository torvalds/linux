/*
 * Definitions for using the procedures in btext.c.
 *
 * Benjamin Herrenschmidt <benh@kernel.crashing.org>
 */
#ifndef __PPC_BTEXT_H
#define __PPC_BTEXT_H
#ifdef __KERNEL__

extern void btext_clearscreen(void);
extern void btext_flushscreen(void);

extern int boot_text_mapped;

extern int btext_initialize(struct device_node *np);

extern void map_boot_text(void);
extern void init_boot_display(void);
extern void btext_update_display(unsigned long phys, int width, int height,
				 int depth, int pitch);

extern void btext_drawchar(char c);
extern void btext_drawstring(const char *str);
extern void btext_drawhex(unsigned long v);

#endif /* __KERNEL__ */
#endif /* __PPC_BTEXT_H */
