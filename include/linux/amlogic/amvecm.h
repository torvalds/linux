/*
 * AMLOGIC Audio/Video streaming port driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#ifndef AMVECM_H
#define AMVECM_H

#include "linux/amlogic/ve.h"
#include "linux/amlogic/cm.h"
#include <linux/amlogic/amstream.h>


struct ve_dnlp_s          video_ve_dnlp;
struct tcon_gamma_table_s video_gamma_table_r;
struct tcon_gamma_table_s video_gamma_table_g;
struct tcon_gamma_table_s video_gamma_table_b;
struct tcon_gamma_table_s video_gamma_table_r_adj;
struct tcon_gamma_table_s video_gamma_table_g_adj;
struct tcon_gamma_table_s video_gamma_table_b_adj;
struct tcon_rgb_ogo_s     video_rgb_ogo;

#define FLAG_RSV31              (1 << 31)
#define FLAG_RSV30              (1 << 30)
#define FLAG_VE_DNLP            (1 << 29)
#define FLAG_VE_NEW_DNLP        (1 << 28)
#define FLAG_RSV27              (1 << 27)
#define FLAG_RSV26              (1 << 26)
#define FLAG_RSV25              (1 << 25)
#define FLAG_RSV24              (1 << 24)
#define FLAG_RSV23              (1 << 23)
#define FLAG_RSV22              (1 << 22)
#define FLAG_RSV21              (1 << 21)
#define FLAG_RSV20              (1 << 20)
#define FLAG_VE_DNLP_EN         (1 << 19)
#define FLAG_VE_DNLP_DIS        (1 << 18)
#define FLAG_RSV17              (1 << 17)
#define FLAG_RSV16              (1 << 16)
#define FLAG_GAMMA_TABLE_EN     (1 << 15)
#define FLAG_GAMMA_TABLE_DIS    (1 << 14)
#define FLAG_GAMMA_TABLE_R      (1 << 13)
#define FLAG_GAMMA_TABLE_G      (1 << 12)
#define FLAG_GAMMA_TABLE_B      (1 << 11)
#define FLAG_RGB_OGO            (1 << 10)
#define FLAG_RSV9               (1 <<  9)
#define FLAG_RSV8               (1 <<  8)
#define FLAG_BRI_CON            (1 <<  7)
#define FLAG_LVDS_FREQ_SW       (1 <<  6)
#define FLAG_REG_MAP5           (1 <<  5)
#define FLAG_REG_MAP4           (1 <<  4)
#define FLAG_REG_MAP3           (1 <<  3)
#define FLAG_REG_MAP2           (1 <<  2)
#define FLAG_REG_MAP1           (1 <<  1)
#define FLAG_REG_MAP0           (1 <<  0)


#define AMVECM_IOC_MAGIC  'C'

#define AMVECM_IOC_VE_DNLP       _IOW(AMVECM_IOC_MAGIC, 0x21, struct ve_dnlp_s  )
#define AMVECM_IOC_G_HIST_AVG    _IOW(AMVECM_IOC_MAGIC, 0x22, struct ve_hist_s  )
#define AMVECM_IOC_VE_DNLP_EN     _IO(AMVECM_IOC_MAGIC, 0x23)
#define AMVECM_IOC_VE_DNLP_DIS    _IO(AMVECM_IOC_MAGIC, 0x24)
#define AMVECM_IOC_VE_NEW_DNLP   _IOW(AMVECM_IOC_MAGIC, 0x25, struct ve_dnlp_table_s  )


// VPP.CM IOCTL command list
#define AMVECM_IOC_LOAD_REG     _IOW(AMVECM_IOC_MAGIC, 0x30, struct am_regs_s)


// VPP.GAMMA IOCTL command list
#define AMVECM_IOC_GAMMA_TABLE_EN  _IO(AMVECM_IOC_MAGIC, 0x40)
#define AMVECM_IOC_GAMMA_TABLE_DIS _IO(AMVECM_IOC_MAGIC, 0x41)
#define AMVECM_IOC_GAMMA_TABLE_R  _IOW(AMVECM_IOC_MAGIC, 0x42, struct tcon_gamma_table_s)
#define AMVECM_IOC_GAMMA_TABLE_G  _IOW(AMVECM_IOC_MAGIC, 0x43, struct tcon_gamma_table_s)
#define AMVECM_IOC_GAMMA_TABLE_B  _IOW(AMVECM_IOC_MAGIC, 0x44, struct tcon_gamma_table_s)
#define AMVECM_IOC_S_RGB_OGO      _IOW(AMVECM_IOC_MAGIC, 0x45, struct tcon_rgb_ogo_s)
#define AMVECM_IOC_G_RGB_OGO      _IOR(AMVECM_IOC_MAGIC, 0x46, struct tcon_rgb_ogo_s)
#if (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8)
#undef WRITE_CBUS_REG
#undef WRITE_CBUS_REG_BITS
#undef READ_CBUS_REG
#undef READ_CBUS_REG_BITS

#define WRITE_CBUS_REG(x,val)				WRITE_VCBUS_REG(x,val)
#define WRITE_CBUS_REG_BITS(x,val,start,length)		WRITE_VCBUS_REG_BITS(x,val,start,length)
#define READ_CBUS_REG(x)				READ_VCBUS_REG(x)
#define READ_CBUS_REG_BITS(x,start,length)		READ_VCBUS_REG_BITS(x,start,length)
#endif

#endif /* AMVECM_H */

