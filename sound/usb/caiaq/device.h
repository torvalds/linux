#ifndef CAIAQ_DEVICE_H
#define CAIAQ_DEVICE_H

#include "../usbaudio.h"

#define USB_VID_NATIVEINSTRUMENTS 0x17cc

#define USB_PID_RIGKONTROL2		0x1969
#define USB_PID_RIGKONTROL3		0x1940
#define USB_PID_KORECONTROLLER		0x4711
#define USB_PID_KORECONTROLLER2		0x4712
#define USB_PID_AK1			0x0815
#define USB_PID_AUDIO2DJ		0x041c
#define USB_PID_AUDIO4DJ		0x0839
#define USB_PID_AUDIO8DJ		0x1978
#define USB_PID_SESSIONIO		0x1915
#define USB_PID_GUITARRIGMOBILE		0x0d8d
#define USB_PID_TRAKTORKONTROLX1	0x2305
#define USB_PID_TRAKTORKONTROLS4	0xbaff
#define USB_PID_TRAKTORAUDIO2		0x041d

#define EP1_BUFSIZE 64
#define EP4_BUFSIZE 512
#define CAIAQ_USB_STR_LEN 0xff
#define MAX_STREAMS 32

//#define	SND_USB_CAIAQ_DEBUG

#define MODNAME "snd-usb-caiaq"
#define log(x...) snd_printk(KERN_WARNING MODNAME" log: " x)

#ifdef SND_USB_CAIAQ_DEBUG
#define debug(x...) snd_printk(KERN_WARNING MODNAME " debug: " x)
#else
#define debug(x...) do { } while(0)
#endif

#define EP1_CMD_GET_DEVICE_INFO	0x1
#define EP1_CMD_READ_ERP	0x2
#define EP1_CMD_READ_ANALOG	0x3
#define EP1_CMD_READ_IO		0x4
#define EP1_CMD_WRITE_IO	0x5
#define EP1_CMD_MIDI_READ	0x6
#define EP1_CMD_MIDI_WRITE	0x7
#define EP1_CMD_AUDIO_PARAMS	0x9
#define EP1_CMD_AUTO_MSG	0xb
#define EP1_CMD_DIMM_LEDS       0xc

struct caiaq_device_spec {
	unsigned short fw_version;
	unsigned char hw_subtype;
	unsigned char num_erp;
	unsigned char num_analog_in;
	unsigned char num_digital_in;
	unsigned char num_digital_out;
	unsigned char num_analog_audio_out;
	unsigned char num_analog_audio_in;
	unsigned char num_digital_audio_out;
	unsigned char num_digital_audio_in;
	unsigned char num_midi_out;
	unsigned char num_midi_in;
	unsigned char data_alignment;
} __attribute__ ((packed));

struct snd_usb_caiaq_cb_info;

struct snd_usb_caiaqdev {
	struct snd_usb_audio chip;

	struct urb ep1_in_urb;
	struct urb midi_out_urb;
	struct urb **data_urbs_in;
	struct urb **data_urbs_out;
	struct snd_usb_caiaq_cb_info *data_cb_info;

	unsigned char ep1_in_buf[EP1_BUFSIZE];
	unsigned char ep1_out_buf[EP1_BUFSIZE];
	unsigned char midi_out_buf[EP1_BUFSIZE];

	struct caiaq_device_spec spec;
	spinlock_t spinlock;
	wait_queue_head_t ep1_wait_queue;
	wait_queue_head_t prepare_wait_queue;
	int spec_received, audio_parm_answer;
	int midi_out_active;

	char vendor_name[CAIAQ_USB_STR_LEN];
	char product_name[CAIAQ_USB_STR_LEN];

	int n_streams, n_audio_in, n_audio_out;
	int streaming, first_packet, output_running;
	int audio_in_buf_pos[MAX_STREAMS];
	int audio_out_buf_pos[MAX_STREAMS];
	int period_in_count[MAX_STREAMS];
	int period_out_count[MAX_STREAMS];
	int input_panic, output_panic, warned;
	char *audio_in_buf, *audio_out_buf;
	unsigned int samplerates, bpp;

	struct snd_pcm_substream *sub_playback[MAX_STREAMS];
	struct snd_pcm_substream *sub_capture[MAX_STREAMS];

	/* Controls */
	unsigned char control_state[256];
	unsigned char ep8_out_buf[2];

	/* Linux input */
#ifdef CONFIG_SND_USB_CAIAQ_INPUT
	struct input_dev *input_dev;
	char phys[64];			/* physical device path */
	unsigned short keycode[128];
	struct urb *ep4_in_urb;
	unsigned char ep4_in_buf[EP4_BUFSIZE];
#endif

	/* ALSA */
	struct snd_pcm *pcm;
	struct snd_pcm_hardware pcm_info;
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_substream *midi_receive_substream;
	struct snd_rawmidi_substream *midi_out_substream;
};

struct snd_usb_caiaq_cb_info {
	struct snd_usb_caiaqdev *dev;
	int index;
};

#define caiaqdev(c) ((struct snd_usb_caiaqdev*)(c)->private_data)

int snd_usb_caiaq_set_audio_params (struct snd_usb_caiaqdev *dev, int rate, int depth, int bbp);
int snd_usb_caiaq_set_auto_msg (struct snd_usb_caiaqdev *dev, int digital, int analog, int erp);
int snd_usb_caiaq_send_command(struct snd_usb_caiaqdev *dev,
			       unsigned char command,
			       const unsigned char *buffer,
			       int len);

#endif /* CAIAQ_DEVICE_H */
