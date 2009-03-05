#ifndef _RADEON_H
#define _RADEON_H


#define RADEON_REGSIZE			0x4000


#define MM_INDEX                               0x0000  
#define MM_DATA                                0x0004  
#define BUS_CNTL                               0x0030  
#define HI_STAT                                0x004C  
#define BUS_CNTL1                              0x0034
#define I2C_CNTL_1			       0x0094  
#define CNFG_CNTL                              0x00E0
#define CNFG_MEMSIZE                           0x00F8
#define CNFG_APER_0_BASE                       0x0100
#define CNFG_APER_1_BASE                       0x0104
#define CNFG_APER_SIZE                         0x0108
#define CNFG_REG_1_BASE                        0x010C
#define CNFG_REG_APER_SIZE                     0x0110
#define PAD_AGPINPUT_DELAY                     0x0164  
#define PAD_CTLR_STRENGTH                      0x0168  
#define PAD_CTLR_UPDATE                        0x016C
#define PAD_CTLR_MISC                          0x0aa0
#define AGP_CNTL                               0x0174
#define BM_STATUS                              0x0160
#define CAP0_TRIG_CNTL			       0x0950
#define CAP1_TRIG_CNTL		               0x09c0
#define VIPH_CONTROL			       0x0C40
#define VENDOR_ID                              0x0F00  
#define DEVICE_ID                              0x0F02  
#define COMMAND                                0x0F04  
#define STATUS                                 0x0F06  
#define REVISION_ID                            0x0F08  
#define REGPROG_INF                            0x0F09  
#define SUB_CLASS                              0x0F0A  
#define BASE_CODE                              0x0F0B  
#define CACHE_LINE                             0x0F0C  
#define LATENCY                                0x0F0D  
#define HEADER                                 0x0F0E  
#define BIST                                   0x0F0F  
#define REG_MEM_BASE                           0x0F10  
#define REG_IO_BASE                            0x0F14  
#define REG_REG_BASE                           0x0F18
#define ADAPTER_ID                             0x0F2C
#define BIOS_ROM                               0x0F30
#define CAPABILITIES_PTR                       0x0F34  
#define INTERRUPT_LINE                         0x0F3C  
#define INTERRUPT_PIN                          0x0F3D  
#define MIN_GRANT                              0x0F3E  
#define MAX_LATENCY                            0x0F3F  
#define ADAPTER_ID_W                           0x0F4C  
#define PMI_CAP_ID                             0x0F50  
#define PMI_NXT_CAP_PTR                        0x0F51  
#define PMI_PMC_REG                            0x0F52  
#define PM_STATUS                              0x0F54  
#define PMI_DATA                               0x0F57  
#define AGP_CAP_ID                             0x0F58  
#define AGP_STATUS                             0x0F5C  
#define AGP_COMMAND                            0x0F60  
#define AIC_CTRL                               0x01D0
#define AIC_STAT                               0x01D4
#define AIC_PT_BASE                            0x01D8
#define AIC_LO_ADDR                            0x01DC  
#define AIC_HI_ADDR                            0x01E0  
#define AIC_TLB_ADDR                           0x01E4  
#define AIC_TLB_DATA                           0x01E8  
#define DAC_CNTL                               0x0058  
#define DAC_CNTL2                              0x007c
#define CRTC_GEN_CNTL                          0x0050  
#define MEM_CNTL                               0x0140  
#define MC_CNTL                                0x0140
#define EXT_MEM_CNTL                           0x0144  
#define MC_TIMING_CNTL                         0x0144
#define MC_AGP_LOCATION                        0x014C  
#define MEM_IO_CNTL_A0                         0x0178  
#define MEM_REFRESH_CNTL                       0x0178
#define MEM_INIT_LATENCY_TIMER                 0x0154  
#define MC_INIT_GFX_LAT_TIMER                  0x0154
#define MEM_SDRAM_MODE_REG                     0x0158  
#define AGP_BASE                               0x0170  
#define MEM_IO_CNTL_A1                         0x017C  
#define MC_READ_CNTL_AB                        0x017C
#define MEM_IO_CNTL_B0                         0x0180
#define MC_INIT_MISC_LAT_TIMER                 0x0180
#define MEM_IO_CNTL_B1                         0x0184
#define MC_IOPAD_CNTL                          0x0184
#define MC_DEBUG                               0x0188
#define MC_STATUS                              0x0150  
#define MEM_IO_OE_CNTL                         0x018C  
#define MC_CHIP_IO_OE_CNTL_AB                  0x018C
#define MC_FB_LOCATION                         0x0148  
#define HOST_PATH_CNTL                         0x0130  
#define MEM_VGA_WP_SEL                         0x0038  
#define MEM_VGA_RP_SEL                         0x003C  
#define HDP_DEBUG                              0x0138  
#define SW_SEMAPHORE                           0x013C
#define CRTC2_GEN_CNTL                         0x03f8  
#define CRTC2_DISPLAY_BASE_ADDR                0x033c
#define SURFACE_CNTL                           0x0B00  
#define SURFACE0_LOWER_BOUND                   0x0B04  
#define SURFACE1_LOWER_BOUND                   0x0B14  
#define SURFACE2_LOWER_BOUND                   0x0B24  
#define SURFACE3_LOWER_BOUND                   0x0B34  
#define SURFACE4_LOWER_BOUND                   0x0B44  
#define SURFACE5_LOWER_BOUND                   0x0B54
#define SURFACE6_LOWER_BOUND                   0x0B64
#define SURFACE7_LOWER_BOUND                   0x0B74
#define SURFACE0_UPPER_BOUND                   0x0B08  
#define SURFACE1_UPPER_BOUND                   0x0B18  
#define SURFACE2_UPPER_BOUND                   0x0B28  
#define SURFACE3_UPPER_BOUND                   0x0B38  
#define SURFACE4_UPPER_BOUND                   0x0B48  
#define SURFACE5_UPPER_BOUND                   0x0B58  
#define SURFACE6_UPPER_BOUND                   0x0B68  
#define SURFACE7_UPPER_BOUND                   0x0B78  
#define SURFACE0_INFO                          0x0B0C  
#define SURFACE1_INFO                          0x0B1C  
#define SURFACE2_INFO                          0x0B2C  
#define SURFACE3_INFO                          0x0B3C  
#define SURFACE4_INFO                          0x0B4C  
#define SURFACE5_INFO                          0x0B5C  
#define SURFACE6_INFO                          0x0B6C
#define SURFACE7_INFO                          0x0B7C
#define SURFACE_ACCESS_FLAGS                   0x0BF8
#define SURFACE_ACCESS_CLR                     0x0BFC  
#define GEN_INT_CNTL                           0x0040  
#define GEN_INT_STATUS                         0x0044  
#define CRTC_EXT_CNTL                          0x0054
#define RB3D_CNTL			       0x1C3C  
#define WAIT_UNTIL                             0x1720  
#define ISYNC_CNTL                             0x1724  
#define RBBM_GUICNTL                           0x172C  
#define RBBM_STATUS                            0x0E40  
#define RBBM_STATUS_alt_1                      0x1740  
#define RBBM_CNTL                              0x00EC  
#define RBBM_CNTL_alt_1                        0x0E44  
#define RBBM_SOFT_RESET                        0x00F0  
#define RBBM_SOFT_RESET_alt_1                  0x0E48  
#define NQWAIT_UNTIL                           0x0E50  
#define RBBM_DEBUG                             0x0E6C
#define RBBM_CMDFIFO_ADDR                      0x0E70
#define RBBM_CMDFIFO_DATAL                     0x0E74
#define RBBM_CMDFIFO_DATAH                     0x0E78  
#define RBBM_CMDFIFO_STAT                      0x0E7C  
#define CRTC_STATUS                            0x005C  
#define GPIO_VGA_DDC                           0x0060  
#define GPIO_DVI_DDC                           0x0064  
#define GPIO_MONID                             0x0068  
#define GPIO_CRT2_DDC                          0x006c
#define PALETTE_INDEX                          0x00B0  
#define PALETTE_DATA                           0x00B4  
#define PALETTE_30_DATA                        0x00B8  
#define CRTC_H_TOTAL_DISP                      0x0200  
#define CRTC_H_SYNC_STRT_WID                   0x0204  
#define CRTC_V_TOTAL_DISP                      0x0208  
#define CRTC_V_SYNC_STRT_WID                   0x020C  
#define CRTC_VLINE_CRNT_VLINE                  0x0210  
#define CRTC_CRNT_FRAME                        0x0214
#define CRTC_GUI_TRIG_VLINE                    0x0218
#define CRTC_DEBUG                             0x021C
#define CRTC_OFFSET_RIGHT                      0x0220  
#define CRTC_OFFSET                            0x0224  
#define CRTC_OFFSET_CNTL                       0x0228  
#define CRTC_PITCH                             0x022C  
#define OVR_CLR                                0x0230  
#define OVR_WID_LEFT_RIGHT                     0x0234  
#define OVR_WID_TOP_BOTTOM                     0x0238  
#define DISPLAY_BASE_ADDR                      0x023C  
#define SNAPSHOT_VH_COUNTS                     0x0240  
#define SNAPSHOT_F_COUNT                       0x0244  
#define N_VIF_COUNT                            0x0248  
#define SNAPSHOT_VIF_COUNT                     0x024C  
#define FP_CRTC_H_TOTAL_DISP                   0x0250  
#define FP_CRTC_V_TOTAL_DISP                   0x0254  
#define CRT_CRTC_H_SYNC_STRT_WID               0x0258
#define CRT_CRTC_V_SYNC_STRT_WID               0x025C
#define CUR_OFFSET                             0x0260
#define CUR_HORZ_VERT_POSN                     0x0264  
#define CUR_HORZ_VERT_OFF                      0x0268  
#define CUR_CLR0                               0x026C  
#define CUR_CLR1                               0x0270  
#define FP_HORZ_VERT_ACTIVE                    0x0278  
#define CRTC_MORE_CNTL                         0x027C  
#define CRTC_H_CUTOFF_ACTIVE_EN                (1<<4)
#define CRTC_V_CUTOFF_ACTIVE_EN                (1<<5)
#define DAC_EXT_CNTL                           0x0280  
#define FP_GEN_CNTL                            0x0284  
#define FP_HORZ_STRETCH                        0x028C  
#define FP_VERT_STRETCH                        0x0290  
#define FP_H_SYNC_STRT_WID                     0x02C4  
#define FP_V_SYNC_STRT_WID                     0x02C8  
#define AUX_WINDOW_HORZ_CNTL                   0x02D8  
#define AUX_WINDOW_VERT_CNTL                   0x02DC  
//#define DDA_CONFIG			       0x02e0
//#define DDA_ON_OFF			       0x02e4
#define DVI_I2C_CNTL_1			       0x02e4
#define GRPH_BUFFER_CNTL                       0x02F0
#define GRPH2_BUFFER_CNTL                      0x03F0
#define VGA_BUFFER_CNTL                        0x02F4
#define OV0_Y_X_START                          0x0400
#define OV0_Y_X_END                            0x0404  
#define OV0_PIPELINE_CNTL                      0x0408  
#define OV0_REG_LOAD_CNTL                      0x0410  
#define OV0_SCALE_CNTL                         0x0420  
#define OV0_V_INC                              0x0424  
#define OV0_P1_V_ACCUM_INIT                    0x0428  
#define OV0_P23_V_ACCUM_INIT                   0x042C  
#define OV0_P1_BLANK_LINES_AT_TOP              0x0430  
#define OV0_P23_BLANK_LINES_AT_TOP             0x0434  
#define OV0_BASE_ADDR                          0x043C  
#define OV0_VID_BUF0_BASE_ADRS                 0x0440  
#define OV0_VID_BUF1_BASE_ADRS                 0x0444  
#define OV0_VID_BUF2_BASE_ADRS                 0x0448  
#define OV0_VID_BUF3_BASE_ADRS                 0x044C  
#define OV0_VID_BUF4_BASE_ADRS                 0x0450
#define OV0_VID_BUF5_BASE_ADRS                 0x0454
#define OV0_VID_BUF_PITCH0_VALUE               0x0460
#define OV0_VID_BUF_PITCH1_VALUE               0x0464  
#define OV0_AUTO_FLIP_CNTRL                    0x0470  
#define OV0_DEINTERLACE_PATTERN                0x0474  
#define OV0_SUBMIT_HISTORY                     0x0478  
#define OV0_H_INC                              0x0480  
#define OV0_STEP_BY                            0x0484  
#define OV0_P1_H_ACCUM_INIT                    0x0488  
#define OV0_P23_H_ACCUM_INIT                   0x048C  
#define OV0_P1_X_START_END                     0x0494  
#define OV0_P2_X_START_END                     0x0498  
#define OV0_P3_X_START_END                     0x049C  
#define OV0_FILTER_CNTL                        0x04A0  
#define OV0_FOUR_TAP_COEF_0                    0x04B0  
#define OV0_FOUR_TAP_COEF_1                    0x04B4  
#define OV0_FOUR_TAP_COEF_2                    0x04B8
#define OV0_FOUR_TAP_COEF_3                    0x04BC
#define OV0_FOUR_TAP_COEF_4                    0x04C0
#define OV0_FLAG_CNTRL                         0x04DC  
#define OV0_SLICE_CNTL                         0x04E0  
#define OV0_VID_KEY_CLR_LOW                    0x04E4  
#define OV0_VID_KEY_CLR_HIGH                   0x04E8  
#define OV0_GRPH_KEY_CLR_LOW                   0x04EC  
#define OV0_GRPH_KEY_CLR_HIGH                  0x04F0  
#define OV0_KEY_CNTL                           0x04F4  
#define OV0_TEST                               0x04F8  
#define SUBPIC_CNTL                            0x0540  
#define SUBPIC_DEFCOLCON                       0x0544  
#define SUBPIC_Y_X_START                       0x054C  
#define SUBPIC_Y_X_END                         0x0550  
#define SUBPIC_V_INC                           0x0554  
#define SUBPIC_H_INC                           0x0558  
#define SUBPIC_BUF0_OFFSET                     0x055C
#define SUBPIC_BUF1_OFFSET                     0x0560
#define SUBPIC_LC0_OFFSET                      0x0564
#define SUBPIC_LC1_OFFSET                      0x0568  
#define SUBPIC_PITCH                           0x056C  
#define SUBPIC_BTN_HLI_COLCON                  0x0570  
#define SUBPIC_BTN_HLI_Y_X_START               0x0574  
#define SUBPIC_BTN_HLI_Y_X_END                 0x0578  
#define SUBPIC_PALETTE_INDEX                   0x057C  
#define SUBPIC_PALETTE_DATA                    0x0580  
#define SUBPIC_H_ACCUM_INIT                    0x0584  
#define SUBPIC_V_ACCUM_INIT                    0x0588  
#define DISP_MISC_CNTL                         0x0D00  
#define DAC_MACRO_CNTL                         0x0D04  
#define DISP_PWR_MAN                           0x0D08  
#define DISP_TEST_DEBUG_CNTL                   0x0D10  
#define DISP_HW_DEBUG                          0x0D14  
#define DAC_CRC_SIG1                           0x0D18
#define DAC_CRC_SIG2                           0x0D1C
#define OV0_LIN_TRANS_A                        0x0D20
#define OV0_LIN_TRANS_B                        0x0D24  
#define OV0_LIN_TRANS_C                        0x0D28  
#define OV0_LIN_TRANS_D                        0x0D2C  
#define OV0_LIN_TRANS_E                        0x0D30  
#define OV0_LIN_TRANS_F                        0x0D34  
#define OV0_GAMMA_0_F                          0x0D40  
#define OV0_GAMMA_10_1F                        0x0D44  
#define OV0_GAMMA_20_3F                        0x0D48  
#define OV0_GAMMA_40_7F                        0x0D4C  
#define OV0_GAMMA_380_3BF                      0x0D50  
#define OV0_GAMMA_3C0_3FF                      0x0D54  
#define DISP_MERGE_CNTL                        0x0D60  
#define DISP_OUTPUT_CNTL                       0x0D64  
#define DISP_LIN_TRANS_GRPH_A                  0x0D80  
#define DISP_LIN_TRANS_GRPH_B                  0x0D84
#define DISP_LIN_TRANS_GRPH_C                  0x0D88
#define DISP_LIN_TRANS_GRPH_D                  0x0D8C
#define DISP_LIN_TRANS_GRPH_E                  0x0D90  
#define DISP_LIN_TRANS_GRPH_F                  0x0D94  
#define DISP_LIN_TRANS_VID_A                   0x0D98  
#define DISP_LIN_TRANS_VID_B                   0x0D9C  
#define DISP_LIN_TRANS_VID_C                   0x0DA0  
#define DISP_LIN_TRANS_VID_D                   0x0DA4  
#define DISP_LIN_TRANS_VID_E                   0x0DA8  
#define DISP_LIN_TRANS_VID_F                   0x0DAC  
#define RMX_HORZ_FILTER_0TAP_COEF              0x0DB0  
#define RMX_HORZ_FILTER_1TAP_COEF              0x0DB4  
#define RMX_HORZ_FILTER_2TAP_COEF              0x0DB8  
#define RMX_HORZ_PHASE                         0x0DBC  
#define DAC_EMBEDDED_SYNC_CNTL                 0x0DC0  
#define DAC_BROAD_PULSE                        0x0DC4  
#define DAC_SKEW_CLKS                          0x0DC8
#define DAC_INCR                               0x0DCC
#define DAC_NEG_SYNC_LEVEL                     0x0DD0
#define DAC_POS_SYNC_LEVEL                     0x0DD4  
#define DAC_BLANK_LEVEL                        0x0DD8  
#define CLOCK_CNTL_INDEX                       0x0008  
#define CLOCK_CNTL_DATA                        0x000C  
#define CP_RB_CNTL                             0x0704  
#define CP_RB_BASE                             0x0700  
#define CP_RB_RPTR_ADDR                        0x070C  
#define CP_RB_RPTR                             0x0710  
#define CP_RB_WPTR                             0x0714  
#define CP_RB_WPTR_DELAY                       0x0718  
#define CP_IB_BASE                             0x0738  
#define CP_IB_BUFSZ                            0x073C  
#define SCRATCH_REG0                           0x15E0  
#define GUI_SCRATCH_REG0                       0x15E0  
#define SCRATCH_REG1                           0x15E4  
#define GUI_SCRATCH_REG1                       0x15E4  
#define SCRATCH_REG2                           0x15E8
#define GUI_SCRATCH_REG2                       0x15E8
#define SCRATCH_REG3                           0x15EC
#define GUI_SCRATCH_REG3                       0x15EC  
#define SCRATCH_REG4                           0x15F0  
#define GUI_SCRATCH_REG4                       0x15F0  
#define SCRATCH_REG5                           0x15F4  
#define GUI_SCRATCH_REG5                       0x15F4  
#define SCRATCH_UMSK                           0x0770  
#define SCRATCH_ADDR                           0x0774  
#define DP_BRUSH_FRGD_CLR                      0x147C  
#define DP_BRUSH_BKGD_CLR                      0x1478
#define DST_LINE_START                         0x1600
#define DST_LINE_END                           0x1604  
#define SRC_OFFSET                             0x15AC  
#define SRC_PITCH                              0x15B0
#define SRC_TILE                               0x1704
#define SRC_PITCH_OFFSET                       0x1428
#define SRC_X                                  0x1414  
#define SRC_Y                                  0x1418  
#define SRC_X_Y                                0x1590  
#define SRC_Y_X                                0x1434  
#define DST_Y_X				       0x1438
#define DST_WIDTH_HEIGHT		       0x1598
#define DST_HEIGHT_WIDTH		       0x143c
#define DST_OFFSET                             0x1404
#define SRC_CLUT_ADDRESS                       0x1780  
#define SRC_CLUT_DATA                          0x1784  
#define SRC_CLUT_DATA_RD                       0x1788  
#define HOST_DATA0                             0x17C0  
#define HOST_DATA1                             0x17C4  
#define HOST_DATA2                             0x17C8  
#define HOST_DATA3                             0x17CC  
#define HOST_DATA4                             0x17D0  
#define HOST_DATA5                             0x17D4  
#define HOST_DATA6                             0x17D8  
#define HOST_DATA7                             0x17DC
#define HOST_DATA_LAST                         0x17E0
#define DP_SRC_ENDIAN                          0x15D4
#define DP_SRC_FRGD_CLR                        0x15D8  
#define DP_SRC_BKGD_CLR                        0x15DC  
#define SC_LEFT                                0x1640  
#define SC_RIGHT                               0x1644  
#define SC_TOP                                 0x1648  
#define SC_BOTTOM                              0x164C  
#define SRC_SC_RIGHT                           0x1654  
#define SRC_SC_BOTTOM                          0x165C  
#define DP_CNTL                                0x16C0  
#define DP_CNTL_XDIR_YDIR_YMAJOR               0x16D0  
#define DP_DATATYPE                            0x16C4  
#define DP_MIX                                 0x16C8  
#define DP_WRITE_MSK                           0x16CC  
#define DP_XOP                                 0x17F8  
#define CLR_CMP_CLR_SRC                        0x15C4
#define CLR_CMP_CLR_DST                        0x15C8
#define CLR_CMP_CNTL                           0x15C0
#define CLR_CMP_MSK                            0x15CC  
#define DSTCACHE_MODE                          0x1710  
#define DSTCACHE_CTLSTAT                       0x1714  
#define DEFAULT_PITCH_OFFSET                   0x16E0  
#define DEFAULT_SC_BOTTOM_RIGHT                0x16E8  
#define DEFAULT_SC_TOP_LEFT                    0x16EC
#define SRC_PITCH_OFFSET                       0x1428
#define DST_PITCH_OFFSET                       0x142C
#define DP_GUI_MASTER_CNTL                     0x146C  
#define SC_TOP_LEFT                            0x16EC  
#define SC_BOTTOM_RIGHT                        0x16F0  
#define SRC_SC_BOTTOM_RIGHT                    0x16F4  
#define RB2D_DSTCACHE_MODE		       0x3428
#define RB2D_DSTCACHE_CTLSTAT_broken	       0x342C /* do not use */
#define LVDS_GEN_CNTL			       0x02d0
#define LVDS_PLL_CNTL			       0x02d4
#define FP2_GEN_CNTL                           0x0288
#define TMDS_CNTL                              0x0294
#define TMDS_CRC			       0x02a0
#define TMDS_TRANSMITTER_CNTL		       0x02a4
#define MPP_TB_CONFIG            	       0x01c0
#define PAMAC0_DLY_CNTL                        0x0a94
#define PAMAC1_DLY_CNTL                        0x0a98
#define PAMAC2_DLY_CNTL                        0x0a9c
#define FW_CNTL                                0x0118
#define FCP_CNTL                               0x0910
#define VGA_DDA_ON_OFF                         0x02ec
#define TV_MASTER_CNTL                         0x0800

//#define BASE_CODE			       0x0f0b
#define BIOS_0_SCRATCH			       0x0010
#define BIOS_1_SCRATCH			       0x0014
#define BIOS_2_SCRATCH			       0x0018
#define BIOS_3_SCRATCH			       0x001c
#define BIOS_4_SCRATCH			       0x0020
#define BIOS_5_SCRATCH			       0x0024
#define BIOS_6_SCRATCH			       0x0028
#define BIOS_7_SCRATCH			       0x002c

#define HDP_SOFT_RESET                         (1 << 26)

#define TV_DAC_CNTL                            0x088c
#define GPIOPAD_MASK                           0x0198
#define GPIOPAD_A                              0x019c
#define GPIOPAD_EN                             0x01a0
#define GPIOPAD_Y                              0x01a4
#define ZV_LCDPAD_MASK                         0x01a8
#define ZV_LCDPAD_A                            0x01ac
#define ZV_LCDPAD_EN                           0x01b0
#define ZV_LCDPAD_Y                            0x01b4

/* PLL Registers */
#define CLK_PIN_CNTL                               0x0001
#define PPLL_CNTL                                  0x0002
#define PPLL_REF_DIV                               0x0003
#define PPLL_DIV_0                                 0x0004
#define PPLL_DIV_1                                 0x0005
#define PPLL_DIV_2                                 0x0006
#define PPLL_DIV_3                                 0x0007
#define VCLK_ECP_CNTL                              0x0008
#define HTOTAL_CNTL                                0x0009
#define M_SPLL_REF_FB_DIV                          0x000a
#define AGP_PLL_CNTL                               0x000b
#define SPLL_CNTL                                  0x000c
#define SCLK_CNTL                                  0x000d
#define MPLL_CNTL                                  0x000e
#define MDLL_CKO                                   0x000f
#define MDLL_RDCKA                                 0x0010
#define MCLK_CNTL                                  0x0012
#define AGP_PLL_CNTL                               0x000b
#define PLL_TEST_CNTL                              0x0013
#define CLK_PWRMGT_CNTL                            0x0014
#define PLL_PWRMGT_CNTL                            0x0015
#define MCLK_MISC                                  0x001f
#define P2PLL_CNTL                                 0x002a
#define P2PLL_REF_DIV                              0x002b
#define PIXCLKS_CNTL                               0x002d
#define SCLK_MORE_CNTL				   0x0035

/* MCLK_CNTL bit constants */
#define FORCEON_MCLKA				   (1 << 16)
#define FORCEON_MCLKB         		   	   (1 << 17)
#define FORCEON_YCLKA         	    	   	   (1 << 18)
#define FORCEON_YCLKB         		   	   (1 << 19)
#define FORCEON_MC            		   	   (1 << 20)
#define FORCEON_AIC           		   	   (1 << 21)

/* SCLK_CNTL bit constants */
#define DYN_STOP_LAT_MASK			   0x00007ff8
#define CP_MAX_DYN_STOP_LAT			   0x0008
#define SCLK_FORCEON_MASK			   0xffff8000

/* SCLK_MORE_CNTL bit constants */
#define SCLK_MORE_FORCEON			   0x0700

/* BUS_CNTL bit constants */
#define BUS_DBL_RESYNC                             0x00000001
#define BUS_MSTR_RESET                             0x00000002
#define BUS_FLUSH_BUF                              0x00000004
#define BUS_STOP_REQ_DIS                           0x00000008
#define BUS_ROTATION_DIS                           0x00000010
#define BUS_MASTER_DIS                             0x00000040
#define BUS_ROM_WRT_EN                             0x00000080
#define BUS_DIS_ROM                                0x00001000
#define BUS_PCI_READ_RETRY_EN                      0x00002000
#define BUS_AGP_AD_STEPPING_EN                     0x00004000
#define BUS_PCI_WRT_RETRY_EN                       0x00008000
#define BUS_MSTR_RD_MULT                           0x00100000
#define BUS_MSTR_RD_LINE                           0x00200000
#define BUS_SUSPEND                                0x00400000
#define LAT_16X                                    0x00800000
#define BUS_RD_DISCARD_EN                          0x01000000
#define BUS_RD_ABORT_EN                            0x02000000
#define BUS_MSTR_WS                                0x04000000
#define BUS_PARKING_DIS                            0x08000000
#define BUS_MSTR_DISCONNECT_EN                     0x10000000
#define BUS_WRT_BURST                              0x20000000
#define BUS_READ_BURST                             0x40000000
#define BUS_RDY_READ_DLY                           0x80000000

/* PIXCLKS_CNTL */
#define PIX2CLK_SRC_SEL_MASK                       0x03
#define PIX2CLK_SRC_SEL_CPUCLK                     0x00
#define PIX2CLK_SRC_SEL_PSCANCLK                   0x01
#define PIX2CLK_SRC_SEL_BYTECLK                    0x02
#define PIX2CLK_SRC_SEL_P2PLLCLK                   0x03
#define PIX2CLK_ALWAYS_ONb                         (1<<6)
#define PIX2CLK_DAC_ALWAYS_ONb                     (1<<7)
#define PIXCLK_TV_SRC_SEL                          (1 << 8)
#define PIXCLK_LVDS_ALWAYS_ONb                     (1 << 14)
#define PIXCLK_TMDS_ALWAYS_ONb                     (1 << 15)


/* CLOCK_CNTL_INDEX bit constants */
#define PLL_WR_EN                                  0x00000080

/* CNFG_CNTL bit constants */
#define CFG_VGA_RAM_EN                             0x00000100
#define CFG_ATI_REV_ID_MASK			   (0xf << 16)
#define CFG_ATI_REV_A11				   (0 << 16)
#define CFG_ATI_REV_A12				   (1 << 16)
#define CFG_ATI_REV_A13				   (2 << 16)

/* CRTC_EXT_CNTL bit constants */
#define VGA_ATI_LINEAR                             0x00000008
#define VGA_128KAP_PAGING                          0x00000010
#define	XCRT_CNT_EN				   (1 << 6)
#define CRTC_HSYNC_DIS				   (1 << 8)
#define CRTC_VSYNC_DIS				   (1 << 9)
#define CRTC_DISPLAY_DIS			   (1 << 10)
#define CRTC_CRT_ON				   (1 << 15)


/* DSTCACHE_CTLSTAT bit constants */
#define RB2D_DC_FLUSH_2D			   (1 << 0)
#define RB2D_DC_FREE_2D				   (1 << 2)
#define RB2D_DC_FLUSH_ALL			   (RB2D_DC_FLUSH_2D | RB2D_DC_FREE_2D)
#define RB2D_DC_BUSY				   (1 << 31)

/* DSTCACHE_MODE bits constants */
#define RB2D_DC_AUTOFLUSH_ENABLE                   (1 << 8)
#define RB2D_DC_DC_DISABLE_IGNORE_PE               (1 << 17)

/* CRTC_GEN_CNTL bit constants */
#define CRTC_DBL_SCAN_EN                           0x00000001
#define CRTC_CUR_EN                                0x00010000
#define CRTC_INTERLACE_EN			   (1 << 1)
#define CRTC_BYPASS_LUT_EN     			   (1 << 14)
#define CRTC_EXT_DISP_EN      			   (1 << 24)
#define CRTC_EN					   (1 << 25)
#define CRTC_DISP_REQ_EN_B                         (1 << 26)

/* CRTC_STATUS bit constants */
#define CRTC_VBLANK                                0x00000001

/* CRTC2_GEN_CNTL bit constants */
#define CRT2_ON                                    (1 << 7)
#define CRTC2_DISPLAY_DIS                          (1 << 23)
#define CRTC2_EN                                   (1 << 25)
#define CRTC2_DISP_REQ_EN_B                        (1 << 26)

/* CUR_OFFSET, CUR_HORZ_VERT_POSN, CUR_HORZ_VERT_OFF bit constants */
#define CUR_LOCK                                   0x80000000

/* GPIO bit constants */
#define GPIO_A_0		(1 <<  0)
#define GPIO_A_1		(1 <<  1)
#define GPIO_Y_0		(1 <<  8)
#define GPIO_Y_1		(1 <<  9)
#define GPIO_EN_0		(1 << 16)
#define GPIO_EN_1		(1 << 17)
#define GPIO_MASK_0		(1 << 24)
#define GPIO_MASK_1		(1 << 25)
#define VGA_DDC_DATA_OUTPUT	GPIO_A_0
#define VGA_DDC_CLK_OUTPUT	GPIO_A_1
#define VGA_DDC_DATA_INPUT	GPIO_Y_0
#define VGA_DDC_CLK_INPUT	GPIO_Y_1
#define VGA_DDC_DATA_OUT_EN	GPIO_EN_0
#define VGA_DDC_CLK_OUT_EN	GPIO_EN_1


/* FP bit constants */
#define FP_CRTC_H_TOTAL_MASK			   0x000003ff
#define FP_CRTC_H_DISP_MASK			   0x01ff0000
#define FP_CRTC_V_TOTAL_MASK			   0x00000fff
#define FP_CRTC_V_DISP_MASK			   0x0fff0000
#define FP_H_SYNC_STRT_CHAR_MASK		   0x00001ff8
#define FP_H_SYNC_WID_MASK			   0x003f0000
#define FP_V_SYNC_STRT_MASK			   0x00000fff
#define FP_V_SYNC_WID_MASK			   0x001f0000
#define FP_CRTC_H_TOTAL_SHIFT			   0x00000000
#define FP_CRTC_H_DISP_SHIFT			   0x00000010
#define FP_CRTC_V_TOTAL_SHIFT			   0x00000000
#define FP_CRTC_V_DISP_SHIFT			   0x00000010
#define FP_H_SYNC_STRT_CHAR_SHIFT		   0x00000003
#define FP_H_SYNC_WID_SHIFT			   0x00000010
#define FP_V_SYNC_STRT_SHIFT			   0x00000000
#define FP_V_SYNC_WID_SHIFT			   0x00000010

/* FP_GEN_CNTL bit constants */
#define FP_FPON					   (1 << 0)
#define FP_TMDS_EN				   (1 << 2)
#define FP_PANEL_FORMAT                            (1 << 3)
#define FP_EN_TMDS				   (1 << 7)
#define FP_DETECT_SENSE				   (1 << 8)
#define R200_FP_SOURCE_SEL_MASK                    (3 << 10)
#define R200_FP_SOURCE_SEL_CRTC1                   (0 << 10)
#define R200_FP_SOURCE_SEL_CRTC2                   (1 << 10)
#define R200_FP_SOURCE_SEL_RMX                     (2 << 10)
#define R200_FP_SOURCE_SEL_TRANS                   (3 << 10)
#define FP_SEL_CRTC1				   (0 << 13)
#define FP_SEL_CRTC2				   (1 << 13)
#define FP_USE_VGA_HSYNC                           (1 << 14)
#define FP_CRTC_DONT_SHADOW_HPAR		   (1 << 15)
#define FP_CRTC_DONT_SHADOW_VPAR		   (1 << 16)
#define FP_CRTC_DONT_SHADOW_HEND		   (1 << 17)
#define FP_CRTC_USE_SHADOW_VEND			   (1 << 18)
#define FP_RMX_HVSYNC_CONTROL_EN		   (1 << 20)
#define FP_DFP_SYNC_SEL				   (1 << 21)
#define FP_CRTC_LOCK_8DOT			   (1 << 22)
#define FP_CRT_SYNC_SEL				   (1 << 23)
#define FP_USE_SHADOW_EN			   (1 << 24)
#define FP_CRT_SYNC_ALT				   (1 << 26)

/* FP2_GEN_CNTL bit constants */
#define FP2_BLANK_EN             (1 <<  1)
#define FP2_ON                   (1 <<  2)
#define FP2_PANEL_FORMAT         (1 <<  3)
#define FP2_SOURCE_SEL_MASK      (3 << 10)
#define FP2_SOURCE_SEL_CRTC2     (1 << 10)
#define FP2_SRC_SEL_MASK         (3 << 13)
#define FP2_SRC_SEL_CRTC2        (1 << 13)
#define FP2_FP_POL               (1 << 16)
#define FP2_LP_POL               (1 << 17)
#define FP2_SCK_POL              (1 << 18)
#define FP2_LCD_CNTL_MASK        (7 << 19)
#define FP2_PAD_FLOP_EN          (1 << 22)
#define FP2_CRC_EN               (1 << 23)
#define FP2_CRC_READ_EN          (1 << 24)
#define FP2_DV0_EN               (1 << 25)
#define FP2_DV0_RATE_SEL_SDR     (1 << 26)


/* LVDS_GEN_CNTL bit constants */
#define LVDS_ON					   (1 << 0)
#define LVDS_DISPLAY_DIS			   (1 << 1)
#define LVDS_PANEL_TYPE				   (1 << 2)
#define LVDS_PANEL_FORMAT			   (1 << 3)
#define LVDS_EN					   (1 << 7)
#define LVDS_BL_MOD_LEVEL_MASK			   0x0000ff00
#define LVDS_BL_MOD_LEVEL_SHIFT			   8
#define LVDS_BL_MOD_EN				   (1 << 16)
#define LVDS_DIGON				   (1 << 18)
#define LVDS_BLON				   (1 << 19)
#define LVDS_SEL_CRTC2				   (1 << 23)
#define LVDS_STATE_MASK	\
	(LVDS_ON | LVDS_DISPLAY_DIS | LVDS_BL_MOD_LEVEL_MASK | LVDS_BLON)

/* LVDS_PLL_CNTL bit constatns */
#define HSYNC_DELAY_SHIFT			   0x1c
#define HSYNC_DELAY_MASK			   (0xf << 0x1c)

/* TMDS_TRANSMITTER_CNTL bit constants */
#define TMDS_PLL_EN				   (1 << 0)
#define TMDS_PLLRST				   (1 << 1)
#define TMDS_RAN_PAT_RST			   (1 << 7)
#define TMDS_ICHCSEL				   (1 << 28)

/* FP_HORZ_STRETCH bit constants */
#define HORZ_STRETCH_RATIO_MASK			   0xffff
#define HORZ_STRETCH_RATIO_MAX			   4096
#define HORZ_PANEL_SIZE				   (0x1ff << 16)
#define HORZ_PANEL_SHIFT			   16
#define HORZ_STRETCH_PIXREP			   (0 << 25)
#define HORZ_STRETCH_BLEND			   (1 << 26)
#define HORZ_STRETCH_ENABLE			   (1 << 25)
#define HORZ_AUTO_RATIO				   (1 << 27)
#define HORZ_FP_LOOP_STRETCH			   (0x7 << 28)
#define HORZ_AUTO_RATIO_INC			   (1 << 31)


/* FP_VERT_STRETCH bit constants */
#define VERT_STRETCH_RATIO_MASK			   0xfff
#define VERT_STRETCH_RATIO_MAX			   4096
#define VERT_PANEL_SIZE				   (0xfff << 12)
#define VERT_PANEL_SHIFT			   12
#define VERT_STRETCH_LINREP			   (0 << 26)
#define VERT_STRETCH_BLEND			   (1 << 26)
#define VERT_STRETCH_ENABLE			   (1 << 25)
#define VERT_AUTO_RATIO_EN			   (1 << 27)
#define VERT_FP_LOOP_STRETCH			   (0x7 << 28)
#define VERT_STRETCH_RESERVED			   0xf1000000

/* DAC_CNTL bit constants */   
#define DAC_8BIT_EN                                0x00000100
#define DAC_4BPP_PIX_ORDER                         0x00000200
#define DAC_CRC_EN                                 0x00080000
#define DAC_MASK_ALL				   (0xff << 24)
#define DAC_PDWN                                   (1 << 15)
#define DAC_EXPAND_MODE				   (1 << 14)
#define DAC_VGA_ADR_EN				   (1 << 13)
#define DAC_RANGE_CNTL				   (3 <<  0)
#define DAC_RANGE_CNTL_MASK    			   0x03
#define DAC_BLANKING				   (1 <<  2)
#define DAC_CMP_EN                                 (1 <<  3)
#define DAC_CMP_OUTPUT                             (1 <<  7)

/* DAC_CNTL2 bit constants */   
#define DAC2_EXPAND_MODE			   (1 << 14)
#define DAC2_CMP_EN                                (1 << 7)
#define DAC2_PALETTE_ACCESS_CNTL                   (1 << 5)

/* DAC_EXT_CNTL bit constants */
#define DAC_FORCE_BLANK_OFF_EN                     (1 << 4)
#define DAC_FORCE_DATA_EN                          (1 << 5)
#define DAC_FORCE_DATA_SEL_MASK                    (3 << 6)
#define DAC_FORCE_DATA_MASK                        0x0003ff00
#define DAC_FORCE_DATA_SHIFT                       8

/* GEN_RESET_CNTL bit constants */
#define SOFT_RESET_GUI                             0x00000001
#define SOFT_RESET_VCLK                            0x00000100
#define SOFT_RESET_PCLK                            0x00000200
#define SOFT_RESET_ECP                             0x00000400
#define SOFT_RESET_DISPENG_XCLK                    0x00000800

/* MEM_CNTL bit constants */
#define MEM_CTLR_STATUS_IDLE                       0x00000000
#define MEM_CTLR_STATUS_BUSY                       0x00100000
#define MEM_SEQNCR_STATUS_IDLE                     0x00000000
#define MEM_SEQNCR_STATUS_BUSY                     0x00200000
#define MEM_ARBITER_STATUS_IDLE                    0x00000000
#define MEM_ARBITER_STATUS_BUSY                    0x00400000
#define MEM_REQ_UNLOCK                             0x00000000
#define MEM_REQ_LOCK                               0x00800000
#define MEM_NUM_CHANNELS_MASK 			   0x00000001
#define MEM_USE_B_CH_ONLY                          0x00000002
#define RV100_MEM_HALF_MODE                        0x00000008
#define R300_MEM_NUM_CHANNELS_MASK                 0x00000003
#define R300_MEM_USE_CD_CH_ONLY                    0x00000004


/* RBBM_SOFT_RESET bit constants */
#define SOFT_RESET_CP           		   (1 <<  0)
#define SOFT_RESET_HI           		   (1 <<  1)
#define SOFT_RESET_SE           		   (1 <<  2)
#define SOFT_RESET_RE           		   (1 <<  3)
#define SOFT_RESET_PP           		   (1 <<  4)
#define SOFT_RESET_E2           		   (1 <<  5)
#define SOFT_RESET_RB           		   (1 <<  6)
#define SOFT_RESET_HDP          		   (1 <<  7)

/* WAIT_UNTIL bit constants */
#define WAIT_DMA_GUI_IDLE			   (1 << 9)
#define WAIT_2D_IDLECLEAN			   (1 << 16)

/* SURFACE_CNTL bit consants */
#define SURF_TRANSLATION_DIS			   (1 << 8)
#define NONSURF_AP0_SWP_16BPP			   (1 << 20)
#define NONSURF_AP0_SWP_32BPP			   (1 << 21)
#define NONSURF_AP1_SWP_16BPP			   (1 << 22)
#define NONSURF_AP1_SWP_32BPP			   (1 << 23)

/* DEFAULT_SC_BOTTOM_RIGHT bit constants */
#define DEFAULT_SC_RIGHT_MAX			   (0x1fff << 0)
#define DEFAULT_SC_BOTTOM_MAX			   (0x1fff << 16)

/* MM_INDEX bit constants */
#define MM_APER                                    0x80000000

/* CLR_CMP_CNTL bit constants */
#define COMPARE_SRC_FALSE                          0x00000000
#define COMPARE_SRC_TRUE                           0x00000001
#define COMPARE_SRC_NOT_EQUAL                      0x00000004
#define COMPARE_SRC_EQUAL                          0x00000005
#define COMPARE_SRC_EQUAL_FLIP                     0x00000007
#define COMPARE_DST_FALSE                          0x00000000
#define COMPARE_DST_TRUE                           0x00000100
#define COMPARE_DST_NOT_EQUAL                      0x00000400
#define COMPARE_DST_EQUAL                          0x00000500
#define COMPARE_DESTINATION                        0x00000000
#define COMPARE_SOURCE                             0x01000000
#define COMPARE_SRC_AND_DST                        0x02000000


/* DP_CNTL bit constants */
#define DST_X_RIGHT_TO_LEFT                        0x00000000
#define DST_X_LEFT_TO_RIGHT                        0x00000001
#define DST_Y_BOTTOM_TO_TOP                        0x00000000
#define DST_Y_TOP_TO_BOTTOM                        0x00000002
#define DST_X_MAJOR                                0x00000000
#define DST_Y_MAJOR                                0x00000004
#define DST_X_TILE                                 0x00000008
#define DST_Y_TILE                                 0x00000010
#define DST_LAST_PEL                               0x00000020
#define DST_TRAIL_X_RIGHT_TO_LEFT                  0x00000000
#define DST_TRAIL_X_LEFT_TO_RIGHT                  0x00000040
#define DST_TRAP_FILL_RIGHT_TO_LEFT                0x00000000
#define DST_TRAP_FILL_LEFT_TO_RIGHT                0x00000080
#define DST_BRES_SIGN                              0x00000100
#define DST_HOST_BIG_ENDIAN_EN                     0x00000200
#define DST_POLYLINE_NONLAST                       0x00008000
#define DST_RASTER_STALL                           0x00010000
#define DST_POLY_EDGE                              0x00040000


/* DP_CNTL_YDIR_XDIR_YMAJOR bit constants (short version of DP_CNTL) */
#define DST_X_MAJOR_S                              0x00000000
#define DST_Y_MAJOR_S                              0x00000001
#define DST_Y_BOTTOM_TO_TOP_S                      0x00000000
#define DST_Y_TOP_TO_BOTTOM_S                      0x00008000
#define DST_X_RIGHT_TO_LEFT_S                      0x00000000
#define DST_X_LEFT_TO_RIGHT_S                      0x80000000


/* DP_DATATYPE bit constants */
#define DST_8BPP                                   0x00000002
#define DST_15BPP                                  0x00000003
#define DST_16BPP                                  0x00000004
#define DST_24BPP                                  0x00000005
#define DST_32BPP                                  0x00000006
#define DST_8BPP_RGB332                            0x00000007
#define DST_8BPP_Y8                                0x00000008
#define DST_8BPP_RGB8                              0x00000009
#define DST_16BPP_VYUY422                          0x0000000b
#define DST_16BPP_YVYU422                          0x0000000c
#define DST_32BPP_AYUV444                          0x0000000e
#define DST_16BPP_ARGB4444                         0x0000000f
#define BRUSH_SOLIDCOLOR                           0x00000d00
#define SRC_MONO                                   0x00000000
#define SRC_MONO_LBKGD                             0x00010000
#define SRC_DSTCOLOR                               0x00030000
#define BYTE_ORDER_MSB_TO_LSB                      0x00000000
#define BYTE_ORDER_LSB_TO_MSB                      0x40000000
#define DP_CONVERSION_TEMP                         0x80000000
#define HOST_BIG_ENDIAN_EN			   (1 << 29)


/* DP_GUI_MASTER_CNTL bit constants */
#define GMC_SRC_PITCH_OFFSET_DEFAULT               0x00000000
#define GMC_SRC_PITCH_OFFSET_LEAVE                 0x00000001
#define GMC_DST_PITCH_OFFSET_DEFAULT               0x00000000
#define GMC_DST_PITCH_OFFSET_LEAVE                 0x00000002
#define GMC_SRC_CLIP_DEFAULT                       0x00000000
#define GMC_SRC_CLIP_LEAVE                         0x00000004
#define GMC_DST_CLIP_DEFAULT                       0x00000000
#define GMC_DST_CLIP_LEAVE                         0x00000008
#define GMC_BRUSH_8x8MONO                          0x00000000
#define GMC_BRUSH_8x8MONO_LBKGD                    0x00000010
#define GMC_BRUSH_8x1MONO                          0x00000020
#define GMC_BRUSH_8x1MONO_LBKGD                    0x00000030
#define GMC_BRUSH_1x8MONO                          0x00000040
#define GMC_BRUSH_1x8MONO_LBKGD                    0x00000050
#define GMC_BRUSH_32x1MONO                         0x00000060
#define GMC_BRUSH_32x1MONO_LBKGD                   0x00000070
#define GMC_BRUSH_32x32MONO                        0x00000080
#define GMC_BRUSH_32x32MONO_LBKGD                  0x00000090
#define GMC_BRUSH_8x8COLOR                         0x000000a0
#define GMC_BRUSH_8x1COLOR                         0x000000b0
#define GMC_BRUSH_1x8COLOR                         0x000000c0
#define GMC_BRUSH_SOLID_COLOR                       0x000000d0
#define GMC_DST_8BPP                               0x00000200
#define GMC_DST_15BPP                              0x00000300
#define GMC_DST_16BPP                              0x00000400
#define GMC_DST_24BPP                              0x00000500
#define GMC_DST_32BPP                              0x00000600
#define GMC_DST_8BPP_RGB332                        0x00000700
#define GMC_DST_8BPP_Y8                            0x00000800
#define GMC_DST_8BPP_RGB8                          0x00000900
#define GMC_DST_16BPP_VYUY422                      0x00000b00
#define GMC_DST_16BPP_YVYU422                      0x00000c00
#define GMC_DST_32BPP_AYUV444                      0x00000e00
#define GMC_DST_16BPP_ARGB4444                     0x00000f00
#define GMC_SRC_MONO                               0x00000000
#define GMC_SRC_MONO_LBKGD                         0x00001000
#define GMC_SRC_DSTCOLOR                           0x00003000
#define GMC_BYTE_ORDER_MSB_TO_LSB                  0x00000000
#define GMC_BYTE_ORDER_LSB_TO_MSB                  0x00004000
#define GMC_DP_CONVERSION_TEMP_9300                0x00008000
#define GMC_DP_CONVERSION_TEMP_6500                0x00000000
#define GMC_DP_SRC_RECT                            0x02000000
#define GMC_DP_SRC_HOST                            0x03000000
#define GMC_DP_SRC_HOST_BYTEALIGN                  0x04000000
#define GMC_3D_FCN_EN_CLR                          0x00000000
#define GMC_3D_FCN_EN_SET                          0x08000000
#define GMC_DST_CLR_CMP_FCN_LEAVE                  0x00000000
#define GMC_DST_CLR_CMP_FCN_CLEAR                  0x10000000
#define GMC_AUX_CLIP_LEAVE                         0x00000000
#define GMC_AUX_CLIP_CLEAR                         0x20000000
#define GMC_WRITE_MASK_LEAVE                       0x00000000
#define GMC_WRITE_MASK_SET                         0x40000000
#define GMC_CLR_CMP_CNTL_DIS      		   (1 << 28)
#define GMC_SRC_DATATYPE_COLOR			   (3 << 12)
#define ROP3_S                			   0x00cc0000
#define ROP3_SRCCOPY				   0x00cc0000
#define ROP3_P                			   0x00f00000
#define ROP3_PATCOPY				   0x00f00000
#define DP_SRC_SOURCE_MASK        		   (7    << 24)
#define GMC_BRUSH_NONE            		   (15   <<  4)
#define DP_SRC_SOURCE_MEMORY			   (2    << 24)
#define GMC_BRUSH_SOLIDCOLOR			   0x000000d0

/* DP_MIX bit constants */
#define DP_SRC_RECT                                0x00000200
#define DP_SRC_HOST                                0x00000300
#define DP_SRC_HOST_BYTEALIGN                      0x00000400

/* MPLL_CNTL bit constants */
#define MPLL_RESET                                 0x00000001

/* MDLL_CKO bit constants */
#define MCKOA_SLEEP                                0x00000001
#define MCKOA_RESET                                0x00000002
#define MCKOA_REF_SKEW_MASK                        0x00000700
#define MCKOA_FB_SKEW_MASK                         0x00007000

/* MDLL_RDCKA bit constants */
#define MRDCKA0_SLEEP                              0x00000001
#define MRDCKA0_RESET                              0x00000002
#define MRDCKA1_SLEEP                              0x00010000
#define MRDCKA1_RESET                              0x00020000

/* VCLK_ECP_CNTL constants */
#define VCLK_SRC_SEL_MASK                          0x03
#define VCLK_SRC_SEL_CPUCLK                        0x00
#define VCLK_SRC_SEL_PSCANCLK                      0x01
#define VCLK_SRC_SEL_BYTECLK	                   0x02
#define VCLK_SRC_SEL_PPLLCLK			   0x03
#define PIXCLK_ALWAYS_ONb                          0x00000040
#define PIXCLK_DAC_ALWAYS_ONb                      0x00000080

/* BUS_CNTL1 constants */
#define BUS_CNTL1_MOBILE_PLATFORM_SEL_MASK         0x0c000000
#define BUS_CNTL1_MOBILE_PLATFORM_SEL_SHIFT        26
#define BUS_CNTL1_AGPCLK_VALID                     0x80000000

/* PLL_PWRMGT_CNTL constants */
#define PLL_PWRMGT_CNTL_SPLL_TURNOFF               0x00000002
#define PLL_PWRMGT_CNTL_PPLL_TURNOFF               0x00000004
#define PLL_PWRMGT_CNTL_P2PLL_TURNOFF              0x00000008
#define PLL_PWRMGT_CNTL_TVPLL_TURNOFF              0x00000010
#define PLL_PWRMGT_CNTL_MOBILE_SU                  0x00010000
#define PLL_PWRMGT_CNTL_SU_SCLK_USE_BCLK           0x00020000
#define PLL_PWRMGT_CNTL_SU_MCLK_USE_BCLK           0x00040000

/* TV_DAC_CNTL constants */
#define TV_DAC_CNTL_BGSLEEP                        0x00000040
#define TV_DAC_CNTL_DETECT                         0x00000010
#define TV_DAC_CNTL_BGADJ_MASK                     0x000f0000
#define TV_DAC_CNTL_DACADJ_MASK                    0x00f00000
#define TV_DAC_CNTL_BGADJ__SHIFT                   16
#define TV_DAC_CNTL_DACADJ__SHIFT                  20
#define TV_DAC_CNTL_RDACPD                         0x01000000
#define TV_DAC_CNTL_GDACPD                         0x02000000
#define TV_DAC_CNTL_BDACPD                         0x04000000

/* DISP_MISC_CNTL constants */
#define DISP_MISC_CNTL_SOFT_RESET_GRPH_PP          (1 << 0)
#define DISP_MISC_CNTL_SOFT_RESET_SUBPIC_PP        (1 << 1)
#define DISP_MISC_CNTL_SOFT_RESET_OV0_PP           (1 << 2)
#define DISP_MISC_CNTL_SOFT_RESET_GRPH_SCLK        (1 << 4)
#define DISP_MISC_CNTL_SOFT_RESET_SUBPIC_SCLK      (1 << 5)
#define DISP_MISC_CNTL_SOFT_RESET_OV0_SCLK         (1 << 6)
#define DISP_MISC_CNTL_SOFT_RESET_GRPH2_PP         (1 << 12)
#define DISP_MISC_CNTL_SOFT_RESET_GRPH2_SCLK       (1 << 15)
#define DISP_MISC_CNTL_SOFT_RESET_LVDS             (1 << 16)
#define DISP_MISC_CNTL_SOFT_RESET_TMDS             (1 << 17)
#define DISP_MISC_CNTL_SOFT_RESET_DIG_TMDS         (1 << 18)
#define DISP_MISC_CNTL_SOFT_RESET_TV               (1 << 19)

/* DISP_PWR_MAN constants */
#define DISP_PWR_MAN_DISP_PWR_MAN_D3_CRTC_EN       (1 << 0)
#define DISP_PWR_MAN_DISP2_PWR_MAN_D3_CRTC2_EN     (1 << 4)
#define DISP_PWR_MAN_DISP_D3_RST                   (1 << 16)
#define DISP_PWR_MAN_DISP_D3_REG_RST               (1 << 17)
#define DISP_PWR_MAN_DISP_D3_GRPH_RST              (1 << 18)
#define DISP_PWR_MAN_DISP_D3_SUBPIC_RST            (1 << 19)
#define DISP_PWR_MAN_DISP_D3_OV0_RST               (1 << 20)
#define DISP_PWR_MAN_DISP_D1D2_GRPH_RST            (1 << 21)
#define DISP_PWR_MAN_DISP_D1D2_SUBPIC_RST          (1 << 22)
#define DISP_PWR_MAN_DISP_D1D2_OV0_RST             (1 << 23)
#define DISP_PWR_MAN_DIG_TMDS_ENABLE_RST           (1 << 24)
#define DISP_PWR_MAN_TV_ENABLE_RST                 (1 << 25)
#define DISP_PWR_MAN_AUTO_PWRUP_EN                 (1 << 26)

/* masks */

#define CNFG_MEMSIZE_MASK		0x1f000000
#define MEM_CFG_TYPE			0x40000000
#define DST_OFFSET_MASK			0x003fffff
#define DST_PITCH_MASK			0x3fc00000
#define DEFAULT_TILE_MASK		0xc0000000
#define	PPLL_DIV_SEL_MASK		0x00000300
#define	PPLL_RESET			0x00000001
#define	PPLL_SLEEP			0x00000002
#define PPLL_ATOMIC_UPDATE_EN		0x00010000
#define PPLL_REF_DIV_MASK		0x000003ff
#define	PPLL_FB3_DIV_MASK		0x000007ff
#define	PPLL_POST3_DIV_MASK		0x00070000
#define PPLL_ATOMIC_UPDATE_R		0x00008000
#define PPLL_ATOMIC_UPDATE_W		0x00008000
#define	PPLL_VGA_ATOMIC_UPDATE_EN	0x00020000
#define R300_PPLL_REF_DIV_ACC_MASK	(0x3ff << 18)
#define R300_PPLL_REF_DIV_ACC_SHIFT	18

#define GUI_ACTIVE			0x80000000


#define MC_IND_INDEX                           0x01F8
#define MC_IND_DATA                            0x01FC

/* PAD_CTLR_STRENGTH */
#define PAD_MANUAL_OVERRIDE		0x80000000

// pllCLK_PIN_CNTL
#define CLK_PIN_CNTL__OSC_EN_MASK                          0x00000001L
#define CLK_PIN_CNTL__OSC_EN                               0x00000001L
#define CLK_PIN_CNTL__XTL_LOW_GAIN_MASK                    0x00000004L
#define CLK_PIN_CNTL__XTL_LOW_GAIN                         0x00000004L
#define CLK_PIN_CNTL__DONT_USE_XTALIN_MASK                 0x00000010L
#define CLK_PIN_CNTL__DONT_USE_XTALIN                      0x00000010L
#define CLK_PIN_CNTL__SLOW_CLOCK_SOURCE_MASK               0x00000020L
#define CLK_PIN_CNTL__SLOW_CLOCK_SOURCE                    0x00000020L
#define CLK_PIN_CNTL__CG_CLK_TO_OUTPIN_MASK                0x00000800L
#define CLK_PIN_CNTL__CG_CLK_TO_OUTPIN                     0x00000800L
#define CLK_PIN_CNTL__CG_COUNT_UP_TO_OUTPIN_MASK           0x00001000L
#define CLK_PIN_CNTL__CG_COUNT_UP_TO_OUTPIN                0x00001000L
#define CLK_PIN_CNTL__ACCESS_REGS_IN_SUSPEND_MASK          0x00002000L
#define CLK_PIN_CNTL__ACCESS_REGS_IN_SUSPEND               0x00002000L
#define CLK_PIN_CNTL__CG_SPARE_MASK                        0x00004000L
#define CLK_PIN_CNTL__CG_SPARE                             0x00004000L
#define CLK_PIN_CNTL__SCLK_DYN_START_CNTL_MASK             0x00008000L
#define CLK_PIN_CNTL__SCLK_DYN_START_CNTL                  0x00008000L
#define CLK_PIN_CNTL__CP_CLK_RUNNING_MASK                  0x00010000L
#define CLK_PIN_CNTL__CP_CLK_RUNNING                       0x00010000L
#define CLK_PIN_CNTL__CG_SPARE_RD_MASK                     0x00060000L
#define CLK_PIN_CNTL__XTALIN_ALWAYS_ONb_MASK               0x00080000L
#define CLK_PIN_CNTL__XTALIN_ALWAYS_ONb                    0x00080000L
#define CLK_PIN_CNTL__PWRSEQ_DELAY_MASK                    0xff000000L

// pllCLK_PWRMGT_CNTL
#define	CLK_PWRMGT_CNTL__MPLL_PWRMGT_OFF__SHIFT         0x00000000
#define	CLK_PWRMGT_CNTL__SPLL_PWRMGT_OFF__SHIFT         0x00000001
#define	CLK_PWRMGT_CNTL__PPLL_PWRMGT_OFF__SHIFT         0x00000002
#define	CLK_PWRMGT_CNTL__P2PLL_PWRMGT_OFF__SHIFT        0x00000003
#define	CLK_PWRMGT_CNTL__MCLK_TURNOFF__SHIFT            0x00000004
#define	CLK_PWRMGT_CNTL__SCLK_TURNOFF__SHIFT            0x00000005
#define	CLK_PWRMGT_CNTL__PCLK_TURNOFF__SHIFT            0x00000006
#define	CLK_PWRMGT_CNTL__P2CLK_TURNOFF__SHIFT           0x00000007
#define	CLK_PWRMGT_CNTL__MC_CH_MODE__SHIFT              0x00000008
#define	CLK_PWRMGT_CNTL__TEST_MODE__SHIFT               0x00000009
#define	CLK_PWRMGT_CNTL__GLOBAL_PMAN_EN__SHIFT          0x0000000a
#define	CLK_PWRMGT_CNTL__ENGINE_DYNCLK_MODE__SHIFT      0x0000000c
#define	CLK_PWRMGT_CNTL__ACTIVE_HILO_LAT__SHIFT         0x0000000d
#define	CLK_PWRMGT_CNTL__DISP_DYN_STOP_LAT__SHIFT       0x0000000f
#define	CLK_PWRMGT_CNTL__MC_BUSY__SHIFT                 0x00000010
#define	CLK_PWRMGT_CNTL__MC_INT_CNTL__SHIFT             0x00000011
#define	CLK_PWRMGT_CNTL__MC_SWITCH__SHIFT               0x00000012
#define	CLK_PWRMGT_CNTL__DLL_READY__SHIFT               0x00000013
#define	CLK_PWRMGT_CNTL__DISP_PM__SHIFT                 0x00000014
#define	CLK_PWRMGT_CNTL__DYN_STOP_MODE__SHIFT           0x00000015
#define	CLK_PWRMGT_CNTL__CG_NO1_DEBUG__SHIFT            0x00000018
#define	CLK_PWRMGT_CNTL__TVPLL_PWRMGT_OFF__SHIFT        0x0000001e
#define	CLK_PWRMGT_CNTL__TVCLK_TURNOFF__SHIFT           0x0000001f

// pllP2PLL_CNTL
#define P2PLL_CNTL__P2PLL_RESET_MASK                       0x00000001L
#define P2PLL_CNTL__P2PLL_RESET                            0x00000001L
#define P2PLL_CNTL__P2PLL_SLEEP_MASK                       0x00000002L
#define P2PLL_CNTL__P2PLL_SLEEP                            0x00000002L
#define P2PLL_CNTL__P2PLL_TST_EN_MASK                      0x00000004L
#define P2PLL_CNTL__P2PLL_TST_EN                           0x00000004L
#define P2PLL_CNTL__P2PLL_REFCLK_SEL_MASK                  0x00000010L
#define P2PLL_CNTL__P2PLL_REFCLK_SEL                       0x00000010L
#define P2PLL_CNTL__P2PLL_FBCLK_SEL_MASK                   0x00000020L
#define P2PLL_CNTL__P2PLL_FBCLK_SEL                        0x00000020L
#define P2PLL_CNTL__P2PLL_TCPOFF_MASK                      0x00000040L
#define P2PLL_CNTL__P2PLL_TCPOFF                           0x00000040L
#define P2PLL_CNTL__P2PLL_TVCOMAX_MASK                     0x00000080L
#define P2PLL_CNTL__P2PLL_TVCOMAX                          0x00000080L
#define P2PLL_CNTL__P2PLL_PCP_MASK                         0x00000700L
#define P2PLL_CNTL__P2PLL_PVG_MASK                         0x00003800L
#define P2PLL_CNTL__P2PLL_PDC_MASK                         0x0000c000L
#define P2PLL_CNTL__P2PLL_ATOMIC_UPDATE_EN_MASK            0x00010000L
#define P2PLL_CNTL__P2PLL_ATOMIC_UPDATE_EN                 0x00010000L
#define P2PLL_CNTL__P2PLL_ATOMIC_UPDATE_SYNC_MASK          0x00040000L
#define P2PLL_CNTL__P2PLL_ATOMIC_UPDATE_SYNC               0x00040000L
#define P2PLL_CNTL__P2PLL_DISABLE_AUTO_RESET_MASK          0x00080000L
#define P2PLL_CNTL__P2PLL_DISABLE_AUTO_RESET               0x00080000L

// pllPIXCLKS_CNTL
#define	PIXCLKS_CNTL__PIX2CLK_SRC_SEL__SHIFT               0x00000000
#define	PIXCLKS_CNTL__PIX2CLK_INVERT__SHIFT                0x00000004
#define	PIXCLKS_CNTL__PIX2CLK_SRC_INVERT__SHIFT            0x00000005
#define	PIXCLKS_CNTL__PIX2CLK_ALWAYS_ONb__SHIFT            0x00000006
#define	PIXCLKS_CNTL__PIX2CLK_DAC_ALWAYS_ONb__SHIFT        0x00000007
#define	PIXCLKS_CNTL__PIXCLK_TV_SRC_SEL__SHIFT             0x00000008
#define	PIXCLKS_CNTL__PIXCLK_BLEND_ALWAYS_ONb__SHIFT       0x0000000b
#define	PIXCLKS_CNTL__PIXCLK_GV_ALWAYS_ONb__SHIFT          0x0000000c
#define	PIXCLKS_CNTL__PIXCLK_DIG_TMDS_ALWAYS_ONb__SHIFT    0x0000000d
#define	PIXCLKS_CNTL__PIXCLK_LVDS_ALWAYS_ONb__SHIFT        0x0000000e
#define	PIXCLKS_CNTL__PIXCLK_TMDS_ALWAYS_ONb__SHIFT        0x0000000f


// pllPIXCLKS_CNTL
#define PIXCLKS_CNTL__PIX2CLK_SRC_SEL_MASK                 0x00000003L
#define PIXCLKS_CNTL__PIX2CLK_INVERT                       0x00000010L
#define PIXCLKS_CNTL__PIX2CLK_SRC_INVERT                   0x00000020L
#define PIXCLKS_CNTL__PIX2CLK_ALWAYS_ONb                   0x00000040L
#define PIXCLKS_CNTL__PIX2CLK_DAC_ALWAYS_ONb               0x00000080L
#define PIXCLKS_CNTL__PIXCLK_TV_SRC_SEL                    0x00000100L
#define PIXCLKS_CNTL__PIXCLK_BLEND_ALWAYS_ONb              0x00000800L
#define PIXCLKS_CNTL__PIXCLK_GV_ALWAYS_ONb                 0x00001000L
#define PIXCLKS_CNTL__PIXCLK_DIG_TMDS_ALWAYS_ONb           0x00002000L
#define PIXCLKS_CNTL__PIXCLK_LVDS_ALWAYS_ONb               0x00004000L
#define PIXCLKS_CNTL__PIXCLK_TMDS_ALWAYS_ONb               0x00008000L
#define PIXCLKS_CNTL__DISP_TVOUT_PIXCLK_TV_ALWAYS_ONb      (1 << 9)
#define PIXCLKS_CNTL__R300_DVOCLK_ALWAYS_ONb               (1 << 10)
#define PIXCLKS_CNTL__R300_PIXCLK_DVO_ALWAYS_ONb           (1 << 13)
#define PIXCLKS_CNTL__R300_PIXCLK_TRANS_ALWAYS_ONb         (1 << 16)
#define PIXCLKS_CNTL__R300_PIXCLK_TVO_ALWAYS_ONb           (1 << 17)
#define PIXCLKS_CNTL__R300_P2G2CLK_ALWAYS_ONb              (1 << 18)
#define PIXCLKS_CNTL__R300_P2G2CLK_DAC_ALWAYS_ONb          (1 << 19)
#define PIXCLKS_CNTL__R300_DISP_DAC_PIXCLK_DAC2_BLANK_OFF  (1 << 23)


// pllP2PLL_DIV_0
#define P2PLL_DIV_0__P2PLL_FB_DIV_MASK                     0x000007ffL
#define P2PLL_DIV_0__P2PLL_ATOMIC_UPDATE_W_MASK            0x00008000L
#define P2PLL_DIV_0__P2PLL_ATOMIC_UPDATE_W                 0x00008000L
#define P2PLL_DIV_0__P2PLL_ATOMIC_UPDATE_R_MASK            0x00008000L
#define P2PLL_DIV_0__P2PLL_ATOMIC_UPDATE_R                 0x00008000L
#define P2PLL_DIV_0__P2PLL_POST_DIV_MASK                   0x00070000L

// pllSCLK_CNTL
#define SCLK_CNTL__SCLK_SRC_SEL_MASK                    0x00000007L
#define SCLK_CNTL__CP_MAX_DYN_STOP_LAT                  0x00000008L
#define SCLK_CNTL__HDP_MAX_DYN_STOP_LAT                 0x00000010L
#define SCLK_CNTL__TV_MAX_DYN_STOP_LAT                  0x00000020L
#define SCLK_CNTL__E2_MAX_DYN_STOP_LAT                  0x00000040L
#define SCLK_CNTL__SE_MAX_DYN_STOP_LAT                  0x00000080L
#define SCLK_CNTL__IDCT_MAX_DYN_STOP_LAT                0x00000100L
#define SCLK_CNTL__VIP_MAX_DYN_STOP_LAT                 0x00000200L
#define SCLK_CNTL__RE_MAX_DYN_STOP_LAT                  0x00000400L
#define SCLK_CNTL__PB_MAX_DYN_STOP_LAT                  0x00000800L
#define SCLK_CNTL__TAM_MAX_DYN_STOP_LAT                 0x00001000L
#define SCLK_CNTL__TDM_MAX_DYN_STOP_LAT                 0x00002000L
#define SCLK_CNTL__RB_MAX_DYN_STOP_LAT                  0x00004000L
#define SCLK_CNTL__DYN_STOP_LAT_MASK                     0x00007ff8
#define SCLK_CNTL__FORCE_DISP2                          0x00008000L
#define SCLK_CNTL__FORCE_CP                             0x00010000L
#define SCLK_CNTL__FORCE_HDP                            0x00020000L
#define SCLK_CNTL__FORCE_DISP1                          0x00040000L
#define SCLK_CNTL__FORCE_TOP                            0x00080000L
#define SCLK_CNTL__FORCE_E2                             0x00100000L
#define SCLK_CNTL__FORCE_SE                             0x00200000L
#define SCLK_CNTL__FORCE_IDCT                           0x00400000L
#define SCLK_CNTL__FORCE_VIP                            0x00800000L
#define SCLK_CNTL__FORCE_RE                             0x01000000L
#define SCLK_CNTL__FORCE_PB                             0x02000000L
#define SCLK_CNTL__FORCE_TAM                            0x04000000L
#define SCLK_CNTL__FORCE_TDM                            0x08000000L
#define SCLK_CNTL__FORCE_RB                             0x10000000L
#define SCLK_CNTL__FORCE_TV_SCLK                        0x20000000L
#define SCLK_CNTL__FORCE_SUBPIC                         0x40000000L
#define SCLK_CNTL__FORCE_OV0                            0x80000000L
#define SCLK_CNTL__R300_FORCE_VAP                       (1<<21)
#define SCLK_CNTL__R300_FORCE_SR                        (1<<25)
#define SCLK_CNTL__R300_FORCE_PX                        (1<<26)
#define SCLK_CNTL__R300_FORCE_TX                        (1<<27)
#define SCLK_CNTL__R300_FORCE_US                        (1<<28)
#define SCLK_CNTL__R300_FORCE_SU                        (1<<30)
#define SCLK_CNTL__FORCEON_MASK                         0xffff8000L

// pllSCLK_CNTL2
#define SCLK_CNTL2__R300_TCL_MAX_DYN_STOP_LAT           (1<<10)
#define SCLK_CNTL2__R300_GA_MAX_DYN_STOP_LAT            (1<<11)
#define SCLK_CNTL2__R300_CBA_MAX_DYN_STOP_LAT           (1<<12)
#define SCLK_CNTL2__R300_FORCE_TCL                      (1<<13)
#define SCLK_CNTL2__R300_FORCE_CBA                      (1<<14)
#define SCLK_CNTL2__R300_FORCE_GA                       (1<<15)

// SCLK_MORE_CNTL
#define SCLK_MORE_CNTL__DISPREGS_MAX_DYN_STOP_LAT          0x00000001L
#define SCLK_MORE_CNTL__MC_GUI_MAX_DYN_STOP_LAT            0x00000002L
#define SCLK_MORE_CNTL__MC_HOST_MAX_DYN_STOP_LAT           0x00000004L
#define SCLK_MORE_CNTL__FORCE_DISPREGS                     0x00000100L
#define SCLK_MORE_CNTL__FORCE_MC_GUI                       0x00000200L
#define SCLK_MORE_CNTL__FORCE_MC_HOST                      0x00000400L
#define SCLK_MORE_CNTL__STOP_SCLK_EN                       0x00001000L
#define SCLK_MORE_CNTL__STOP_SCLK_A                        0x00002000L
#define SCLK_MORE_CNTL__STOP_SCLK_B                        0x00004000L
#define SCLK_MORE_CNTL__STOP_SCLK_C                        0x00008000L
#define SCLK_MORE_CNTL__HALF_SPEED_SCLK                    0x00010000L
#define SCLK_MORE_CNTL__IO_CG_VOLTAGE_DROP                 0x00020000L
#define SCLK_MORE_CNTL__TVFB_SOFT_RESET                    0x00040000L
#define SCLK_MORE_CNTL__VOLTAGE_DROP_SYNC                  0x00080000L
#define SCLK_MORE_CNTL__IDLE_DELAY_HALF_SCLK               0x00400000L
#define SCLK_MORE_CNTL__AGP_BUSY_HALF_SCLK                 0x00800000L
#define SCLK_MORE_CNTL__CG_SPARE_RD_C_MASK                 0xff000000L
#define SCLK_MORE_CNTL__FORCEON                            0x00000700L

// MCLK_CNTL
#define MCLK_CNTL__MCLKA_SRC_SEL_MASK                   0x00000007L
#define MCLK_CNTL__YCLKA_SRC_SEL_MASK                   0x00000070L
#define MCLK_CNTL__MCLKB_SRC_SEL_MASK                   0x00000700L
#define MCLK_CNTL__YCLKB_SRC_SEL_MASK                   0x00007000L
#define MCLK_CNTL__FORCE_MCLKA_MASK                     0x00010000L
#define MCLK_CNTL__FORCE_MCLKA                          0x00010000L
#define MCLK_CNTL__FORCE_MCLKB_MASK                     0x00020000L
#define MCLK_CNTL__FORCE_MCLKB                          0x00020000L
#define MCLK_CNTL__FORCE_YCLKA_MASK                     0x00040000L
#define MCLK_CNTL__FORCE_YCLKA                          0x00040000L
#define MCLK_CNTL__FORCE_YCLKB_MASK                     0x00080000L
#define MCLK_CNTL__FORCE_YCLKB                          0x00080000L
#define MCLK_CNTL__FORCE_MC_MASK                        0x00100000L
#define MCLK_CNTL__FORCE_MC                             0x00100000L
#define MCLK_CNTL__FORCE_AIC_MASK                       0x00200000L
#define MCLK_CNTL__FORCE_AIC                            0x00200000L
#define MCLK_CNTL__MRDCKA0_SOUTSEL_MASK                 0x03000000L
#define MCLK_CNTL__MRDCKA1_SOUTSEL_MASK                 0x0c000000L
#define MCLK_CNTL__MRDCKB0_SOUTSEL_MASK                 0x30000000L
#define MCLK_CNTL__MRDCKB1_SOUTSEL_MASK                 0xc0000000L
#define MCLK_CNTL__R300_DISABLE_MC_MCLKA                (1 << 21)
#define MCLK_CNTL__R300_DISABLE_MC_MCLKB                (1 << 21)

// MCLK_MISC
#define MCLK_MISC__SCLK_SOURCED_FROM_MPLL_SEL_MASK         0x00000003L
#define MCLK_MISC__MCLK_FROM_SPLL_DIV_SEL_MASK             0x00000004L
#define MCLK_MISC__MCLK_FROM_SPLL_DIV_SEL                  0x00000004L
#define MCLK_MISC__ENABLE_SCLK_FROM_MPLL_MASK              0x00000008L
#define MCLK_MISC__ENABLE_SCLK_FROM_MPLL                   0x00000008L
#define MCLK_MISC__MPLL_MODEA_MODEC_HW_SEL_EN_MASK         0x00000010L
#define MCLK_MISC__MPLL_MODEA_MODEC_HW_SEL_EN              0x00000010L
#define MCLK_MISC__DLL_READY_LAT_MASK                      0x00000100L
#define MCLK_MISC__DLL_READY_LAT                           0x00000100L
#define MCLK_MISC__MC_MCLK_MAX_DYN_STOP_LAT_MASK           0x00001000L
#define MCLK_MISC__MC_MCLK_MAX_DYN_STOP_LAT                0x00001000L
#define MCLK_MISC__IO_MCLK_MAX_DYN_STOP_LAT_MASK           0x00002000L
#define MCLK_MISC__IO_MCLK_MAX_DYN_STOP_LAT                0x00002000L
#define MCLK_MISC__MC_MCLK_DYN_ENABLE_MASK                 0x00004000L
#define MCLK_MISC__MC_MCLK_DYN_ENABLE                      0x00004000L
#define MCLK_MISC__IO_MCLK_DYN_ENABLE_MASK                 0x00008000L
#define MCLK_MISC__IO_MCLK_DYN_ENABLE                      0x00008000L
#define MCLK_MISC__CGM_CLK_TO_OUTPIN_MASK                  0x00010000L
#define MCLK_MISC__CGM_CLK_TO_OUTPIN                       0x00010000L
#define MCLK_MISC__CLK_OR_COUNT_SEL_MASK                   0x00020000L
#define MCLK_MISC__CLK_OR_COUNT_SEL                        0x00020000L
#define MCLK_MISC__EN_MCLK_TRISTATE_IN_SUSPEND_MASK        0x00040000L
#define MCLK_MISC__EN_MCLK_TRISTATE_IN_SUSPEND             0x00040000L
#define MCLK_MISC__CGM_SPARE_RD_MASK                       0x00300000L
#define MCLK_MISC__CGM_SPARE_A_RD_MASK                     0x00c00000L
#define MCLK_MISC__TCLK_TO_YCLKB_EN_MASK                   0x01000000L
#define MCLK_MISC__TCLK_TO_YCLKB_EN                        0x01000000L
#define MCLK_MISC__CGM_SPARE_A_MASK                        0x0e000000L

// VCLK_ECP_CNTL
#define VCLK_ECP_CNTL__VCLK_SRC_SEL_MASK                   0x00000003L
#define VCLK_ECP_CNTL__VCLK_INVERT                         0x00000010L
#define VCLK_ECP_CNTL__PIXCLK_SRC_INVERT                   0x00000020L
#define VCLK_ECP_CNTL__PIXCLK_ALWAYS_ONb                   0x00000040L
#define VCLK_ECP_CNTL__PIXCLK_DAC_ALWAYS_ONb               0x00000080L
#define VCLK_ECP_CNTL__ECP_DIV_MASK                        0x00000300L
#define VCLK_ECP_CNTL__ECP_FORCE_ON                        0x00040000L
#define VCLK_ECP_CNTL__SUBCLK_FORCE_ON                     0x00080000L
#define VCLK_ECP_CNTL__R300_DISP_DAC_PIXCLK_DAC_BLANK_OFF  (1<<23)

// PLL_PWRMGT_CNTL
#define PLL_PWRMGT_CNTL__MPLL_TURNOFF_MASK                 0x00000001L
#define PLL_PWRMGT_CNTL__MPLL_TURNOFF                      0x00000001L
#define PLL_PWRMGT_CNTL__SPLL_TURNOFF_MASK                 0x00000002L
#define PLL_PWRMGT_CNTL__SPLL_TURNOFF                      0x00000002L
#define PLL_PWRMGT_CNTL__PPLL_TURNOFF_MASK                 0x00000004L
#define PLL_PWRMGT_CNTL__PPLL_TURNOFF                      0x00000004L
#define PLL_PWRMGT_CNTL__P2PLL_TURNOFF_MASK                0x00000008L
#define PLL_PWRMGT_CNTL__P2PLL_TURNOFF                     0x00000008L
#define PLL_PWRMGT_CNTL__TVPLL_TURNOFF_MASK                0x00000010L
#define PLL_PWRMGT_CNTL__TVPLL_TURNOFF                     0x00000010L
#define PLL_PWRMGT_CNTL__AGPCLK_DYN_STOP_LAT_MASK          0x000001e0L
#define PLL_PWRMGT_CNTL__APM_POWER_STATE_MASK              0x00000600L
#define PLL_PWRMGT_CNTL__APM_PWRSTATE_RD_MASK              0x00001800L
#define PLL_PWRMGT_CNTL__PM_MODE_SEL_MASK                  0x00002000L
#define PLL_PWRMGT_CNTL__PM_MODE_SEL                       0x00002000L
#define PLL_PWRMGT_CNTL__EN_PWRSEQ_DONE_COND_MASK          0x00004000L
#define PLL_PWRMGT_CNTL__EN_PWRSEQ_DONE_COND               0x00004000L
#define PLL_PWRMGT_CNTL__EN_DISP_PARKED_COND_MASK          0x00008000L
#define PLL_PWRMGT_CNTL__EN_DISP_PARKED_COND               0x00008000L
#define PLL_PWRMGT_CNTL__MOBILE_SU_MASK                    0x00010000L
#define PLL_PWRMGT_CNTL__MOBILE_SU                         0x00010000L
#define PLL_PWRMGT_CNTL__SU_SCLK_USE_BCLK_MASK             0x00020000L
#define PLL_PWRMGT_CNTL__SU_SCLK_USE_BCLK                  0x00020000L
#define PLL_PWRMGT_CNTL__SU_MCLK_USE_BCLK_MASK             0x00040000L
#define PLL_PWRMGT_CNTL__SU_MCLK_USE_BCLK                  0x00040000L
#define PLL_PWRMGT_CNTL__SU_SUSTAIN_DISABLE_MASK           0x00080000L
#define PLL_PWRMGT_CNTL__SU_SUSTAIN_DISABLE                0x00080000L
#define PLL_PWRMGT_CNTL__TCL_BYPASS_DISABLE_MASK           0x00100000L
#define PLL_PWRMGT_CNTL__TCL_BYPASS_DISABLE                0x00100000L
#define PLL_PWRMGT_CNTL__TCL_CLOCK_CTIVE_RD_MASK          0x00200000L
#define PLL_PWRMGT_CNTL__TCL_CLOCK_ACTIVE_RD               0x00200000L
#define PLL_PWRMGT_CNTL__CG_NO2_DEBUG_MASK                 0xff000000L

// CLK_PWRMGT_CNTL
#define CLK_PWRMGT_CNTL__MPLL_PWRMGT_OFF_MASK           0x00000001L
#define CLK_PWRMGT_CNTL__MPLL_PWRMGT_OFF                0x00000001L
#define CLK_PWRMGT_CNTL__SPLL_PWRMGT_OFF_MASK           0x00000002L
#define CLK_PWRMGT_CNTL__SPLL_PWRMGT_OFF                0x00000002L
#define CLK_PWRMGT_CNTL__PPLL_PWRMGT_OFF_MASK           0x00000004L
#define CLK_PWRMGT_CNTL__PPLL_PWRMGT_OFF                0x00000004L
#define CLK_PWRMGT_CNTL__P2PLL_PWRMGT_OFF_MASK          0x00000008L
#define CLK_PWRMGT_CNTL__P2PLL_PWRMGT_OFF               0x00000008L
#define CLK_PWRMGT_CNTL__MCLK_TURNOFF_MASK              0x00000010L
#define CLK_PWRMGT_CNTL__MCLK_TURNOFF                   0x00000010L
#define CLK_PWRMGT_CNTL__SCLK_TURNOFF_MASK              0x00000020L
#define CLK_PWRMGT_CNTL__SCLK_TURNOFF                   0x00000020L
#define CLK_PWRMGT_CNTL__PCLK_TURNOFF_MASK              0x00000040L
#define CLK_PWRMGT_CNTL__PCLK_TURNOFF                   0x00000040L
#define CLK_PWRMGT_CNTL__P2CLK_TURNOFF_MASK             0x00000080L
#define CLK_PWRMGT_CNTL__P2CLK_TURNOFF                  0x00000080L
#define CLK_PWRMGT_CNTL__MC_CH_MODE_MASK                0x00000100L
#define CLK_PWRMGT_CNTL__MC_CH_MODE                     0x00000100L
#define CLK_PWRMGT_CNTL__TEST_MODE_MASK                 0x00000200L
#define CLK_PWRMGT_CNTL__TEST_MODE                      0x00000200L
#define CLK_PWRMGT_CNTL__GLOBAL_PMAN_EN_MASK            0x00000400L
#define CLK_PWRMGT_CNTL__GLOBAL_PMAN_EN                 0x00000400L
#define CLK_PWRMGT_CNTL__ENGINE_DYNCLK_MODE_MASK        0x00001000L
#define CLK_PWRMGT_CNTL__ENGINE_DYNCLK_MODE             0x00001000L
#define CLK_PWRMGT_CNTL__ACTIVE_HILO_LAT_MASK           0x00006000L
#define CLK_PWRMGT_CNTL__DISP_DYN_STOP_LAT_MASK         0x00008000L
#define CLK_PWRMGT_CNTL__DISP_DYN_STOP_LAT              0x00008000L
#define CLK_PWRMGT_CNTL__MC_BUSY_MASK                   0x00010000L
#define CLK_PWRMGT_CNTL__MC_BUSY                        0x00010000L
#define CLK_PWRMGT_CNTL__MC_INT_CNTL_MASK               0x00020000L
#define CLK_PWRMGT_CNTL__MC_INT_CNTL                    0x00020000L
#define CLK_PWRMGT_CNTL__MC_SWITCH_MASK                 0x00040000L
#define CLK_PWRMGT_CNTL__MC_SWITCH                      0x00040000L
#define CLK_PWRMGT_CNTL__DLL_READY_MASK                 0x00080000L
#define CLK_PWRMGT_CNTL__DLL_READY                      0x00080000L
#define CLK_PWRMGT_CNTL__DISP_PM_MASK                   0x00100000L
#define CLK_PWRMGT_CNTL__DISP_PM                        0x00100000L
#define CLK_PWRMGT_CNTL__DYN_STOP_MODE_MASK             0x00e00000L
#define CLK_PWRMGT_CNTL__CG_NO1_DEBUG_MASK              0x3f000000L
#define CLK_PWRMGT_CNTL__TVPLL_PWRMGT_OFF_MASK          0x40000000L
#define CLK_PWRMGT_CNTL__TVPLL_PWRMGT_OFF               0x40000000L
#define CLK_PWRMGT_CNTL__TVCLK_TURNOFF_MASK             0x80000000L
#define CLK_PWRMGT_CNTL__TVCLK_TURNOFF                  0x80000000L

// BUS_CNTL1
#define BUS_CNTL1__PMI_IO_DISABLE_MASK                     0x00000001L
#define BUS_CNTL1__PMI_IO_DISABLE                          0x00000001L
#define BUS_CNTL1__PMI_MEM_DISABLE_MASK                    0x00000002L
#define BUS_CNTL1__PMI_MEM_DISABLE                         0x00000002L
#define BUS_CNTL1__PMI_BM_DISABLE_MASK                     0x00000004L
#define BUS_CNTL1__PMI_BM_DISABLE                          0x00000004L
#define BUS_CNTL1__PMI_INT_DISABLE_MASK                    0x00000008L
#define BUS_CNTL1__PMI_INT_DISABLE                         0x00000008L
#define BUS_CNTL1__BUS2_IMMEDIATE_PMI_DISABLE_MASK         0x00000020L
#define BUS_CNTL1__BUS2_IMMEDIATE_PMI_DISABLE              0x00000020L
#define BUS_CNTL1__BUS2_VGA_REG_COHERENCY_DIS_MASK         0x00000100L
#define BUS_CNTL1__BUS2_VGA_REG_COHERENCY_DIS              0x00000100L
#define BUS_CNTL1__BUS2_VGA_MEM_COHERENCY_DIS_MASK         0x00000200L
#define BUS_CNTL1__BUS2_VGA_MEM_COHERENCY_DIS              0x00000200L
#define BUS_CNTL1__BUS2_HDP_REG_COHERENCY_DIS_MASK         0x00000400L
#define BUS_CNTL1__BUS2_HDP_REG_COHERENCY_DIS              0x00000400L
#define BUS_CNTL1__BUS2_GUI_INITIATOR_COHERENCY_DIS_MASK   0x00000800L
#define BUS_CNTL1__BUS2_GUI_INITIATOR_COHERENCY_DIS        0x00000800L
#define BUS_CNTL1__MOBILE_PLATFORM_SEL_MASK                0x0c000000L
#define BUS_CNTL1__SEND_SBA_LATENCY_MASK                   0x70000000L
#define BUS_CNTL1__AGPCLK_VALID_MASK                       0x80000000L
#define BUS_CNTL1__AGPCLK_VALID                            0x80000000L

// BUS_CNTL1
#define	BUS_CNTL1__PMI_IO_DISABLE__SHIFT                   0x00000000
#define	BUS_CNTL1__PMI_MEM_DISABLE__SHIFT                  0x00000001
#define	BUS_CNTL1__PMI_BM_DISABLE__SHIFT                   0x00000002
#define	BUS_CNTL1__PMI_INT_DISABLE__SHIFT                  0x00000003
#define	BUS_CNTL1__BUS2_IMMEDIATE_PMI_DISABLE__SHIFT       0x00000005
#define	BUS_CNTL1__BUS2_VGA_REG_COHERENCY_DIS__SHIFT       0x00000008
#define	BUS_CNTL1__BUS2_VGA_MEM_COHERENCY_DIS__SHIFT       0x00000009
#define	BUS_CNTL1__BUS2_HDP_REG_COHERENCY_DIS__SHIFT       0x0000000a
#define	BUS_CNTL1__BUS2_GUI_INITIATOR_COHERENCY_DIS__SHIFT 0x0000000b
#define	BUS_CNTL1__MOBILE_PLATFORM_SEL__SHIFT              0x0000001a
#define	BUS_CNTL1__SEND_SBA_LATENCY__SHIFT                 0x0000001c
#define	BUS_CNTL1__AGPCLK_VALID__SHIFT                     0x0000001f

// CRTC_OFFSET_CNTL
#define CRTC_OFFSET_CNTL__CRTC_TILE_LINE_MASK              0x0000000fL
#define CRTC_OFFSET_CNTL__CRTC_TILE_LINE_RIGHT_MASK        0x000000f0L
#define CRTC_OFFSET_CNTL__CRTC_TILE_EN_RIGHT_MASK          0x00004000L
#define CRTC_OFFSET_CNTL__CRTC_TILE_EN_RIGHT               0x00004000L
#define CRTC_OFFSET_CNTL__CRTC_TILE_EN_MASK                0x00008000L
#define CRTC_OFFSET_CNTL__CRTC_TILE_EN                     0x00008000L
#define CRTC_OFFSET_CNTL__CRTC_OFFSET_FLIP_CNTL_MASK       0x00010000L
#define CRTC_OFFSET_CNTL__CRTC_OFFSET_FLIP_CNTL            0x00010000L
#define CRTC_OFFSET_CNTL__CRTC_STEREO_OFFSET_EN_MASK       0x00020000L
#define CRTC_OFFSET_CNTL__CRTC_STEREO_OFFSET_EN            0x00020000L
#define CRTC_OFFSET_CNTL__CRTC_STEREO_SYNC_EN_MASK         0x000c0000L
#define CRTC_OFFSET_CNTL__CRTC_STEREO_SYNC_OUT_EN_MASK     0x00100000L
#define CRTC_OFFSET_CNTL__CRTC_STEREO_SYNC_OUT_EN          0x00100000L
#define CRTC_OFFSET_CNTL__CRTC_STEREO_SYNC_MASK            0x00200000L
#define CRTC_OFFSET_CNTL__CRTC_STEREO_SYNC                 0x00200000L
#define CRTC_OFFSET_CNTL__CRTC_GUI_TRIG_OFFSET_LEFT_EN_MASK 0x10000000L
#define CRTC_OFFSET_CNTL__CRTC_GUI_TRIG_OFFSET_LEFT_EN     0x10000000L
#define CRTC_OFFSET_CNTL__CRTC_GUI_TRIG_OFFSET_RIGHT_EN_MASK 0x20000000L
#define CRTC_OFFSET_CNTL__CRTC_GUI_TRIG_OFFSET_RIGHT_EN    0x20000000L
#define CRTC_OFFSET_CNTL__CRTC_GUI_TRIG_OFFSET_MASK        0x40000000L
#define CRTC_OFFSET_CNTL__CRTC_GUI_TRIG_OFFSET             0x40000000L
#define CRTC_OFFSET_CNTL__CRTC_OFFSET_LOCK_MASK            0x80000000L
#define CRTC_OFFSET_CNTL__CRTC_OFFSET_LOCK                 0x80000000L

// CRTC_GEN_CNTL
#define CRTC_GEN_CNTL__CRTC_DBL_SCAN_EN_MASK               0x00000001L
#define CRTC_GEN_CNTL__CRTC_DBL_SCAN_EN                    0x00000001L
#define CRTC_GEN_CNTL__CRTC_INTERLACE_EN_MASK              0x00000002L
#define CRTC_GEN_CNTL__CRTC_INTERLACE_EN                   0x00000002L
#define CRTC_GEN_CNTL__CRTC_C_SYNC_EN_MASK                 0x00000010L
#define CRTC_GEN_CNTL__CRTC_C_SYNC_EN                      0x00000010L
#define CRTC_GEN_CNTL__CRTC_PIX_WIDTH_MASK                 0x00000f00L
#define CRTC_GEN_CNTL__CRTC_ICON_EN_MASK                   0x00008000L
#define CRTC_GEN_CNTL__CRTC_ICON_EN                        0x00008000L
#define CRTC_GEN_CNTL__CRTC_CUR_EN_MASK                    0x00010000L
#define CRTC_GEN_CNTL__CRTC_CUR_EN                         0x00010000L
#define CRTC_GEN_CNTL__CRTC_VSTAT_MODE_MASK                0x00060000L
#define CRTC_GEN_CNTL__CRTC_CUR_MODE_MASK                  0x00700000L
#define CRTC_GEN_CNTL__CRTC_EXT_DISP_EN_MASK               0x01000000L
#define CRTC_GEN_CNTL__CRTC_EXT_DISP_EN                    0x01000000L
#define CRTC_GEN_CNTL__CRTC_EN_MASK                        0x02000000L
#define CRTC_GEN_CNTL__CRTC_EN                             0x02000000L
#define CRTC_GEN_CNTL__CRTC_DISP_REQ_EN_B_MASK             0x04000000L
#define CRTC_GEN_CNTL__CRTC_DISP_REQ_EN_B                  0x04000000L

// CRTC2_GEN_CNTL
#define CRTC2_GEN_CNTL__CRTC2_DBL_SCAN_EN_MASK             0x00000001L
#define CRTC2_GEN_CNTL__CRTC2_DBL_SCAN_EN                  0x00000001L
#define CRTC2_GEN_CNTL__CRTC2_INTERLACE_EN_MASK            0x00000002L
#define CRTC2_GEN_CNTL__CRTC2_INTERLACE_EN                 0x00000002L
#define CRTC2_GEN_CNTL__CRTC2_SYNC_TRISTATE_MASK           0x00000010L
#define CRTC2_GEN_CNTL__CRTC2_SYNC_TRISTATE                0x00000010L
#define CRTC2_GEN_CNTL__CRTC2_HSYNC_TRISTATE_MASK          0x00000020L
#define CRTC2_GEN_CNTL__CRTC2_HSYNC_TRISTATE               0x00000020L
#define CRTC2_GEN_CNTL__CRTC2_VSYNC_TRISTATE_MASK          0x00000040L
#define CRTC2_GEN_CNTL__CRTC2_VSYNC_TRISTATE               0x00000040L
#define CRTC2_GEN_CNTL__CRT2_ON_MASK                       0x00000080L
#define CRTC2_GEN_CNTL__CRT2_ON                            0x00000080L
#define CRTC2_GEN_CNTL__CRTC2_PIX_WIDTH_MASK               0x00000f00L
#define CRTC2_GEN_CNTL__CRTC2_ICON_EN_MASK                 0x00008000L
#define CRTC2_GEN_CNTL__CRTC2_ICON_EN                      0x00008000L
#define CRTC2_GEN_CNTL__CRTC2_CUR_EN_MASK                  0x00010000L
#define CRTC2_GEN_CNTL__CRTC2_CUR_EN                       0x00010000L
#define CRTC2_GEN_CNTL__CRTC2_CUR_MODE_MASK                0x00700000L
#define CRTC2_GEN_CNTL__CRTC2_DISPLAY_DIS_MASK             0x00800000L
#define CRTC2_GEN_CNTL__CRTC2_DISPLAY_DIS                  0x00800000L
#define CRTC2_GEN_CNTL__CRTC2_EN_MASK                      0x02000000L
#define CRTC2_GEN_CNTL__CRTC2_EN                           0x02000000L
#define CRTC2_GEN_CNTL__CRTC2_DISP_REQ_EN_B_MASK           0x04000000L
#define CRTC2_GEN_CNTL__CRTC2_DISP_REQ_EN_B                0x04000000L
#define CRTC2_GEN_CNTL__CRTC2_C_SYNC_EN_MASK               0x08000000L
#define CRTC2_GEN_CNTL__CRTC2_C_SYNC_EN                    0x08000000L
#define CRTC2_GEN_CNTL__CRTC2_HSYNC_DIS_MASK               0x10000000L
#define CRTC2_GEN_CNTL__CRTC2_HSYNC_DIS                    0x10000000L
#define CRTC2_GEN_CNTL__CRTC2_VSYNC_DIS_MASK               0x20000000L
#define CRTC2_GEN_CNTL__CRTC2_VSYNC_DIS                    0x20000000L

// AGP_CNTL
#define AGP_CNTL__MAX_IDLE_CLK_MASK                        0x000000ffL
#define AGP_CNTL__HOLD_RD_FIFO_MASK                        0x00000100L
#define AGP_CNTL__HOLD_RD_FIFO                             0x00000100L
#define AGP_CNTL__HOLD_RQ_FIFO_MASK                        0x00000200L
#define AGP_CNTL__HOLD_RQ_FIFO                             0x00000200L
#define AGP_CNTL__EN_2X_STBB_MASK                          0x00000400L
#define AGP_CNTL__EN_2X_STBB                               0x00000400L
#define AGP_CNTL__FORCE_FULL_SBA_MASK                      0x00000800L
#define AGP_CNTL__FORCE_FULL_SBA                           0x00000800L
#define AGP_CNTL__SBA_DIS_MASK                             0x00001000L
#define AGP_CNTL__SBA_DIS                                  0x00001000L
#define AGP_CNTL__AGP_REV_ID_MASK                          0x00002000L
#define AGP_CNTL__AGP_REV_ID                               0x00002000L
#define AGP_CNTL__REG_CRIPPLE_AGP4X_MASK                   0x00004000L
#define AGP_CNTL__REG_CRIPPLE_AGP4X                        0x00004000L
#define AGP_CNTL__REG_CRIPPLE_AGP2X4X_MASK                 0x00008000L
#define AGP_CNTL__REG_CRIPPLE_AGP2X4X                      0x00008000L
#define AGP_CNTL__FORCE_INT_VREF_MASK                      0x00010000L
#define AGP_CNTL__FORCE_INT_VREF                           0x00010000L
#define AGP_CNTL__PENDING_SLOTS_VAL_MASK                   0x00060000L
#define AGP_CNTL__PENDING_SLOTS_SEL_MASK                   0x00080000L
#define AGP_CNTL__PENDING_SLOTS_SEL                        0x00080000L
#define AGP_CNTL__EN_EXTENDED_AD_STB_2X_MASK               0x00100000L
#define AGP_CNTL__EN_EXTENDED_AD_STB_2X                    0x00100000L
#define AGP_CNTL__DIS_QUEUED_GNT_FIX_MASK                  0x00200000L
#define AGP_CNTL__DIS_QUEUED_GNT_FIX                       0x00200000L
#define AGP_CNTL__EN_RDATA2X4X_MULTIRESET_MASK             0x00400000L
#define AGP_CNTL__EN_RDATA2X4X_MULTIRESET                  0x00400000L
#define AGP_CNTL__EN_RBFCALM_MASK                          0x00800000L
#define AGP_CNTL__EN_RBFCALM                               0x00800000L
#define AGP_CNTL__FORCE_EXT_VREF_MASK                      0x01000000L
#define AGP_CNTL__FORCE_EXT_VREF                           0x01000000L
#define AGP_CNTL__DIS_RBF_MASK                             0x02000000L
#define AGP_CNTL__DIS_RBF                                  0x02000000L
#define AGP_CNTL__DELAY_FIRST_SBA_EN_MASK                  0x04000000L
#define AGP_CNTL__DELAY_FIRST_SBA_EN                       0x04000000L
#define AGP_CNTL__DELAY_FIRST_SBA_VAL_MASK                 0x38000000L
#define AGP_CNTL__AGP_MISC_MASK                            0xc0000000L

// AGP_CNTL
#define	AGP_CNTL__MAX_IDLE_CLK__SHIFT                      0x00000000
#define	AGP_CNTL__HOLD_RD_FIFO__SHIFT                      0x00000008
#define	AGP_CNTL__HOLD_RQ_FIFO__SHIFT                      0x00000009
#define	AGP_CNTL__EN_2X_STBB__SHIFT                        0x0000000a
#define	AGP_CNTL__FORCE_FULL_SBA__SHIFT                    0x0000000b
#define	AGP_CNTL__SBA_DIS__SHIFT                           0x0000000c
#define	AGP_CNTL__AGP_REV_ID__SHIFT                        0x0000000d
#define	AGP_CNTL__REG_CRIPPLE_AGP4X__SHIFT                 0x0000000e
#define	AGP_CNTL__REG_CRIPPLE_AGP2X4X__SHIFT               0x0000000f
#define	AGP_CNTL__FORCE_INT_VREF__SHIFT                    0x00000010
#define	AGP_CNTL__PENDING_SLOTS_VAL__SHIFT                 0x00000011
#define	AGP_CNTL__PENDING_SLOTS_SEL__SHIFT                 0x00000013
#define	AGP_CNTL__EN_EXTENDED_AD_STB_2X__SHIFT             0x00000014
#define	AGP_CNTL__DIS_QUEUED_GNT_FIX__SHIFT                0x00000015
#define	AGP_CNTL__EN_RDATA2X4X_MULTIRESET__SHIFT           0x00000016
#define	AGP_CNTL__EN_RBFCALM__SHIFT                        0x00000017
#define	AGP_CNTL__FORCE_EXT_VREF__SHIFT                    0x00000018
#define	AGP_CNTL__DIS_RBF__SHIFT                           0x00000019
#define	AGP_CNTL__DELAY_FIRST_SBA_EN__SHIFT                0x0000001a
#define	AGP_CNTL__DELAY_FIRST_SBA_VAL__SHIFT               0x0000001b
#define	AGP_CNTL__AGP_MISC__SHIFT                          0x0000001e

// DISP_MISC_CNTL
#define DISP_MISC_CNTL__SOFT_RESET_GRPH_PP_MASK            0x00000001L
#define DISP_MISC_CNTL__SOFT_RESET_GRPH_PP                 0x00000001L
#define DISP_MISC_CNTL__SOFT_RESET_SUBPIC_PP_MASK          0x00000002L
#define DISP_MISC_CNTL__SOFT_RESET_SUBPIC_PP               0x00000002L
#define DISP_MISC_CNTL__SOFT_RESET_OV0_PP_MASK             0x00000004L
#define DISP_MISC_CNTL__SOFT_RESET_OV0_PP                  0x00000004L
#define DISP_MISC_CNTL__SOFT_RESET_GRPH_SCLK_MASK          0x00000010L
#define DISP_MISC_CNTL__SOFT_RESET_GRPH_SCLK               0x00000010L
#define DISP_MISC_CNTL__SOFT_RESET_SUBPIC_SCLK_MASK        0x00000020L
#define DISP_MISC_CNTL__SOFT_RESET_SUBPIC_SCLK             0x00000020L
#define DISP_MISC_CNTL__SOFT_RESET_OV0_SCLK_MASK           0x00000040L
#define DISP_MISC_CNTL__SOFT_RESET_OV0_SCLK                0x00000040L
#define DISP_MISC_CNTL__SYNC_STRENGTH_MASK                 0x00000300L
#define DISP_MISC_CNTL__SYNC_PAD_FLOP_EN_MASK              0x00000400L
#define DISP_MISC_CNTL__SYNC_PAD_FLOP_EN                   0x00000400L
#define DISP_MISC_CNTL__SOFT_RESET_GRPH2_PP_MASK           0x00001000L
#define DISP_MISC_CNTL__SOFT_RESET_GRPH2_PP                0x00001000L
#define DISP_MISC_CNTL__SOFT_RESET_GRPH2_SCLK_MASK         0x00008000L
#define DISP_MISC_CNTL__SOFT_RESET_GRPH2_SCLK              0x00008000L
#define DISP_MISC_CNTL__SOFT_RESET_LVDS_MASK               0x00010000L
#define DISP_MISC_CNTL__SOFT_RESET_LVDS                    0x00010000L
#define DISP_MISC_CNTL__SOFT_RESET_TMDS_MASK               0x00020000L
#define DISP_MISC_CNTL__SOFT_RESET_TMDS                    0x00020000L
#define DISP_MISC_CNTL__SOFT_RESET_DIG_TMDS_MASK           0x00040000L
#define DISP_MISC_CNTL__SOFT_RESET_DIG_TMDS                0x00040000L
#define DISP_MISC_CNTL__SOFT_RESET_TV_MASK                 0x00080000L
#define DISP_MISC_CNTL__SOFT_RESET_TV                      0x00080000L
#define DISP_MISC_CNTL__PALETTE2_MEM_RD_MARGIN_MASK        0x00f00000L
#define DISP_MISC_CNTL__PALETTE_MEM_RD_MARGIN_MASK         0x0f000000L
#define DISP_MISC_CNTL__RMX_BUF_MEM_RD_MARGIN_MASK         0xf0000000L

// DISP_PWR_MAN
#define DISP_PWR_MAN__DISP_PWR_MAN_D3_CRTC_EN_MASK         0x00000001L
#define DISP_PWR_MAN__DISP_PWR_MAN_D3_CRTC_EN              0x00000001L
#define DISP_PWR_MAN__DISP2_PWR_MAN_D3_CRTC2_EN_MASK       0x00000010L
#define DISP_PWR_MAN__DISP2_PWR_MAN_D3_CRTC2_EN            0x00000010L
#define DISP_PWR_MAN__DISP_PWR_MAN_DPMS_MASK               0x00000300L
#define DISP_PWR_MAN__DISP_D3_RST_MASK                     0x00010000L
#define DISP_PWR_MAN__DISP_D3_RST                          0x00010000L
#define DISP_PWR_MAN__DISP_D3_REG_RST_MASK                 0x00020000L
#define DISP_PWR_MAN__DISP_D3_REG_RST                      0x00020000L
#define DISP_PWR_MAN__DISP_D3_GRPH_RST_MASK                0x00040000L
#define DISP_PWR_MAN__DISP_D3_GRPH_RST                     0x00040000L
#define DISP_PWR_MAN__DISP_D3_SUBPIC_RST_MASK              0x00080000L
#define DISP_PWR_MAN__DISP_D3_SUBPIC_RST                   0x00080000L
#define DISP_PWR_MAN__DISP_D3_OV0_RST_MASK                 0x00100000L
#define DISP_PWR_MAN__DISP_D3_OV0_RST                      0x00100000L
#define DISP_PWR_MAN__DISP_D1D2_GRPH_RST_MASK              0x00200000L
#define DISP_PWR_MAN__DISP_D1D2_GRPH_RST                   0x00200000L
#define DISP_PWR_MAN__DISP_D1D2_SUBPIC_RST_MASK            0x00400000L
#define DISP_PWR_MAN__DISP_D1D2_SUBPIC_RST                 0x00400000L
#define DISP_PWR_MAN__DISP_D1D2_OV0_RST_MASK               0x00800000L
#define DISP_PWR_MAN__DISP_D1D2_OV0_RST                    0x00800000L
#define DISP_PWR_MAN__DIG_TMDS_ENABLE_RST_MASK             0x01000000L
#define DISP_PWR_MAN__DIG_TMDS_ENABLE_RST                  0x01000000L
#define DISP_PWR_MAN__TV_ENABLE_RST_MASK                   0x02000000L
#define DISP_PWR_MAN__TV_ENABLE_RST                        0x02000000L
#define DISP_PWR_MAN__AUTO_PWRUP_EN_MASK                   0x04000000L
#define DISP_PWR_MAN__AUTO_PWRUP_EN                        0x04000000L

// MC_IND_INDEX
#define MC_IND_INDEX__MC_IND_ADDR_MASK                     0x0000001fL
#define MC_IND_INDEX__MC_IND_WR_EN_MASK                    0x00000100L
#define MC_IND_INDEX__MC_IND_WR_EN                         0x00000100L

// MC_IND_DATA
#define MC_IND_DATA__MC_IND_DATA_MASK                      0xffffffffL

// MC_CHP_IO_CNTL_A1
#define	MC_CHP_IO_CNTL_A1__MEM_SLEWN_CKA__SHIFT            0x00000000
#define	MC_CHP_IO_CNTL_A1__MEM_SLEWN_AA__SHIFT             0x00000001
#define	MC_CHP_IO_CNTL_A1__MEM_SLEWN_DQMA__SHIFT           0x00000002
#define	MC_CHP_IO_CNTL_A1__MEM_SLEWN_DQSA__SHIFT           0x00000003
#define	MC_CHP_IO_CNTL_A1__MEM_SLEWP_CKA__SHIFT            0x00000004
#define	MC_CHP_IO_CNTL_A1__MEM_SLEWP_AA__SHIFT             0x00000005
#define	MC_CHP_IO_CNTL_A1__MEM_SLEWP_DQMA__SHIFT           0x00000006
#define	MC_CHP_IO_CNTL_A1__MEM_SLEWP_DQSA__SHIFT           0x00000007
#define	MC_CHP_IO_CNTL_A1__MEM_PREAMP_AA__SHIFT            0x00000008
#define	MC_CHP_IO_CNTL_A1__MEM_PREAMP_DQMA__SHIFT          0x00000009
#define	MC_CHP_IO_CNTL_A1__MEM_PREAMP_DQSA__SHIFT          0x0000000a
#define	MC_CHP_IO_CNTL_A1__MEM_IO_MODEA__SHIFT             0x0000000c
#define	MC_CHP_IO_CNTL_A1__MEM_REC_CKA__SHIFT              0x0000000e
#define	MC_CHP_IO_CNTL_A1__MEM_REC_AA__SHIFT               0x00000010
#define	MC_CHP_IO_CNTL_A1__MEM_REC_DQMA__SHIFT             0x00000012
#define	MC_CHP_IO_CNTL_A1__MEM_REC_DQSA__SHIFT             0x00000014
#define	MC_CHP_IO_CNTL_A1__MEM_SYNC_PHASEA__SHIFT          0x00000016
#define	MC_CHP_IO_CNTL_A1__MEM_SYNC_CENTERA__SHIFT         0x00000017
#define	MC_CHP_IO_CNTL_A1__MEM_SYNC_ENA__SHIFT             0x00000018
#define	MC_CHP_IO_CNTL_A1__MEM_CLK_SELA__SHIFT             0x0000001a
#define	MC_CHP_IO_CNTL_A1__MEM_CLK_INVA__SHIFT             0x0000001c
#define	MC_CHP_IO_CNTL_A1__MEM_DATA_ENIMP_A__SHIFT         0x0000001e
#define	MC_CHP_IO_CNTL_A1__MEM_CNTL_ENIMP_A__SHIFT         0x0000001f

// MC_CHP_IO_CNTL_B1
#define	MC_CHP_IO_CNTL_B1__MEM_SLEWN_CKB__SHIFT            0x00000000
#define	MC_CHP_IO_CNTL_B1__MEM_SLEWN_AB__SHIFT             0x00000001
#define	MC_CHP_IO_CNTL_B1__MEM_SLEWN_DQMB__SHIFT           0x00000002
#define	MC_CHP_IO_CNTL_B1__MEM_SLEWN_DQSB__SHIFT           0x00000003
#define	MC_CHP_IO_CNTL_B1__MEM_SLEWP_CKB__SHIFT            0x00000004
#define	MC_CHP_IO_CNTL_B1__MEM_SLEWP_AB__SHIFT             0x00000005
#define	MC_CHP_IO_CNTL_B1__MEM_SLEWP_DQMB__SHIFT           0x00000006
#define	MC_CHP_IO_CNTL_B1__MEM_SLEWP_DQSB__SHIFT           0x00000007
#define	MC_CHP_IO_CNTL_B1__MEM_PREAMP_AB__SHIFT            0x00000008
#define	MC_CHP_IO_CNTL_B1__MEM_PREAMP_DQMB__SHIFT          0x00000009
#define	MC_CHP_IO_CNTL_B1__MEM_PREAMP_DQSB__SHIFT          0x0000000a
#define	MC_CHP_IO_CNTL_B1__MEM_IO_MODEB__SHIFT             0x0000000c
#define	MC_CHP_IO_CNTL_B1__MEM_REC_CKB__SHIFT              0x0000000e
#define	MC_CHP_IO_CNTL_B1__MEM_REC_AB__SHIFT               0x00000010
#define	MC_CHP_IO_CNTL_B1__MEM_REC_DQMB__SHIFT             0x00000012
#define	MC_CHP_IO_CNTL_B1__MEM_REC_DQSB__SHIFT             0x00000014
#define	MC_CHP_IO_CNTL_B1__MEM_SYNC_PHASEB__SHIFT          0x00000016
#define	MC_CHP_IO_CNTL_B1__MEM_SYNC_CENTERB__SHIFT         0x00000017
#define	MC_CHP_IO_CNTL_B1__MEM_SYNC_ENB__SHIFT             0x00000018
#define	MC_CHP_IO_CNTL_B1__MEM_CLK_SELB__SHIFT             0x0000001a
#define	MC_CHP_IO_CNTL_B1__MEM_CLK_INVB__SHIFT             0x0000001c
#define	MC_CHP_IO_CNTL_B1__MEM_DATA_ENIMP_B__SHIFT         0x0000001e
#define	MC_CHP_IO_CNTL_B1__MEM_CNTL_ENIMP_B__SHIFT         0x0000001f

// MC_CHP_IO_CNTL_A1
#define MC_CHP_IO_CNTL_A1__MEM_SLEWN_CKA_MASK              0x00000001L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWN_CKA                   0x00000001L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWN_AA_MASK               0x00000002L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWN_AA                    0x00000002L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWN_DQMA_MASK             0x00000004L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWN_DQMA                  0x00000004L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWN_DQSA_MASK             0x00000008L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWN_DQSA                  0x00000008L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWP_CKA_MASK              0x00000010L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWP_CKA                   0x00000010L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWP_AA_MASK               0x00000020L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWP_AA                    0x00000020L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWP_DQMA_MASK             0x00000040L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWP_DQMA                  0x00000040L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWP_DQSA_MASK             0x00000080L
#define MC_CHP_IO_CNTL_A1__MEM_SLEWP_DQSA                  0x00000080L
#define MC_CHP_IO_CNTL_A1__MEM_PREAMP_AA_MASK              0x00000100L
#define MC_CHP_IO_CNTL_A1__MEM_PREAMP_AA                   0x00000100L
#define MC_CHP_IO_CNTL_A1__MEM_PREAMP_DQMA_MASK            0x00000200L
#define MC_CHP_IO_CNTL_A1__MEM_PREAMP_DQMA                 0x00000200L
#define MC_CHP_IO_CNTL_A1__MEM_PREAMP_DQSA_MASK            0x00000400L
#define MC_CHP_IO_CNTL_A1__MEM_PREAMP_DQSA                 0x00000400L
#define MC_CHP_IO_CNTL_A1__MEM_IO_MODEA_MASK               0x00003000L
#define MC_CHP_IO_CNTL_A1__MEM_REC_CKA_MASK                0x0000c000L
#define MC_CHP_IO_CNTL_A1__MEM_REC_AA_MASK                 0x00030000L
#define MC_CHP_IO_CNTL_A1__MEM_REC_DQMA_MASK               0x000c0000L
#define MC_CHP_IO_CNTL_A1__MEM_REC_DQSA_MASK               0x00300000L
#define MC_CHP_IO_CNTL_A1__MEM_SYNC_PHASEA_MASK            0x00400000L
#define MC_CHP_IO_CNTL_A1__MEM_SYNC_PHASEA                 0x00400000L
#define MC_CHP_IO_CNTL_A1__MEM_SYNC_CENTERA_MASK           0x00800000L
#define MC_CHP_IO_CNTL_A1__MEM_SYNC_CENTERA                0x00800000L
#define MC_CHP_IO_CNTL_A1__MEM_SYNC_ENA_MASK               0x03000000L
#define MC_CHP_IO_CNTL_A1__MEM_CLK_SELA_MASK               0x0c000000L
#define MC_CHP_IO_CNTL_A1__MEM_CLK_INVA_MASK               0x10000000L
#define MC_CHP_IO_CNTL_A1__MEM_CLK_INVA                    0x10000000L
#define MC_CHP_IO_CNTL_A1__MEM_DATA_ENIMP_A_MASK           0x40000000L
#define MC_CHP_IO_CNTL_A1__MEM_DATA_ENIMP_A                0x40000000L
#define MC_CHP_IO_CNTL_A1__MEM_CNTL_ENIMP_A_MASK           0x80000000L
#define MC_CHP_IO_CNTL_A1__MEM_CNTL_ENIMP_A                0x80000000L

// MC_CHP_IO_CNTL_B1
#define MC_CHP_IO_CNTL_B1__MEM_SLEWN_CKB_MASK              0x00000001L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWN_CKB                   0x00000001L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWN_AB_MASK               0x00000002L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWN_AB                    0x00000002L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWN_DQMB_MASK             0x00000004L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWN_DQMB                  0x00000004L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWN_DQSB_MASK             0x00000008L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWN_DQSB                  0x00000008L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWP_CKB_MASK              0x00000010L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWP_CKB                   0x00000010L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWP_AB_MASK               0x00000020L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWP_AB                    0x00000020L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWP_DQMB_MASK             0x00000040L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWP_DQMB                  0x00000040L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWP_DQSB_MASK             0x00000080L
#define MC_CHP_IO_CNTL_B1__MEM_SLEWP_DQSB                  0x00000080L
#define MC_CHP_IO_CNTL_B1__MEM_PREAMP_AB_MASK              0x00000100L
#define MC_CHP_IO_CNTL_B1__MEM_PREAMP_AB                   0x00000100L
#define MC_CHP_IO_CNTL_B1__MEM_PREAMP_DQMB_MASK            0x00000200L
#define MC_CHP_IO_CNTL_B1__MEM_PREAMP_DQMB                 0x00000200L
#define MC_CHP_IO_CNTL_B1__MEM_PREAMP_DQSB_MASK            0x00000400L
#define MC_CHP_IO_CNTL_B1__MEM_PREAMP_DQSB                 0x00000400L
#define MC_CHP_IO_CNTL_B1__MEM_IO_MODEB_MASK               0x00003000L
#define MC_CHP_IO_CNTL_B1__MEM_REC_CKB_MASK                0x0000c000L
#define MC_CHP_IO_CNTL_B1__MEM_REC_AB_MASK                 0x00030000L
#define MC_CHP_IO_CNTL_B1__MEM_REC_DQMB_MASK               0x000c0000L
#define MC_CHP_IO_CNTL_B1__MEM_REC_DQSB_MASK               0x00300000L
#define MC_CHP_IO_CNTL_B1__MEM_SYNC_PHASEB_MASK            0x00400000L
#define MC_CHP_IO_CNTL_B1__MEM_SYNC_PHASEB                 0x00400000L
#define MC_CHP_IO_CNTL_B1__MEM_SYNC_CENTERB_MASK           0x00800000L
#define MC_CHP_IO_CNTL_B1__MEM_SYNC_CENTERB                0x00800000L
#define MC_CHP_IO_CNTL_B1__MEM_SYNC_ENB_MASK               0x03000000L
#define MC_CHP_IO_CNTL_B1__MEM_CLK_SELB_MASK               0x0c000000L
#define MC_CHP_IO_CNTL_B1__MEM_CLK_INVB_MASK               0x10000000L
#define MC_CHP_IO_CNTL_B1__MEM_CLK_INVB                    0x10000000L
#define MC_CHP_IO_CNTL_B1__MEM_DATA_ENIMP_B_MASK           0x40000000L
#define MC_CHP_IO_CNTL_B1__MEM_DATA_ENIMP_B                0x40000000L
#define MC_CHP_IO_CNTL_B1__MEM_CNTL_ENIMP_B_MASK           0x80000000L
#define MC_CHP_IO_CNTL_B1__MEM_CNTL_ENIMP_B                0x80000000L

// MEM_SDRAM_MODE_REG
#define MEM_SDRAM_MODE_REG__MEM_MODE_REG_MASK              0x00007fffL
#define MEM_SDRAM_MODE_REG__MEM_WR_LATENCY_MASK            0x000f0000L
#define MEM_SDRAM_MODE_REG__MEM_CAS_LATENCY_MASK           0x00700000L
#define MEM_SDRAM_MODE_REG__MEM_CMD_LATENCY_MASK           0x00800000L
#define MEM_SDRAM_MODE_REG__MEM_CMD_LATENCY                0x00800000L
#define MEM_SDRAM_MODE_REG__MEM_STR_LATENCY_MASK           0x01000000L
#define MEM_SDRAM_MODE_REG__MEM_STR_LATENCY                0x01000000L
#define MEM_SDRAM_MODE_REG__MEM_FALL_OUT_CMD_MASK          0x02000000L
#define MEM_SDRAM_MODE_REG__MEM_FALL_OUT_CMD               0x02000000L
#define MEM_SDRAM_MODE_REG__MEM_FALL_OUT_DATA_MASK         0x04000000L
#define MEM_SDRAM_MODE_REG__MEM_FALL_OUT_DATA              0x04000000L
#define MEM_SDRAM_MODE_REG__MEM_FALL_OUT_STR_MASK          0x08000000L
#define MEM_SDRAM_MODE_REG__MEM_FALL_OUT_STR               0x08000000L
#define MEM_SDRAM_MODE_REG__MC_INIT_COMPLETE_MASK          0x10000000L
#define MEM_SDRAM_MODE_REG__MC_INIT_COMPLETE               0x10000000L
#define MEM_SDRAM_MODE_REG__MEM_DDR_DLL_MASK               0x20000000L
#define MEM_SDRAM_MODE_REG__MEM_DDR_DLL                    0x20000000L
#define MEM_SDRAM_MODE_REG__MEM_CFG_TYPE_MASK              0x40000000L
#define MEM_SDRAM_MODE_REG__MEM_CFG_TYPE                   0x40000000L
#define MEM_SDRAM_MODE_REG__MEM_SDRAM_RESET_MASK           0x80000000L
#define MEM_SDRAM_MODE_REG__MEM_SDRAM_RESET                0x80000000L

// MEM_SDRAM_MODE_REG
#define	MEM_SDRAM_MODE_REG__MEM_MODE_REG__SHIFT            0x00000000
#define	MEM_SDRAM_MODE_REG__MEM_WR_LATENCY__SHIFT          0x00000010
#define	MEM_SDRAM_MODE_REG__MEM_CAS_LATENCY__SHIFT         0x00000014
#define	MEM_SDRAM_MODE_REG__MEM_CMD_LATENCY__SHIFT         0x00000017
#define	MEM_SDRAM_MODE_REG__MEM_STR_LATENCY__SHIFT         0x00000018
#define	MEM_SDRAM_MODE_REG__MEM_FALL_OUT_CMD__SHIFT        0x00000019
#define	MEM_SDRAM_MODE_REG__MEM_FALL_OUT_DATA__SHIFT       0x0000001a
#define	MEM_SDRAM_MODE_REG__MEM_FALL_OUT_STR__SHIFT        0x0000001b
#define	MEM_SDRAM_MODE_REG__MC_INIT_COMPLETE__SHIFT        0x0000001c
#define	MEM_SDRAM_MODE_REG__MEM_DDR_DLL__SHIFT             0x0000001d
#define	MEM_SDRAM_MODE_REG__MEM_CFG_TYPE__SHIFT            0x0000001e
#define	MEM_SDRAM_MODE_REG__MEM_SDRAM_RESET__SHIFT         0x0000001f

// MEM_REFRESH_CNTL
#define MEM_REFRESH_CNTL__MEM_REFRESH_RATE_MASK            0x000000ffL
#define MEM_REFRESH_CNTL__MEM_REFRESH_DIS_MASK             0x00000100L
#define MEM_REFRESH_CNTL__MEM_REFRESH_DIS                  0x00000100L
#define MEM_REFRESH_CNTL__MEM_DYNAMIC_CKE_MASK             0x00000200L
#define MEM_REFRESH_CNTL__MEM_DYNAMIC_CKE                  0x00000200L
#define MEM_REFRESH_CNTL__MEM_TRFC_MASK                    0x0000f000L
#define MEM_REFRESH_CNTL__MEM_CLKA0_ENABLE_MASK            0x00010000L
#define MEM_REFRESH_CNTL__MEM_CLKA0_ENABLE                 0x00010000L
#define MEM_REFRESH_CNTL__MEM_CLKA0b_ENABLE_MASK           0x00020000L
#define MEM_REFRESH_CNTL__MEM_CLKA0b_ENABLE                0x00020000L
#define MEM_REFRESH_CNTL__MEM_CLKA1_ENABLE_MASK            0x00040000L
#define MEM_REFRESH_CNTL__MEM_CLKA1_ENABLE                 0x00040000L
#define MEM_REFRESH_CNTL__MEM_CLKA1b_ENABLE_MASK           0x00080000L
#define MEM_REFRESH_CNTL__MEM_CLKA1b_ENABLE                0x00080000L
#define MEM_REFRESH_CNTL__MEM_CLKAFB_ENABLE_MASK           0x00100000L
#define MEM_REFRESH_CNTL__MEM_CLKAFB_ENABLE                0x00100000L
#define MEM_REFRESH_CNTL__DLL_FB_SLCT_CKA_MASK             0x00c00000L
#define MEM_REFRESH_CNTL__MEM_CLKB0_ENABLE_MASK            0x01000000L
#define MEM_REFRESH_CNTL__MEM_CLKB0_ENABLE                 0x01000000L
#define MEM_REFRESH_CNTL__MEM_CLKB0b_ENABLE_MASK           0x02000000L
#define MEM_REFRESH_CNTL__MEM_CLKB0b_ENABLE                0x02000000L
#define MEM_REFRESH_CNTL__MEM_CLKB1_ENABLE_MASK            0x04000000L
#define MEM_REFRESH_CNTL__MEM_CLKB1_ENABLE                 0x04000000L
#define MEM_REFRESH_CNTL__MEM_CLKB1b_ENABLE_MASK           0x08000000L
#define MEM_REFRESH_CNTL__MEM_CLKB1b_ENABLE                0x08000000L
#define MEM_REFRESH_CNTL__MEM_CLKBFB_ENABLE_MASK           0x10000000L
#define MEM_REFRESH_CNTL__MEM_CLKBFB_ENABLE                0x10000000L
#define MEM_REFRESH_CNTL__DLL_FB_SLCT_CKB_MASK             0xc0000000L

// MC_STATUS
#define MC_STATUS__MEM_PWRUP_COMPL_A_MASK                  0x00000001L
#define MC_STATUS__MEM_PWRUP_COMPL_A                       0x00000001L
#define MC_STATUS__MEM_PWRUP_COMPL_B_MASK                  0x00000002L
#define MC_STATUS__MEM_PWRUP_COMPL_B                       0x00000002L
#define MC_STATUS__MC_IDLE_MASK                            0x00000004L
#define MC_STATUS__MC_IDLE                                 0x00000004L
#define MC_STATUS__IMP_N_VALUE_R_BACK_MASK                 0x00000078L
#define MC_STATUS__IMP_P_VALUE_R_BACK_MASK                 0x00000780L
#define MC_STATUS__TEST_OUT_R_BACK_MASK                    0x00000800L
#define MC_STATUS__TEST_OUT_R_BACK                         0x00000800L
#define MC_STATUS__DUMMY_OUT_R_BACK_MASK                   0x00001000L
#define MC_STATUS__DUMMY_OUT_R_BACK                        0x00001000L
#define MC_STATUS__IMP_N_VALUE_A_R_BACK_MASK               0x0001e000L
#define MC_STATUS__IMP_P_VALUE_A_R_BACK_MASK               0x001e0000L
#define MC_STATUS__IMP_N_VALUE_CK_R_BACK_MASK              0x01e00000L
#define MC_STATUS__IMP_P_VALUE_CK_R_BACK_MASK              0x1e000000L

// MDLL_CKO
#define MDLL_CKO__MCKOA_SLEEP_MASK                         0x00000001L
#define MDLL_CKO__MCKOA_SLEEP                              0x00000001L
#define MDLL_CKO__MCKOA_RESET_MASK                         0x00000002L
#define MDLL_CKO__MCKOA_RESET                              0x00000002L
#define MDLL_CKO__MCKOA_RANGE_MASK                         0x0000000cL
#define MDLL_CKO__ERSTA_SOUTSEL_MASK                       0x00000030L
#define MDLL_CKO__MCKOA_FB_SEL_MASK                        0x000000c0L
#define MDLL_CKO__MCKOA_REF_SKEW_MASK                      0x00000700L
#define MDLL_CKO__MCKOA_FB_SKEW_MASK                       0x00007000L
#define MDLL_CKO__MCKOA_BP_SEL_MASK                        0x00008000L
#define MDLL_CKO__MCKOA_BP_SEL                             0x00008000L
#define MDLL_CKO__MCKOB_SLEEP_MASK                         0x00010000L
#define MDLL_CKO__MCKOB_SLEEP                              0x00010000L
#define MDLL_CKO__MCKOB_RESET_MASK                         0x00020000L
#define MDLL_CKO__MCKOB_RESET                              0x00020000L
#define MDLL_CKO__MCKOB_RANGE_MASK                         0x000c0000L
#define MDLL_CKO__ERSTB_SOUTSEL_MASK                       0x00300000L
#define MDLL_CKO__MCKOB_FB_SEL_MASK                        0x00c00000L
#define MDLL_CKO__MCKOB_REF_SKEW_MASK                      0x07000000L
#define MDLL_CKO__MCKOB_FB_SKEW_MASK                       0x70000000L
#define MDLL_CKO__MCKOB_BP_SEL_MASK                        0x80000000L
#define MDLL_CKO__MCKOB_BP_SEL                             0x80000000L

// MDLL_RDCKA
#define MDLL_RDCKA__MRDCKA0_SLEEP_MASK                     0x00000001L
#define MDLL_RDCKA__MRDCKA0_SLEEP                          0x00000001L
#define MDLL_RDCKA__MRDCKA0_RESET_MASK                     0x00000002L
#define MDLL_RDCKA__MRDCKA0_RESET                          0x00000002L
#define MDLL_RDCKA__MRDCKA0_RANGE_MASK                     0x0000000cL
#define MDLL_RDCKA__MRDCKA0_REF_SEL_MASK                   0x00000030L
#define MDLL_RDCKA__MRDCKA0_FB_SEL_MASK                    0x000000c0L
#define MDLL_RDCKA__MRDCKA0_REF_SKEW_MASK                  0x00000700L
#define MDLL_RDCKA__MRDCKA0_SINSEL_MASK                    0x00000800L
#define MDLL_RDCKA__MRDCKA0_SINSEL                         0x00000800L
#define MDLL_RDCKA__MRDCKA0_FB_SKEW_MASK                   0x00007000L
#define MDLL_RDCKA__MRDCKA0_BP_SEL_MASK                    0x00008000L
#define MDLL_RDCKA__MRDCKA0_BP_SEL                         0x00008000L
#define MDLL_RDCKA__MRDCKA1_SLEEP_MASK                     0x00010000L
#define MDLL_RDCKA__MRDCKA1_SLEEP                          0x00010000L
#define MDLL_RDCKA__MRDCKA1_RESET_MASK                     0x00020000L
#define MDLL_RDCKA__MRDCKA1_RESET                          0x00020000L
#define MDLL_RDCKA__MRDCKA1_RANGE_MASK                     0x000c0000L
#define MDLL_RDCKA__MRDCKA1_REF_SEL_MASK                   0x00300000L
#define MDLL_RDCKA__MRDCKA1_FB_SEL_MASK                    0x00c00000L
#define MDLL_RDCKA__MRDCKA1_REF_SKEW_MASK                  0x07000000L
#define MDLL_RDCKA__MRDCKA1_SINSEL_MASK                    0x08000000L
#define MDLL_RDCKA__MRDCKA1_SINSEL                         0x08000000L
#define MDLL_RDCKA__MRDCKA1_FB_SKEW_MASK                   0x70000000L
#define MDLL_RDCKA__MRDCKA1_BP_SEL_MASK                    0x80000000L
#define MDLL_RDCKA__MRDCKA1_BP_SEL                         0x80000000L

// MDLL_RDCKB
#define MDLL_RDCKB__MRDCKB0_SLEEP_MASK                     0x00000001L
#define MDLL_RDCKB__MRDCKB0_SLEEP                          0x00000001L
#define MDLL_RDCKB__MRDCKB0_RESET_MASK                     0x00000002L
#define MDLL_RDCKB__MRDCKB0_RESET                          0x00000002L
#define MDLL_RDCKB__MRDCKB0_RANGE_MASK                     0x0000000cL
#define MDLL_RDCKB__MRDCKB0_REF_SEL_MASK                   0x00000030L
#define MDLL_RDCKB__MRDCKB0_FB_SEL_MASK                    0x000000c0L
#define MDLL_RDCKB__MRDCKB0_REF_SKEW_MASK                  0x00000700L
#define MDLL_RDCKB__MRDCKB0_SINSEL_MASK                    0x00000800L
#define MDLL_RDCKB__MRDCKB0_SINSEL                         0x00000800L
#define MDLL_RDCKB__MRDCKB0_FB_SKEW_MASK                   0x00007000L
#define MDLL_RDCKB__MRDCKB0_BP_SEL_MASK                    0x00008000L
#define MDLL_RDCKB__MRDCKB0_BP_SEL                         0x00008000L
#define MDLL_RDCKB__MRDCKB1_SLEEP_MASK                     0x00010000L
#define MDLL_RDCKB__MRDCKB1_SLEEP                          0x00010000L
#define MDLL_RDCKB__MRDCKB1_RESET_MASK                     0x00020000L
#define MDLL_RDCKB__MRDCKB1_RESET                          0x00020000L
#define MDLL_RDCKB__MRDCKB1_RANGE_MASK                     0x000c0000L
#define MDLL_RDCKB__MRDCKB1_REF_SEL_MASK                   0x00300000L
#define MDLL_RDCKB__MRDCKB1_FB_SEL_MASK                    0x00c00000L
#define MDLL_RDCKB__MRDCKB1_REF_SKEW_MASK                  0x07000000L
#define MDLL_RDCKB__MRDCKB1_SINSEL_MASK                    0x08000000L
#define MDLL_RDCKB__MRDCKB1_SINSEL                         0x08000000L
#define MDLL_RDCKB__MRDCKB1_FB_SKEW_MASK                   0x70000000L
#define MDLL_RDCKB__MRDCKB1_BP_SEL_MASK                    0x80000000L
#define MDLL_RDCKB__MRDCKB1_BP_SEL                         0x80000000L

#define MDLL_R300_RDCK__MRDCKA_SLEEP                       0x00000001L
#define MDLL_R300_RDCK__MRDCKA_RESET                       0x00000002L
#define MDLL_R300_RDCK__MRDCKB_SLEEP                       0x00000004L
#define MDLL_R300_RDCK__MRDCKB_RESET                       0x00000008L
#define MDLL_R300_RDCK__MRDCKC_SLEEP                       0x00000010L
#define MDLL_R300_RDCK__MRDCKC_RESET                       0x00000020L
#define MDLL_R300_RDCK__MRDCKD_SLEEP                       0x00000040L
#define MDLL_R300_RDCK__MRDCKD_RESET                       0x00000080L

#define pllCLK_PIN_CNTL                             0x0001
#define pllPPLL_CNTL                                0x0002
#define pllPPLL_REF_DIV                             0x0003
#define pllPPLL_DIV_0                               0x0004
#define pllPPLL_DIV_1                               0x0005
#define pllPPLL_DIV_2                               0x0006
#define pllPPLL_DIV_3                               0x0007
#define pllVCLK_ECP_CNTL                            0x0008
#define pllHTOTAL_CNTL                              0x0009
#define pllM_SPLL_REF_FB_DIV                        0x000A
#define pllAGP_PLL_CNTL                             0x000B
#define pllSPLL_CNTL                                0x000C
#define pllSCLK_CNTL                                0x000D
#define pllMPLL_CNTL                                0x000E
#define pllMDLL_CKO                                 0x000F
#define pllMDLL_RDCKA                               0x0010
#define pllMDLL_RDCKB                               0x0011
#define pllMCLK_CNTL                                0x0012
#define pllPLL_TEST_CNTL                            0x0013
#define pllCLK_PWRMGT_CNTL                          0x0014
#define pllPLL_PWRMGT_CNTL                          0x0015
#define pllCG_TEST_MACRO_RW_WRITE                   0x0016
#define pllCG_TEST_MACRO_RW_READ                    0x0017
#define pllCG_TEST_MACRO_RW_DATA                    0x0018
#define pllCG_TEST_MACRO_RW_CNTL                    0x0019
#define pllDISP_TEST_MACRO_RW_WRITE                 0x001A
#define pllDISP_TEST_MACRO_RW_READ                  0x001B
#define pllDISP_TEST_MACRO_RW_DATA                  0x001C
#define pllDISP_TEST_MACRO_RW_CNTL                  0x001D
#define pllSCLK_CNTL2                               0x001E
#define pllMCLK_MISC                                0x001F
#define pllTV_PLL_FINE_CNTL                         0x0020
#define pllTV_PLL_CNTL                              0x0021
#define pllTV_PLL_CNTL1                             0x0022
#define pllTV_DTO_INCREMENTS                        0x0023
#define pllSPLL_AUX_CNTL                            0x0024
#define pllMPLL_AUX_CNTL                            0x0025
#define pllP2PLL_CNTL                               0x002A
#define pllP2PLL_REF_DIV                            0x002B
#define pllP2PLL_DIV_0                              0x002C
#define pllPIXCLKS_CNTL                             0x002D
#define pllHTOTAL2_CNTL                             0x002E
#define pllSSPLL_CNTL                               0x0030
#define pllSSPLL_REF_DIV                            0x0031
#define pllSSPLL_DIV_0                              0x0032
#define pllSS_INT_CNTL                              0x0033
#define pllSS_TST_CNTL                              0x0034
#define pllSCLK_MORE_CNTL                           0x0035

#define ixMC_PERF_CNTL                             0x0000
#define ixMC_PERF_SEL                              0x0001
#define ixMC_PERF_REGION_0                         0x0002
#define ixMC_PERF_REGION_1                         0x0003
#define ixMC_PERF_COUNT_0                          0x0004
#define ixMC_PERF_COUNT_1                          0x0005
#define ixMC_PERF_COUNT_2                          0x0006
#define ixMC_PERF_COUNT_3                          0x0007
#define ixMC_PERF_COUNT_MEMCH_A                    0x0008
#define ixMC_PERF_COUNT_MEMCH_B                    0x0009
#define ixMC_IMP_CNTL                              0x000A
#define ixMC_CHP_IO_CNTL_A0                        0x000B
#define ixMC_CHP_IO_CNTL_A1                        0x000C
#define ixMC_CHP_IO_CNTL_B0                        0x000D
#define ixMC_CHP_IO_CNTL_B1                        0x000E
#define ixMC_IMP_CNTL_0                            0x000F
#define ixTC_MISMATCH_1                            0x0010
#define ixTC_MISMATCH_2                            0x0011
#define ixMC_BIST_CTRL                             0x0012
#define ixREG_COLLAR_WRITE                         0x0013
#define ixREG_COLLAR_READ                          0x0014
#define ixR300_MC_IMP_CNTL                         0x0018
#define ixR300_MC_CHP_IO_CNTL_A0                   0x0019
#define ixR300_MC_CHP_IO_CNTL_A1                   0x001a
#define ixR300_MC_CHP_IO_CNTL_B0                   0x001b
#define ixR300_MC_CHP_IO_CNTL_B1                   0x001c
#define ixR300_MC_CHP_IO_CNTL_C0                   0x001d
#define ixR300_MC_CHP_IO_CNTL_C1                   0x001e
#define ixR300_MC_CHP_IO_CNTL_D0                   0x001f
#define ixR300_MC_CHP_IO_CNTL_D1                   0x0020
#define ixR300_MC_IMP_CNTL_0                       0x0021
#define ixR300_MC_ELPIDA_CNTL                      0x0022
#define ixR300_MC_CHP_IO_OE_CNTL_CD                0x0023
#define ixR300_MC_READ_CNTL_CD                     0x0024
#define ixR300_MC_MC_INIT_WR_LAT_TIMER             0x0025
#define ixR300_MC_DEBUG_CNTL                       0x0026
#define ixR300_MC_BIST_CNTL_0                      0x0028
#define ixR300_MC_BIST_CNTL_1                      0x0029
#define ixR300_MC_BIST_CNTL_2                      0x002a
#define ixR300_MC_BIST_CNTL_3                      0x002b
#define ixR300_MC_BIST_CNTL_4                      0x002c
#define ixR300_MC_BIST_CNTL_5                      0x002d
#define ixR300_MC_IMP_STATUS                       0x002e
#define ixR300_MC_DLL_CNTL                         0x002f
#define NB_TOM                                     0x15C


#endif	/* _RADEON_H */

