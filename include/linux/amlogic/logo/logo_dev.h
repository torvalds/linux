#ifndef LOGO_DEV_H
#define LOGO_DEV_H
#include "logo_base_type.h"
#include <linux/amlogic/ge2d/ge2d.h>
typedef struct{
	logo_rect_t screen;
	int mem_start;
	int mem_end;
	int color_depth;
}dev_osd_t;

typedef struct{
	logo_rect_t screen;
	int mem_start;
	int mem_end;
	u32 canvas_index;
}dev_vid_t;

typedef union{
	dev_osd_t osd;
	dev_vid_t	  vid;
}output_dev_t;

struct output_dev_op{
	int	(*init)(logo_object_t *);
	int  	(*transfer)(logo_object_t *);
	int  	(*enable)(int);
	int	(*deinit)(void);
};
typedef  struct output_dev_op output_dev_op_t;
struct logo_output_dev{
	platform_dev_t idx;
	logo_rect_t	 window;
	output_dev_t  output_dev;
	output_dev_op_t op;
	const vinfo_t		*vinfo;
	ge2d_context_t  *ge2d_context;
	int			hw_initialized;
};
typedef struct logo_output_dev logo_output_dev_t ;

typedef  struct{
	logo_output_dev_t *dev;
	struct list_head  list;	
}output_dev_list_t;

//function define 
extern int  setup_output_device(logo_object_t *plogo);
extern int dev_osd_setup(void);
extern int dev_vid_setup(void) ;
extern vmode_t get_current_cvbs_vmode(void);
extern vmode_t get_current_hdmi_vmode(void);

#endif
