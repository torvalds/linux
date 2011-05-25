#ifndef __USBMIXER_H
#define __USBMIXER_H

struct usb_mixer_interface {
	struct snd_usb_audio *chip;
	struct list_head list;
	unsigned int ignore_ctl_error;
	struct urb *urb;
	/* array[MAX_ID_ELEMS], indexed by unit id */
	struct usb_mixer_elem_info **id_elems;

	/* the usb audio specification version this interface complies to */
	int protocol;

	/* Sound Blaster remote control stuff */
	const struct rc_config *rc_cfg;
	u32 rc_code;
	wait_queue_head_t rc_waitq;
	struct urb *rc_urb;
	struct usb_ctrlrequest *rc_setup_packet;
	u8 rc_buffer[6];

	u8 audigy2nx_leds[3];
	u8 xonar_u1_status;
};

#define MAX_CHANNELS	16	/* max logical channels */

enum {
	USB_MIXER_BOOLEAN,
	USB_MIXER_INV_BOOLEAN,
	USB_MIXER_S8,
	USB_MIXER_U8,
	USB_MIXER_S16,
	USB_MIXER_U16,
};

struct usb_mixer_elem_info {
	struct usb_mixer_interface *mixer;
	struct usb_mixer_elem_info *next_id_elem; /* list of controls with same id */
	struct snd_ctl_elem_id *elem_id;
	unsigned int id;
	unsigned int control;	/* CS or ICN (high byte) */
	unsigned int cmask; /* channel mask bitmap: 0 = master */
	unsigned int ch_readonly;
	unsigned int master_readonly;
	int channels;
	int val_type;
	int min, max, res;
	int dBmin, dBmax;
	int cached;
	int cache_val[MAX_CHANNELS];
	u8 initialized;
};

int snd_usb_create_mixer(struct snd_usb_audio *chip, int ctrlif,
			 int ignore_error);
void snd_usb_mixer_disconnect(struct list_head *p);

void snd_usb_mixer_notify_id(struct usb_mixer_interface *mixer, int unitid);

int snd_usb_mixer_set_ctl_value(struct usb_mixer_elem_info *cval,
				int request, int validx, int value_set);
void snd_usb_mixer_inactivate(struct usb_mixer_interface *mixer);
int snd_usb_mixer_activate(struct usb_mixer_interface *mixer);

int snd_usb_mixer_add_control(struct usb_mixer_interface *mixer,
			      struct snd_kcontrol *kctl);

#endif /* __USBMIXER_H */
