/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AK4396_H_INCLUDED
#define AK4396_H_INCLUDED

#define AK4396_WRITE		0x2000

#define AK4396_CONTROL_1	0
#define AK4396_CONTROL_2	1
#define AK4396_CONTROL_3	2
#define AK4396_LCH_ATT		3
#define AK4396_RCH_ATT		4

/* control 1 */
#define AK4396_RSTN		0x01
#define AK4396_DIF_MASK		0x0e
#define AK4396_DIF_16_LSB	0x00
#define AK4396_DIF_20_LSB	0x02
#define AK4396_DIF_24_MSB	0x04
#define AK4396_DIF_24_I2S	0x06
#define AK4396_DIF_24_LSB	0x08
#define AK4396_ACKS		0x80
/* control 2 */
#define AK4396_SMUTE		0x01
#define AK4396_DEM_MASK		0x06
#define AK4396_DEM_441		0x00
#define AK4396_DEM_OFF		0x02
#define AK4396_DEM_48		0x04
#define AK4396_DEM_32		0x06
#define AK4396_DFS_MASK		0x18
#define AK4396_DFS_NORMAL	0x00
#define AK4396_DFS_DOUBLE	0x08
#define AK4396_DFS_QUAD		0x10
#define AK4396_SLOW		0x20
#define AK4396_DZFM		0x40
#define AK4396_DZFE		0x80
/* control 3 */
#define AK4396_DZFB		0x04
#define AK4396_DCKB		0x10
#define AK4396_DCKS		0x20
#define AK4396_DSDM		0x40
#define AK4396_D_P_MASK		0x80
#define AK4396_PCM		0x00
#define AK4396_DSD		0x80

#endif
