/*
 * BRIEF MODULE DESCRIPTION
 *	Au1000 USB Device-Side Driver
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *		stevel@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define USBDEV_REV 0x0110 // BCD
#define USBDEV_EP0_MAX_PACKET_SIZE 64

typedef enum {
	ATTACHED = 0,
	POWERED,
	DEFAULT,
	ADDRESS,
	CONFIGURED
} usbdev_state_t;

typedef enum {
	CB_NEW_STATE = 0,
	CB_PKT_COMPLETE
} usbdev_cb_type_t;


typedef struct usbdev_pkt {
	int                ep_addr;    // ep addr this packet routed to
	int                size;       // size of payload in bytes
	unsigned           status;     // packet status
	struct usbdev_pkt* next;       // function layer can't touch this
	u8                 payload[0]; // the payload
} usbdev_pkt_t;

#define PKT_STATUS_ACK  (1<<0)
#define PKT_STATUS_NAK  (1<<1)
#define PKT_STATUS_SU   (1<<2)

extern int usbdev_init(struct usb_device_descriptor* dev_desc,
		       struct usb_config_descriptor* config_desc,
		       struct usb_interface_descriptor* if_desc,
		       struct usb_endpoint_descriptor* ep_desc,
		       struct usb_string_descriptor* str_desc[],
		       void (*cb)(usbdev_cb_type_t, unsigned long, void *),
		       void* cb_data);

extern void usbdev_exit(void);

extern int usbdev_alloc_packet  (int ep_addr, int data_size,
				 usbdev_pkt_t** pkt);
extern int usbdev_send_packet   (int ep_addr, usbdev_pkt_t* pkt);
extern int usbdev_receive_packet(int ep_addr, usbdev_pkt_t** pkt);
extern int usbdev_get_byte_count(int ep_addr);
