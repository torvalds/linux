#ifndef _VIDEO_S3BLIT_H
#define _VIDEO_S3BLIT_H

/* s3 commands */
#define S3_BITBLT       0xc011
#define S3_TWOPOINTLINE 0x2811
#define S3_FILLEDRECT   0x40b1

#define S3_FIFO_EMPTY 0x0400
#define S3_HDW_BUSY   0x0200

/* Enhanced register mapping (MMIO mode) */

#define S3_READ_SEL      0xbee8 /* offset f */
#define S3_MULT_MISC     0xbee8 /* offset e */
#define S3_ERR_TERM      0x92e8
#define S3_FRGD_COLOR    0xa6e8
#define S3_BKGD_COLOR    0xa2e8
#define S3_PIXEL_CNTL    0xbee8 /* offset a */
#define S3_FRGD_MIX      0xbae8
#define S3_BKGD_MIX      0xb6e8
#define S3_CUR_Y         0x82e8
#define S3_CUR_X         0x86e8
#define S3_DESTY_AXSTP   0x8ae8
#define S3_DESTX_DIASTP  0x8ee8
#define S3_MIN_AXIS_PCNT 0xbee8 /* offset 0 */
#define S3_MAJ_AXIS_PCNT 0x96e8
#define S3_CMD           0x9ae8
#define S3_GP_STAT       0x9ae8
#define S3_ADVFUNC_CNTL  0x4ae8
#define S3_WRT_MASK      0xaae8
#define S3_RD_MASK       0xaee8

/* Enhanced register mapping (Packed MMIO mode, write only) */
#define S3_ALT_CURXY     0x8100
#define S3_ALT_CURXY2    0x8104
#define S3_ALT_STEP      0x8108
#define S3_ALT_STEP2     0x810c
#define S3_ALT_ERR       0x8110
#define S3_ALT_CMD       0x8118
#define S3_ALT_MIX       0x8134
#define S3_ALT_PCNT      0x8148
#define S3_ALT_PAT       0x8168

/* Drawing modes */
#define S3_NOTCUR          0x0000
#define S3_LOGICALZERO     0x0001
#define S3_LOGICALONE      0x0002
#define S3_LEAVEASIS       0x0003
#define S3_NOTNEW          0x0004
#define S3_CURXORNEW       0x0005
#define S3_NOT_CURXORNEW   0x0006
#define S3_NEW             0x0007
#define S3_NOTCURORNOTNEW  0x0008
#define S3_CURORNOTNEW     0x0009
#define S3_NOTCURORNEW     0x000a
#define S3_CURORNEW        0x000b
#define S3_CURANDNEW       0x000c
#define S3_NOTCURANDNEW    0x000d
#define S3_CURANDNOTNEW    0x000e
#define S3_NOTCURANDNOTNEW 0x000f

#define S3_CRTC_ADR    0x03d4
#define S3_CRTC_DATA   0x03d5

#define S3_REG_LOCK2 0x39
#define S3_HGC_MODE  0x45

#define S3_HWGC_ORGX_H 0x46
#define S3_HWGC_ORGX_L 0x47
#define S3_HWGC_ORGY_H 0x48
#define S3_HWGC_ORGY_L 0x49
#define S3_HWGC_DX     0x4e
#define S3_HWGC_DY     0x4f


#define S3_LAW_CTL 0x58

#endif /* _VIDEO_S3BLIT_H */
