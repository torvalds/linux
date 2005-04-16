/*
 * Definitions for using the procedures in btext.c.
 *
 * Benjamin Herrenschmidt <benh@kernel.crashing.org>
 */
#ifndef __PPC_BTEXT_H
#define __PPC_BTEXT_H
#ifdef __KERNEL__

#include <asm/bootx.h>

extern void btext_clearscreen(void);
extern void btext_flushscreen(void);

extern unsigned long disp_BAT[2];

extern boot_infos_t disp_bi;
extern int boot_text_mapped;

void btext_init(boot_infos_t *bi);
void btext_welcome(void);
void btext_prepare_BAT(void);
void btext_setup_display(int width, int height, int depth, int pitch,
			 unsigned long address);
void map_boot_text(void);
void btext_update_display(unsigned long phys, int width, int height,
			  int depth, int pitch);

void btext_drawchar(char c);
void btext_drawstring(const char *str);
void btext_drawhex(unsigned long v);

#endif /* __KERNEL__ */
#endif /* __PPC_BTEXT_H */
