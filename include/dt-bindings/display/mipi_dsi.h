/* SPDX-License-Identifier: GPL-2.0 */
/*
drivers/video/rockchip/transmitter/mipi_dsi.h
*/
#ifndef MIPI_DSI_H_
#define MIPI_DSI_H_

#ifdef CONFIG_MIPI_DSI_FT
#include "..\..\common\config.h"
#endif

//DSI DATA TYPE
#define DTYPE_DCS_SWRITE_0P		0x05 
#define DTYPE_DCS_SWRITE_1P		0x15 
#define DTYPE_DCS_LWRITE		0x39 
#define DTYPE_GEN_LWRITE		0x29 
#define DTYPE_GEN_SWRITE_2P		0x23 
#define DTYPE_GEN_SWRITE_1P		0x13
#define DTYPE_GEN_SWRITE_0P		0x03

//command transmit mode
#define HSDT			0x00
#define LPDT			0x01

//DSI DATA TYPE FLAG
#define DATA_TYPE_DCS			0x00
#define DATA_TYPE_GEN			0x01

//Video Mode
#define VM_NBMWSP		0x00  //Non burst mode with sync pulses
#define VM_NBMWSE		0x01  //Non burst mode with sync events
#define VM_BM			0x02  //Burst mode

//Video Pixel Format
#define VPF_16BPP		0x00
#define VPF_18BPP		0x01	 //packed
#define VPF_18BPPL		0x02     //loosely packed
#define VPF_24BPP		0x03

//Display Command Set
#define dcs_enter_idle_mode 		0x39
#define dcs_enter_invert_mode 		0x21
#define dcs_enter_normal_mode 		0x13
#define dcs_enter_partial_mode  	0x12
#define dcs_enter_sleep_mode  		0x10
#define dcs_exit_idle_mode  		0x38
#define dcs_exit_invert_mode  		0x20
#define dcs_exit_sleep_mode  		0x11
#define dcs_get_address_mode  		0x0b
#define dcs_get_blue_channel  		0x08
#define dcs_get_diagnostic_result  	0x0f
#define dcs_get_display_mode  		0x0d
#define dcs_get_green_channel  		0x07
#define dcs_get_pixel_format  		0x0c
#define dcs_get_power_mode  		0x0a
#define dcs_get_red_channel 		0x06
#define dcs_get_scanline 	 		0x45
#define dcs_get_signal_mode  		0x0e
#define dcs_nop				 		0x00
#define dcs_read_DDB_continue  		0xa8
#define dcs_read_DDB_start  		0xa1
#define dcs_read_memory_continue  	0x3e
#define dcs_read_memory_start  		0x2e
#define dcs_set_address_mode  		0x36
#define dcs_set_column_address  	0x2a
#define dcs_set_display_off  		0x28
#define dcs_set_display_on  		0x29
#define dcs_set_gamma_curve  		0x26
#define dcs_set_page_address  		0x2b
#define dcs_set_partial_area  		0x30
#define dcs_set_pixel_format  		0x3a
#define dcs_set_scroll_area  		0x33
#define dcs_set_scroll_start  		0x37
#define dcs_set_tear_off 	 		0x34
#define dcs_set_tear_on 	 		0x35
#define dcs_set_tear_scanline  		0x44
#define dcs_soft_reset 		 		0x01
#define dcs_write_LUT 		 		0x2d
#define dcs_write_memory_continue  	0x3c
#define dcs_write_memory_start 		0x2c

#ifndef MHz
#define MHz   1000000
#endif


#if 0
typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

typedef signed long s64;
typedef unsigned long u64;
#endif


//iomux
#define OLD_RK_IOMUX 0


#endif /* end of MIPI_DSI_H_ */
