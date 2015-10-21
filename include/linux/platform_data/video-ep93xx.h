#ifndef __VIDEO_EP93XX_H
#define __VIDEO_EP93XX_H

struct platform_device;
struct fb_info;

/* VideoAttributes flags */
#define EP93XXFB_STATE_MACHINE_ENABLE	(1 << 0)
#define EP93XXFB_PIXEL_CLOCK_ENABLE	(1 << 1)
#define EP93XXFB_VSYNC_ENABLE		(1 << 2)
#define EP93XXFB_PIXEL_DATA_ENABLE	(1 << 3)
#define EP93XXFB_COMPOSITE_SYNC		(1 << 4)
#define EP93XXFB_SYNC_VERT_HIGH		(1 << 5)
#define EP93XXFB_SYNC_HORIZ_HIGH	(1 << 6)
#define EP93XXFB_SYNC_BLANK_HIGH	(1 << 7)
#define EP93XXFB_PCLK_FALLING		(1 << 8)
#define EP93XXFB_ENABLE_AC		(1 << 9)
#define EP93XXFB_ENABLE_LCD		(1 << 10)
#define EP93XXFB_ENABLE_CCIR		(1 << 12)
#define EP93XXFB_USE_PARALLEL_INTERFACE	(1 << 13)
#define EP93XXFB_ENABLE_INTERRUPT	(1 << 14)
#define EP93XXFB_USB_INTERLACE		(1 << 16)
#define EP93XXFB_USE_EQUALIZATION	(1 << 17)
#define EP93XXFB_USE_DOUBLE_HORZ	(1 << 18)
#define EP93XXFB_USE_DOUBLE_VERT	(1 << 19)
#define EP93XXFB_USE_BLANK_PIXEL	(1 << 20)
#define EP93XXFB_USE_SDCSN0		(0 << 21)
#define EP93XXFB_USE_SDCSN1		(1 << 21)
#define EP93XXFB_USE_SDCSN2		(2 << 21)
#define EP93XXFB_USE_SDCSN3		(3 << 21)

#define EP93XXFB_ENABLE			(EP93XXFB_STATE_MACHINE_ENABLE	| \
					 EP93XXFB_PIXEL_CLOCK_ENABLE	| \
					 EP93XXFB_VSYNC_ENABLE		| \
					 EP93XXFB_PIXEL_DATA_ENABLE)

struct ep93xxfb_mach_info {
	unsigned int			flags;
	int	(*setup)(struct platform_device *pdev);
	void	(*teardown)(struct platform_device *pdev);
	void	(*blank)(int blank_mode, struct fb_info *info);
};

#endif /* __VIDEO_EP93XX_H */
