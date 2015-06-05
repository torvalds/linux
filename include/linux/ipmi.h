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

#include <uapi/linux/ipmi.h>

#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/acpi.h> /* For acpi_handle */

struct module;
struct device;

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
struct ipmi_recv_msg {
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
	   the size or existence of this, since it may change. */
	unsigned char   msg_data[IPMI_MAX_MSG_LENGTH];
};

/* Allocate and free the receive message. */
void ipmi_free_recv_msg(struct ipmi_recv_msg *msg);

struct ipmi_user_hndl {
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
 * Poll the IPMI interface for the user.  This causes the IPMI code to
 * do an immediate check for information from the driver and handle
 * anything that is immediately pending.  This will not block in any
 * way.  This is useful if you need to spin waiting for something to
 * happen in the IPMI driver.
 */
void ipmi_poll_interface(ipmi_user_t user);

/*
 * When commands come in to the SMS, the user can register to receive
 * them.  Only one user can be listening on a specific netfn/cmd/chan tuple
 * at a time, you will get an EBUSY error if the command is already
 * registered.  If a command is received that does not have a user
 * registered, the driver will automatically return the proper
 * error.  Channels are specified as a bitfield, use IPMI_CHAN_ALL to
 * mean all channels.
 */
int ipmi_register_for_cmd(ipmi_user_t   user,
			  unsigned char netfn,
			  unsigned char cmd,
			  unsigned int  chans);
int ipmi_unregister_for_cmd(ipmi_user_t   user,
			    unsigned char netfn,
			    unsigned char cmd,
			    unsigned int  chans);

/*
 * Go into a mode where the driver will not autonomously attempt to do
 * things with the interface.  It will still respond to attentions and
 * interrupts, and it will expect that commands will complete.  It
 * will not automatcially check for flags, events, or things of that
 * nature.
 *
 * This is primarily used for firmware upgrades.  The idea is that
 * when you go into firmware upgrade mode, you do this operation
 * and the driver will not attempt to do anything but what you tell
 * it or what the BMC asks for.
 *
 * Note that if you send a command that resets the BMC, the driver
 * will still expect a response from that command.  So the BMC should
 * reset itself *after* the response is sent.  Resetting before the
 * response is just silly.
 *
 * If in auto maintenance mode, the driver will automatically go into
 * maintenance mode for 30 seconds if it sees a cold reset, a warm
 * reset, or a firmware NetFN.  This means that code that uses only
 * firmware NetFN commands to do upgrades will work automatically
 * without change, assuming it sends a message every 30 seconds or
 * less.
 *
 * See the IPMI_MAINTENANCE_MODE_xxx defines for what the mode means.
 */
int ipmi_get_maintenance_mode(ipmi_user_t user);
int ipmi_set_maintenance_mode(ipmi_user_t user, int mode);

/*
 * When the user is created, it will not receive IPMI events by
 * default.  The user must set this to TRUE to get incoming events.
 * The first user that sets this to TRUE will receive all events that
 * have been queued while no one was waiting for events.
 */
int ipmi_set_gets_events(ipmi_user_t user, bool val);

/*
 * Called when a new SMI is registered.  This will also be called on
 * every existing interface when a new watcher is registered with
 * ipmi_smi_watcher_register().
 */
struct ipmi_smi_watcher {
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

/*
 * How did the IPMI driver find out about the device?
 */
enum ipmi_addr_src {
	SI_INVALID = 0, SI_HOTMOD, SI_HARDCODED, SI_SPMI, SI_ACPI, SI_SMBIOS,
	SI_PCI,	SI_DEVICETREE, SI_DEFAULT
};
const char *ipmi_addr_src_to_str(enum ipmi_addr_src src);

union ipmi_smi_info_union {
#ifdef CONFIG_ACPI
	/*
	 * the acpi_info element is defined for the SI_ACPI
	 * address type
	 */
	struct {
		acpi_handle acpi_handle;
	} acpi_info;
#endif
};

struct ipmi_smi_info {
	enum ipmi_addr_src addr_src;

	/*
	 * Base device for the interface.  Don't forget to put this when
	 * you are done.
	 */
	struct device *dev;

	/*
	 * The addr_info provides more detailed info for some IPMI
	 * devices, depending on the addr_src.  Currently only SI_ACPI
	 * info is provided.
	 */
	union ipmi_smi_info_union addr_info;
};

/* This is to get the private info of ipmi_smi_t */
extern int ipmi_get_smi_info(int if_num, struct ipmi_smi_info *data);

#endif /* __LINUX_IPMI_H */
