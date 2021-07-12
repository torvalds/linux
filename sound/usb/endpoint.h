/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __USBAUDIO_ENDPOINT_H
#define __USBAUDIO_ENDPOINT_H

#define SND_USB_ENDPOINT_TYPE_DATA     0
#define SND_USB_ENDPOINT_TYPE_SYNC     1

struct snd_usb_endpoint *snd_usb_get_endpoint(struct snd_usb_audio *chip,
					      int ep_num);

int snd_usb_add_endpoint(struct snd_usb_audio *chip, int ep_num, int type);

struct snd_usb_endpoint *
snd_usb_endpoint_open(struct snd_usb_audio *chip,
		      const struct audioformat *fp,
		      const struct snd_pcm_hw_params *params,
		      bool is_sync_ep);
void snd_usb_endpoint_close(struct snd_usb_audio *chip,
			    struct snd_usb_endpoint *ep);
int snd_usb_endpoint_configure(struct snd_usb_audio *chip,
			       struct snd_usb_endpoint *ep);

bool snd_usb_endpoint_compatible(struct snd_usb_audio *chip,
				 struct snd_usb_endpoint *ep,
				 const struct audioformat *fp,
				 const struct snd_pcm_hw_params *params);

void snd_usb_endpoint_set_sync(struct snd_usb_audio *chip,
			       struct snd_usb_endpoint *data_ep,
			       struct snd_usb_endpoint *sync_ep);
void snd_usb_endpoint_set_callback(struct snd_usb_endpoint *ep,
				   void (*prepare)(struct snd_usb_substream *subs,
						   struct urb *urb),
				   void (*retire)(struct snd_usb_substream *subs,
						  struct urb *urb),
				   struct snd_usb_substream *data_subs);

int snd_usb_endpoint_start(struct snd_usb_endpoint *ep);
void snd_usb_endpoint_stop(struct snd_usb_endpoint *ep);
void snd_usb_endpoint_sync_pending_stop(struct snd_usb_endpoint *ep);
void snd_usb_endpoint_suspend(struct snd_usb_endpoint *ep);
int  snd_usb_endpoint_activate(struct snd_usb_endpoint *ep);
void snd_usb_endpoint_release(struct snd_usb_endpoint *ep);
void snd_usb_endpoint_free_all(struct snd_usb_audio *chip);

int snd_usb_endpoint_implicit_feedback_sink(struct snd_usb_endpoint *ep);
int snd_usb_endpoint_next_packet_size(struct snd_usb_endpoint *ep,
				      struct snd_urb_ctx *ctx, int idx);

#endif /* __USBAUDIO_ENDPOINT_H */
