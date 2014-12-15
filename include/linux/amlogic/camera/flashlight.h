/*******************************************************************
 *
 *  Copyright C 2011 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 2011/8/26   19:46
 *
 *******************************************************************/
#ifndef _VIDEO_AMLOGIC_FLASHLIGHT_INCLUDE_
#define _VIDEO_AMLOGIC_FLASHLIGHT_INCLUDE_
typedef struct {
	void (*flashlight_on)(void);
	void (*flashlight_off)(void);
}aml_plat_flashlight_data_t;

typedef enum {
	FLASHLIGHT_AUTO = 0,
	FLASHLIGHT_ON,
	FLASHLIGHT_OFF,
	FLASHLIGHT_TORCH,
	FLASHLIGHT_RED_EYE,
}aml_plat_flashlight_status_t;

#endif


