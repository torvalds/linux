/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SONYLAPTOP_H_
#define _SONYLAPTOP_H_

#include <linux/types.h>

#ifdef __KERNEL__

/* used only for communication between v4l and sony-laptop */

#define SONY_PIC_COMMAND_GETCAMERA		 1	/* obsolete */
#define SONY_PIC_COMMAND_SETCAMERA		 2
#define SONY_PIC_COMMAND_GETCAMERABRIGHTNESS	 3	/* obsolete */
#define SONY_PIC_COMMAND_SETCAMERABRIGHTNESS	 4
#define SONY_PIC_COMMAND_GETCAMERACONTRAST	 5	/* obsolete */
#define SONY_PIC_COMMAND_SETCAMERACONTRAST	 6
#define SONY_PIC_COMMAND_GETCAMERAHUE		 7	/* obsolete */
#define SONY_PIC_COMMAND_SETCAMERAHUE		 8
#define SONY_PIC_COMMAND_GETCAMERACOLOR		 9	/* obsolete */
#define SONY_PIC_COMMAND_SETCAMERACOLOR		10
#define SONY_PIC_COMMAND_GETCAMERASHARPNESS	11	/* obsolete */
#define SONY_PIC_COMMAND_SETCAMERASHARPNESS	12
#define SONY_PIC_COMMAND_GETCAMERAPICTURE	13	/* obsolete */
#define SONY_PIC_COMMAND_SETCAMERAPICTURE	14
#define SONY_PIC_COMMAND_GETCAMERAAGC		15	/* obsolete */
#define SONY_PIC_COMMAND_SETCAMERAAGC		16
#define SONY_PIC_COMMAND_GETCAMERADIRECTION	17	/* obsolete */
#define SONY_PIC_COMMAND_GETCAMERAROMVERSION	18	/* obsolete */
#define SONY_PIC_COMMAND_GETCAMERAREVISION	19	/* obsolete */

int sony_pic_camera_command(int command, u8 value);

#endif	/* __KERNEL__ */

#endif /* _SONYLAPTOP_H_ */
