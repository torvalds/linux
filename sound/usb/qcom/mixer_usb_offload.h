/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __USB_OFFLOAD_MIXER_H
#define __USB_OFFLOAD_MIXER_H

int snd_usb_offload_create_ctl(struct snd_usb_audio *chip, struct device *bedev);

#endif /* __USB_OFFLOAD_MIXER_H */
