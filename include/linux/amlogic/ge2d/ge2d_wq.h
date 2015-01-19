#ifndef BITBLT_H_
#define BITBLT_H_

#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/list.h>
#include <linux/amlogic/osd/osd.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/slab.h>
#define 	GE2D_HIGHEST_PRIORITY   0
#define 	GE2D_LOWEST_PRIORITY    255

#define 	FLAG_CONFIG_UPDATE_ONLY 0
#define 	FLAG_CONFIG_ALL         1

#define 	UPDATE_SRC_DATA     0x01
#define 	UPDATE_SRC_GEN      0x02
#define 	UPDATE_DST_DATA     0x04
#define 	UPDATE_DST_GEN      0x08
#define 	UPDATE_DP_GEN       0x10
#define 	UPDATE_SCALE_COEF   0x20
#define 	UPDATE_ALL          0x3f
#define	GE2D_ATTR_MAX	  2
#define   GE2D_MAX_WORK_QUEUE_NUM   4
#define   GE2D_IRQ_NO   	INT_GE2D
#define	FILE_NAME		"[GE2D_WQ]"
typedef  enum  	
{
		OSD0_OSD0 =0,
		OSD0_OSD1,	 
		OSD1_OSD1,
		OSD1_OSD0,
		ALLOC_OSD0,
		ALLOC_OSD1,
		ALLOC_ALLOC,
		TYPE_INVALID,
}ge2d_src_dst_t;
typedef enum  	
{
    CANVAS_OSD0 =0,
    CANVAS_OSD1,	 
    CANVAS_ALLOC,
    CANVAS_TYPE_INVALID,
}ge2d_src_canvas_type;
typedef  struct {
  struct list_head  list;
  ge2d_cmd_t   cmd ;
  ge2d_config_t config;

}ge2d_queue_item_t;

typedef struct{
	struct list_head   list;			//connect all process in one queue for RR process.
    	ge2d_config_t       config;   //current wq configuration
    	ge2d_cmd_t         	cmd;
	struct list_head	work_queue;
   	struct list_head	free_queue;
	wait_queue_head_t	cmd_complete;
   	int				queue_dirty;
   	int				queue_need_recycle;
	spinlock_t	 	lock; 	// for get and release item.
} ge2d_context_t;

typedef  struct {
	wait_queue_head_t	cmd_complete;
	struct completion		process_complete;
	spinlock_t	 	sem_lock;  //for queue switch and create destroy queue.
	struct semaphore		cmd_in_sem;
}ge2d_event_t;



typedef  struct {
   struct list_head			process_queue;
   ge2d_context_t*		current_wq;
   ge2d_context_t*		last_wq;
   struct task_struct*		ge2d_thread;
   ge2d_event_t			event ;
   int		 			irq_num;
   int 		 			ge2d_state;
   int					process_queue_state;
}ge2d_manager_t ;

typedef  struct  {
	int  xres;
	int  yres ;
	int  canvas_index;
	int  bpp;
	int  ge2d_color_index;
}src_dst_para_t ;

static const  int   bpp_type_lut[]={
	//16bit 
	COLOR_INDEX_16_655, 	// 0
	COLOR_INDEX_16_844, 	// 1
	COLOR_INDEX_16_6442, 	// 2
	COLOR_INDEX_16_4444_R,	// 3
	COLOR_INDEX_16_565,		// 4
	COLOR_INDEX_16_4444_A,	// 5
	COLOR_INDEX_16_1555_A,	// 6
	COLOR_INDEX_16_4642_R,	// 7
	//24bit
	COLOR_INDEX_24_RGB,	// 0
	COLOR_INDEX_24_5658,	// 1
	COLOR_INDEX_24_8565,	// 2
	COLOR_INDEX_24_6666_R,	// 3
	COLOR_INDEX_24_6666_A,	// 4
	COLOR_INDEX_24_888_B,	// 5
	0,
	0,
	//32bit
	COLOR_INDEX_32_RGBA,	// 0
	COLOR_INDEX_32_ARGB,	// 1
	COLOR_INDEX_32_ABGR, 	// 2
	COLOR_INDEX_32_BGRA,	// 3
	0,0,0,0
};

static const  int   default_ge2d_color_lut[]={
	0, 
	0,
	0,//BPP_TYPE_02_PAL4    = 2, 
	0,
	0,//BPP_TYPE_04_PAL16   = 4,
    	0,
    	0, 
    	0,
	0,//BPP_TYPE_08_PAL256=8,
	GE2D_FORMAT_S16_RGB_655,//BPP_TYPE_16_655 =9,
	GE2D_FORMAT_S16_RGB_844,//BPP_TYPE_16_844 =10,
	GE2D_FORMAT_S16_RGBA_6442,//BPP_TYPE_16_6442 =11 ,
	GE2D_FORMAT_S16_RGBA_4444,//BPP_TYPE_16_4444_R = 12,
	GE2D_FORMAT_S16_RGBA_4642,//BPP_TYPE_16_4642_R = 13,
	GE2D_FORMAT_S16_ARGB_1555,//BPP_TYPE_16_1555_A=14,
	GE2D_FORMAT_S16_ARGB_4444,//BPP_TYPE_16_4444_A = 15,
	GE2D_FORMAT_S16_RGB_565,//BPP_TYPE_16_565 =16,
	0,
	0,
	GE2D_FORMAT_S24_ARGB_6666,//BPP_TYPE_24_6666_A=19,
	GE2D_FORMAT_S24_RGBA_6666,//BPP_TYPE_24_6666_R=20,
	GE2D_FORMAT_S24_ARGB_8565,//BPP_TYPE_24_8565 =21,
	GE2D_FORMAT_S24_RGBA_5658,//BPP_TYPE_24_5658 = 22,
	GE2D_FORMAT_S24_BGR,//BPP_TYPE_24_888_B = 23,
	GE2D_FORMAT_S24_RGB,//BPP_TYPE_24_RGB = 24,
	0,
	0,
	0,
	0,
	GE2D_FORMAT_S32_BGRA,//BPP_TYPE_32_BGRA=29,
	GE2D_FORMAT_S32_ABGR,//BPP_TYPE_32_ABGR = 30,
	GE2D_FORMAT_S32_RGBA,//BPP_TYPE_32_RGBA=31,
	GE2D_FORMAT_S32_ARGB,//BPP_TYPE_32_ARGB=32,
};
typedef   enum{
	GE2D_OP_DEFAULT=0,
	GE2D_OP_FILLRECT,
	GE2D_OP_BLIT,
	GE2D_OP_STRETCHBLIT,
	GE2D_OP_BLEND,
	GE2D_OP_MAXNUM
}ge2d_op_type_t;

typedef struct {
	unsigned long  addr;
	unsigned int	w;
	unsigned int	h;
}config_planes_t;

typedef  struct{
	int	key_enable;
	int	key_color;
	int 	key_mask;
	int   key_mode;
}src_key_ctrl_t;
typedef    struct {
	int  src_dst_type;
	int  alu_const_color;
	unsigned int src_format;
	unsigned int dst_format ; //add for src&dst all in user space.

	config_planes_t src_planes[4];
	config_planes_t dst_planes[4];
	src_key_ctrl_t  src_key;
}config_para_t;

typedef  struct  {
    int  canvas_index;
    int  top;
    int  left;
    int  width;
    int  height;
    int  format;
    int  mem_type;
    int  color;
    unsigned char x_rev;
    unsigned char y_rev;
    unsigned char fill_color_en;
    unsigned char fill_mode;    
}src_dst_para_ex_t ;

typedef    struct {
    src_dst_para_ex_t src_para;
    src_dst_para_ex_t src2_para;
    src_dst_para_ex_t dst_para;

//key mask
    src_key_ctrl_t  src_key;
    src_key_ctrl_t  src2_key;

    int alu_const_color;
    unsigned src1_gb_alpha;
    unsigned op_mode;
    unsigned char bitmask_en;
    unsigned char bytemask_only;
    unsigned int  bitmask;
    unsigned char dst_xy_swap;

// scaler and phase releated
    unsigned hf_init_phase;
    int hf_rpt_num;
    unsigned hsc_start_phase_step;
    int hsc_phase_slope;
    unsigned vf_init_phase;
    int vf_rpt_num;
    unsigned vsc_start_phase_step;
    int vsc_phase_slope;
    unsigned char src1_vsc_phase0_always_en;
    unsigned char src1_hsc_phase0_always_en;
    unsigned char src1_hsc_rpt_ctrl;  //1bit, 0: using minus, 1: using repeat data
    unsigned char src1_vsc_rpt_ctrl;  //1bit, 0: using minus  1: using repeat data

//canvas info
    config_planes_t src_planes[4];
    config_planes_t src2_planes[4];
    config_planes_t dst_planes[4];
}config_para_ex_t;
extern int   ge2d_setup(void);
extern int   ge2d_deinit(void);
extern int   ge2d_context_config(ge2d_context_t *context, config_para_t *ge2d_config);
extern int   ge2d_context_config_ex(ge2d_context_t *context, config_para_ex_t *ge2d_config);
	
extern int ge2d_wq_init(void);
extern int  destroy_ge2d_work_queue(ge2d_context_t* ) ;
extern ge2d_context_t* create_ge2d_work_queue(void) ;

extern int ge2d_wq_remove_config(ge2d_context_t *wq);

extern void ge2d_wq_set_scale_coef(ge2d_context_t *wq, unsigned v_scale_coef, unsigned h_scale_coef);
extern int	ge2d_antiflicker_enable(ge2d_context_t *context,unsigned long enable);
extern ge2d_src1_data_t * ge2d_wq_get_src_data(ge2d_context_t *wq);
extern ge2d_src1_gen_t * ge2d_wq_get_src_gen(ge2d_context_t *wq);
extern ge2d_src2_dst_data_t * ge2d_wq_get_dst_data(ge2d_context_t *wq);
extern ge2d_src2_dst_gen_t * ge2d_wq_get_dst_gen(ge2d_context_t *wq);
extern ge2d_dp_gen_t * ge2d_wq_get_dp_gen(ge2d_context_t *wq);
extern ge2d_cmd_t * ge2d_wq_get_cmd(ge2d_context_t *wq);

extern int ge2d_wq_add_work(ge2d_context_t *wq);



#endif // BITBLT_H
