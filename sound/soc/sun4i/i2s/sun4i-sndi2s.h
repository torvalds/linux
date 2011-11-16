/*
********************************************************************************************************
*                          SUN4I----HDMI AUDIO
*                   (c) Copyright 2002-2004, All winners Co,Ld.
*                          All Right Reserved
*
* FileName: sun4i-codechip.h   author:chenpailin  date:2011-07-19
* Description:
* Others:
* History:
*   <author>      <time>      <version>   <desc>
*   chenpailin   2011-07-19     1.0      modify this module
********************************************************************************************************
*/
#ifndef SUN4I_SNDI2S_H_
#define SUN4I_SNDI2S_H_

struct sun4i_sndi2s_platform_data {
	int iis_bclk;
	int iis_ws;
	int iis_data;
	void (*power)(int);
	int model;
}

#endif