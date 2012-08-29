#ifndef __USBAUDIO_ENDPOINT_H
#define __USBAUDIO_ENDPOINT_H

#define SND_USB_ENDPOINT_TYPE_DATA     0
#define SND_USB_ENDPOINT_TYPE_SYNC     1

struct snd_usb_endpoint *snd_usb_add_endpoint(struct snd_usb_audio *chip,
					      struct usb_host_interface *alts,
					      int ep_num, int direction, int type);

int snd_usb_endpoint_set_params(struct snd_usb_endpoint *ep,
				struct snd_pcm_hw_params *hw_params,
				struct audioformat *fmt,
				struct snd_usb_endpoint *sync_ep);

int  snd_usb_endpoint_start(struct snd_usb_endpoint *ep, int can_sleep);
void snd_usb_endpoint_stop(struct snd_usb_endpoint *ep,
			   int force, int can_sleep, int wait);
int  snd_usb_endpoint_activate(struct snd_usb_endpoint *ep);
int  snd_usb_endpoint_deactivate(struct snd_usb_endpoint *ep);
void snd_usb_endpoint_free(struct list_head *head);

int snd_usb_endpoint_implict_feedback_sink(struct snd_usb_endpoint *ep);

void snd_usb_handle_sync_urb(struct snd_usb_endpoint *ep,
			     struct snd_usb_endpoint *sender,
			     const struct urb *urb);

#endif /* __USBAUDIO_ENDPOINT_H */
