/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  drivers/s390/net/iucv.h
 *    IUCV base support.
 *
 *  S390 version
 *    Copyright 2000, 2006 IBM Corporation
 *    Author(s):Alan Altmark (Alan_Altmark@us.ibm.com)
 *		Xenia Tkatschow (xenia@us.ibm.com)
 *    Rewritten for af_iucv:
 *	Martin Schwidefsky <schwidefsky@de.ibm.com>
 *
 *
 * Functionality:
 * To explore any of the IUCV functions, one must first register their
 * program using iucv_register(). Once your program has successfully
 * completed a register, it can exploit the other functions.
 * For further reference on all IUCV functionality, refer to the
 * CP Programming Services book, also available on the web thru
 * www.vm.ibm.com/pubs, manual # SC24-6084
 *
 * Definition of Return Codes
 * - All positive return codes including zero are reflected back
 *   from CP. The definition of each return code can be found in
 *   CP Programming Services book.
 * - Return Code of:
 *   -EINVAL: Invalid value
 *   -ENOMEM: storage allocation failed
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/dma-types.h>
#include <asm/debug.h>

/*
 * IUCV option flags usable by device drivers:
 *
 * IUCV_IPRMDATA  Indicates that your program can handle a message in the
 *		  parameter list / a message is sent in the parameter list.
 *		  Used for iucv_path_accept, iucv_path_connect,
 *		  iucv_message_reply, iucv_message_send, iucv_message_send2way.
 * IUCV_IPQUSCE	  Indicates that you do not want to receive messages on this
 *		  path until an iucv_path_resume is issued.
 *		  Used for iucv_path_accept, iucv_path_connect.
 * IUCV_IPBUFLST  Indicates that an address list is used for the message data.
 *		  Used for iucv_message_receive, iucv_message_send,
 *		  iucv_message_send2way.
 * IUCV_IPPRTY	  Specifies that you want to send priority messages.
 *		  Used for iucv_path_accept, iucv_path_connect,
 *		  iucv_message_reply, iucv_message_send, iucv_message_send2way.
 * IUCV_IPSYNC	  Indicates a synchronous send request.
 *		  Used for iucv_message_send, iucv_message_send2way.
 * IUCV_IPANSLST  Indicates that an address list is used for the reply data.
 *		  Used for iucv_message_reply, iucv_message_send2way.
 * IUCV_IPLOCAL	  Specifies that the communication partner has to be on the
 *		  local system. If local is specified no target class can be
 *		  specified.
 *		  Used for iucv_path_connect.
 *
 * All flags are defined in the input field IPFLAGS1 of each function
 * and can be found in CP Programming Services.
 */
#define IUCV_IPRMDATA	0x80
#define IUCV_IPQUSCE	0x40
#define IUCV_IPBUFLST	0x40
#define IUCV_IPPRTY	0x20
#define IUCV_IPANSLST	0x08
#define IUCV_IPSYNC	0x04
#define IUCV_IPLOCAL	0x01

/*
 * iucv_array : Defines buffer array.
 * Inside the array may be 31- bit addresses and 31-bit lengths.
 * Use a pointer to an iucv_array as the buffer, reply or answer
 * parameter on iucv_message_send, iucv_message_send2way, iucv_message_receive
 * and iucv_message_reply if IUCV_IPBUFLST or IUCV_IPANSLST are used.
 */
struct iucv_array {
	dma32_t address;
	u32 length;
} __attribute__ ((aligned (8)));

extern const struct bus_type iucv_bus;

struct device_driver;

struct device *iucv_alloc_device(const struct attribute_group **attrs,
				 struct device_driver *driver, void *priv,
				 const char *fmt, ...) __printf(4, 5);

/*
 * struct iucv_path
 * pathid: 16 bit path identification
 * msglim: 16 bit message limit
 * flags: properties of the path: IPRMDATA, IPQUSCE, IPPRTY
 * handler:  address of iucv handler structure
 * private: private information of the handler associated with the path
 * list: list_head for the iucv_handler path list.
 */
struct iucv_path {
	u16 pathid;
	u16 msglim;
	u8  flags;
	void *private;
	struct iucv_handler *handler;
	struct list_head list;
};

/*
 * struct iucv_message
 * id: 32 bit message id
 * audit: 32 bit error information of purged or replied messages
 * class: 32 bit target class of a message (source class for replies)
 * tag: 32 bit tag to be associated with the message
 * length: 32 bit length of the message / reply
 * reply_size: 32 bit maximum allowed length of the reply
 * rmmsg: 8 byte inline message
 * flags: message properties (IUCV_IPPRTY)
 */
struct iucv_message {
	u32 id;
	u32 audit;
	u32 class;
	u32 tag;
	u32 length;
	u32 reply_size;
	u8  rmmsg[8];
	u8  flags;
} __packed;

/*
 * struct iucv_handler
 *
 * A vector of functions that handle IUCV interrupts. Each functions gets
 * a parameter area as defined by the CP Programming Services and private
 * pointer that is provided by the user of the interface.
 */
struct iucv_handler {
	 /*
	  * The path_pending function is called after an iucv interrupt
	  * type 0x01 has been received. The base code allocates a path
	  * structure and "asks" the handler if this path belongs to the
	  * handler. To accept the path the path_pending function needs
	  * to call iucv_path_accept and return 0. If the callback returns
	  * a value != 0 the iucv base code will continue with the next
	  * handler. The order in which the path_pending functions are
	  * called is the order of the registration of the iucv handlers
	  * to the base code.
	  */
	int  (*path_pending)(struct iucv_path *, u8 *ipvmid, u8 *ipuser);
	/*
	 * The path_complete function is called after an iucv interrupt
	 * type 0x02 has been received for a path that has been established
	 * for this handler with iucv_path_connect and got accepted by the
	 * peer with iucv_path_accept.
	 */
	void (*path_complete)(struct iucv_path *, u8 *ipuser);
	 /*
	  * The path_severed function is called after an iucv interrupt
	  * type 0x03 has been received. The communication peer shutdown
	  * his end of the communication path. The path still exists and
	  * remaining messages can be received until a iucv_path_sever
	  * shuts down the other end of the path as well.
	  */
	void (*path_severed)(struct iucv_path *, u8 *ipuser);
	/*
	 * The path_quiesced function is called after an icuv interrupt
	 * type 0x04 has been received. The communication peer has quiesced
	 * the path. Delivery of messages is stopped until iucv_path_resume
	 * has been called.
	 */
	void (*path_quiesced)(struct iucv_path *, u8 *ipuser);
	/*
	 * The path_resumed function is called after an icuv interrupt
	 * type 0x05 has been received. The communication peer has resumed
	 * the path.
	 */
	void (*path_resumed)(struct iucv_path *, u8 *ipuser);
	/*
	 * The message_pending function is called after an icuv interrupt
	 * type 0x06 or type 0x07 has been received. A new message is
	 * available and can be received with iucv_message_receive.
	 */
	void (*message_pending)(struct iucv_path *, struct iucv_message *);
	/*
	 * The message_complete function is called after an icuv interrupt
	 * type 0x08 or type 0x09 has been received. A message send with
	 * iucv_message_send2way has been replied to. The reply can be
	 * received with iucv_message_receive.
	 */
	void (*message_complete)(struct iucv_path *, struct iucv_message *);

	struct list_head list;
	struct list_head paths;
};

int iucv_register(struct iucv_handler *handler, int smp);
void iucv_unregister(struct iucv_handler *handler, int smp);

/**
 * iucv_path_alloc - Allocate a new path structure for use with iucv_connect.
 * @msglim: initial message limit
 * @flags: initial flags
 * @gfp: kmalloc allocation flag
 *
 * Returns: NULL if the memory allocation failed or a pointer to the
 * path structure.
 */
static inline struct iucv_path *iucv_path_alloc(u16 msglim, u8 flags, gfp_t gfp)
{
	struct iucv_path *path;

	path = kzalloc(sizeof(struct iucv_path), gfp);
	if (path) {
		path->msglim = msglim;
		path->flags = flags;
	}
	return path;
}

/**
 * iucv_path_free - Frees a path structure.
 * @path: address of iucv path structure
 */
static inline void iucv_path_free(struct iucv_path *path)
{
	kfree(path);
}

int iucv_path_accept(struct iucv_path *path, struct iucv_handler *handler,
		     u8 *userdata, void *private);

int iucv_path_connect(struct iucv_path *path, struct iucv_handler *handler,
		      u8 *userid, u8 *system, u8 *userdata,
		      void *private);

int iucv_path_quiesce(struct iucv_path *path, u8 *userdata);

int iucv_path_resume(struct iucv_path *path, u8 *userdata);

int iucv_path_sever(struct iucv_path *path, u8 *userdata);

int iucv_message_purge(struct iucv_path *path, struct iucv_message *msg,
		       u32 srccls);

int iucv_message_receive(struct iucv_path *path, struct iucv_message *msg,
			 u8 flags, void *buffer, size_t size, size_t *residual);

int __iucv_message_receive(struct iucv_path *path, struct iucv_message *msg,
			   u8 flags, void *buffer, size_t size,
			   size_t *residual);

int iucv_message_reject(struct iucv_path *path, struct iucv_message *msg);

int iucv_message_reply(struct iucv_path *path, struct iucv_message *msg,
		       u8 flags, void *reply, size_t size);

int iucv_message_send(struct iucv_path *path, struct iucv_message *msg,
		      u8 flags, u32 srccls, void *buffer, size_t size);

int __iucv_message_send(struct iucv_path *path, struct iucv_message *msg,
			u8 flags, u32 srccls, void *buffer, size_t size);

int iucv_message_send2way(struct iucv_path *path, struct iucv_message *msg,
			  u8 flags, u32 srccls, void *buffer, size_t size,
			  void *answer, size_t asize, size_t *residual);

struct iucv_interface {
	int (*message_receive)(struct iucv_path *path, struct iucv_message *msg,
		u8 flags, void *buffer, size_t size, size_t *residual);
	int (*__message_receive)(struct iucv_path *path,
		struct iucv_message *msg, u8 flags, void *buffer, size_t size,
		size_t *residual);
	int (*message_reply)(struct iucv_path *path, struct iucv_message *msg,
		u8 flags, void *reply, size_t size);
	int (*message_reject)(struct iucv_path *path, struct iucv_message *msg);
	int (*message_send)(struct iucv_path *path, struct iucv_message *msg,
		u8 flags, u32 srccls, void *buffer, size_t size);
	int (*__message_send)(struct iucv_path *path, struct iucv_message *msg,
		u8 flags, u32 srccls, void *buffer, size_t size);
	int (*message_send2way)(struct iucv_path *path,
		struct iucv_message *msg, u8 flags, u32 srccls, void *buffer,
		size_t size, void *answer, size_t asize, size_t *residual);
	int (*message_purge)(struct iucv_path *path, struct iucv_message *msg,
		u32 srccls);
	int (*path_accept)(struct iucv_path *path, struct iucv_handler *handler,
		u8 userdata[16], void *private);
	int (*path_connect)(struct iucv_path *path,
		struct iucv_handler *handler,
		u8 userid[8], u8 system[8], u8 userdata[16], void *private);
	int (*path_quiesce)(struct iucv_path *path, u8 userdata[16]);
	int (*path_resume)(struct iucv_path *path, u8 userdata[16]);
	int (*path_sever)(struct iucv_path *path, u8 userdata[16]);
	int (*iucv_register)(struct iucv_handler *handler, int smp);
	void (*iucv_unregister)(struct iucv_handler *handler, int smp);
	const struct bus_type *bus;
	struct device *root;
};

extern struct iucv_interface iucv_if;
