/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __USBMIXER_H
#define __USBMIXER_H

#include <sound/info.h>

struct media_mixer_ctl;

struct usbmix_connector_map {
	u8 id;
	u8 delegated_id;
	u8 control;
	u8 channel;
};

struct usb_mixer_interface {
	struct snd_usb_audio *chip;
	struct usb_host_interface *hostif;
	struct list_head list;
	unsigned int ignore_ctl_error;
	struct urb *urb;
	/* array[MAX_ID_ELEMS], indexed by unit id */
	struct usb_mixer_elem_list **id_elems;

	/* the usb audio specification version this interface complies to */
	int protocol;

	/* optional connector delegation map */
	const struct usbmix_connector_map *connector_map;

	/* Sound Blaster remote control stuff */
	const struct rc_config *rc_cfg;
	u32 rc_code;
	wait_queue_head_t rc_waitq;
	struct urb *rc_urb;
	struct usb_ctrlrequest *rc_setup_packet;
	u8 rc_buffer[6];
	struct media_mixer_ctl *media_mixer_ctl;

	bool disconnected;

	void *private_data;
	void (*private_free)(struct usb_mixer_interface *mixer);
	void (*private_suspend)(struct usb_mixer_interface *mixer);
};

#define MAX_CHANNELS	16	/* max logical channels */

enum {
	USB_MIXER_BOOLEAN,
	USB_MIXER_INV_BOOLEAN,
	USB_MIXER_S8,
	USB_MIXER_U8,
	USB_MIXER_S16,
	USB_MIXER_U16,
	USB_MIXER_S32,
	USB_MIXER_U32,
	USB_MIXER_BESPOKEN,	/* non-standard type */
};

typedef void (*usb_mixer_elem_dump_func_t)(struct snd_info_buffer *buffer,
					 struct usb_mixer_elem_list *list);
typedef int (*usb_mixer_elem_resume_func_t)(struct usb_mixer_elem_list *elem);

struct usb_mixer_elem_list {
	struct usb_mixer_interface *mixer;
	struct usb_mixer_elem_list *next_id_elem; /* list of controls with same id */
	struct snd_kcontrol *kctl;
	unsigned int id;
	bool is_std_info;
	usb_mixer_elem_dump_func_t dump;
	usb_mixer_elem_resume_func_t resume;
};

/* iterate over mixer element list of the given unit id */
#define for_each_mixer_elem(list, mixer, id)	\
	for ((list) = (mixer)->id_elems[id]; (list); (list) = (list)->next_id_elem)
#define mixer_elem_list_to_info(list) \
	container_of(list, struct usb_mixer_elem_info, head)

struct usb_mixer_elem_info {
	struct usb_mixer_elem_list head;
	unsigned int control;	/* CS or ICN (high byte) */
	unsigned int cmask; /* channel mask bitmap: 0 = master */
	unsigned int idx_off; /* Control index offset */
	unsigned int ch_readonly;
	unsigned int master_readonly;
	int channels;
	int val_type;
	int min, max, res;
	int max_exposed; /* control API exposes the value in 0..max_exposed */
	int dBmin, dBmax;
	int cached;
	int cache_val[MAX_CHANNELS];
	u8 initialized;
	u8 min_mute;
	void *private_data;
};

int snd_usb_create_mixer(struct snd_usb_audio *chip, int ctrlif);
void snd_usb_mixer_disconnect(struct usb_mixer_interface *mixer);

void snd_usb_mixer_notify_id(struct usb_mixer_interface *mixer, int unitid);

int snd_usb_mixer_set_ctl_value(struct usb_mixer_elem_info *cval,
				int request, int validx, int value_set);

int snd_usb_mixer_add_list(struct usb_mixer_elem_list *list,
			   struct snd_kcontrol *kctl,
			   bool is_std_info);

#define snd_usb_mixer_add_control(list, kctl) \
	snd_usb_mixer_add_list(list, kctl, true)

void snd_usb_mixer_elem_init_std(struct usb_mixer_elem_list *list,
				 struct usb_mixer_interface *mixer,
				 int unitid);

int snd_usb_mixer_vol_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			  unsigned int size, unsigned int __user *_tlv);

int snd_usb_mixer_suspend(struct usb_mixer_interface *mixer);
int snd_usb_mixer_resume(struct usb_mixer_interface *mixer);

int snd_usb_set_cur_mix_value(struct usb_mixer_elem_info *cval, int channel,
                             int index, int value);

int snd_usb_get_cur_mix_value(struct usb_mixer_elem_info *cval,
                             int channel, int index, int *value);

extern void snd_usb_mixer_elem_free(struct snd_kcontrol *kctl);

extern const struct snd_kcontrol_new *snd_usb_feature_unit_ctl;

#endif /* __USBMIXER_H */
