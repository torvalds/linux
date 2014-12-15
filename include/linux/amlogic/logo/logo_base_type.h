#ifndef  LOGO_BASE_TYPE_H
#define LOGO_BASE_TYPE_H


struct logo_output_dev;
struct logo_parser ;
typedef enum {
	DISP_MODE_ORIGIN,	//at top-left corner.
	DISP_MODE_CENTER,
	DISP_MODE_FULL_SCREEN
}logo_display_mode_t;

typedef enum{
	LOGO_DEV_OSD0=0,
	LOGO_DEV_OSD1,
	LOGO_DEV_VID,
	LOGO_DEV_MEM,
	LOGO_DEV_MAX
}platform_dev_t;

typedef enum  {
	SRC_TYPE_BMP=0,
	SRC_TYPE_JPG ,
	SRC_TYPE_PNG,
	MAX_PIC_TYPE,
}pic_type_t;

typedef  struct {
	int  x;
	int  y;
	int  w;
	int  h;
}logo_rect_t;
typedef  struct {
	char  name[10];
	int    mem_start;
	int 	mem_end;
}platform_resource_t;

struct logo_input_para{
	char *mem_addr;  //represent logo load addr .
	platform_dev_t  output_dev_type;
	logo_display_mode_t dis_mode; 
	vmode_t vout_mode;
	int	progress;
	int	loaded;
};
typedef struct logo_input_para logo_input_para_t ;
typedef  struct {
	char name[10];
	struct logo_input_para para;
	struct logo_output_dev *dev; 
	struct logo_parser  *parser;
	int	need_transfer;  //logo pic need transfer from parser output to
					   // logo output device.
	platform_resource_t platform_res[LOGO_DEV_MAX];				   
}logo_object_t;

#endif
