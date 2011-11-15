/*
********************************************************************************************************
*                          SUN4I----HDMI AUDIO
*                   (c) Copyright 2002-2004, All winners Co,Ld.
*                          All Right Reserved
*
* FileName: sun4i-sndhdmi.h   author:chenpailin   
* Description: 
* Others: 
* History:
*   <author>      <time>      <version>   <desc> 
*   chenpailin   2011-07-19     1.0      modify this module 
********************************************************************************************************
*/
#ifndef SUN4I_SNDHDMI_H_
#define SUN4I_SNDHDMI_H_

struct sun4i_sndhdmi_platform_data {
	int hdmiaudio_bclk;
	int hdmiaudio_ws;
	int hdmiaudio_data;
	void (*power)(int);
	int model;
}
#endif