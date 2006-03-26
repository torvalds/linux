/*
 * ipmi.h
 *
 * MontaVista IPMI interface
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __LINUX_IPMI_H
#define __LINUX_IPMI_H

#include <linux/ipmi_msgdefs.h>
#include <linux/compiler.h>
#include <linux/device.h>

/*
 * This file describes an interface to an IPMI driver.  You have to
 * have a fairly good understanding of IPMI to use this, so go read
 * the specs first before actually trying to do anything.
 *
 * With that said, this driver provides a multi-user interface to the
 * IPMI driver, and it allows multiple IPMI physical interfaces below
 * the driver.  The physical interfaces bind as a lower layer on the
 * driver.  They appear as interfaces to the application using this
 * interface.
 *
 * Multi-user means that multiple applications may use the driver,
 * send commands, receive responses, etc.  The driver keeps track of
 * commands the user sends and tracks the responses.  The responses
 * will go back to the application that send the command.  If the
 * response doesn't come back in time, the driver will return a
 * timeout error response to the application.  Asynchronous events
 * from the BMC event queue will go to all users bound to the driver.
 * The incoming event queue in the BMC will automatically be flushed
 * if it becomes full and it is queried once a second to see if
 * anything is in it.  Incoming commands to the driver will get
 * delivered as commands.
 *
 * This driver provides two main interfaces: one for in-kernel
 * applications and another for userland applications.  The
 * capabilities are basically the same for both interface, although
 * the interfaces are somewhat different.  The stuff in the
 * #ifdef KERNEL below is the in-kernel interface.  The userland
 * interface is defined later in the file.  */



/*
 * This is an overlay for all the address types, so it's easy to
 * determine the actual address type.  This is kind of like addresses
 * work for sockets.
 */
#define IPMI_MAX_ADDR_SIZE 32
struct ipmi_addr
{
	 /* Try to take these from the "Channel Medium Type" table
	    in section 6.5 of the IPMI 1.5 manual. */
	int   addr_type;
	short channel;
	char  data[IPMI_MAX_ADDR_SIZE];
};

/*
 * When the address is not used, the type will be set to this value.
 * The channel is the BMC's channel number for the channel (usually
 * 0), or IPMC_BMC_CHANNEL if communicating directly with the BMC.
 */
#define IPMI_SYSTEM_INTERFACE_ADDR_TYPE	0x0c
struct ipmi_system_interface_addr
{
	int           addr_type;
	short         channel;
	unsigned char lun;
};

/* An IPMB Address. */
#define IPMI_IPMB_ADDR_TYPE		0x01
/* Used for broadcast get device id as described in section 17.9 of the
   IPMI 1.5 manual. */ 
#define IPMI_IPMB_BROADCAST_ADDR_TYPE	0x41
struct ipmi_ipmb_addr
{
	int           addr_type;
	short         channel;
	unsigned char slave_addr;
	unsigned char lun;
};

/*
 * A LAN Address.  This is an address to/from a LAN interface bridged
 * by the BMC, not an address actually out on the LAN.
 *
 * A concious decision was made here to deviate slightly from the IPMI
 * spec.  We do not use rqSWID and rsSWID like it shows in the
 * message.  Instead, we use remote_SWID and local_SWID.  This means
 * that any message (a request or response) from another device will
 * always have exactly the same address.  If you didn't do this,
 * requests and responses from the same device would have different
 * addresses, and that's not too cool.
 *
 * In this address, the remote_SWID is always the SWID the remote
 * message came from, or the SWID we are sending the message to.
 * local_SWID is always our SWID.  Note that having our SWID in the
 * message is a little weird, but this is required.
 */
#define IPMI_LAN_ADDR_TYPE		0x04
struct ipmi_lan_addr
{
	int           addr_type;
	short         channel;
	unsigned char privilege;
	unsigned char session_handle;
	unsigned char remote_SWID;
	unsigned char local_SWID;
	unsigned char lun;
};


/*
 * Channel for talking directly with the BMC.  When using this
 * channel, This is for the system interface address type only.  FIXME
 * - is this right, or should we use -1?
 */
#define IPMI_BMC_CHANNEL  0xf
#define IPMI_NUM_CHANNELS 0x10


/*
 * A raw IPMI message without any addressing.  This covers both
 * commands and responses.  The completion code is always the first
 * byte of data in the response (as the spec shows the messages laid
 * out).
 */
struct ipmi_msg
{
	unsigned char  netfn;
	unsigned char  cmd;
	unsigned short data_len;
	unsigned char  __user *data;
};

struct kernel_ipmi_msg
{
	unsigned char  netfn;
	unsigned char  cmd;
	unsigned short data_len;
	unsigned char  *data;
};

/*
 * Various defines that are useful for IPMI applications.
 */
#define IPMI_INVALID_CMD_COMPLETION_CODE	0xC1
#define IPMI_TIMEOUT_COMPLETION_CODE		0xC3
#define IPMI_UNKNOWN_ERR_COMPLETION_CODE	0xff


/*
 * Receive types for messages coming from the receive interface.  This
 * is used for the receive in-kernel interface and in the receive
 * IOCTL.
 *
 * The "IPMI_RESPONSE_RESPNOSE_TYPE" is a little strange sounding, but
 * it allows you to get the message results when you send a response
 * message.
 */
#define IPMI_RESPONSE_RECV_TYPE		1 /* A response to a command */
#define IPMI_ASYNC_EVENT_RECV_TYPE	2 /* Something from the event queue */
#define IPMI_CMD_RECV_TYPE		3 /* A command from somewhere else */
#define IPMI_RESPONSE_RESPONSE_TYPE	4 /* The response for
					      a sent response, giving any
					      error status for sending the
					      response.  When you send a
					      response message, this will
					      be returned. */
/* Note that async events and received commands do not have a completion
   code as the first byte of the incoming data, unlike a response. */



#ifdef __KERNEL__

/*
 * The in-kernel interface.
 */
#include <linux/list.h>
#include <linux/module.h>

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
extern struct proc_dir_entry *proc_ipmi_root;
#endif /* CONFIG_PROC_FS */

/* Opaque type for a IPMI message user.  One of these is needed to
   send and receive messages. */
typedef struct ipmi_user *ipmi_user_t;

/*
 * Stuff coming from the receive interface comes as one of these.
 * They are allocated, the receiver must free them with
 * ipmi_free_recv_msg() when done with the message.  The link is not
 * used after the message is delivered, so the upper layer may use the
 * link to build a linked list, if it likes.
 */
struct ipmi_recv_msg
{
	struct list_head link;

	/* The type of message as defined in the "Receive Types"
           defines above. */
	int              recv_type;

	ipmi_user_t      user;
	struct ipmi_addr addr;
	long             msgid;
	struct kernel_ipmi_msg  msg;

	/* The user_msg_data is the data supplied when a message was
	   sent, if this is a response to a sent message.  If this is
	   not a response to a sent message, then user_msg_data will
	   be NULL.  If the user above is NULL, then this will be the
	   intf. */
	void             *user_msg_data;

	/* Call this when done with the message.  It will presumably free
	   the message and do any other necessary cleanup. */
	void (*done)(struct ipmi_recv_msg *msg);

	/* Place-holder for the data, don't make any assumptions about
	   the size or existance of this, since it may change. */
	unsigned char   msg_data[IPMI_MAX_MSG_LENGTH];
};

/* Allocate and free the receive message. */
void ipmi_free_recv_msg(struct ipmi_recv_msg *msg);

struct ipmi_user_hndl
{
        /* Routine type to call when a message needs to be routed to
	   the upper layer.  This will be called with some locks held,
	   the only IPMI routines that can be called are ipmi_request
	   and the alloc/free operations.  The handler_data is the
	   variable supplied when the receive handler was registered. */
	void (*ipmi_recv_hndl)(struct ipmi_recv_msg *msg,
			       void                 *user_msg_data);

	/* Called when the interface detects a watchdog pre-timeout.  If
	   this is NULL, it will be ignored for the user. */
	void (*ipmi_watchdog_pretimeout)(void *handler_data);
};

/* Create a new user of the IPMI layer on the given interface number. */
int ipmi_create_user(unsigned int          if_num,
		     struct ipmi_user_hndl *handler,
		     void                  *handler_data,
		     ipmi_user_t           *user);

/* Destroy the given user of the IPMI layer.  Note that after this
   function returns, the system is guaranteed to not call any
   callbacks for the user.  Thus as long as you destroy all the users
   before you unload a module, you will be safe.  And if you destroy
   the users before you destroy the callback structures, it should be
   safe, too. */
int ipmi_destroy_user(ipmi_user_t user);

/* Get the IPMI version of the BMC we are talking to. */
void ipmi_get_version(ipmi_user_t   user,
		      unsigned char *major,
		      unsigned char *minor);

/* Set and get the slave address and LUN that we will use for our
   source messages.  Note that this affects the interface, not just
   this user, so it will affect all users of this interface.  This is
   so some initialization code can come in and do the OEM-specific
   things it takes to determine your address (if not the BMC) and set
   it for everyone else.  Note that each channel can have its own address. */
int ipmi_set_my_address(ipmi_user_t   user,
			unsigned int  channel,
			unsigned char address);
int ipmi_get_my_address(ipmi_user_t   user,
			unsigned int  channel,
			unsigned char *address);
int ipmi_set_my_LUN(ipmi_user_t   user,
		    unsigned int  channel,
		    unsigned char LUN);
int ipmi_get_my_LUN(ipmi_user_t   user,
		    unsigned int  channel,
		    unsigned char *LUN);

/*
 * Like ipmi_request, but lets you specify the number of retries and
 * the retry time.  The retries is the number of times the message
 * will be resent if no reply is received.  If set to -1, the default
 * value will be used.  The retry time is the time in milliseconds
 * between retries.  If set to zero, the default value will be
 * used.
 *
 * Don't use this unless you *really* have to.  It's primarily for the
 * IPMI over LAN converter; since the LAN stuff does its own retries,
 * it makes no sense to do it here.  However, this can be used if you
 * have unusual requirements.
 */
int ipmi_request_settime(ipmi_user_t      user,
			 struct ipmi_addr *addr,
			 long             msgid,
			 struct kernel_ipmi_msg  *msg,
			 void             *user_msg_data,
			 int              priority,
			 int              max_retries,
			 unsigned int     retry_time_ms);

/*
 * Like ipmi_request, but with messages supplied.  This will not
 * allocate any memory, and the messages may be statically allocated
 * (just make sure to do the "done" handling on them).  Note that this
 * is primarily for the watchdog timer, since it should be able to
 * send messages even if no memory is available.  This is subject to
 * change as the system changes, so don't use it unless you REALLY
 * have to.
 */
int ipmi_request_supply_msgs(ipmi_user_t          user,
			     struct ipmi_addr     *addr,
			     long                 msgid,
			     struct kernel_ipmi_msg *msg,
			     void                 *user_msg_data,
			     void                 *supplied_smi,
			     struct ipmi_recv_msg *supplied_recv,
			     int                  priority);

/*
 * When commands come in to the SMS, the user can register to receive
 * them.  Only one user can be listening on a specific netfn/cmd pair
 * at a time, you will get an EBUSY error if the command is already
 * registered.  If a command is received that does not have a user
 * registered, the driver will automatically return the proper
 * error.
 */
int ipmi_register_for_cmd(ipmi_user_t   user,
			  unsigned char netfn,
			  unsigned char cmd);
int ipmi_unregister_for_cmd(ipmi_user_t   user,
			    unsigned char netfn,
			    unsigned char cmd);

/*
 * Allow run-to-completion mode to be set for the interface of
 * a specific user.
 */
void ipmi_user_set_run_to_completion(ipmi_user_t user, int val);

/*
 * When the user is created, it will not receive IPMI events by
 * default.  The user must set this to TRUE to get incoming events.
 * The first user that sets this to TRUE will receive all events that
 * have been queued while no one was waiting for events.
 */
int ipmi_set_gets_events(ipmi_user_t user, int val);

/*
 * Called when a new SMI is registered.  This will also be called on
 * every existing interface when a new watcher is registered with
 * ipmi_smi_watcher_register().
 */
struct ipmi_smi_watcher
{
	struct list_head link;

	/* You must set the owner to the current module, if you are in
	   a module (generally just set it to "THIS_MODULE"). */
	struct module *owner;

	/* These two are called with read locks held for the interface
	   the watcher list.  So you can add and remove users from the
	   IPMI interface, send messages, etc., but you cannot add
	   or remove SMI watchers or SMI interfaces. */
	void (*new_smi)(int if_num, struct device *dev);
	void (*smi_gone)(int if_num);
};

int ipmi_smi_watcher_register(struct ipmi_smi_watcher *watcher);
int ipmi_smi_watcher_unregister(struct ipmi_smi_watcher *watcher);

/* The following are various helper functions for dealing with IPMI
   addresses. */

/* Return the maximum length of an IPMI address given it's type. */
unsigned int ipmi_addr_length(int addr_type);

/* Validate that the given IPMI address is valid. */
int ipmi_validate_addr(struct ipmi_addr *addr, int len);

#endif /* __KERNEL__ */


/*
 * The userland interface
 */

/*
 * The userland interface for the IPMI driver is a standard character
 * device, with each instance of an interface registered as a minor
 * number under the major character device.
 *
 * The read and write calls do not work, to get messages in and out
 * requires ioctl calls because of the complexity of the data.  select
 * and poll do work, so you can wait for input using the file
 * descriptor, you just can use read to get it.
 *
 * In general, you send a command down to the interface and receive
 * responses back.  You can use the msgid value to correlate commands
 * and responses, the driver will take care of figuring out which
 * incoming messages are for which command and find the proper msgid
 * value to report.  You will only receive reponses for commands you
 * send.  Asynchronous events, however, go to all open users, so you
 * must be ready to handle these (or ignore them if you don't care).
 *
 * The address type depends upon the channel type.  When talking
 * directly to the BMC (IPMC_BMC_CHANNEL), the address is ignored
 * (IPMI_UNUSED_ADDR_TYPE).  When talking to an IPMB channel, you must
 * supply a valid IPMB address with the addr_type set properly.
 *
 * When talking to normal channels, the driver takes care of the
 * details of formatting and sending messages on that channel.  You do
 * not, for instance, have to format a send command, you just send
 * whatever command you want to the channel, the driver will create
 * the send command, automatically issue receive command and get even
 * commands, and pass those up to the proper user.
 */


/* The magic IOCTL value for this interface. */
#define IPMI_IOC_MAGIC 'i'


/* Messages sent to the interface are this format. */
struct ipmi_req
{
	unsigned char __user *addr; /* Address to send the message to. */
	unsigned int  addr_len;

	long    msgid; /* The sequence number for the message.  This
			  exact value will be reported back in the
			  response to this request if it is a command.
			  If it is a response, this will be used as
			  the sequence value for the response.  */

	struct ipmi_msg msg;
};
/*
 * Send a message to the interfaces.  error values are:
 *   - EFAULT - an address supplied was invalid.
 *   - EINVAL - The address supplied was not valid, or the command
 *              was not allowed.
 *   - EMSGSIZE - The message to was too large.
 *   - ENOMEM - Buffers could not be allocated for the command.
 */
#define IPMICTL_SEND_COMMAND		_IOR(IPMI_IOC_MAGIC, 13,	\
					     struct ipmi_req)

/* Messages sent to the interface with timing parameters are this
   format. */
struct ipmi_req_settime
{
	struct ipmi_req req;

	/* See ipmi_request_settime() above for details on these
           values. */
	int          retries;
	unsigned int retry_time_ms;
};
/*
 * Send a message to the interfaces with timing parameters.  error values
 * are:
 *   - EFAULT - an address supplied was invalid.
 *   - EINVAL - The address supplied was not valid, or the command
 *              was not allowed.
 *   - EMSGSIZE - The message to was too large.
 *   - ENOMEM - Buffers could not be allocated for the command.
 */
#define IPMICTL_SEND_COMMAND_SETTIME	_IOR(IPMI_IOC_MAGIC, 21,	\
					     struct ipmi_req_settime)

/* Messages received from the interface are this format. */
struct ipmi_recv
{
	int     recv_type; /* Is this a command, response or an
			      asyncronous event. */

	unsigned char __user *addr;    /* Address the message was from is put
				   here.  The caller must supply the
				   memory. */
	unsigned int  addr_len; /* The size of the address buffer.
				   The caller supplies the full buffer
				   length, this value is updated to
				   the actual message length when the
				   message is received. */

	long    msgid; /* The sequence number specified in the request
			  if this is a response.  If this is a command,
			  this will be the sequence number from the
			  command. */

	struct ipmi_msg msg; /* The data field must point to a buffer.
				The data_size field must be set to the
				size of the message buffer.  The
				caller supplies the full buffer
				length, this value is updated to the
				actual message length when the message
				is received. */
};

/*
 * Receive a message.  error values:
 *  - EAGAIN - no messages in the queue.
 *  - EFAULT - an address supplied was invalid.
 *  - EINVAL - The address supplied was not valid.
 *  - EMSGSIZE - The message to was too large to fit into the message buffer,
 *               the message will be left in the buffer. */
#define IPMICTL_RECEIVE_MSG		_IOWR(IPMI_IOC_MAGIC, 12,	\
					      struct ipmi_recv)

/*
 * Like RECEIVE_MSG, but if the message won't fit in the buffer, it
 * will truncate the contents instead of leaving the data in the
 * buffer.
 */
#define IPMICTL_RECEIVE_MSG_TRUNC	_IOWR(IPMI_IOC_MAGIC, 11,	\
					      struct ipmi_recv)

/* Register to get commands from other entities on this interface. */
struct ipmi_cmdspec
{
	unsigned char netfn;
	unsigned char cmd;
};

/* 
 * Register to receive a specific command.  error values:
 *   - EFAULT - an address supplied was invalid.
 *   - EBUSY - The netfn/cmd supplied was already in use.
 *   - ENOMEM - could not allocate memory for the entry.
 */
#define IPMICTL_REGISTER_FOR_CMD	_IOR(IPMI_IOC_MAGIC, 14,	\
					     struct ipmi_cmdspec)
/*
 * Unregister a regsitered command.  error values:
 *  - EFAULT - an address supplied was invalid.
 *  - ENOENT - The netfn/cmd was not found registered for this user.
 */
#define IPMICTL_UNREGISTER_FOR_CMD	_IOR(IPMI_IOC_MAGIC, 15,	\
					     struct ipmi_cmdspec)

/* 
 * Set whether this interface receives events.  Note that the first
 * user registered for events will get all pending events for the
 * interface.  error values:
 *  - EFAULT - an address supplied was invalid.
 */
#define IPMICTL_SET_GETS_EVENTS_CMD	_IOR(IPMI_IOC_MAGIC, 16, int)

/*
 * Set and get the slave address and LUN that we will use for our
 * source messages.  Note that this affects the interface, not just
 * this user, so it will affect all users of this interface.  This is
 * so some initialization code can come in and do the OEM-specific
 * things it takes to determine your address (if not the BMC) and set
 * it for everyone else.  You should probably leave the LUN alone.
 */
struct ipmi_channel_lun_address_set
{
	unsigned short channel;
	unsigned char  value;
};
#define IPMICTL_SET_MY_CHANNEL_ADDRESS_CMD _IOR(IPMI_IOC_MAGIC, 24, struct ipmi_channel_lun_address_set)
#define IPMICTL_GET_MY_CHANNEL_ADDRESS_CMD _IOR(IPMI_IOC_MAGIC, 25, struct ipmi_channel_lun_address_set)
#define IPMICTL_SET_MY_CHANNEL_LUN_CMD	   _IOR(IPMI_IOC_MAGIC, 26, struct ipmi_channel_lun_address_set)
#define IPMICTL_GET_MY_CHANNEL_LUN_CMD	   _IOR(IPMI_IOC_MAGIC, 27, struct ipmi_channel_lun_address_set)
/* Legacy interfaces, these only set IPMB 0. */
#define IPMICTL_SET_MY_ADDRESS_CMD	_IOR(IPMI_IOC_MAGIC, 17, unsigned int)
#define IPMICTL_GET_MY_ADDRESS_CMD	_IOR(IPMI_IOC_MAGIC, 18, unsigned int)
#define IPMICTL_SET_MY_LUN_CMD		_IOR(IPMI_IOC_MAGIC, 19, unsigned int)
#define IPMICTL_GET_MY_LUN_CMD		_IOR(IPMI_IOC_MAGIC, 20, unsigned int)

/*
 * Get/set the default timing values for an interface.  You shouldn't
 * generally mess with these.
 */
struct ipmi_timing_parms
{
	int          retries;
	unsigned int retry_time_ms;
};
#define IPMICTL_SET_TIMING_PARMS_CMD	_IOR(IPMI_IOC_MAGIC, 22, \
					     struct ipmi_timing_parms)
#define IPMICTL_GET_TIMING_PARMS_CMD	_IOR(IPMI_IOC_MAGIC, 23, \
					     struct ipmi_timing_parms)

#endif /* __LINUX_IPMI_H */
