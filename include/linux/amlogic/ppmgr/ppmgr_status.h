/*******************************************************************
 *
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 2010/4/1   19:46
 *
 *******************************************************************/
extern int get_ppmgr_status(void);
extern void set_ppmgr_status(int flag);
#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
extern unsigned get_ppmgr_3dmode(void);
extern void set_ppmgr_3dmode(unsigned mode);
extern unsigned get_ppmgr_viewmode(void);
extern void set_ppmgr_viewmode(unsigned mode);
extern unsigned get_ppmgr_scaledown(void);
extern void set_ppmgr_scaledown(unsigned scale_down);
extern unsigned get_ppmgr_direction3d(void);
extern void set_ppmgr_direction3d(unsigned angle);
#endif
int get_use_prot(void);
