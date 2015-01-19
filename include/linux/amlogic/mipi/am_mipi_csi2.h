/*******************************************************************
 *
 *  Copyright C 2012 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 2012/3/13   19:46
 *
 *******************************************************************/

#ifndef __AM_MIPI_CSI2_H__
#define __AM_MIPI_CSI2_H__

#include <mach/io.h>
#include <linux/amlogic/tvin/tvin_v4l2.h>
#define CSI2_BUF_POOL_SIZE    6
#define CSI2_OUTPUT_BUF_POOL_SIZE   1

#define AM_CSI2_FLAG_NULL                   0x00000000
#define AM_CSI2_FLAG_INITED               0x00000001
#define AM_CSI2_FLAG_DEV_READY        0x00000002
#define AM_CSI2_FLAG_STARTED            0x00000004

enum am_csi2_mode {
	AM_CSI2_ALL_MEM,
	AM_CSI2_VDIN,
};

struct am_csi2_pixel_fmt {
    char  *name;
    u32   fourcc;          /* v4l2 format id */
    int    depth;
};

struct am_csi2_camera_para {
    const char* name;
    unsigned output_pixel;
    unsigned output_line;
    unsigned active_pixel;
    unsigned active_line;
    unsigned frame_rate;
    unsigned ui_val; //ns
    unsigned hs_freq; //hz
    unsigned char clock_lane_mode;
    unsigned char mirror;
    unsigned zoom;
    unsigned angle;
    struct am_csi2_pixel_fmt* in_fmt;
    struct am_csi2_pixel_fmt* out_fmt;
};

struct am_csi2_client_config {
    enum am_csi2_mode mode;
    unsigned char lanes;	    /* 0..3 */
    unsigned char channel;    /* bitmask[3:0] */
    int vdin_num;
    char name[32];
    void *pdev;	/* client platform device */
};

struct am_csi2_pdata {
    struct am_csi2_client_config *clients;
    int num_clients;
};

typedef struct am_csi2_s am_csi2_t;

typedef struct am_csi2_ops_s {
    enum am_csi2_mode mode;
    struct am_csi2_pixel_fmt* (*getPixelFormat)(u32 fourcc,bool input);
    int (*init) (am_csi2_t* dev);
    int (*streamon) (am_csi2_t* dev);
    int (*streamoff ) (am_csi2_t* dev);
    int (*fill) (am_csi2_t* dev);
    int (*uninit) (am_csi2_t* dev);
    void* privdata;
    int data_num;
} am_csi2_ops_t;

typedef struct am_csi2_frame_s {
    unsigned ddr_address;
    int index;
    unsigned status;
    unsigned w;
    unsigned h;
    int read_cnt;
    unsigned err;
}am_csi2_frame_t;

typedef struct am_csi2_output_s {
    void * vaddr;
    unsigned output_pixel;
    unsigned output_line;
    u32   fourcc;          /* v4l2 format id */
    int    depth;
    unsigned frame_size;
    unsigned char frame_available;
    unsigned zoom;
    unsigned angle;
    am_csi2_frame_t frame[CSI2_OUTPUT_BUF_POOL_SIZE];
}am_csi2_output_t;

typedef struct am_csi2_input_s {
    unsigned active_pixel;
    unsigned active_line;
    u32   fourcc;          /* v4l2 format id */
    int    depth;
    unsigned frame_size;
    unsigned char frame_available;
    am_csi2_frame_t frame[CSI2_BUF_POOL_SIZE];
}am_csi2_input_t;

typedef struct am_csi2_hw_s {
    unsigned char lanes;
    unsigned char channel;
    unsigned char mode;
    unsigned char clock_lane_mode; // 0 clock gate 1: always on
    am_csi2_frame_t* frame;
    unsigned active_pixel;
    unsigned active_line;
    unsigned frame_size;
    unsigned ui_val; //ns
    unsigned hs_freq; //hz
    unsigned urgent;
}am_csi2_hw_t;

struct am_csi2_s{
    char* name;

    enum am_csi2_mode mode;
    unsigned char lanes;	    /* 0..3 */
    unsigned char channel;    /* bitmask[3:0] */
    int vdin_num;

    int id;	
    struct platform_device *pdev;
    struct am_csi2_client_config *client;

    struct mutex lock;
#ifdef CONFIG_MEM_MIPI
    int irq;
#endif
    unsigned pbufAddr;
    unsigned decbuf_size;

    unsigned frame_rate;
    unsigned ui_val; //ns
    unsigned hs_freq; //hz
    unsigned char clock_lane_mode; // 0 clock gate 1: always on
    unsigned char mirror;

    unsigned status;

    am_csi2_input_t input;
    am_csi2_output_t output;
    struct am_csi2_ops_s *ops;
};

extern int start_mipi_csi2_service(struct am_csi2_camera_para *para);
extern int stop_mipi_csi2_service(struct am_csi2_camera_para *para);
extern void am_mipi_csi2_init(csi_parm_t* info);
extern void am_mipi_csi2_uninit(void);
extern void init_am_mipi_csi2_clock(void);
extern void cal_csi_para(csi_parm_t* info);

#define MIPI_DEBUG
#ifdef MIPI_DEBUG
#define mipi_dbg(fmt, args...) printk(fmt,## args)
#else
#define mipi_dbg(fmt, args...)
#endif
#define mipi_error(fmt, args...) printk(fmt,## args)

#define CSI_ADPT_START_REG      CSI2_CLK_RESET//MIPI CSI2 ADAPTOR Registers
#define CSI_ADPT_END_REG        CSI2_GEN_CTRL1//

#define CSI_PHY_START_REG      MIPI_PHY_CTRL           //MIPI CSI2 PHY Registers
#define CSI_PHY_END_REG        MIPI_PHY_DDR_STS        //

#define CSI_HST_START_REG       MIPI_CSI2_HOST_VERSION          //MIPI CSI2 HOST Registers
#define CSI_HST_END_REG         MIPI_CSI2_HOST_PHY_TST_CTRL1    //

#define CSI_ADPT_REG_BASE_ADDR			IO_VPU_BUS_BASE
#define CSI_PHY_REG_BASE_ADDR		        IO_MIPI_PHY_BASE
#define CSI_HST_REG_BASE_ADDR		        IO_MIPI_HOST_BASE

////MIPI CSI2 ADAPTOR Registers
#define CSI_ADPT_REG_OFFSET(reg)		(reg << 2)
#define CSI_ADPT_REG_ADDR(reg)			(CSI_ADPT_REG_BASE_ADDR + CSI_ADPT_REG_OFFSET(reg))

#define WRITE_CSI_ADPT_REG(reg, val) 				aml_write_reg32(CSI_ADPT_REG_ADDR(reg), (val))
#define READ_CSI_ADPT_REG(reg) 					aml_read_reg32(CSI_ADPT_REG_ADDR(reg))
#define WRITE_CSI_ADPT_REG_BITS(reg, val, start, len) 	        aml_set_reg32_bits(CSI_ADPT_REG_ADDR(reg), (val),start,len)
#define CLR_CSI_ADPT_REG_MASK(reg, mask)   		        aml_clr_reg32_mask(CSI_ADPT_REG_ADDR(reg), (mask))
#define SET_CSI_ADPT_REG_MASK(reg, mask)     			aml_set_reg32_mask(CSI_ADPT_REG_ADDR(reg), (mask))
////MIPI CSI2 PHY Registers
#define CSI_PHY_REG_OFFSET(reg)		        (reg << 2)
#define CSI_PHY_REG_ADDR(reg)			(CSI_PHY_REG_BASE_ADDR + CSI_PHY_REG_OFFSET(reg))

#define WRITE_CSI_PHY_REG(reg, val) 				aml_write_reg32(CSI_PHY_REG_ADDR(reg), (val))
#define READ_CSI_PHY_REG(reg) 					aml_read_reg32(CSI_PHY_REG_ADDR(reg))
#define WRITE_CSI_PHY_REG_BITS(reg, val, start, len) 	        aml_set_reg32_bits(CSI_PHY_REG_ADDR(reg), (val),start,len)
#define CLR_CSI_PHY_REG_MASK(reg, mask)   		        aml_clr_reg32_mask(CSI_PHY_REG_ADDR(reg), (mask))
#define SET_CSI_PHY_REG_MASK(reg, mask)     			aml_set_reg32_mask(CSI_PHY_REG_ADDR(reg), (mask))

////MIPI CSI2 HOST Registers
#define CSI_HST_REG_OFFSET(reg)		        (reg << 2)
#define CSI_HST_REG_ADDR(reg)			(CSI_HST_REG_BASE_ADDR + CSI_HST_REG_OFFSET(reg))

#define WRITE_CSI_HST_REG(reg, val) 				aml_write_reg32(CSI_HST_REG_ADDR(reg), (val))
#define READ_CSI_HST_REG(reg) 					aml_read_reg32(CSI_HST_REG_ADDR(reg))
#define WRITE_CSI_HST_REG_BITS(reg, val, start, len) 	        aml_set_reg32_bits(CSI_HST_REG_ADDR(reg), (val),start,len)
#define CLR_CSI_HST_REG_MASK(reg, mask)   		        aml_clr_reg32_mask(CSI_HST_REG_ADDR(reg), (mask))
#define SET_CSI_HST_REG_MASK(reg, mask)     			aml_set_reg32_mask(CSI_HST_REG_ADDR(reg), (mask))
//struct device;
//struct v4l2_device;

//#define CSI2_CLK_RESET                0x2a00
#define CSI2_CFG_CLK_ENABLE_DWC         3 
#define CSI2_CFG_CLK_AUTO_GATE_OFF      2
#define CSI2_CFG_CLK_ENABLE             1
#define CSI2_CFG_SW_RESET               0

//#define CSI2_GEN_CTRL0                0x2a01
#define CSI2_CFG_CLR_WRRSP              27
#define CSI2_CFG_DDR_EN                 26
#define CSI2_CFG_A_BRST_NUM             20  //25:20
#define CSI2_CFG_A_ID                   14  //19:14
#define CSI2_CFG_URGENT_EN              13
#define CSI2_CFG_DDR_ADDR_LPBK          12
#define CSI2_CFG_BUFFER_PIC_SIZE        11
#define CSI2_CFG_422TO444_MODE          10
#define CSI2_CFG_INV_FIELD              9
#define CSI2_CFG_INTERLACE_EN           8
#define CSI2_CFG_FORCE_LINE_COUNT       7
#define CSI2_CFG_FORCE_PIX_COUNT        6
#define CSI2_CFG_COLOR_EXPAND           5
#define CSI2_CFG_ALL_TO_MEM             4
#define CSI2_CFG_VIRTUAL_CHANNEL_EN     0  //3:0

//#define CSI2_FORCE_PIC_SIZE           0x2a02
#define CSI2_CFG_LINE_COUNT             16 //31:16
#define CSI2_CFG_PIX_COUNT              0 //15:0

//#define CSI2_INTERRUPT_CTRL_STAT              0x2a05
#define CSI2_CFG_VS_RISE_INTERRUPT_CLR          18
#define CSI2_CFG_VS_FAIL_INTERRUPT_CLR          17
#define CSI2_CFG_FIELD_DONE_INTERRUPT_CLR       16
#define CSI2_CFG_VS_RISE_INTERRUPT              2
#define CSI2_CFG_VS_FAIL_INTERRUPT              1
#define CSI2_CFG_FIELD_DONE_INTERRUPT           0

//#define MIPI_PHY_CTRL                         0x00
#define MIPI_PHY_CFG_CHPU_TO_ANALOG             5
#define MIPI_PHY_CFG_SHTDWN_CLK_LANE            4
#define MIPI_PHY_CFG_SHTDWN_DATA_LANE           0

#define MIPI_PHY_CFG_CLK_CHNLB_SHIFT            2
#endif //__AM_MIPI_CSI2_H__
