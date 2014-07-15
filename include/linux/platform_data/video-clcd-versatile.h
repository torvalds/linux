#ifndef PLAT_CLCD_H
#define PLAT_CLCD_H

#ifdef CONFIG_PLAT_VERSATILE_CLCD
struct clcd_panel *versatile_clcd_get_panel(const char *);
int versatile_clcd_setup_dma(struct clcd_fb *, unsigned long);
int versatile_clcd_mmap_dma(struct clcd_fb *, struct vm_area_struct *);
void versatile_clcd_remove_dma(struct clcd_fb *);
#else
static inline struct clcd_panel *versatile_clcd_get_panel(const char *s)
{
	return NULL;
}
static inline int versatile_clcd_setup_dma(struct clcd_fb *fb, unsigned long framesize)
{
	return -ENODEV;
}
static inline int versatile_clcd_mmap_dma(struct clcd_fb *fb, struct vm_area_struct *vm)
{
	return -ENODEV;
}
static inline void versatile_clcd_remove_dma(struct clcd_fb *fb)
{
}
#endif

#endif
