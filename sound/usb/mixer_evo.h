/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __USB_MIXER_EVO_H
#define __USB_MIXER_EVO_H

#include "mixer.h"

#define USB_AUDIENT_VID (0x2708)
#define USB_EVO4_PID (0x0006)

int snd_evo_controls_create(struct usb_mixer_interface *mixer);

#endif /* __USB_MIXER_EVO_H */
