#ifndef _LINUX_SVGA_H
#define _LINUX_SVGA_H

#ifdef __KERNEL__

#include <linux/pci.h>
#include <video/vga.h>

/* Terminator for register set */

#define VGA_REGSET_END_VAL	0xFF
#define VGA_REGSET_END		{VGA_REGSET_END_VAL, 0, 0}

struct vga_regset {
	u8 regnum;
	u8 lowbit;
	u8 highbit;
};

/* ------------------------------------------------------------------------- */

#define SVGA_FORMAT_END_VAL	0xFFFF
#define SVGA_FORMAT_END		{SVGA_FORMAT_END_VAL, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 0, 0, 0, 0, 0, 0}

struct svga_fb_format {
	/* var part */
	u32 bits_per_pixel;
	struct fb_bitfield red;
	struct fb_bitfield green;
	struct fb_bitfield blue;
	struct fb_bitfield transp;
	u32 nonstd;
	/* fix part */
	u32 type;
	u32 type_aux;
	u32 visual;
	u32 xpanstep;
	u32 xresstep;
};

struct svga_timing_regs {
	const struct vga_regset *h_total_regs;
	const struct vga_regset *h_display_regs;
	const struct vga_regset *h_blank_start_regs;
	const struct vga_regset *h_blank_end_regs;
	const struct vga_regset *h_sync_start_regs;
	const struct vga_regset *h_sync_end_regs;

	const struct vga_regset *v_total_regs;
	const struct vga_regset *v_display_regs;
	const struct vga_regset *v_blank_start_regs;
	const struct vga_regset *v_blank_end_regs;
	const struct vga_regset *v_sync_start_regs;
	const struct vga_regset *v_sync_end_regs;
};

struct svga_pll {
	u16 m_min;
	u16 m_max;
	u16 n_min;
	u16 n_max;
	u16 r_min;
	u16 r_max;  /* r_max < 32 */
	u32 f_vco_min;
	u32 f_vco_max;
	u32 f_base;
};


/* Write a value to the attribute register */

static inline void svga_wattr(u8 index, u8 data)
{
	inb(0x3DA);
	outb(index, 0x3C0);
	outb(data, 0x3C0);
}

/* Write a value to a sequence register with a mask */

static inline void svga_wseq_mask(u8 index, u8 data, u8 mask)
{
	vga_wseq(NULL, index, (data & mask) | (vga_rseq(NULL, index) & ~mask));
}

/* Write a value to a CRT register with a mask */

static inline void svga_wcrt_mask(u8 index, u8 data, u8 mask)
{
	vga_wcrt(NULL, index, (data & mask) | (vga_rcrt(NULL, index) & ~mask));
}

static inline int svga_primary_device(struct pci_dev *dev)
{
	u16 flags;
	pci_read_config_word(dev, PCI_COMMAND, &flags);
	return (flags & PCI_COMMAND_IO);
}


void svga_wcrt_multi(const struct vga_regset *regset, u32 value);
void svga_wseq_multi(const struct vga_regset *regset, u32 value);

void svga_set_default_gfx_regs(void);
void svga_set_default_atc_regs(void);
void svga_set_default_seq_regs(void);
void svga_set_default_crt_regs(void);
void svga_set_textmode_vga_regs(void);

void svga_settile(struct fb_info *info, struct fb_tilemap *map);
void svga_tilecopy(struct fb_info *info, struct fb_tilearea *area);
void svga_tilefill(struct fb_info *info, struct fb_tilerect *rect);
void svga_tileblit(struct fb_info *info, struct fb_tileblit *blit);
void svga_tilecursor(struct fb_info *info, struct fb_tilecursor *cursor);
int svga_get_tilemax(struct fb_info *info);
void svga_get_caps(struct fb_info *info, struct fb_blit_caps *caps,
		   struct fb_var_screeninfo *var);

int svga_compute_pll(const struct svga_pll *pll, u32 f_wanted, u16 *m, u16 *n, u16 *r, int node);
int svga_check_timings(const struct svga_timing_regs *tm, struct fb_var_screeninfo *var, int node);
void svga_set_timings(const struct svga_timing_regs *tm, struct fb_var_screeninfo *var, u32 hmul, u32 hdiv, u32 vmul, u32 vdiv, u32 hborder, int node);

int svga_match_format(const struct svga_fb_format *frm, struct fb_var_screeninfo *var, struct fb_fix_screeninfo *fix);

#endif /* __KERNEL__  */
#endif /* _LINUX_SVGA_H */

