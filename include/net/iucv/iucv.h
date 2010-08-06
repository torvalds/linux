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
 * For furthur reference on all IUCV functionality, refer to the
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
	u32 address;
	u32 length;
} __attribute__ ((aligned (8)));

extern struct bus_type iucv_bus;
extern struct device *iucv_root;

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
};

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
	int  (*path_pending)(struct iucv_path *, u8 ipvmid[8], u8 ipuser[16]);
	/*
	 * The path_complete function is called after an iucv interrupt
	 * type 0x02 has been received for a path that has been established
	 * for this handler with iucv_path_connect and got accepted by the
	 * peer with iucv_path_accept.
	 */
	void (*path_complete)(struct iucv_path *, u8 ipuser[16]);
	 /*
	  * The path_severed function is called after an iucv interrupt
	  * type 0x03 has been received. The communication peer shutdown
	  * his end of the communication path. The path still exists and
	  * remaining messages can be received until a iucv_path_sever
	  * shuts down the other end of the path as well.
	  */
	void (*path_severed)(struct iucv_path *, u8 ipuser[16]);
	/*
	 * The path_quiesced function is called after an icuv interrupt
	 * type 0x04 has been received. The communication peer has quiesced
	 * the path. Delivery of messages is stopped until iucv_path_resume
	 * has been called.
	 */
	void (*path_quiesced)(struct iucv_path *, u8 ipuser[16]);
	/*
	 * The path_resumed function is called after an icuv interrupt
	 * type 0x05 has been received. The communication peer has resumed
	 * the path.
	 */
	void (*path_resumed)(struct iucv_path *, u8 ipuser[16]);
	/*
	 * The message_pending function is called after an icuv interrupt
	 * type 0x06 or type 0x07 has been received. A new message is
	 * availabe and can be received with iucv_message_receive.
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

/**
 * iucv_register:
 * @handler: address of iucv handler structure
 * @smp: != 0 indicates that the handler can deal with out of order messages
 *
 * Registers a driver with IUCV.
 *
 * Returns 0 on success, -ENOMEM if the memory allocation for the pathid
 * table failed, or -EIO if IUCV_DECLARE_BUFFER failed on all cpus.
 */
int iucv_register(struct iucv_handler *handler, int smp);

/**
 * iucv_unregister
 * @handler:  address of iucv handler structure
 * @smp: != 0 indicates that the handler can deal with out of order messages
 *
 * Unregister driver from IUCV.
 */
void iucv_unregister(struct iucv_handler *handle, int smp);

/**
 * iucv_path_alloc
 * @msglim: initial message limit
 * @flags: initial flags
 * @gfp: kmalloc allocation flag
 *
 * Allocate a new path structure for use with iucv_connect.
 *
 * Returns NULL if the memory allocation failed or a pointer to the
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
 * iucv_path_free
 * @path: address of iucv path structure
 *
 * Frees a path structure.
 */
static inline void iucv_path_free(struct iucv_path *path)
{
	kfree(path);
}

/**
 * iucv_path_accept
 * @path: address of iucv path structure
 * @handler: address of iucv handler structure
 * @userdata: 16 bytes of data reflected to the communication partner
 * @private: private data passed to interrupt handlers for this path
 *
 * This function is issued after the user received a connection pending
 * external interrupt and now wishes to complete the IUCV communication path.
 *
 * Returns the result of the CP IUCV call.
 */
int iucv_path_accept(struct iucv_path *path, struct iucv_handler *handler,
		     u8 userdata[16], void *private);

/**
 * iucv_path_connect
 * @path: address of iucv path structure
 * @handler: address of iucv handler structure
 * @userid: 8-byte user identification
 * @system: 8-byte target system identification
 * @userdata: 16 bytes of data reflected to the communication partner
 * @private: private data passed to interrupt handlers for this path
 *
 * This function establishes an IUCV path. Although the connect may complete
 * successfully, you are not able to use the path until you receive an IUCV
 * Connection Complete external interrupt.
 *
 * Returns the result of the CP IUCV call.
 */
int iucv_path_connect(struct iucv_path *path, struct iucv_handler *handler,
		      u8 userid[8], u8 system[8], u8 userdata[16],
		      void *private);

/**
 * iucv_path_quiesce:
 * @path: address of iucv path structure
 * @userdata: 16 bytes of data reflected to the communication partner
 *
 * This function temporarily suspends incoming messages on an IUCV path.
 * You can later reactivate the path by invoking the iucv_resume function.
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_path_quiesce(struct iucv_path *path, u8 userdata[16]);

/**
 * iucv_path_resume:
 * @path: address of iucv path structure
 * @userdata: 16 bytes of data reflected to the communication partner
 *
 * This function resumes incoming messages on an IUCV path that has
 * been stopped with iucv_path_quiesce.
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_path_resume(struct iucv_path *path, u8 userdata[16]);

/**
 * iucv_path_sever
 * @path: address of iucv path structure
 * @userdata: 16 bytes of data reflected to the communication partner
 *
 * This function terminates an IUCV path.
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_path_sever(struct iucv_path *path, u8 userdata[16]);

/**
 * iucv_message_purge
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @srccls: source class of message
 *
 * Cancels a message you have sent.
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_message_purge(struct iucv_path *path, struct iucv_message *msg,
		       u32 srccls);

/**
 * iucv_message_receive
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @flags: flags that affect how the message is received (IUCV_IPBUFLST)
 * @buffer: address of data buffer or address of struct iucv_array
 * @size: length of data buffer
 * @residual:
 *
 * This function receives messages that are being sent to you over
 * established paths. This function will deal with RMDATA messages
 * embedded in struct iucv_message as well.
 *
 * Locking:	local_bh_enable/local_bh_disable
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_message_receive(struct iucv_path *path, struct iucv_message *msg,
			 u8 flags, void *buffer, size_t size, size_t *residual);

/**
 * __iucv_message_receive
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @flags: flags that affect how the message is received (IUCV_IPBUFLST)
 * @buffer: address of data buffer or address of struct iucv_array
 * @size: length of data buffer
 * @residual:
 *
 * This function receives messages that are being sent to you over
 * established paths. This function will deal with RMDATA messages
 * embedded in struct iucv_message as well.
 *
 * Locking:	no locking.
 *
 * Returns the result from the CP IUCV call.
 */
int __iucv_message_receive(struct iucv_path *path, struct iucv_message *msg,
			   u8 flags, void *buffer, size_t size,
			   size_t *residual);

/**
 * iucv_message_reject
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 *
 * The reject function refuses a specified message. Between the time you
 * are notified of a message and the time that you complete the message,
 * the message may be rejected.
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_message_reject(struct iucv_path *path, struct iucv_message *msg);

/**
 * iucv_message_reply
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @flags: how the reply is sent (IUCV_IPRMDATA, IUCV_IPPRTY, IUCV_IPBUFLST)
 * @reply: address of data buffer or address of struct iucv_array
 * @size: length of reply data buffer
 *
 * This function responds to the two-way messages that you receive. You
 * must identify completely the message to which you wish to reply. ie,
 * pathid, msgid, and trgcls. Prmmsg signifies the data is moved into
 * the parameter list.
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_message_reply(struct iucv_path *path, struct iucv_message *msg,
		       u8 flags, void *reply, size_t size);

/**
 * iucv_message_send
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @flags: how the message is sent (IUCV_IPRMDATA, IUCV_IPPRTY, IUCV_IPBUFLST)
 * @srccls: source class of message
 * @buffer: address of data buffer or address of struct iucv_array
 * @size: length of send buffer
 *
 * This function transmits data to another application. Data to be
 * transmitted is in a buffer and this is a one-way message and the
 * receiver will not reply to the message.
 *
 * Locking:	local_bh_enable/local_bh_disable
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_message_send(struct iucv_path *path, struct iucv_message *msg,
		      u8 flags, u32 srccls, void *buffer, size_t size);

/**
 * __iucv_message_send
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @flags: how the message is sent (IUCV_IPRMDATA, IUCV_IPPRTY, IUCV_IPBUFLST)
 * @srccls: source class of message
 * @buffer: address of data buffer or address of struct iucv_array
 * @size: length of send buffer
 *
 * This function transmits data to another application. Data to be
 * transmitted is in a buffer and this is a one-way message and the
 * receiver will not reply to the message.
 *
 * Locking:	no locking.
 *
 * Returns the result from the CP IUCV call.
 */
int __iucv_message_send(struct iucv_path *path, struct iucv_message *msg,
			u8 flags, u32 srccls, void *buffer, size_t size);

/**
 * iucv_message_send2way
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @flags: how the message is sent and the reply is received
 *	   (IUCV_IPRMDATA, IUCV_IPBUFLST, IUCV_IPPRTY, IUCV_ANSLST)
 * @srccls: source class of message
 * @buffer: address of data buffer or address of struct iucv_array
 * @size: length of send buffer
 * @ansbuf: address of answer buffer or address of struct iucv_array
 * @asize: size of reply buffer
 *
 * This function transmits data to another application. Data to be
 * transmitted is in a buffer. The receiver of the send is expected to
 * reply to the message and a buffer is provided into which IUCV moves
 * the reply to this message.
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_message_send2way(struct iucv_path *path, struct iucv_message *msg,
			  u8 flags, u32 srccls, void *buffer, size_t size,
			  void *answer, size_t asize, size_t *residual);
