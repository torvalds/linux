/*
 * selection.h
 *
 * Interface between console.c, tty_io.c, vt.c, vc_screen.c and selection.c
 */

#ifndef _LINUX_SELECTION_H_
#define _LINUX_SELECTION_H_

#include <linux/tiocl.h>
#include <linux/vt_buffer.h>

struct tty_struct;

extern struct vc_data *sel_cons;

extern void clear_selection(void);
extern int set_selection(const struct tiocl_selection __user *sel, struct tty_struct *tty);
extern int paste_selection(struct tty_struct *tty);
extern int sel_loadlut(char __user *p);
extern int mouse_reporting(void);
extern void mouse_report(struct tty_struct * tty, int butt, int mrx, int mry);

extern int console_blanked;

extern unsigned char color_table[];
extern int default_red[];
extern int default_grn[];
extern int default_blu[];

extern unsigned short *screen_pos(struct vc_data *vc, int w_offset, int viewed);
extern u16 screen_glyph(struct vc_data *vc, int offset);
extern void complement_pos(struct vc_data *vc, int offset);
extern void invert_screen(struct vc_data *vc, int offset, int count, int shift);

extern void getconsxy(struct vc_data *vc, unsigned char *p);
extern void putconsxy(struct vc_data *vc, unsigned char *p);

extern u16 vcs_scr_readw(struct vc_data *vc, const u16 *org);
extern void vcs_scr_writew(struct vc_data *vc, u16 val, u16 *org);

#endif
