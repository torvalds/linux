/*
 * TVIN Modules Exported Header File
 *
 * Author: Lin Xu <lin.xu@amlogic.com>
 *         Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __TVIN_H
#define __TVIN_H

#include <plat/io.h>
#include <linux/amlogic/amports/cm.h>

#define R_CBUS_REG(reg) aml_read_reg32(CBUS_REG_ADDR(reg))
#define W_CBUS_REG(reg, val) aml_write_reg32(CBUS_REG_ADDR(reg), val)
#define R_CBUS_BIT(reg, start, len) \
	aml_get_reg32_bits(CBUS_REG_ADDR(reg), start, len)
#define W_CBUS_BIT(reg, val, start, len) \
	aml_set_reg32_bits(CBUS_REG_ADDR(reg), val, start, len)

#define R_APB_REG(reg) aml_read_reg32(APB_REG_ADDR(reg))
#define W_APB_REG(reg, val) aml_write_reg32(APB_REG_ADDR(reg), val)
#define R_APB_BIT(reg, start, len) \
	aml_get_reg32_bits(APB_REG_ADDR(reg), start, len)
#define W_APB_BIT(reg, val, start, len) \
	aml_set_reg32_bits(APB_REG_ADDR(reg), val, start, len)


enum {
    MEMP_VDIN_WITHOUT_3D = 0,
    MEMP_VDIN_WITH_3D,
    MEMP_DCDR_WITHOUT_3D,
    MEMP_DCDR_WITH_3D,
    MEMP_ATV_WITHOUT_3D,
    MEMP_ATV_WITH_3D,
};

// ***************************************************************************
// *** TVIN general definition/enum/struct ***********************************
// ***************************************************************************
/* tvin input port select */
typedef enum tvin_port_e {
        TVIN_PORT_NULL    = 0x00000000,
        TVIN_PORT_MPEG0   = 0x00000100,
        TVIN_PORT_BT656   = 0x00000200,
        TVIN_PORT_BT601,
        TVIN_PORT_CAMERA,
        TVIN_PORT_VGA0    = 0x00000400,
        TVIN_PORT_VGA1,
        TVIN_PORT_VGA2,
        TVIN_PORT_VGA3,
        TVIN_PORT_VGA4,
        TVIN_PORT_VGA5,
        TVIN_PORT_VGA6,
        TVIN_PORT_VGA7,
        TVIN_PORT_COMP0   = 0x00000800,
        TVIN_PORT_COMP1,
        TVIN_PORT_COMP2,
        TVIN_PORT_COMP3,
        TVIN_PORT_COMP4,
        TVIN_PORT_COMP5,
        TVIN_PORT_COMP6,
        TVIN_PORT_COMP7,
        TVIN_PORT_CVBS0   = 0x00001000,
        TVIN_PORT_CVBS1,
        TVIN_PORT_CVBS2,
        TVIN_PORT_CVBS3,
        TVIN_PORT_CVBS4,
        TVIN_PORT_CVBS5,
        TVIN_PORT_CVBS6,
        TVIN_PORT_CVBS7,
        TVIN_PORT_SVIDEO0 = 0x00002000,
        TVIN_PORT_SVIDEO1,
        TVIN_PORT_SVIDEO2,
        TVIN_PORT_SVIDEO3,
        TVIN_PORT_SVIDEO4,
        TVIN_PORT_SVIDEO5,
        TVIN_PORT_SVIDEO6,
        TVIN_PORT_SVIDEO7,
        TVIN_PORT_HDMI0   = 0x00004000,
        TVIN_PORT_HDMI1,
        TVIN_PORT_HDMI2,
        TVIN_PORT_HDMI3,
        TVIN_PORT_HDMI4,
        TVIN_PORT_HDMI5,
        TVIN_PORT_HDMI6,
        TVIN_PORT_HDMI7,
        TVIN_PORT_DVIN0   = 0x00008000,
        TVIN_PORT_VIU     = 0x0000C000,
        TVIN_PORT_MIPI    = 0x00010000,
        TVIN_PORT_ISP     = 0x00020000,
        TVIN_PORT_MAX     = 0x80000000,
} tvin_port_t;

const char * tvin_port_str(enum tvin_port_e port);


/* tvin signal format table */
typedef enum tvin_sig_fmt_e {
        TVIN_SIG_FMT_NULL = 0,
        //VGA Formats
        TVIN_SIG_FMT_VGA_512X384P_60HZ_D147             = 0x001,
        TVIN_SIG_FMT_VGA_560X384P_60HZ_D147             = 0x002,
        TVIN_SIG_FMT_VGA_640X200P_59HZ_D924             = 0x003,
        TVIN_SIG_FMT_VGA_640X350P_85HZ_D080             = 0x004,
        TVIN_SIG_FMT_VGA_640X400P_59HZ_D940             = 0x005,
        TVIN_SIG_FMT_VGA_640X400P_85HZ_D080             = 0x006,
        TVIN_SIG_FMT_VGA_640X400P_59HZ_D638             = 0x007,
        TVIN_SIG_FMT_VGA_640X400P_56HZ_D416             = 0x008,
        TVIN_SIG_FMT_VGA_640X480P_66HZ_D619             = 0x009,
        TVIN_SIG_FMT_VGA_640X480P_66HZ_D667             = 0x00a,
        TVIN_SIG_FMT_VGA_640X480P_59HZ_D940             = 0x00b,
        TVIN_SIG_FMT_VGA_640X480P_60HZ_D000             = 0x00c,
        TVIN_SIG_FMT_VGA_640X480P_72HZ_D809             = 0x00d,
        TVIN_SIG_FMT_VGA_640X480P_75HZ_D000_A           = 0x00e,
        TVIN_SIG_FMT_VGA_640X480P_85HZ_D008             = 0x00f,
        TVIN_SIG_FMT_VGA_640X480P_59HZ_D638             = 0x010,
        TVIN_SIG_FMT_VGA_640X480P_75HZ_D000_B           = 0x011,
        TVIN_SIG_FMT_VGA_640X870P_75HZ_D000             = 0x012,
        TVIN_SIG_FMT_VGA_720X350P_70HZ_D086             = 0x013,
        TVIN_SIG_FMT_VGA_720X400P_85HZ_D039             = 0x014,
        TVIN_SIG_FMT_VGA_720X400P_70HZ_D086             = 0x015,
        TVIN_SIG_FMT_VGA_720X400P_87HZ_D849             = 0x016,
        TVIN_SIG_FMT_VGA_720X400P_59HZ_D940             = 0x017,
        TVIN_SIG_FMT_VGA_720X480P_59HZ_D940             = 0x018,
        TVIN_SIG_FMT_VGA_768X480P_59HZ_D896             = 0x019,
        TVIN_SIG_FMT_VGA_800X600P_56HZ_D250             = 0x01a,
        TVIN_SIG_FMT_VGA_800X600P_60HZ_D000             = 0x01b,
        TVIN_SIG_FMT_VGA_800X600P_60HZ_D000_A           = 0x01c,
        TVIN_SIG_FMT_VGA_800X600P_60HZ_D317             = 0x01d,
        TVIN_SIG_FMT_VGA_800X600P_72HZ_D188             = 0x01e,
        TVIN_SIG_FMT_VGA_800X600P_75HZ_D000             = 0x01f,
        TVIN_SIG_FMT_VGA_800X600P_85HZ_D061             = 0x020,
        TVIN_SIG_FMT_VGA_832X624P_75HZ_D087             = 0x021,
        TVIN_SIG_FMT_VGA_848X480P_84HZ_D751             = 0x022,
        TVIN_SIG_FMT_VGA_960X600P_59HZ_D635             = 0x023,
        TVIN_SIG_FMT_VGA_1024X768P_59HZ_D278            = 0x024,
        TVIN_SIG_FMT_VGA_1024X768P_60HZ_D000            = 0x025,
        TVIN_SIG_FMT_VGA_1024X768P_60HZ_D000_A          = 0x026,
        TVIN_SIG_FMT_VGA_1024X768P_60HZ_D000_B          = 0x027,
        TVIN_SIG_FMT_VGA_1024X768P_74HZ_D927            = 0x028,
        TVIN_SIG_FMT_VGA_1024X768P_60HZ_D004            = 0x029,
        TVIN_SIG_FMT_VGA_1024X768P_70HZ_D069            = 0x02a,
        TVIN_SIG_FMT_VGA_1024X768P_75HZ_D029            = 0x02b,
        TVIN_SIG_FMT_VGA_1024X768P_84HZ_D997            = 0x02c,
        TVIN_SIG_FMT_VGA_1024X768P_74HZ_D925            = 0x02d,
        TVIN_SIG_FMT_VGA_1024X768P_60HZ_D020            = 0x02e,
        TVIN_SIG_FMT_VGA_1024X768P_70HZ_D008            = 0x02f,
        TVIN_SIG_FMT_VGA_1024X768P_75HZ_D782            = 0x030,
        TVIN_SIG_FMT_VGA_1024X768P_77HZ_D069            = 0x031,
        TVIN_SIG_FMT_VGA_1024X768P_71HZ_D799            = 0x032,
        TVIN_SIG_FMT_VGA_1024X1024P_60HZ_D000           = 0x033,
        TVIN_SIG_FMT_VGA_1152X864P_60HZ_D000            = 0x034,
        TVIN_SIG_FMT_VGA_1152X864P_70HZ_D012            = 0x035,
        TVIN_SIG_FMT_VGA_1152X864P_75HZ_D000            = 0x036,
        TVIN_SIG_FMT_VGA_1152X864P_84HZ_D999            = 0x037,
        TVIN_SIG_FMT_VGA_1152X870P_75HZ_D062            = 0x038,
        TVIN_SIG_FMT_VGA_1152X900P_65HZ_D950            = 0x039,
        TVIN_SIG_FMT_VGA_1152X900P_66HZ_D004            = 0x03a,
        TVIN_SIG_FMT_VGA_1152X900P_76HZ_D047            = 0x03b,
        TVIN_SIG_FMT_VGA_1152X900P_76HZ_D149            = 0x03c,
        TVIN_SIG_FMT_VGA_1280X720P_59HZ_D855            = 0x03d,
        TVIN_SIG_FMT_VGA_1280X720P_60HZ_D000_A          = 0x03e,
        TVIN_SIG_FMT_VGA_1280X720P_60HZ_D000_B          = 0x03f,
        TVIN_SIG_FMT_VGA_1280X720P_60HZ_D000_C          = 0x040,
        TVIN_SIG_FMT_VGA_1280X720P_60HZ_D000_D          = 0x041,
        TVIN_SIG_FMT_VGA_1280X768P_59HZ_D870            = 0x042,
        TVIN_SIG_FMT_VGA_1280X768P_59HZ_D995            = 0x043,
        TVIN_SIG_FMT_VGA_1280X768P_60HZ_D100            = 0x044,
        TVIN_SIG_FMT_VGA_1280X768P_85HZ_D000            = 0x045,
        TVIN_SIG_FMT_VGA_1280X768P_74HZ_D893            = 0x046,
        TVIN_SIG_FMT_VGA_1280X768P_84HZ_D837            = 0x047,
        TVIN_SIG_FMT_VGA_1280X800P_59HZ_D810            = 0x048,
        TVIN_SIG_FMT_VGA_1280X800P_59HZ_D810_A          = 0x049,
        TVIN_SIG_FMT_VGA_1280X800P_60HZ_D000            = 0x04a,
        TVIN_SIG_FMT_VGA_1280X800P_85HZ_D000            = 0x04b,
        TVIN_SIG_FMT_VGA_1280X960P_60HZ_D000            = 0x04c,
        TVIN_SIG_FMT_VGA_1280X960P_60HZ_D000_A          = 0x04d,
        TVIN_SIG_FMT_VGA_1280X960P_75HZ_D000            = 0x04e,
        TVIN_SIG_FMT_VGA_1280X960P_85HZ_D002            = 0x04f,
        TVIN_SIG_FMT_VGA_1280X1024P_60HZ_D020           = 0x050,
        TVIN_SIG_FMT_VGA_1280X1024P_60HZ_D020_A         = 0x051,
        TVIN_SIG_FMT_VGA_1280X1024P_75HZ_D025           = 0x052,
        TVIN_SIG_FMT_VGA_1280X1024P_85HZ_D024           = 0x053,
        TVIN_SIG_FMT_VGA_1280X1024P_59HZ_D979           = 0x054,
        TVIN_SIG_FMT_VGA_1280X1024P_72HZ_D005           = 0x055,
        TVIN_SIG_FMT_VGA_1280X1024P_60HZ_D002           = 0x056,
        TVIN_SIG_FMT_VGA_1280X1024P_67HZ_D003           = 0x057,
        TVIN_SIG_FMT_VGA_1280X1024P_74HZ_D112           = 0x058,
        TVIN_SIG_FMT_VGA_1280X1024P_76HZ_D179           = 0x059,
        TVIN_SIG_FMT_VGA_1280X1024P_66HZ_D718           = 0x05a,
        TVIN_SIG_FMT_VGA_1280X1024P_66HZ_D677           = 0x05b,
        TVIN_SIG_FMT_VGA_1280X1024P_76HZ_D107           = 0x05c,
        TVIN_SIG_FMT_VGA_1280X1024P_59HZ_D996           = 0x05d,
        TVIN_SIG_FMT_VGA_1280X1024P_60HZ_D000           = 0x05e,
        TVIN_SIG_FMT_VGA_1360X768P_59HZ_D799            = 0x05f,
        TVIN_SIG_FMT_VGA_1360X768P_60HZ_D015            = 0x060,
        TVIN_SIG_FMT_VGA_1360X768P_60HZ_D015_A          = 0x061,
        TVIN_SIG_FMT_VGA_1360X850P_60HZ_D000            = 0x062,
        TVIN_SIG_FMT_VGA_1360X1024P_60HZ_D000           = 0x063,
        TVIN_SIG_FMT_VGA_1366X768P_59HZ_D790            = 0x064,
        TVIN_SIG_FMT_VGA_1366X768P_60HZ_D000            = 0x065,
        TVIN_SIG_FMT_VGA_1400X1050P_59HZ_D978           = 0x066,
        TVIN_SIG_FMT_VGA_1440X900P_59HZ_D887            = 0x067,
        TVIN_SIG_FMT_VGA_1440X1080P_60HZ_D000           = 0x068,
        TVIN_SIG_FMT_VGA_1600X900P_60HZ_D000            = 0x069,
        TVIN_SIG_FMT_VGA_1600X1024P_60HZ_D000           = 0x06a,
        TVIN_SIG_FMT_VGA_1600X1200P_59HZ_D869           = 0x06b,
        TVIN_SIG_FMT_VGA_1600X1200P_60HZ_D000           = 0x06c,
        TVIN_SIG_FMT_VGA_1600X1200P_65HZ_D000           = 0x06d,
        TVIN_SIG_FMT_VGA_1600X1200P_70HZ_D000           = 0x06e,
        TVIN_SIG_FMT_VGA_1680X1050P_59HZ_D954           = 0x06f,
        TVIN_SIG_FMT_VGA_1680X1080P_60HZ_D000           = 0x070,
        TVIN_SIG_FMT_VGA_1920X1080P_49HZ_D929           = 0x071,
        TVIN_SIG_FMT_VGA_1920X1080P_59HZ_D963_A         = 0x072,
        TVIN_SIG_FMT_VGA_1920X1080P_59HZ_D963           = 0x073,
        TVIN_SIG_FMT_VGA_1920X1080P_60HZ_D000           = 0x074,
        TVIN_SIG_FMT_VGA_1920X1200P_59HZ_D950           = 0x075,
        TVIN_SIG_FMT_VGA_1024X768P_60HZ_D000_C          = 0x076,
        TVIN_SIG_FMT_VGA_1024X768P_60HZ_D000_D          = 0x077,
        TVIN_SIG_FMT_VGA_1920X1200P_59HZ_D988           = 0x078,
        TVIN_SIG_FMT_VGA_1400X900P_60HZ_D000            = 0x079,
        TVIN_SIG_FMT_VGA_1680X1050P_60HZ_D000           = 0x07a,
        TVIN_SIG_FMT_VGA_800X600P_60HZ_D062             = 0x07b,
        TVIN_SIG_FMT_VGA_800X600P_60HZ_317_B            = 0x07c,
        TVIN_SIG_FMT_VGA_RESERVE8                       = 0x07d,
        TVIN_SIG_FMT_VGA_RESERVE9                       = 0x07e,
        TVIN_SIG_FMT_VGA_RESERVE10                      = 0x07f,
        TVIN_SIG_FMT_VGA_RESERVE11                      = 0x080,
        TVIN_SIG_FMT_VGA_RESERVE12                      = 0x081,
        TVIN_SIG_FMT_VGA_MAX                            = 0x082,
        TVIN_SIG_FMT_VGA_THRESHOLD                      = 0x200,
        //Component Formats
        TVIN_SIG_FMT_COMP_480P_60HZ_D000                = 0x201,
        TVIN_SIG_FMT_COMP_480I_59HZ_D940                = 0x202,
        TVIN_SIG_FMT_COMP_576P_50HZ_D000                = 0x203,
        TVIN_SIG_FMT_COMP_576I_50HZ_D000                = 0x204,
        TVIN_SIG_FMT_COMP_720P_59HZ_D940                = 0x205,
        TVIN_SIG_FMT_COMP_720P_50HZ_D000                = 0x206,
        TVIN_SIG_FMT_COMP_1080P_23HZ_D976               = 0x207,
        TVIN_SIG_FMT_COMP_1080P_24HZ_D000               = 0x208,
        TVIN_SIG_FMT_COMP_1080P_25HZ_D000               = 0x209,
        TVIN_SIG_FMT_COMP_1080P_30HZ_D000               = 0x20a,
        TVIN_SIG_FMT_COMP_1080P_50HZ_D000               = 0x20b,
        TVIN_SIG_FMT_COMP_1080P_60HZ_D000               = 0x20c,
        TVIN_SIG_FMT_COMP_1080I_47HZ_D952               = 0x20d,
        TVIN_SIG_FMT_COMP_1080I_48HZ_D000               = 0x20e,
        TVIN_SIG_FMT_COMP_1080I_50HZ_D000_A             = 0x20f,
        TVIN_SIG_FMT_COMP_1080I_50HZ_D000_B             = 0x210,
        TVIN_SIG_FMT_COMP_1080I_50HZ_D000_C             = 0x211,
        TVIN_SIG_FMT_COMP_1080I_60HZ_D000               = 0x212,
        TVIN_SIG_FMT_COMP_MAX                           = 0x213,
        TVIN_SIG_FMT_COMP_THRESHOLD                     = 0x400,
        //HDMI Formats
        TVIN_SIG_FMT_HDMI_640X480P_60HZ                 = 0x401,
        TVIN_SIG_FMT_HDMI_720X480P_60HZ                 = 0x402,
        TVIN_SIG_FMT_HDMI_1280X720P_60HZ                = 0x403,
        TVIN_SIG_FMT_HDMI_1920X1080I_60HZ               = 0x404,
        TVIN_SIG_FMT_HDMI_1440X480I_60HZ                = 0x405,
        TVIN_SIG_FMT_HDMI_1440X240P_60HZ                = 0x406,
        TVIN_SIG_FMT_HDMI_2880X480I_60HZ                = 0x407,
        TVIN_SIG_FMT_HDMI_2880X240P_60HZ                = 0x408,
        TVIN_SIG_FMT_HDMI_1440X480P_60HZ                = 0x409,
        TVIN_SIG_FMT_HDMI_1920X1080P_60HZ               = 0x40a,
        TVIN_SIG_FMT_HDMI_720X576P_50HZ                 = 0x40b,
        TVIN_SIG_FMT_HDMI_1280X720P_50HZ                = 0x40c,
        TVIN_SIG_FMT_HDMI_1920X1080I_50HZ_A             = 0x40d,
        TVIN_SIG_FMT_HDMI_1440X576I_50HZ                = 0x40e,
        TVIN_SIG_FMT_HDMI_1440X288P_50HZ                = 0x40f,
        TVIN_SIG_FMT_HDMI_2880X576I_50HZ                = 0x410,
        TVIN_SIG_FMT_HDMI_2880X288P_50HZ                = 0x411,
        TVIN_SIG_FMT_HDMI_1440X576P_50HZ                = 0x412,
        TVIN_SIG_FMT_HDMI_1920X1080P_50HZ               = 0x413,
        TVIN_SIG_FMT_HDMI_1920X1080P_24HZ               = 0x414,
        TVIN_SIG_FMT_HDMI_1920X1080P_25HZ               = 0x415,
        TVIN_SIG_FMT_HDMI_1920X1080P_30HZ               = 0x416,
        TVIN_SIG_FMT_HDMI_2880X480P_60HZ                = 0x417,
        TVIN_SIG_FMT_HDMI_2880X576P_60HZ                = 0x418,
        TVIN_SIG_FMT_HDMI_1920X1080I_50HZ_B             = 0x419,
        TVIN_SIG_FMT_HDMI_1920X1080I_100HZ              = 0x41a,
        TVIN_SIG_FMT_HDMI_1280X720P_100HZ               = 0x41b,
        TVIN_SIG_FMT_HDMI_720X576P_100HZ                = 0x41c,
        TVIN_SIG_FMT_HDMI_1440X576I_100HZ               = 0x41d,
        TVIN_SIG_FMT_HDMI_1920X1080I_120HZ              = 0x41e,
        TVIN_SIG_FMT_HDMI_1280X720P_120HZ               = 0x41f,
        TVIN_SIG_FMT_HDMI_720X480P_120HZ                = 0x420,
        TVIN_SIG_FMT_HDMI_1440X480I_120HZ               = 0x421,
        TVIN_SIG_FMT_HDMI_720X576P_200HZ                = 0x422,
        TVIN_SIG_FMT_HDMI_1440X576I_200HZ               = 0x423,
        TVIN_SIG_FMT_HDMI_720X480P_240HZ                = 0x424,
        TVIN_SIG_FMT_HDMI_1440X480I_240HZ               = 0x425,
        TVIN_SIG_FMT_HDMI_1280X720P_24HZ                = 0x426,
        TVIN_SIG_FMT_HDMI_1280X720P_25HZ                = 0x427,
        TVIN_SIG_FMT_HDMI_1280X720P_30HZ                = 0x428,
        TVIN_SIG_FMT_HDMI_1920X1080P_120HZ              = 0x429,
        TVIN_SIG_FMT_HDMI_1920X1080P_100HZ              = 0x42a,
        TVIN_SIG_FMT_HDMI_1280X720P_60HZ_FRAME_PACKING  = 0x42b,
        TVIN_SIG_FMT_HDMI_1280X720P_50HZ_FRAME_PACKING  = 0x42c,
        TVIN_SIG_FMT_HDMI_1280X720P_24HZ_FRAME_PACKING  = 0x42d,
        TVIN_SIG_FMT_HDMI_1280X720P_30HZ_FRAME_PACKING  = 0x42e,
        TVIN_SIG_FMT_HDMI_1920X1080I_60HZ_FRAME_PACKING = 0x42f,
        TVIN_SIG_FMT_HDMI_1920X1080I_50HZ_FRAME_PACKING = 0x430,
        TVIN_SIG_FMT_HDMI_1920X1080P_24HZ_FRAME_PACKING = 0x431,
        TVIN_SIG_FMT_HDMI_1920X1080P_30HZ_FRAME_PACKING = 0x432,
        TVIN_SIG_FMT_HDMI_800X600_00HZ                  = 0x433,
        TVIN_SIG_FMT_HDMI_1024X768_00HZ                 = 0x434,
        TVIN_SIG_FMT_HDMI_720X400_00HZ                  = 0x435,
        TVIN_SIG_FMT_HDMI_1280X768_00HZ                 = 0x436,
        TVIN_SIG_FMT_HDMI_1280X800_00HZ                 = 0x437,
        TVIN_SIG_FMT_HDMI_1280X960_00HZ                 = 0x438,
        TVIN_SIG_FMT_HDMI_1280X1024_00HZ                = 0x439,
        TVIN_SIG_FMT_HDMI_1360X768_00HZ                 = 0x43a,
        TVIN_SIG_FMT_HDMI_1366X768_00HZ                 = 0x43b,
        TVIN_SIG_FMT_HDMI_1600X1200_00HZ                = 0x43c,
        TVIN_SIG_FMT_HDMI_1920X1200_00HZ                = 0x43d,
        TVIN_SIG_FMT_HDMI_1440X900_00HZ                 = 0x43e,
        TVIN_SIG_FMT_HDMI_1400X1050_00HZ                = 0x43f,
        TVIN_SIG_FMT_HDMI_1680X1050_00HZ                = 0x440,
            /* for alternative and 4k2k */
        TVIN_SIG_FMT_HDMI_1920X1080I_60HZ_ALTERNATIVE   = 0x441,
        TVIN_SIG_FMT_HDMI_1920X1080I_50HZ_ALTERNATIVE   = 0x442,
        TVIN_SIG_FMT_HDMI_1920X1080P_24HZ_ALTERNATIVE   = 0x443,
        TVIN_SIG_FMT_HDMI_1920X1080P_30HZ_ALTERNATIVE   = 0x444,
        TVIN_SIG_FMT_HDMI_3840_2160_00HZ                = 0x445,
        TVIN_SIG_FMT_HDMI_4096_2160_00HZ                = 0x446,
        TVIN_SIG_FMT_HDMI_RESERVE7                      = 0x447,
        TVIN_SIG_FMT_HDMI_RESERVE8                      = 0x448,
        TVIN_SIG_FMT_HDMI_RESERVE9                      = 0x449,
        TVIN_SIG_FMT_HDMI_RESERVE10                     = 0x44a,
        TVIN_SIG_FMT_HDMI_RESERVE11                     = 0x44b,
        TVIN_SIG_FMT_HDMI_720X480P_60HZ_FRAME_PACKING   = 0x44c,
        TVIN_SIG_FMT_HDMI_720X576P_50HZ_FRAME_PACKING   = 0x44d,
        TVIN_SIG_FMT_HDMI_MAX                           = 0x44e,
        TVIN_SIG_FMT_HDMI_THRESHOLD                     = 0x600,
        //Video Formats
        TVIN_SIG_FMT_CVBS_NTSC_M                        = 0x601,
        TVIN_SIG_FMT_CVBS_NTSC_443                      = 0x602,
        TVIN_SIG_FMT_CVBS_PAL_I                         = 0x603,
        TVIN_SIG_FMT_CVBS_PAL_M                         = 0x604,
        TVIN_SIG_FMT_CVBS_PAL_60                        = 0x605,
        TVIN_SIG_FMT_CVBS_PAL_CN                        = 0x606,
        TVIN_SIG_FMT_CVBS_SECAM                         = 0x607,
        TVIN_SIG_FMT_CVBS_MAX                           = 0x608,
        TVIN_SIG_FMT_CVBS_THRESHOLD                     = 0x800,
        //656 Formats
        TVIN_SIG_FMT_BT656IN_576I_50HZ                  = 0x801,
        TVIN_SIG_FMT_BT656IN_480I_60HZ                  = 0x802,
        //601 Formats
        TVIN_SIG_FMT_BT601IN_576I_50HZ                  = 0x803,
        TVIN_SIG_FMT_BT601IN_480I_60HZ                  = 0x804,
        //Camera Formats
        TVIN_SIG_FMT_CAMERA_640X480P_30HZ               = 0x805,
        TVIN_SIG_FMT_CAMERA_800X600P_30HZ               = 0x806,
        TVIN_SIG_FMT_CAMERA_1024X768P_30HZ              = 0x807,
        TVIN_SIG_FMT_CAMERA_1920X1080P_30HZ             = 0x808,
        TVIN_SIG_FMT_CAMERA_1280X720P_30HZ              = 0x809,
        TVIN_SIG_FMT_BT601_MAX                          = 0x80a,
        TVIN_SIG_FMT_BT601_THRESHOLD                    = 0xa00,
        TVIN_SIG_FMT_MAX,
} tvin_sig_fmt_t;

//tvin signal status
typedef enum tvin_sig_status_e {
        TVIN_SIG_STATUS_NULL = 0, // processing status from init to the finding of the 1st confirmed status
        TVIN_SIG_STATUS_NOSIG,    // no signal - physically no signal
        TVIN_SIG_STATUS_UNSTABLE, // unstable - physically bad signal
        TVIN_SIG_STATUS_NOTSUP,   // not supported - physically good signal & not supported
        TVIN_SIG_STATUS_STABLE,   // stable - physically good signal & supported
} tvin_sig_status_t;

const char *tvin_sig_status_str(enum tvin_sig_status_e status);

// tvin parameters
#define TVIN_PARM_FLAG_CAP      0x00000001 //tvin_parm_t.flag[ 0]: 1/enable or 0/disable frame capture function
#define TVIN_PARM_FLAG_CAL      0x00000002 //tvin_parm_t.flag[ 1]: 1/enable or 0/disable adc calibration
/*used for processing 3d in ppmgr set this flag to drop one field and send real height in vframe*/
#define TVIN_PARM_FLAG_2D_TO_3D 0x00000004 //tvin_parm_t.flag[ 2]: 1/enable or 0/disable 2D->3D mode

typedef enum tvin_trans_fmt {
        TVIN_TFMT_2D = 0,
        TVIN_TFMT_3D_LRH_OLOR,  // 1 Primary: Side-by-Side(Half) Odd/Left picture, Odd/Right p
        TVIN_TFMT_3D_LRH_OLER,  // 2 Primary: Side-by-Side(Half) Odd/Left picture, Even/Right picture
        TVIN_TFMT_3D_LRH_ELOR,  // 3 Primary: Side-by-Side(Half) Even/Left picture, Odd/Right picture
        TVIN_TFMT_3D_LRH_ELER,  // 4 Primary: Side-by-Side(Half) Even/Left picture, Even/Right picture
        TVIN_TFMT_3D_TB,   // 5 Primary: Top-and-Bottom
        TVIN_TFMT_3D_FP,   // 6 Primary: Frame Packing
        TVIN_TFMT_3D_FA,   // 7 Secondary: Field Alternative
        TVIN_TFMT_3D_LA,   // 8 Secondary: Line Alternative
        TVIN_TFMT_3D_LRF,  // 9 Secondary: Side-by-Side(Full)
        TVIN_TFMT_3D_LD,   // 10 Secondary: L+depth
        TVIN_TFMT_3D_LDGD, // 11 Secondary: L+depth+Graphics+Graphics-depth
        /* normal 3D format */
        TVIN_TFMT_3D_DET_TB,// 12
        TVIN_TFMT_3D_DET_LR,// 13
        TVIN_TFMT_3D_DET_INTERLACE,// 14
        TVIN_TFMT_3D_DET_CHESSBOARD,// 15
} tvin_trans_fmt_t;

const char *tvin_trans_fmt_str(enum tvin_trans_fmt trans_fmt);

typedef enum tvin_color_fmt_e {
        TVIN_RGB444 = 0,
        TVIN_YUV422, // 1
        TVIN_YUV444, // 2
        TVIN_YUYV422,// 3
        TVIN_YVYU422,// 4
        TVIN_UYVY422,// 5
        TVIN_VYUY422,// 6
        TVIN_NV12,   // 7
        TVIN_NV21,   // 8
        TVIN_BGGR,   // 9  raw data
        TVIN_RGGB,   // 10 raw data
        TVIN_GBRG,   // 11 raw data
        TVIN_GRBG,   // 12 raw data
        TVIN_COLOR_FMT_MAX,
}tvin_color_fmt_t;

const char *tvin_color_fmt_str(enum tvin_color_fmt_e color_fmt);
typedef enum tvin_scan_mode_e {
        TVIN_SCAN_MODE_NULL = 0,
        TVIN_SCAN_MODE_PROGRESSIVE,
        TVIN_SCAN_MODE_INTERLACED,
} tvin_scan_mode_t;

typedef struct tvin_info_s {
        enum tvin_trans_fmt    trans_fmt;
        enum tvin_sig_fmt_e    fmt;
        enum tvin_sig_status_e status;
		enum tvin_color_fmt_e  cfmt;
		unsigned int		   fps;
        unsigned int           reserved;
}tvin_info_t;

typedef struct tvin_buf_info_s {
        unsigned int vf_size;
        unsigned int buf_count;
        unsigned int buf_width;
        unsigned int buf_height;
        unsigned int buf_size;
        unsigned int wr_list_size;
} tvin_buf_info_t;

typedef struct tvin_video_buf_s {
        unsigned int index;
        unsigned int reserved;
} tvin_video_buf_t;

// hs=he=vs=ve=0 is to disable Cut Window
typedef struct tvin_cutwin_s {
        unsigned short hs;
        unsigned short he;
        unsigned short vs;
        unsigned short ve;
} tvin_cutwin_t;

typedef struct tvin_parm_s {
        int                         index;    // index of frontend for vdin
        enum tvin_port_e            port;     // must set port in IOCTL
        struct tvin_info_s          info;
        unsigned int                hist_pow;
        unsigned int                luma_sum;
        unsigned int                pixel_sum;
        unsigned short              histgram[64];
        unsigned int                flag;
	unsigned short              dest_width;//for vdin horizontal scale down
	unsigned short              dest_height;//for vdin vertical scale down
		bool                h_reverse;//for vdin horizontal reverse
		bool                v_reverse;//for vdin vertical reverse
        unsigned int                reserved;
} tvin_parm_t;



// ***************************************************************************
// *** AFE module definition/enum/struct *************************************
// ***************************************************************************

typedef enum tvafe_cmd_status_e {
        TVAFE_CMD_STATUS_IDLE = 0,   // idle, be ready for TVIN_IOC_S_AFE_VGA_AUTO command
        TVAFE_CMD_STATUS_PROCESSING, // TVIN_IOC_S_AFE_VGA_AUTO command is in process
        TVAFE_CMD_STATUS_SUCCESSFUL, // TVIN_IOC_S_AFE_VGA_AUTO command is done with success
        TVAFE_CMD_STATUS_FAILED,     // TVIN_IOC_S_AFE_VGA_AUTO command is done with failure
        TVAFE_CMD_STATUS_TERMINATED, // TVIN_IOC_S_AFE_VGA_AUTO command is terminated by others related
} tvafe_cmd_status_t;

typedef struct tvafe_vga_edid_s {
        unsigned char value[256]; //256 byte EDID
} tvafe_vga_edid_t;

typedef struct tvafe_comp_wss_s {
        unsigned int wss1[5];
        unsigned int wss2[5];
} tvafe_comp_wss_t;

typedef struct tvafe_vga_parm_s {
        signed short clk_step;  // clock < 0, tune down clock freq
        // clock > 0, tune up clock freq
        unsigned short phase;     // phase is 0~31, it is absolute value
        signed short hpos_step; // hpos_step < 0, shift display to left
        // hpos_step > 0, shift display to right
        signed short vpos_step; // vpos_step < 0, shift display to top
        // vpos_step > 0, shift display to bottom
        unsigned int   vga_in_clean;  // flage for vga clean screen
} tvafe_vga_parm_t;

#define TVAFE_ADC_CAL_VALID 0x00000001
typedef struct tvafe_adc_cal_s {
        // ADC A
        unsigned short a_analog_clamp;    // 0x00~0x7f
        unsigned short a_analog_gain;     // 0x00~0xff, means 0dB~6dB
        unsigned short a_digital_offset1; // offset for fine-tuning
        // s11.0:   signed value, 11 integer bits,  0 fraction bits
        unsigned short a_digital_gain;    // 0~3.999
        // u2.10: unsigned value,  2 integer bits, 10 fraction bits
        unsigned short a_digital_offset2; // offset for format
        // s11.0:   signed value, 11 integer bits,  0 fraction bits
        // ADC B
        unsigned short b_analog_clamp;    // ditto to ADC A
        unsigned short b_analog_gain;
        unsigned short b_digital_offset1;
        unsigned short b_digital_gain;
        unsigned short b_digital_offset2;
        // ADC C
        unsigned short c_analog_clamp;    // ditto to ADC A
        unsigned short c_analog_gain;
        unsigned short c_digital_offset1;
        unsigned short c_digital_gain;
        unsigned short c_digital_offset2;
        // ADC D
        unsigned short d_analog_clamp;    // ditto to ADC A
        unsigned short d_analog_gain;
        unsigned short d_digital_offset1;
        unsigned short d_digital_gain;
        unsigned short d_digital_offset2;
        unsigned int   reserved;          // bit[ 0]: TVAFE_ADC_CAL_VALID
} tvafe_adc_cal_t;

typedef struct tvafe_adc_cal_clamp_s {
	short a_analog_clamp_diff;
	short b_analog_clamp_diff;
	short c_analog_clamp_diff;
} tvafe_adc_cal_clamp_t;

typedef struct tvafe_adc_comp_cal_s {
    struct tvafe_adc_cal_s comp_cal_val[3];
} tvafe_adc_comp_cal_t;

typedef enum tvafe_cvbs_video_e {
        TVAFE_CVBS_VIDEO_HV_UNLOCKED = 0,
        TVAFE_CVBS_VIDEO_H_LOCKED,
        TVAFE_CVBS_VIDEO_V_LOCKED,
        TVAFE_CVBS_VIDEO_HV_LOCKED,
} tvafe_cvbs_video_t;

// for pin selection
typedef enum tvafe_adc_pin_e {
        TVAFE_ADC_PIN_NULL = 0,
	TVAFE_ADC_PIN_A_PGA_0	= 1,
	TVAFE_ADC_PIN_A_PGA_1	= 2,
	TVAFE_ADC_PIN_A_PGA_2	= 3,
	TVAFE_ADC_PIN_A_PGA_3	= 4,
	TVAFE_ADC_PIN_A_PGA_4	= 5,
	TVAFE_ADC_PIN_A_PGA_5	= 6,
	TVAFE_ADC_PIN_A_PGA_6	= 7,
	TVAFE_ADC_PIN_A_PGA_7	= 8,
	TVAFE_ADC_PIN_A_0	= 9,
	TVAFE_ADC_PIN_A_1	= 10,
	TVAFE_ADC_PIN_A_2	= 11,
	TVAFE_ADC_PIN_A_3	= 12,
	TVAFE_ADC_PIN_A_4	= 13,
	TVAFE_ADC_PIN_A_5	= 14,
	TVAFE_ADC_PIN_A_6	= 15,
	TVAFE_ADC_PIN_A_7	= 16,
	TVAFE_ADC_PIN_B_0	= 17,
	TVAFE_ADC_PIN_B_1	= 18,
	TVAFE_ADC_PIN_B_2	= 19,
	TVAFE_ADC_PIN_B_3	= 20,
	TVAFE_ADC_PIN_B_4	= 21,
	TVAFE_ADC_PIN_B_5	= 22,
	TVAFE_ADC_PIN_B_6	= 23,
	TVAFE_ADC_PIN_B_7	= 24,
	TVAFE_ADC_PIN_C_0	= 25,
	TVAFE_ADC_PIN_C_1	= 26,
	TVAFE_ADC_PIN_C_2	= 27,
	TVAFE_ADC_PIN_C_3	= 28,
	TVAFE_ADC_PIN_C_4	= 29,
	TVAFE_ADC_PIN_C_5	= 30,
	TVAFE_ADC_PIN_C_6	= 31,
	TVAFE_ADC_PIN_C_7	= 32,
	TVAFE_ADC_PIN_D_0	= 33,
	TVAFE_ADC_PIN_D_1	= 34,
	TVAFE_ADC_PIN_D_2	= 35,
	TVAFE_ADC_PIN_D_3	= 36,
	TVAFE_ADC_PIN_D_4	= 37,
	TVAFE_ADC_PIN_D_5	= 38,
	TVAFE_ADC_PIN_D_6	= 39,
	TVAFE_ADC_PIN_D_7	= 40,
	TVAFE_ADC_PIN_SOG_0	= 41,
	TVAFE_ADC_PIN_SOG_1	= 42,
	TVAFE_ADC_PIN_SOG_2	= 43,
	TVAFE_ADC_PIN_SOG_3	= 44,
	TVAFE_ADC_PIN_SOG_4	= 45,
	TVAFE_ADC_PIN_SOG_5	= 46,
	TVAFE_ADC_PIN_SOG_6	= 47,
	TVAFE_ADC_PIN_SOG_7	= 48,
	TVAFE_ADC_PIN_MAX,
} tvafe_adc_pin_t;

typedef enum tvafe_src_sig_e {
        CVBS0_Y = 0,
        CVBS0_SOG,
        CVBS1_Y,
        CVBS1_SOG,
        CVBS2_Y,
        CVBS2_SOG,
        CVBS3_Y,
        CVBS3_SOG,
        CVBS4_Y,
        CVBS4_SOG,
        CVBS5_Y,
        CVBS5_SOG,
        CVBS6_Y,
        CVBS6_SOG,
        CVBS7_Y,
        CVBS7_SOG,
        S_VIDEO0_Y,
        S_VIDEO0_C,
        S_VIDEO0_SOG,
        S_VIDEO1_Y,
        S_VIDEO1_C,
        S_VIDEO1_SOG,
        S_VIDEO2_Y,
        S_VIDEO2_C,
        S_VIDEO2_SOG,
        S_VIDEO3_Y,
        S_VIDEO3_C,
        S_VIDEO3_SOG,
        S_VIDEO4_Y,
        S_VIDEO4_C,
        S_VIDEO4_SOG,
        S_VIDEO5_Y,
        S_VIDEO5_C,
        S_VIDEO5_SOG,
        S_VIDEO6_Y,
        S_VIDEO6_C,
        S_VIDEO6_SOG,
        S_VIDEO7_Y,
        S_VIDEO7_C,
        S_VIDEO7_SOG,
        VGA0_G,
        VGA0_B,
        VGA0_R,
        VGA0_SOG,
        VGA1_G,
        VGA1_B,
        VGA1_R,
        VGA1_SOG,
        VGA2_G,
        VGA2_B,
        VGA2_R,
        VGA2_SOG,
        VGA3_G,
        VGA3_B,
        VGA3_R,
        VGA3_SOG,
        VGA4_G,
        VGA4_B,
        VGA4_R,
        VGA4_SOG,
        VGA5_G,
        VGA5_B,
        VGA5_R,
        VGA5_SOG,
        VGA6_G,
        VGA6_B,
        VGA6_R,
        VGA6_SOG,
        VGA7_G,
        VGA7_B,
        VGA7_R,
        VGA7_SOG,
        COMP0_Y,
        COMP0_PB,
        COMP0_PR,
        COMP0_SOG,
        COMP1_Y,
        COMP1_PB,
        COMP1_PR,
        COMP1_SOG,
        COMP2_Y,
        COMP2_PB,
        COMP2_PR,
        COMP2_SOG,
        COMP3_Y,
        COMP3_PB,
        COMP3_PR,
        COMP3_SOG,
        COMP4_Y,
        COMP4_PB,
        COMP4_PR,
        COMP4_SOG,
        COMP5_Y,
        COMP5_PB,
        COMP5_PR,
        COMP5_SOG,
        COMP6_Y,
        COMP6_PB,
        COMP6_PR,
        COMP6_SOG,
        COMP7_Y,
        COMP7_PB,
        COMP7_PR,
        COMP7_SOG,
        SCART0_G,
        SCART0_B,
        SCART0_R,
        SCART0_CVBS,
        SCART1_G,
        SCART1_B,
        SCART1_R,
        SCART1_CVBS,
        SCART2_G,
        SCART2_B,
        SCART2_R,
        SCART2_CVBS,
        SCART3_G,
        SCART3_B,
        SCART3_R,
        SCART3_CVBS,
        SCART4_G,
        SCART4_B,
        SCART4_R,
        SCART4_CVBS,
        SCART5_G,
        SCART5_B,
        SCART5_R,
        SCART5_CVBS,
        SCART6_G,
        SCART6_B,
        SCART6_R,
        SCART6_CVBS,
        SCART7_G,
        SCART7_B,
        SCART7_R,
        SCART7_CVBS,
        TVAFE_SRC_SIG_MAX_NUM,
} tvafe_src_sig_t;

typedef struct tvafe_pin_mux_s {
        enum tvafe_adc_pin_e pin[TVAFE_SRC_SIG_MAX_NUM];
} tvafe_pin_mux_t;

// ***************************************************************************
// *** IOCTL command definition **********************************************
// ***************************************************************************

#define TVIN_IOC_MAGIC 'T'

//GENERAL
#define TVIN_IOC_OPEN               _IOW(TVIN_IOC_MAGIC, 0x01, struct tvin_parm_s)
#define TVIN_IOC_START_DEC          _IOW(TVIN_IOC_MAGIC, 0x02, struct tvin_parm_s)
#define TVIN_IOC_STOP_DEC           _IO( TVIN_IOC_MAGIC, 0x03)
#define TVIN_IOC_CLOSE              _IO( TVIN_IOC_MAGIC, 0x04)
#define TVIN_IOC_G_PARM             _IOR(TVIN_IOC_MAGIC, 0x05, struct tvin_parm_s)
#define TVIN_IOC_S_PARM             _IOW(TVIN_IOC_MAGIC, 0x06, struct tvin_parm_s)
#define TVIN_IOC_G_SIG_INFO         _IOR(TVIN_IOC_MAGIC, 0x07, struct tvin_info_s)
#define TVIN_IOC_G_BUF_INFO         _IOR(TVIN_IOC_MAGIC, 0x08, struct tvin_buf_info_s)
#define TVIN_IOC_START_GET_BUF      _IO( TVIN_IOC_MAGIC, 0x09)
#define TVIN_IOC_GET_BUF            _IOR(TVIN_IOC_MAGIC, 0x10, struct tvin_video_buf_s)
#define TVIN_IOC_PAUSE_DEC          _IO(TVIN_IOC_MAGIC, 0x41)
#define TVIN_IOC_RESUME_DEC         _IO(TVIN_IOC_MAGIC, 0x42)
#define TVIN_IOC_VF_REG             _IO(TVIN_IOC_MAGIC, 0x43)
#define TVIN_IOC_VF_UNREG           _IO(TVIN_IOC_MAGIC, 0x44)
#define TVIN_IOC_FREEZE_VF          _IO(TVIN_IOC_MAGIC, 0x45)
#define TVIN_IOC_UNFREEZE_VF        _IO(TVIN_IOC_MAGIC, 0x46)


//TVAFE
#define TVIN_IOC_S_AFE_ADC_CAL      _IOW(TVIN_IOC_MAGIC, 0x11, struct tvafe_adc_cal_s)
#define TVIN_IOC_G_AFE_ADC_CAL      _IOR(TVIN_IOC_MAGIC, 0x12, struct tvafe_adc_cal_s)
#define TVIN_IOC_G_AFE_COMP_WSS     _IOR(TVIN_IOC_MAGIC, 0x13, struct tvafe_comp_wss_s)
#define TVIN_IOC_S_AFE_VGA_EDID     _IOW(TVIN_IOC_MAGIC, 0x14, struct tvafe_vga_edid_s)
#define TVIN_IOC_G_AFE_VGA_EDID     _IOR(TVIN_IOC_MAGIC, 0x15, struct tvafe_vga_edid_s)
#define TVIN_IOC_S_AFE_VGA_PARM     _IOW(TVIN_IOC_MAGIC, 0x16, struct tvafe_vga_parm_s)
#define TVIN_IOC_G_AFE_VGA_PARM     _IOR(TVIN_IOC_MAGIC, 0x17, struct tvafe_vga_parm_s)
#define TVIN_IOC_S_AFE_VGA_AUTO     _IO( TVIN_IOC_MAGIC, 0x18)
#define TVIN_IOC_G_AFE_CMD_STATUS   _IOR(TVIN_IOC_MAGIC, 0x19, enum tvafe_cmd_status_e)
#define TVIN_IOC_G_AFE_CVBS_LOCK    _IOR(TVIN_IOC_MAGIC, 0x1a, enum tvafe_cvbs_video_e)
#define TVIN_IOC_S_AFE_CVBS_STD     _IOW(TVIN_IOC_MAGIC, 0x1b, enum tvin_sig_fmt_e)
#define TVIN_IOC_CALLMASTER_SET     _IOW(TVIN_IOC_MAGIC, 0x1c, enum tvin_port_e)
#define TVIN_IOC_CALLMASTER_GET	    _IO( TVIN_IOC_MAGIC, 0x1d)
#define TVIN_IOC_S_AFE_ADC_COMP_CAL  _IOW(TVIN_IOC_MAGIC, 0x1e, struct tvafe_adc_comp_cal_s)
#define TVIN_IOC_G_AFE_ADC_COMP_CAL  _IOR(TVIN_IOC_MAGIC, 0x1f, struct tvafe_adc_comp_cal_s)
#define TVIN_IOC_LOAD_REG           _IOW(TVIN_IOC_MAGIC, 0x20, struct am_regs_s)
#define TVIN_IOC_S_AFE_ADC_DIFF     _IOW(TVIN_IOC_MAGIC, 0x21, struct tvafe_adc_cal_clamp_s)
#endif
