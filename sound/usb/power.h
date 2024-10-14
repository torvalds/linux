/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __USBAUDIO_POWER_H
#define __USBAUDIO_POWER_H

struct snd_usb_power_domain {
	int pd_id;              /* UAC3 Power Domain ID */
	int pd_d1d0_rec;        /* D1 to D0 recovery time */
	int pd_d2d0_rec;        /* D2 to D0 recovery time */
	struct usb_host_interface *ctrl_iface; /* Control interface */
};

enum {
	UAC3_PD_STATE_D0,
	UAC3_PD_STATE_D1,
	UAC3_PD_STATE_D2,
};

int snd_usb_power_domain_set(struct snd_usb_audio *chip,
			     struct snd_usb_power_domain *pd,
			     unsigned char state);
struct snd_usb_power_domain *
snd_usb_find_power_domain(struct usb_host_interface *ctrl_iface,
			  unsigned char id);

int snd_usb_autoresume(struct snd_usb_audio *chip);
void snd_usb_autosuspend(struct snd_usb_audio *chip);

#endif /* __USBAUDIO_POWER_H */
