#ifndef __UINPUT_H_
#define __UINPUT_H_
/*
 *  User level driver support for input subsystem
 *
 * Heavily based on evdev.c by Vojtech Pavlik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Aristeu Sergio Rozanski Filho <aris@cathedrallabs.org>
 * 
 * Changes/Revisions:
 *	0.2	16/10/2004 (Micah Dowty <micah@navi.cx>)
 *		- added force feedback support
 *             - added UI_SET_PHYS
 *	0.1	20/06/2002
 *		- first public version
 */
#ifdef __KERNEL__
#define UINPUT_MINOR		223
#define UINPUT_NAME		"uinput"
#define UINPUT_BUFFER_SIZE	16
#define UINPUT_NUM_REQUESTS	16

enum uinput_state { UIST_NEW_DEVICE, UIST_SETUP_COMPLETE, UIST_CREATED };

struct uinput_request {
	int			id;
	int			code;	/* UI_FF_UPLOAD, UI_FF_ERASE */

	int			retval;
	struct completion	done;

	union {
		int		effect_id;
		struct ff_effect* effect;
	} u;
};

struct uinput_device {
	struct input_dev	*dev;
	struct semaphore	sem;
	enum uinput_state	state;
	wait_queue_head_t	waitq;
	unsigned char		ready;
	unsigned char		head;
	unsigned char		tail;
	struct input_event	buff[UINPUT_BUFFER_SIZE];

	struct uinput_request	*requests[UINPUT_NUM_REQUESTS];
	wait_queue_head_t	requests_waitq;
	spinlock_t		requests_lock;
};
#endif	/* __KERNEL__ */

struct uinput_ff_upload {
	int			request_id;
	int			retval;
	struct ff_effect	effect;
};

struct uinput_ff_erase {
	int			request_id;
	int			retval;
	int			effect_id;
};

/* ioctl */
#define UINPUT_IOCTL_BASE	'U'
#define UI_DEV_CREATE		_IO(UINPUT_IOCTL_BASE, 1)
#define UI_DEV_DESTROY		_IO(UINPUT_IOCTL_BASE, 2)

#define UI_SET_EVBIT		_IOW(UINPUT_IOCTL_BASE, 100, int)
#define UI_SET_KEYBIT		_IOW(UINPUT_IOCTL_BASE, 101, int)
#define UI_SET_RELBIT		_IOW(UINPUT_IOCTL_BASE, 102, int)
#define UI_SET_ABSBIT		_IOW(UINPUT_IOCTL_BASE, 103, int)
#define UI_SET_MSCBIT		_IOW(UINPUT_IOCTL_BASE, 104, int)
#define UI_SET_LEDBIT		_IOW(UINPUT_IOCTL_BASE, 105, int)
#define UI_SET_SNDBIT		_IOW(UINPUT_IOCTL_BASE, 106, int)
#define UI_SET_FFBIT		_IOW(UINPUT_IOCTL_BASE, 107, int)
#define UI_SET_PHYS		_IOW(UINPUT_IOCTL_BASE, 108, char*)
#define UI_SET_SWBIT		_IOW(UINPUT_IOCTL_BASE, 109, int)

#define UI_BEGIN_FF_UPLOAD	_IOWR(UINPUT_IOCTL_BASE, 200, struct uinput_ff_upload)
#define UI_END_FF_UPLOAD	_IOW(UINPUT_IOCTL_BASE, 201, struct uinput_ff_upload)
#define UI_BEGIN_FF_ERASE	_IOWR(UINPUT_IOCTL_BASE, 202, struct uinput_ff_erase)
#define UI_END_FF_ERASE		_IOW(UINPUT_IOCTL_BASE, 203, struct uinput_ff_erase)

/* To write a force-feedback-capable driver, the upload_effect
 * and erase_effect callbacks in input_dev must be implemented.
 * The uinput driver will generate a fake input event when one of
 * these callbacks are invoked. The userspace code then uses
 * ioctls to retrieve additional parameters and send the return code.
 * The callback blocks until this return code is sent.
 *
 * The described callback mechanism is only used if EV_FF is set.
 * Otherwise, default implementations of upload_effect and erase_effect
 * are used.
 *
 * To implement upload_effect():
 *   1. Wait for an event with type==EV_UINPUT and code==UI_FF_UPLOAD.
 *      A request ID will be given in 'value'.
 *   2. Allocate a uinput_ff_upload struct, fill in request_id with
 *      the 'value' from the EV_UINPUT event.
 *   3. Issue a UI_BEGIN_FF_UPLOAD ioctl, giving it the
 *      uinput_ff_upload struct. It will be filled in with the
 *      ff_effect passed to upload_effect().
 *   4. Perform the effect upload, and place the modified ff_effect
 *      and a return code back into the uinput_ff_upload struct.
 *   5. Issue a UI_END_FF_UPLOAD ioctl, also giving it the
 *      uinput_ff_upload_effect struct. This will complete execution
 *      of our upload_effect() handler.
 *
 * To implement erase_effect():
 *   1. Wait for an event with type==EV_UINPUT and code==UI_FF_ERASE.
 *      A request ID will be given in 'value'.
 *   2. Allocate a uinput_ff_erase struct, fill in request_id with
 *      the 'value' from the EV_UINPUT event.
 *   3. Issue a UI_BEGIN_FF_ERASE ioctl, giving it the
 *      uinput_ff_erase struct. It will be filled in with the
 *      effect ID passed to erase_effect().
 *   4. Perform the effect erasure, and place a return code back
 *      into the uinput_ff_erase struct.
 *      and a return code back into the uinput_ff_erase struct.
 *   5. Issue a UI_END_FF_ERASE ioctl, also giving it the
 *      uinput_ff_erase_effect struct. This will complete execution
 *      of our erase_effect() handler.
 */

/* This is the new event type, used only by uinput.
 * 'code' is UI_FF_UPLOAD or UI_FF_ERASE, and 'value'
 * is the unique request ID. This number was picked
 * arbitrarily, above EV_MAX (since the input system
 * never sees it) but in the range of a 16-bit int.
 */
#define EV_UINPUT		0x0101
#define UI_FF_UPLOAD		1
#define UI_FF_ERASE		2

#ifndef NBITS
#define NBITS(x) ((((x)-1)/(sizeof(long)*8))+1)
#endif	/* NBITS */

#define UINPUT_MAX_NAME_SIZE	80
struct uinput_user_dev {
	char name[UINPUT_MAX_NAME_SIZE];
	struct input_id id;
        int ff_effects_max;
        int absmax[ABS_MAX + 1];
        int absmin[ABS_MAX + 1];
        int absfuzz[ABS_MAX + 1];
        int absflat[ABS_MAX + 1];
};
#endif	/* __UINPUT_H_ */

