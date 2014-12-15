#ifndef  AMLOGO_LOG_H
#define AMLOGO_LOG_H

#define DEBUG
#ifdef  DEBUG
#define  AMLOG   1
#define LOG_LEVEL_VAR amlog_level_logo
#define LOG_MASK_VAR amlog_mask_logo
#endif

#define  	LOG_LEVEL_MAX 	0xf
#define	LOG_MASK_ALL	0x0

#define  	LOG_LEVEL_HIGH    		0x00f
#define	LOG_LEVEL_1				0x001
#define 	LOG_LEVEL_LOW			0x000

#define LOG_LEVEL_DESC \
"[0x00]LOW[0X01]LEVEL1[0xf]HIGH"	
#define	LOG_MASK_LOADER		0x001
#define	LOG_MASK_PARSER		0x002
#define	LOG_MASK_DEVICE			0x004
#define	LOG_MASK_DESC \
"[01]LOADER[02]PARSER[04]DEVICE"	

#endif

