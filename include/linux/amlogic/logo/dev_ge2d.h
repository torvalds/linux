#ifndef  DEV_GE2D_H
#define DEV_GE2D_H
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/ge2d/ge2d.h> 
typedef  struct {
	rectangle_t  src_rect;
	rectangle_t  dst_rect;
	unsigned int		color;
}src_dst_info_t;

#define		CMD_FILLRECT			0x1
#define		CMD_BITBLT				0x2
#define		CMD_STRETCH_BLIT		0x3

extern ge2d_context_t*	dev_ge2d_setup(void* para);
extern int	dev_ge2d_cmd(ge2d_context_t *context ,int  cmd,src_dst_info_t  *info);
#endif
