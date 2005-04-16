/*  $Id: aty128.h,v 1.1 1999/10/12 11:00:40 geert Exp $
 *  linux/drivers/video/aty128.h
 *  Register definitions for ATI Rage128 boards
 *
 *  Anthony Tong <atong@uiuc.edu>, 1999
 *  Brad Douglas <brad@neruo.com>, 2000
 */

#ifndef REG_RAGE128_H
#define REG_RAGE128_H

#define CLOCK_CNTL_INDEX			0x0008
#define CLOCK_CNTL_DATA				0x000c
#define BIOS_0_SCRATCH				0x0010
#define BUS_CNTL				0x0030
#define BUS_CNTL1				0x0034
#define GEN_INT_CNTL				0x0040
#define CRTC_GEN_CNTL				0x0050
#define CRTC_EXT_CNTL				0x0054
#define DAC_CNTL				0x0058
#define I2C_CNTL_1				0x0094
#define PALETTE_INDEX				0x00b0
#define PALETTE_DATA				0x00b4
#define CONFIG_CNTL				0x00e0
#define GEN_RESET_CNTL				0x00f0
#define CONFIG_MEMSIZE				0x00f8
#define MEM_CNTL				0x0140
#define MEM_POWER_MISC				0x015c
#define AGP_BASE				0x0170
#define AGP_CNTL				0x0174
#define AGP_APER_OFFSET				0x0178
#define PCI_GART_PAGE				0x017c
#define PC_NGUI_MODE				0x0180
#define PC_NGUI_CTLSTAT				0x0184
#define MPP_TB_CONFIG				0x01C0
#define MPP_GP_CONFIG				0x01C8
#define VIPH_CONTROL				0x01D0
#define CRTC_H_TOTAL_DISP			0x0200
#define CRTC_H_SYNC_STRT_WID			0x0204
#define CRTC_V_TOTAL_DISP			0x0208
#define CRTC_V_SYNC_STRT_WID			0x020c
#define CRTC_VLINE_CRNT_VLINE			0x0210
#define CRTC_CRNT_FRAME				0x0214
#define CRTC_GUI_TRIG_VLINE			0x0218
#define CRTC_OFFSET				0x0224
#define CRTC_OFFSET_CNTL			0x0228
#define CRTC_PITCH				0x022c
#define OVR_CLR					0x0230
#define OVR_WID_LEFT_RIGHT			0x0234
#define OVR_WID_TOP_BOTTOM			0x0238
#define LVDS_GEN_CNTL				0x02d0
#define DDA_CONFIG				0x02e0
#define DDA_ON_OFF				0x02e4
#define VGA_DDA_CONFIG				0x02e8
#define VGA_DDA_ON_OFF				0x02ec
#define CRTC2_H_TOTAL_DISP			0x0300
#define CRTC2_H_SYNC_STRT_WID			0x0304
#define CRTC2_V_TOTAL_DISP			0x0308
#define CRTC2_V_SYNC_STRT_WID			0x030c
#define CRTC2_VLINE_CRNT_VLINE			0x0310
#define CRTC2_CRNT_FRAME			0x0314
#define CRTC2_GUI_TRIG_VLINE			0x0318
#define CRTC2_OFFSET				0x0324
#define CRTC2_OFFSET_CNTL			0x0328
#define CRTC2_PITCH				0x032c
#define DDA2_CONFIG				0x03e0
#define DDA2_ON_OFF				0x03e4
#define CRTC2_GEN_CNTL				0x03f8
#define CRTC2_STATUS				0x03fc
#define OV0_SCALE_CNTL				0x0420
#define SUBPIC_CNTL				0x0540
#define PM4_BUFFER_OFFSET			0x0700
#define PM4_BUFFER_CNTL				0x0704
#define PM4_BUFFER_WM_CNTL			0x0708
#define PM4_BUFFER_DL_RPTR_ADDR			0x070c
#define PM4_BUFFER_DL_RPTR			0x0710
#define PM4_BUFFER_DL_WPTR			0x0714
#define PM4_VC_FPU_SETUP			0x071c
#define PM4_FPU_CNTL				0x0720
#define PM4_VC_FORMAT				0x0724
#define PM4_VC_CNTL				0x0728
#define PM4_VC_I01				0x072c
#define PM4_VC_VLOFF				0x0730
#define PM4_VC_VLSIZE				0x0734
#define PM4_IW_INDOFF				0x0738
#define PM4_IW_INDSIZE				0x073c
#define PM4_FPU_FPX0				0x0740
#define PM4_FPU_FPY0				0x0744
#define PM4_FPU_FPX1				0x0748
#define PM4_FPU_FPY1				0x074c
#define PM4_FPU_FPX2				0x0750
#define PM4_FPU_FPY2				0x0754
#define PM4_FPU_FPY3				0x0758
#define PM4_FPU_FPY4				0x075c
#define PM4_FPU_FPY5				0x0760
#define PM4_FPU_FPY6				0x0764
#define PM4_FPU_FPR				0x0768
#define PM4_FPU_FPG				0x076c
#define PM4_FPU_FPB				0x0770
#define PM4_FPU_FPA				0x0774
#define PM4_FPU_INTXY0				0x0780
#define PM4_FPU_INTXY1				0x0784
#define PM4_FPU_INTXY2				0x0788
#define PM4_FPU_INTARGB				0x078c
#define PM4_FPU_FPTWICEAREA			0x0790
#define PM4_FPU_DMAJOR01			0x0794
#define PM4_FPU_DMAJOR12			0x0798
#define PM4_FPU_DMAJOR02			0x079c
#define PM4_FPU_STAT				0x07a0
#define PM4_STAT				0x07b8
#define PM4_TEST_CNTL				0x07d0
#define PM4_MICROCODE_ADDR			0x07d4
#define PM4_MICROCODE_RADDR			0x07d8
#define PM4_MICROCODE_DATAH			0x07dc
#define PM4_MICROCODE_DATAL			0x07e0
#define PM4_CMDFIFO_ADDR			0x07e4
#define PM4_CMDFIFO_DATAH			0x07e8
#define PM4_CMDFIFO_DATAL			0x07ec
#define PM4_BUFFER_ADDR				0x07f0
#define PM4_BUFFER_DATAH			0x07f4
#define PM4_BUFFER_DATAL			0x07f8
#define PM4_MICRO_CNTL				0x07fc
#define CAP0_TRIG_CNTL				0x0950
#define CAP1_TRIG_CNTL				0x09c0

/******************************************************************************
 *                  GUI Block Memory Mapped Registers                         *
 *                     These registers are FIFOed.                            *
 *****************************************************************************/
#define PM4_FIFO_DATA_EVEN			0x1000
#define PM4_FIFO_DATA_ODD			0x1004

#define DST_OFFSET				0x1404
#define DST_PITCH				0x1408
#define DST_WIDTH				0x140c
#define DST_HEIGHT				0x1410
#define SRC_X					0x1414
#define SRC_Y					0x1418
#define DST_X					0x141c
#define DST_Y					0x1420
#define SRC_PITCH_OFFSET			0x1428
#define DST_PITCH_OFFSET			0x142c
#define SRC_Y_X					0x1434
#define DST_Y_X					0x1438
#define DST_HEIGHT_WIDTH			0x143c
#define DP_GUI_MASTER_CNTL			0x146c
#define BRUSH_SCALE				0x1470
#define BRUSH_Y_X				0x1474
#define DP_BRUSH_BKGD_CLR			0x1478
#define DP_BRUSH_FRGD_CLR			0x147c
#define DST_WIDTH_X				0x1588
#define DST_HEIGHT_WIDTH_8			0x158c
#define SRC_X_Y					0x1590
#define DST_X_Y					0x1594
#define DST_WIDTH_HEIGHT			0x1598
#define DST_WIDTH_X_INCY			0x159c
#define DST_HEIGHT_Y				0x15a0
#define DST_X_SUB				0x15a4
#define DST_Y_SUB				0x15a8
#define SRC_OFFSET				0x15ac
#define SRC_PITCH				0x15b0
#define DST_HEIGHT_WIDTH_BW			0x15b4
#define CLR_CMP_CNTL				0x15c0
#define CLR_CMP_CLR_SRC				0x15c4
#define CLR_CMP_CLR_DST				0x15c8
#define CLR_CMP_MASK				0x15cc
#define DP_SRC_FRGD_CLR				0x15d8
#define DP_SRC_BKGD_CLR				0x15dc
#define DST_BRES_ERR				0x1628
#define DST_BRES_INC				0x162c
#define DST_BRES_DEC				0x1630
#define DST_BRES_LNTH				0x1634
#define DST_BRES_LNTH_SUB			0x1638
#define SC_LEFT					0x1640
#define SC_RIGHT				0x1644
#define SC_TOP					0x1648
#define SC_BOTTOM				0x164c
#define SRC_SC_RIGHT				0x1654
#define SRC_SC_BOTTOM				0x165c
#define GUI_DEBUG0				0x16a0
#define GUI_DEBUG1				0x16a4
#define GUI_TIMEOUT				0x16b0
#define GUI_TIMEOUT0				0x16b4
#define GUI_TIMEOUT1				0x16b8
#define GUI_PROBE				0x16bc
#define DP_CNTL					0x16c0
#define DP_DATATYPE				0x16c4
#define DP_MIX					0x16c8
#define DP_WRITE_MASK				0x16cc
#define DP_CNTL_XDIR_YDIR_YMAJOR		0x16d0
#define DEFAULT_OFFSET				0x16e0
#define DEFAULT_PITCH				0x16e4
#define DEFAULT_SC_BOTTOM_RIGHT			0x16e8
#define SC_TOP_LEFT				0x16ec
#define SC_BOTTOM_RIGHT				0x16f0
#define SRC_SC_BOTTOM_RIGHT			0x16f4
#define WAIT_UNTIL				0x1720
#define CACHE_CNTL				0x1724
#define GUI_STAT				0x1740
#define PC_GUI_MODE				0x1744
#define PC_GUI_CTLSTAT				0x1748
#define PC_DEBUG_MODE				0x1760
#define BRES_DST_ERR_DEC			0x1780
#define TRAIL_BRES_T12_ERR_DEC			0x1784
#define TRAIL_BRES_T12_INC			0x1788
#define DP_T12_CNTL				0x178c
#define DST_BRES_T1_LNTH			0x1790
#define DST_BRES_T2_LNTH			0x1794
#define SCALE_SRC_HEIGHT_WIDTH			0x1994
#define SCALE_OFFSET_0				0x1998
#define SCALE_PITCH				0x199c
#define SCALE_X_INC				0x19a0
#define SCALE_Y_INC				0x19a4
#define SCALE_HACC				0x19a8
#define SCALE_VACC				0x19ac
#define SCALE_DST_X_Y				0x19b0
#define SCALE_DST_HEIGHT_WIDTH			0x19b4
#define SCALE_3D_CNTL				0x1a00
#define SCALE_3D_DATATYPE			0x1a20
#define SETUP_CNTL				0x1bc4
#define SOLID_COLOR				0x1bc8
#define WINDOW_XY_OFFSET			0x1bcc
#define DRAW_LINE_POINT				0x1bd0
#define SETUP_CNTL_PM4				0x1bd4
#define DST_PITCH_OFFSET_C			0x1c80
#define DP_GUI_MASTER_CNTL_C			0x1c84
#define SC_TOP_LEFT_C				0x1c88
#define SC_BOTTOM_RIGHT_C			0x1c8c

#define CLR_CMP_MASK_3D				0x1A28
#define MISC_3D_STATE_CNTL_REG			0x1CA0
#define MC_SRC1_CNTL				0x19D8
#define TEX_CNTL				0x1800

/* CONSTANTS */
#define GUI_ACTIVE				0x80000000
#define ENGINE_IDLE				0x0

#define PLL_WR_EN				0x00000080

#define CLK_PIN_CNTL				0x0001
#define PPLL_CNTL				0x0002
#define PPLL_REF_DIV				0x0003
#define PPLL_DIV_0				0x0004
#define PPLL_DIV_1				0x0005
#define PPLL_DIV_2				0x0006
#define PPLL_DIV_3				0x0007
#define VCLK_ECP_CNTL				0x0008
#define HTOTAL_CNTL				0x0009
#define X_MPLL_REF_FB_DIV			0x000a
#define XPLL_CNTL				0x000b
#define XDLL_CNTL				0x000c
#define XCLK_CNTL				0x000d
#define MPLL_CNTL				0x000e
#define MCLK_CNTL				0x000f
#define AGP_PLL_CNTL				0x0010
#define FCP_CNTL				0x0012
#define PLL_TEST_CNTL				0x0013
#define P2PLL_CNTL				0x002a
#define P2PLL_REF_DIV				0x002b
#define P2PLL_DIV_0				0x002b
#define POWER_MANAGEMENT			0x002f

#define PPLL_RESET				0x01
#define PPLL_ATOMIC_UPDATE_EN			0x10000
#define PPLL_VGA_ATOMIC_UPDATE_EN		0x20000
#define PPLL_REF_DIV_MASK			0x3FF
#define PPLL_FB3_DIV_MASK			0x7FF
#define PPLL_POST3_DIV_MASK			0x70000
#define PPLL_ATOMIC_UPDATE_R			0x8000
#define PPLL_ATOMIC_UPDATE_W			0x8000
#define MEM_CFG_TYPE_MASK			0x3
#define XCLK_SRC_SEL_MASK			0x7
#define XPLL_FB_DIV_MASK			0xFF00
#define X_MPLL_REF_DIV_MASK			0xFF

/* CRTC control values (CRTC_GEN_CNTL) */
#define CRTC_CSYNC_EN				0x00000010

#define CRTC2_DBL_SCAN_EN			0x00000001
#define CRTC2_DISPLAY_DIS			0x00800000
#define CRTC2_FIFO_EXTSENSE			0x00200000
#define CRTC2_ICON_EN				0x00100000
#define CRTC2_CUR_EN				0x00010000
#define CRTC2_EN				0x02000000
#define CRTC2_DISP_REQ_EN_B			0x04000000

#define CRTC_PIX_WIDTH_MASK			0x00000700
#define CRTC_PIX_WIDTH_4BPP			0x00000100
#define CRTC_PIX_WIDTH_8BPP			0x00000200
#define CRTC_PIX_WIDTH_15BPP			0x00000300
#define CRTC_PIX_WIDTH_16BPP			0x00000400
#define CRTC_PIX_WIDTH_24BPP			0x00000500
#define CRTC_PIX_WIDTH_32BPP			0x00000600

/* DAC_CNTL bit constants */
#define DAC_8BIT_EN				0x00000100
#define DAC_MASK				0xFF000000
#define DAC_BLANKING				0x00000004
#define DAC_RANGE_CNTL				0x00000003
#define DAC_CLK_SEL				0x00000010
#define DAC_PALETTE_ACCESS_CNTL			0x00000020
#define DAC_PALETTE2_SNOOP_EN			0x00000040
#define DAC_PDWN				0x00008000

/* CRTC_EXT_CNTL */
#define CRT_CRTC_ON				0x00008000

/* GEN_RESET_CNTL bit constants */
#define SOFT_RESET_GUI				0x00000001
#define SOFT_RESET_VCLK				0x00000100
#define SOFT_RESET_PCLK				0x00000200
#define SOFT_RESET_ECP				0x00000400
#define SOFT_RESET_DISPENG_XCLK			0x00000800

/* PC_GUI_CTLSTAT bit constants */
#define PC_BUSY_INIT				0x10000000
#define PC_BUSY_GUI				0x20000000
#define PC_BUSY_NGUI				0x40000000
#define PC_BUSY					0x80000000

#define BUS_MASTER_DIS				0x00000040
#define PM4_BUFFER_CNTL_NONPM4			0x00000000

/* DP_DATATYPE bit constants */
#define DST_8BPP				0x00000002
#define DST_15BPP				0x00000003
#define DST_16BPP				0x00000004
#define DST_24BPP				0x00000005
#define DST_32BPP				0x00000006

#define BRUSH_SOLIDCOLOR			0x00000d00

/* DP_GUI_MASTER_CNTL bit constants */
#define	GMC_SRC_PITCH_OFFSET_DEFAULT		0x00000000
#define GMC_DST_PITCH_OFFSET_DEFAULT		0x00000000
#define GMC_SRC_CLIP_DEFAULT			0x00000000
#define GMC_DST_CLIP_DEFAULT			0x00000000
#define GMC_BRUSH_SOLIDCOLOR			0x000000d0
#define GMC_SRC_DSTCOLOR			0x00003000
#define GMC_BYTE_ORDER_MSB_TO_LSB		0x00000000
#define GMC_DP_SRC_RECT				0x02000000
#define GMC_3D_FCN_EN_CLR			0x00000000
#define GMC_AUX_CLIP_CLEAR			0x20000000
#define GMC_DST_CLR_CMP_FCN_CLEAR		0x10000000
#define GMC_WRITE_MASK_SET			0x40000000
#define GMC_DP_CONVERSION_TEMP_6500		0x00000000

/* DP_GUI_MASTER_CNTL ROP3 named constants */
#define	ROP3_PATCOPY				0x00f00000
#define ROP3_SRCCOPY				0x00cc0000

#define SRC_DSTCOLOR				0x00030000

/* DP_CNTL bit constants */
#define DST_X_RIGHT_TO_LEFT			0x00000000
#define DST_X_LEFT_TO_RIGHT			0x00000001
#define DST_Y_BOTTOM_TO_TOP			0x00000000
#define DST_Y_TOP_TO_BOTTOM			0x00000002
#define DST_X_MAJOR				0x00000000
#define DST_Y_MAJOR				0x00000004
#define DST_X_TILE				0x00000008
#define DST_Y_TILE				0x00000010
#define DST_LAST_PEL				0x00000020
#define DST_TRAIL_X_RIGHT_TO_LEFT		0x00000000
#define DST_TRAIL_X_LEFT_TO_RIGHT		0x00000040
#define DST_TRAP_FILL_RIGHT_TO_LEFT		0x00000000
#define DST_TRAP_FILL_LEFT_TO_RIGHT		0x00000080
#define DST_BRES_SIGN				0x00000100
#define DST_HOST_BIG_ENDIAN_EN			0x00000200
#define DST_POLYLINE_NONLAST			0x00008000
#define DST_RASTER_STALL			0x00010000
#define DST_POLY_EDGE				0x00040000

/* DP_MIX bit constants */
#define DP_SRC_RECT				0x00000200
#define DP_SRC_HOST				0x00000300
#define DP_SRC_HOST_BYTEALIGN			0x00000400

/* LVDS_GEN_CNTL constants */
#define LVDS_BL_MOD_LEVEL_MASK			0x0000ff00
#define LVDS_BL_MOD_LEVEL_SHIFT			8
#define LVDS_BL_MOD_EN				0x00010000
#define LVDS_DIGION				0x00040000
#define LVDS_BLON				0x00080000
#define LVDS_ON					0x00000001
#define LVDS_DISPLAY_DIS			0x00000002
#define LVDS_PANEL_TYPE_2PIX_PER_CLK		0x00000004
#define LVDS_PANEL_24BITS_TFT			0x00000008
#define LVDS_FRAME_MOD_NO			0x00000000
#define LVDS_FRAME_MOD_2_LEVELS			0x00000010
#define LVDS_FRAME_MOD_4_LEVELS			0x00000020
#define LVDS_RST_FM				0x00000040
#define LVDS_EN					0x00000080

/* CRTC2_GEN_CNTL constants */
#define CRTC2_EN				0x02000000

/* POWER_MANAGEMENT constants */
#define PWR_MGT_ON				0x00000001
#define PWR_MGT_MODE_MASK			0x00000006
#define PWR_MGT_MODE_PIN			0x00000000
#define PWR_MGT_MODE_REGISTER			0x00000002
#define PWR_MGT_MODE_TIMER			0x00000004
#define PWR_MGT_MODE_PCI			0x00000006
#define PWR_MGT_AUTO_PWR_UP_EN			0x00000008
#define PWR_MGT_ACTIVITY_PIN_ON			0x00000010
#define PWR_MGT_STANDBY_POL			0x00000020
#define PWR_MGT_SUSPEND_POL			0x00000040
#define PWR_MGT_SELF_REFRESH			0x00000080
#define PWR_MGT_ACTIVITY_PIN_EN			0x00000100
#define PWR_MGT_KEYBD_SNOOP			0x00000200
#define PWR_MGT_TRISTATE_MEM_EN			0x00000800
#define PWR_MGT_SELW4MS				0x00001000
#define PWR_MGT_SLOWDOWN_MCLK			0x00002000

#define PMI_PMSCR_REG				0x60
                                                                                
/* used by ATI bug fix for hardware ROM */
#define RAGE128_MPP_TB_CONFIG                   0x01c0

#endif				/* REG_RAGE128_H */
