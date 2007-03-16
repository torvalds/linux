#define PXAKBD_MAXROW		8
#define PXAKBD_MAXCOL		8

struct pxa27x_keyboard_platform_data {
	int nr_rows, nr_cols;
	int keycodes[PXAKBD_MAXROW][PXAKBD_MAXCOL];
	int gpio_modes[PXAKBD_MAXROW + PXAKBD_MAXCOL];

#ifdef CONFIG_PM
	u32 reg_kpc;
	u32 reg_kprec;
#endif
};
