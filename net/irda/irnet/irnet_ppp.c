/*
 *	IrNET protocol module : Synchronous PPP over an IrDA socket.
 *
 *		Jean II - HPL `00 - <jt@hpl.hp.com>
 *
 * This file implement the PPP interface and /dev/irnet character device.
 * The PPP interface hook to the ppp_generic module, handle all our
 *	relationship to the PPP code in the kernel (and by extension to pppd),
 *	and exchange PPP frames with this module (send/receive).
 * The /dev/irnet device is used primarily for 2 functions :
 *	1) as a stub for pppd (the ppp daemon), so that we can appropriately
 *	generate PPP sessions (we pretend we are a tty).
 *	2) as a control channel (write commands, read events)
 */

#include "irnet_ppp.h"		/* Private header */
/* Please put other headers in irnet.h - Thanks */

/* Generic PPP callbacks (to call us) */
static struct ppp_channel_ops irnet_ppp_ops = {
	.start_xmit = ppp_irnet_send,
	.ioctl = ppp_irnet_ioctl
};

/************************* CONTROL CHANNEL *************************/
/*
 * When a pppd instance is not active on /dev/irnet, it acts as a control
 * channel.
 * Writing allow to set up the IrDA destination of the IrNET channel,
 * and any application may be read events happening in IrNET...
 */

/*------------------------------------------------------------------*/
/*
 * Write is used to send a command to configure a IrNET channel
 * before it is open by pppd. The syntax is : "command argument"
 * Currently there is only two defined commands :
 *	o name : set the requested IrDA nickname of the IrNET peer.
 *	o addr : set the requested IrDA address of the IrNET peer.
 * Note : the code is crude, but effective...
 */
static inline ssize_t
irnet_ctrl_write(irnet_socket *	ap,
		 const char __user *buf,
		 size_t		count)
{
  char		command[IRNET_MAX_COMMAND];
  char *	start;		/* Current command being processed */
  char *	next;		/* Next command to process */
  int		length;		/* Length of current command */

  DENTER(CTRL_TRACE, "(ap=0x%p, count=%Zd)\n", ap, count);

  /* Check for overflow... */
  DABORT(count >= IRNET_MAX_COMMAND, -ENOMEM,
	 CTRL_ERROR, "Too much data !!!\n");

  /* Get the data in the driver */
  if(copy_from_user(command, buf, count))
    {
      DERROR(CTRL_ERROR, "Invalid user space pointer.\n");
      return -EFAULT;
    }

  /* Safe terminate the string */
  command[count] = '\0';
  DEBUG(CTRL_INFO, "Command line received is ``%s'' (%Zd).\n",
	command, count);

  /* Check every commands in the command line */
  next = command;
  while(next != NULL)
    {
      /* Look at the next command */
      start = next;

      /* Scrap whitespaces before the command */
      while(isspace(*start))
	start++;

      /* ',' is our command separator */
      next = strchr(start, ',');
      if(next)
	{
	  *next = '\0';			/* Terminate command */
	  length = next - start;	/* Length */
	  next++;			/* Skip the '\0' */
	}
      else
	length = strlen(start);

      DEBUG(CTRL_INFO, "Found command ``%s'' (%d).\n", start, length);

      /* Check if we recognised one of the known command
       * We can't use "switch" with strings, so hack with "continue" */

      /* First command : name -> Requested IrDA nickname */
      if(!strncmp(start, "name", 4))
	{
	  /* Copy the name only if is included and not "any" */
	  if((length > 5) && (strcmp(start + 5, "any")))
	    {
	      /* Strip out trailing whitespaces */
	      while(isspace(start[length - 1]))
		length--;

	      /* Copy the name for later reuse */
	      memcpy(ap->rname, start + 5, length - 5);
	      ap->rname[length - 5] = '\0';
	    }
	  else
	    ap->rname[0] = '\0';
	  DEBUG(CTRL_INFO, "Got rname = ``%s''\n", ap->rname);

	  /* Restart the loop */
	  continue;
	}

      /* Second command : addr, daddr -> Requested IrDA destination address
       * Also process : saddr -> Requested IrDA source address */
      if((!strncmp(start, "addr", 4)) ||
	 (!strncmp(start, "daddr", 5)) ||
	 (!strncmp(start, "saddr", 5)))
	{
	  __u32		addr = DEV_ADDR_ANY;

	  /* Copy the address only if is included and not "any" */
	  if((length > 5) && (strcmp(start + 5, "any")))
	    {
	      char *	begp = start + 5;
	      char *	endp;

	      /* Scrap whitespaces before the command */
	      while(isspace(*begp))
		begp++;

	      /* Convert argument to a number (last arg is the base) */
	      addr = simple_strtoul(begp, &endp, 16);
	      /* Has it worked  ? (endp should be start + length) */
	      DABORT(endp <= (start + 5), -EINVAL,
		     CTRL_ERROR, "Invalid address.\n");
	    }
	  /* Which type of address ? */
	  if(start[0] == 's')
	    {
	      /* Save it */
	      ap->rsaddr = addr;
	      DEBUG(CTRL_INFO, "Got rsaddr = %08x\n", ap->rsaddr);
	    }
	  else
	    {
	      /* Save it */
	      ap->rdaddr = addr;
	      DEBUG(CTRL_INFO, "Got rdaddr = %08x\n", ap->rdaddr);
	    }

	  /* Restart the loop */
	  continue;
	}

      /* Other possible command : connect N (number of retries) */

      /* No command matched -> Failed... */
      DABORT(1, -EINVAL, CTRL_ERROR, "Not a recognised IrNET command.\n");
    }

  /* Success : we have parsed all commands successfully */
  return(count);
}

#ifdef INITIAL_DISCOVERY
/*------------------------------------------------------------------*/
/*
 * Function irnet_get_discovery_log (self)
 *
 *    Query the content on the discovery log if not done
 *
 * This function query the current content of the discovery log
 * at the startup of the event channel and save it in the internal struct.
 */
static void
irnet_get_discovery_log(irnet_socket *	ap)
{
  __u16		mask = irlmp_service_to_hint(S_LAN);

  /* Ask IrLMP for the current discovery log */
  ap->discoveries = irlmp_get_discoveries(&ap->disco_number, mask,
					  DISCOVERY_DEFAULT_SLOTS);

  /* Check if the we got some results */
  if(ap->discoveries == NULL)
    ap->disco_number = -1;

  DEBUG(CTRL_INFO, "Got the log (0x%p), size is %d\n",
	ap->discoveries, ap->disco_number);
}

/*------------------------------------------------------------------*/
/*
 * Function irnet_read_discovery_log (self, event)
 *
 *    Read the content on the discovery log
 *
 * This function dump the current content of the discovery log
 * at the startup of the event channel.
 * Return 1 if wrote an event on the control channel...
 *
 * State of the ap->disco_XXX variables :
 * Socket creation :  discoveries = NULL ; disco_index = 0 ; disco_number = 0
 * While reading :    discoveries = ptr  ; disco_index = X ; disco_number = Y
 * After reading :    discoveries = NULL ; disco_index = Y ; disco_number = -1
 */
static inline int
irnet_read_discovery_log(irnet_socket *	ap,
			 char *		event)
{
  int		done_event = 0;

  DENTER(CTRL_TRACE, "(ap=0x%p, event=0x%p)\n",
	 ap, event);

  /* Test if we have some work to do or we have already finished */
  if(ap->disco_number == -1)
    {
      DEBUG(CTRL_INFO, "Already done\n");
      return 0;
    }

  /* Test if it's the first time and therefore we need to get the log */
  if(ap->discoveries == NULL)
    irnet_get_discovery_log(ap);

  /* Check if we have more item to dump */
  if(ap->disco_index < ap->disco_number)
    {
      /* Write an event */
      sprintf(event, "Found %08x (%s) behind %08x {hints %02X-%02X}\n",
	      ap->discoveries[ap->disco_index].daddr,
	      ap->discoveries[ap->disco_index].info,
	      ap->discoveries[ap->disco_index].saddr,
	      ap->discoveries[ap->disco_index].hints[0],
	      ap->discoveries[ap->disco_index].hints[1]);
      DEBUG(CTRL_INFO, "Writing discovery %d : %s\n",
	    ap->disco_index, ap->discoveries[ap->disco_index].info);

      /* We have an event */
      done_event = 1;
      /* Next discovery */
      ap->disco_index++;
    }

  /* Check if we have done the last item */
  if(ap->disco_index >= ap->disco_number)
    {
      /* No more items : remove the log and signal termination */
      DEBUG(CTRL_INFO, "Cleaning up log (0x%p)\n",
	    ap->discoveries);
      if(ap->discoveries != NULL)
	{
	  /* Cleanup our copy of the discovery log */
	  kfree(ap->discoveries);
	  ap->discoveries = NULL;
	}
      ap->disco_number = -1;
    }

  return done_event;
}
#endif /* INITIAL_DISCOVERY */

/*------------------------------------------------------------------*/
/*
 * Read is used to get IrNET events
 */
static inline ssize_t
irnet_ctrl_read(irnet_socket *	ap,
		struct file *	file,
		char __user *	buf,
		size_t		count)
{
  DECLARE_WAITQUEUE(wait, current);
  char		event[64];	/* Max event is 61 char */
  ssize_t	ret = 0;

  DENTER(CTRL_TRACE, "(ap=0x%p, count=%Zd)\n", ap, count);

  /* Check if we can write an event out in one go */
  DABORT(count < sizeof(event), -EOVERFLOW, CTRL_ERROR, "Buffer to small.\n");

#ifdef INITIAL_DISCOVERY
  /* Check if we have read the log */
  if(irnet_read_discovery_log(ap, event))
    {
      /* We have an event !!! Copy it to the user */
      if(copy_to_user(buf, event, strlen(event)))
	{
	  DERROR(CTRL_ERROR, "Invalid user space pointer.\n");
	  return -EFAULT;
	}

      DEXIT(CTRL_TRACE, "\n");
      return(strlen(event));
    }
#endif /* INITIAL_DISCOVERY */

  /* Put ourselves on the wait queue to be woken up */
  add_wait_queue(&irnet_events.rwait, &wait);
  current->state = TASK_INTERRUPTIBLE;
  for(;;)
    {
      /* If there is unread events */
      ret = 0;
      if(ap->event_index != irnet_events.index)
	break;
      ret = -EAGAIN;
      if(file->f_flags & O_NONBLOCK)
	break;
      ret = -ERESTARTSYS;
      if(signal_pending(current))
	break;
      /* Yield and wait to be woken up */
      schedule();
    }
  current->state = TASK_RUNNING;
  remove_wait_queue(&irnet_events.rwait, &wait);

  /* Did we got it ? */
  if(ret != 0)
    {
      /* No, return the error code */
      DEXIT(CTRL_TRACE, " - ret %Zd\n", ret);
      return ret;
    }

  /* Which event is it ? */
  switch(irnet_events.log[ap->event_index].event)
    {
    case IRNET_DISCOVER:
      sprintf(event, "Discovered %08x (%s) behind %08x {hints %02X-%02X}\n",
	      irnet_events.log[ap->event_index].daddr,
	      irnet_events.log[ap->event_index].name,
	      irnet_events.log[ap->event_index].saddr,
	      irnet_events.log[ap->event_index].hints.byte[0],
	      irnet_events.log[ap->event_index].hints.byte[1]);
      break;
    case IRNET_EXPIRE:
      sprintf(event, "Expired %08x (%s) behind %08x {hints %02X-%02X}\n",
	      irnet_events.log[ap->event_index].daddr,
	      irnet_events.log[ap->event_index].name,
	      irnet_events.log[ap->event_index].saddr,
	      irnet_events.log[ap->event_index].hints.byte[0],
	      irnet_events.log[ap->event_index].hints.byte[1]);
      break;
    case IRNET_CONNECT_TO:
      sprintf(event, "Connected to %08x (%s) on ppp%d\n",
	      irnet_events.log[ap->event_index].daddr,
	      irnet_events.log[ap->event_index].name,
	      irnet_events.log[ap->event_index].unit);
      break;
    case IRNET_CONNECT_FROM:
      sprintf(event, "Connection from %08x (%s) on ppp%d\n",
	      irnet_events.log[ap->event_index].daddr,
	      irnet_events.log[ap->event_index].name,
	      irnet_events.log[ap->event_index].unit);
      break;
    case IRNET_REQUEST_FROM:
      sprintf(event, "Request from %08x (%s) behind %08x\n",
	      irnet_events.log[ap->event_index].daddr,
	      irnet_events.log[ap->event_index].name,
	      irnet_events.log[ap->event_index].saddr);
      break;
    case IRNET_NOANSWER_FROM:
      sprintf(event, "No-answer from %08x (%s) on ppp%d\n",
	      irnet_events.log[ap->event_index].daddr,
	      irnet_events.log[ap->event_index].name,
	      irnet_events.log[ap->event_index].unit);
      break;
    case IRNET_BLOCKED_LINK:
      sprintf(event, "Blocked link with %08x (%s) on ppp%d\n",
	      irnet_events.log[ap->event_index].daddr,
	      irnet_events.log[ap->event_index].name,
	      irnet_events.log[ap->event_index].unit);
      break;
    case IRNET_DISCONNECT_FROM:
      sprintf(event, "Disconnection from %08x (%s) on ppp%d\n",
	      irnet_events.log[ap->event_index].daddr,
	      irnet_events.log[ap->event_index].name,
	      irnet_events.log[ap->event_index].unit);
      break;
    case IRNET_DISCONNECT_TO:
      sprintf(event, "Disconnected to %08x (%s)\n",
	      irnet_events.log[ap->event_index].daddr,
	      irnet_events.log[ap->event_index].name);
      break;
    default:
      sprintf(event, "Bug\n");
    }
  /* Increment our event index */
  ap->event_index = (ap->event_index + 1) % IRNET_MAX_EVENTS;

  DEBUG(CTRL_INFO, "Event is :%s", event);

  /* Copy it to the user */
  if(copy_to_user(buf, event, strlen(event)))
    {
      DERROR(CTRL_ERROR, "Invalid user space pointer.\n");
      return -EFAULT;
    }

  DEXIT(CTRL_TRACE, "\n");
  return(strlen(event));
}

/*------------------------------------------------------------------*/
/*
 * Poll : called when someone do a select on /dev/irnet.
 * Just check if there are new events...
 */
static inline unsigned int
irnet_ctrl_poll(irnet_socket *	ap,
		struct file *	file,
		poll_table *	wait)
{
  unsigned int mask;

  DENTER(CTRL_TRACE, "(ap=0x%p)\n", ap);

  poll_wait(file, &irnet_events.rwait, wait);
  mask = POLLOUT | POLLWRNORM;
  /* If there is unread events */
  if(ap->event_index != irnet_events.index)
    mask |= POLLIN | POLLRDNORM;
#ifdef INITIAL_DISCOVERY
  if(ap->disco_number != -1)
    {
      /* Test if it's the first time and therefore we need to get the log */
      if(ap->discoveries == NULL)
	irnet_get_discovery_log(ap);
      /* Recheck */
      if(ap->disco_number != -1)
	mask |= POLLIN | POLLRDNORM;
    }
#endif /* INITIAL_DISCOVERY */

  DEXIT(CTRL_TRACE, " - mask=0x%X\n", mask);
  return mask;
}


/*********************** FILESYSTEM CALLBACKS ***********************/
/*
 * Implement the usual open, read, write functions that will be called
 * by the file system when some action is performed on /dev/irnet.
 * Most of those actions will in fact be performed by "pppd" or
 * the control channel, we just act as a redirector...
 */

/*------------------------------------------------------------------*/
/*
 * Open : when somebody open /dev/irnet
 * We basically create a new instance of irnet and initialise it.
 */
static int
dev_irnet_open(struct inode *	inode,
	       struct file *	file)
{
  struct irnet_socket *	ap;
  int			err;

  DENTER(FS_TRACE, "(file=0x%p)\n", file);

#ifdef SECURE_DEVIRNET
  /* This could (should?) be enforced by the permissions on /dev/irnet. */
  if(!capable(CAP_NET_ADMIN))
    return -EPERM;
#endif /* SECURE_DEVIRNET */

  /* Allocate a private structure for this IrNET instance */
  ap = kzalloc(sizeof(*ap), GFP_KERNEL);
  DABORT(ap == NULL, -ENOMEM, FS_ERROR, "Can't allocate struct irnet...\n");

  /* initialize the irnet structure */
  ap->file = file;

  /* PPP channel setup */
  ap->ppp_open = 0;
  ap->chan.private = ap;
  ap->chan.ops = &irnet_ppp_ops;
  ap->chan.mtu = (2048 - TTP_MAX_HEADER - 2 - PPP_HDRLEN);
  ap->chan.hdrlen = 2 + TTP_MAX_HEADER;		/* for A/C + Max IrDA hdr */
  /* PPP parameters */
  ap->mru = (2048 - TTP_MAX_HEADER - 2 - PPP_HDRLEN);
  ap->xaccm[0] = ~0U;
  ap->xaccm[3] = 0x60000000U;
  ap->raccm = ~0U;

  /* Setup the IrDA part... */
  err = irda_irnet_create(ap);
  if(err)
    {
      DERROR(FS_ERROR, "Can't setup IrDA link...\n");
      kfree(ap);
      return err;
    }

  /* For the control channel */
  ap->event_index = irnet_events.index;	/* Cancel all past events */

  /* Put our stuff where we will be able to find it later */
  file->private_data = ap;

  DEXIT(FS_TRACE, " - ap=0x%p\n", ap);
  return 0;
}


/*------------------------------------------------------------------*/
/*
 * Close : when somebody close /dev/irnet
 * Destroy the instance of /dev/irnet
 */
static int
dev_irnet_close(struct inode *	inode,
		struct file *	file)
{
  irnet_socket *	ap = (struct irnet_socket *) file->private_data;

  DENTER(FS_TRACE, "(file=0x%p, ap=0x%p)\n",
	 file, ap);
  DABORT(ap == NULL, 0, FS_ERROR, "ap is NULL !!!\n");

  /* Detach ourselves */
  file->private_data = NULL;

  /* Close IrDA stuff */
  irda_irnet_destroy(ap);

  /* Disconnect from the generic PPP layer if not already done */
  if(ap->ppp_open)
    {
      DERROR(FS_ERROR, "Channel still registered - deregistering !\n");
      ap->ppp_open = 0;
      ppp_unregister_channel(&ap->chan);
    }

  kfree(ap);

  DEXIT(FS_TRACE, "\n");
  return 0;
}

/*------------------------------------------------------------------*/
/*
 * Write does nothing.
 * (we receive packet from ppp_generic through ppp_irnet_send())
 */
static ssize_t
dev_irnet_write(struct file *	file,
		const char __user *buf,
		size_t		count,
		loff_t *	ppos)
{
  irnet_socket *	ap = (struct irnet_socket *) file->private_data;

  DPASS(FS_TRACE, "(file=0x%p, ap=0x%p, count=%Zd)\n",
	file, ap, count);
  DABORT(ap == NULL, -ENXIO, FS_ERROR, "ap is NULL !!!\n");

  /* If we are connected to ppp_generic, let it handle the job */
  if(ap->ppp_open)
    return -EAGAIN;
  else
    return irnet_ctrl_write(ap, buf, count);
}

/*------------------------------------------------------------------*/
/*
 * Read doesn't do much either.
 * (pppd poll us, but ultimately reads through /dev/ppp)
 */
static ssize_t
dev_irnet_read(struct file *	file,
	       char __user *	buf,
	       size_t		count,
	       loff_t *		ppos)
{
  irnet_socket *	ap = (struct irnet_socket *) file->private_data;

  DPASS(FS_TRACE, "(file=0x%p, ap=0x%p, count=%Zd)\n",
	file, ap, count);
  DABORT(ap == NULL, -ENXIO, FS_ERROR, "ap is NULL !!!\n");

  /* If we are connected to ppp_generic, let it handle the job */
  if(ap->ppp_open)
    return -EAGAIN;
  else
    return irnet_ctrl_read(ap, file, buf, count);
}

/*------------------------------------------------------------------*/
/*
 * Poll : called when someone do a select on /dev/irnet
 */
static unsigned int
dev_irnet_poll(struct file *	file,
	       poll_table *	wait)
{
  irnet_socket *	ap = (struct irnet_socket *) file->private_data;
  unsigned int		mask;

  DENTER(FS_TRACE, "(file=0x%p, ap=0x%p)\n",
	 file, ap);

  mask = POLLOUT | POLLWRNORM;
  DABORT(ap == NULL, mask, FS_ERROR, "ap is NULL !!!\n");

  /* If we are connected to ppp_generic, let it handle the job */
  if(!ap->ppp_open)
    mask |= irnet_ctrl_poll(ap, file, wait);

  DEXIT(FS_TRACE, " - mask=0x%X\n", mask);
  return(mask);
}

/*------------------------------------------------------------------*/
/*
 * IOCtl : Called when someone does some ioctls on /dev/irnet
 * This is the way pppd configure us and control us while the PPP
 * instance is active.
 */
static int
dev_irnet_ioctl(struct inode *	inode,
		struct file *	file,
		unsigned int	cmd,
		unsigned long	arg)
{
  irnet_socket *	ap = (struct irnet_socket *) file->private_data;
  int			err;
  int			val;
  void __user *argp = (void __user *)arg;

  DENTER(FS_TRACE, "(file=0x%p, ap=0x%p, cmd=0x%X)\n",
	 file, ap, cmd);

  /* Basic checks... */
  DASSERT(ap != NULL, -ENXIO, PPP_ERROR, "ap is NULL...\n");
#ifdef SECURE_DEVIRNET
  if(!capable(CAP_NET_ADMIN))
    return -EPERM;
#endif /* SECURE_DEVIRNET */

  err = -EFAULT;
  switch(cmd)
    {
      /* Set discipline (should be N_SYNC_PPP or N_TTY) */
    case TIOCSETD:
      if(get_user(val, (int __user *)argp))
	break;
      if((val == N_SYNC_PPP) || (val == N_PPP))
	{
	  DEBUG(FS_INFO, "Entering PPP discipline.\n");
	  /* PPP channel setup (ap->chan in configued in dev_irnet_open())*/
	  err = ppp_register_channel(&ap->chan);
	  if(err == 0)
	    {
	      /* Our ppp side is active */
	      ap->ppp_open = 1;

	      DEBUG(FS_INFO, "Trying to establish a connection.\n");
	      /* Setup the IrDA link now - may fail... */
	      irda_irnet_connect(ap);
	    }
	  else
	    DERROR(FS_ERROR, "Can't setup PPP channel...\n");
	}
      else
	{
	  /* In theory, should be N_TTY */
	  DEBUG(FS_INFO, "Exiting PPP discipline.\n");
	  /* Disconnect from the generic PPP layer */
	  if(ap->ppp_open)
	    {
	      ap->ppp_open = 0;
	      ppp_unregister_channel(&ap->chan);
	    }
	  else
	    DERROR(FS_ERROR, "Channel not registered !\n");
	  err = 0;
	}
      break;

      /* Query PPP channel and unit number */
    case PPPIOCGCHAN:
      if(!ap->ppp_open)
	break;
      if(put_user(ppp_channel_index(&ap->chan), (int __user *)argp))
	break;
      DEBUG(FS_INFO, "Query channel.\n");
      err = 0;
      break;
    case PPPIOCGUNIT:
      if(!ap->ppp_open)
	break;
      if(put_user(ppp_unit_number(&ap->chan), (int __user *)argp))
	break;
      DEBUG(FS_INFO, "Query unit number.\n");
      err = 0;
      break;

      /* All these ioctls can be passed both directly and from ppp_generic,
       * so we just deal with them in one place...
       */
    case PPPIOCGFLAGS:
    case PPPIOCSFLAGS:
    case PPPIOCGASYNCMAP:
    case PPPIOCSASYNCMAP:
    case PPPIOCGRASYNCMAP:
    case PPPIOCSRASYNCMAP:
    case PPPIOCGXASYNCMAP:
    case PPPIOCSXASYNCMAP:
    case PPPIOCGMRU:
    case PPPIOCSMRU:
      DEBUG(FS_INFO, "Standard PPP ioctl.\n");
      if(!capable(CAP_NET_ADMIN))
	err = -EPERM;
      else
	err = ppp_irnet_ioctl(&ap->chan, cmd, arg);
      break;

      /* TTY IOCTLs : Pretend that we are a tty, to keep pppd happy */
      /* Get termios */
    case TCGETS:
      DEBUG(FS_INFO, "Get termios.\n");
      if(kernel_termios_to_user_termios((struct termios __user *)argp, &ap->termios))
	break;
      err = 0;
      break;
      /* Set termios */
    case TCSETSF:
      DEBUG(FS_INFO, "Set termios.\n");
      if(user_termios_to_kernel_termios(&ap->termios, (struct termios __user *)argp))
	break;
      err = 0;
      break;

      /* Set DTR/RTS */
    case TIOCMBIS:
    case TIOCMBIC:
      /* Set exclusive/non-exclusive mode */
    case TIOCEXCL:
    case TIOCNXCL:
      DEBUG(FS_INFO, "TTY compatibility.\n");
      err = 0;
      break;

    case TCGETA:
      DEBUG(FS_INFO, "TCGETA\n");
      break;

    case TCFLSH:
      DEBUG(FS_INFO, "TCFLSH\n");
      /* Note : this will flush buffers in PPP, so it *must* be done
       * We should also worry that we don't accept junk here and that
       * we get rid of our own buffers */
#ifdef FLUSH_TO_PPP
      ppp_output_wakeup(&ap->chan);
#endif /* FLUSH_TO_PPP */
      err = 0;
      break;

    case FIONREAD:
      DEBUG(FS_INFO, "FIONREAD\n");
      val = 0;
      if(put_user(val, (int __user *)argp))
	break;
      err = 0;
      break;

    default:
      DERROR(FS_ERROR, "Unsupported ioctl (0x%X)\n", cmd);
      err = -ENOIOCTLCMD;
    }

  DEXIT(FS_TRACE, " - err = 0x%X\n", err);
  return err;
}

/************************** PPP CALLBACKS **************************/
/*
 * This are the functions that the generic PPP driver in the kernel
 * will call to communicate to us.
 */

/*------------------------------------------------------------------*/
/*
 * Prepare the ppp frame for transmission over the IrDA socket.
 * We make sure that the header space is enough, and we change ppp header
 * according to flags passed by pppd.
 * This is not a callback, but just a helper function used in ppp_irnet_send()
 */
static inline struct sk_buff *
irnet_prepare_skb(irnet_socket *	ap,
		  struct sk_buff *	skb)
{
  unsigned char *	data;
  int			proto;		/* PPP protocol */
  int			islcp;		/* Protocol == LCP */
  int			needaddr;	/* Need PPP address */

  DENTER(PPP_TRACE, "(ap=0x%p, skb=0x%p)\n",
	 ap, skb);

  /* Extract PPP protocol from the frame */
  data  = skb->data;
  proto = (data[0] << 8) + data[1];

  /* LCP packets with codes between 1 (configure-request)
   * and 7 (code-reject) must be sent as though no options
   * have been negotiated. */
  islcp = (proto == PPP_LCP) && (1 <= data[2]) && (data[2] <= 7);

  /* compress protocol field if option enabled */
  if((data[0] == 0) && (ap->flags & SC_COMP_PROT) && (!islcp))
    skb_pull(skb,1);

  /* Check if we need address/control fields */
  needaddr = 2*((ap->flags & SC_COMP_AC) == 0 || islcp);

  /* Is the skb headroom large enough to contain all IrDA-headers? */
  if((skb_headroom(skb) < (ap->max_header_size + needaddr)) ||
      (skb_shared(skb)))
    {
      struct sk_buff *	new_skb;

      DEBUG(PPP_INFO, "Reallocating skb\n");

      /* Create a new skb */
      new_skb = skb_realloc_headroom(skb, ap->max_header_size + needaddr);

      /* We have to free the original skb anyway */
      dev_kfree_skb(skb);

      /* Did the realloc succeed ? */
      DABORT(new_skb == NULL, NULL, PPP_ERROR, "Could not realloc skb\n");

      /* Use the new skb instead */
      skb = new_skb;
    }

  /* prepend address/control fields if necessary */
  if(needaddr)
    {
      skb_push(skb, 2);
      skb->data[0] = PPP_ALLSTATIONS;
      skb->data[1] = PPP_UI;
    }

  DEXIT(PPP_TRACE, "\n");

  return skb;
}

/*------------------------------------------------------------------*/
/*
 * Send a packet to the peer over the IrTTP connection.
 * Returns 1 iff the packet was accepted.
 * Returns 0 iff packet was not consumed.
 * If the packet was not accepted, we will call ppp_output_wakeup
 * at some later time to reactivate flow control in ppp_generic.
 */
static int
ppp_irnet_send(struct ppp_channel *	chan,
	       struct sk_buff *		skb)
{
  irnet_socket *	self = (struct irnet_socket *) chan->private;
  int			ret;

  DENTER(PPP_TRACE, "(channel=0x%p, ap/self=0x%p)\n",
	 chan, self);

  /* Check if things are somewhat valid... */
  DASSERT(self != NULL, 0, PPP_ERROR, "Self is NULL !!!\n");

  /* Check if we are connected */
  if(!(test_bit(0, &self->ttp_open)))
    {
#ifdef CONNECT_IN_SEND
      /* Let's try to connect one more time... */
      /* Note : we won't be connected after this call, but we should be
       * ready for next packet... */
      /* If we are already connecting, this will fail */
      irda_irnet_connect(self);
#endif /* CONNECT_IN_SEND */

      DEBUG(PPP_INFO, "IrTTP not ready ! (%ld-%ld)\n",
	    self->ttp_open, self->ttp_connect);

      /* Note : we can either drop the packet or block the packet.
       *
       * Blocking the packet allow us a better connection time,
       * because by calling ppp_output_wakeup() we can have
       * ppp_generic resending the LCP request immediately to us,
       * rather than waiting for one of pppd periodic transmission of
       * LCP request.
       *
       * On the other hand, if we block all packet, all those periodic
       * transmissions of pppd accumulate in ppp_generic, creating a
       * backlog of LCP request. When we eventually connect later on,
       * we have to transmit all this backlog before we can connect
       * proper (if we don't timeout before).
       *
       * The current strategy is as follow :
       * While we are attempting to connect, we block packets to get
       * a better connection time.
       * If we fail to connect, we drain the queue and start dropping packets
       */
#ifdef BLOCK_WHEN_CONNECT
      /* If we are attempting to connect */
      if(test_bit(0, &self->ttp_connect))
	{
	  /* Blocking packet, ppp_generic will retry later */
	  return 0;
	}
#endif /* BLOCK_WHEN_CONNECT */

      /* Dropping packet, pppd will retry later */
      dev_kfree_skb(skb);
      return 1;
    }

  /* Check if the queue can accept any packet, otherwise block */
  if(self->tx_flow != FLOW_START)
    DRETURN(0, PPP_INFO, "IrTTP queue full (%d skbs)...\n",
	    skb_queue_len(&self->tsap->tx_queue));

  /* Prepare ppp frame for transmission */
  skb = irnet_prepare_skb(self, skb);
  DABORT(skb == NULL, 1, PPP_ERROR, "Prepare skb for Tx failed.\n");

  /* Send the packet to IrTTP */
  ret = irttp_data_request(self->tsap, skb);
  if(ret < 0)
    {
      /*
       * > IrTTPs tx queue is full, so we just have to
       * > drop the frame! You might think that we should
       * > just return -1 and don't deallocate the frame,
       * > but that is dangerous since it's possible that
       * > we have replaced the original skb with a new
       * > one with larger headroom, and that would really
       * > confuse do_dev_queue_xmit() in dev.c! I have
       * > tried :-) DB
       * Correction : we verify the flow control above (self->tx_flow),
       * so we come here only if IrTTP doesn't like the packet (empty,
       * too large, IrTTP not connected). In those rare cases, it's ok
       * to drop it, we don't want to see it here again...
       * Jean II
       */
      DERROR(PPP_ERROR, "IrTTP doesn't like this packet !!! (0x%X)\n", ret);
      /* irttp_data_request already free the packet */
    }

  DEXIT(PPP_TRACE, "\n");
  return 1;	/* Packet has been consumed */
}

/*------------------------------------------------------------------*/
/*
 * Take care of the ioctls that ppp_generic doesn't want to deal with...
 * Note : we are also called from dev_irnet_ioctl().
 */
static int
ppp_irnet_ioctl(struct ppp_channel *	chan,
		unsigned int		cmd,
		unsigned long		arg)
{
  irnet_socket *	ap = (struct irnet_socket *) chan->private;
  int			err;
  int			val;
  u32			accm[8];
  void __user *argp = (void __user *)arg;

  DENTER(PPP_TRACE, "(channel=0x%p, ap=0x%p, cmd=0x%X)\n",
	 chan, ap, cmd);

  /* Basic checks... */
  DASSERT(ap != NULL, -ENXIO, PPP_ERROR, "ap is NULL...\n");

  err = -EFAULT;
  switch(cmd)
    {
      /* PPP flags */
    case PPPIOCGFLAGS:
      val = ap->flags | ap->rbits;
      if(put_user(val, (int __user *) argp))
	break;
      err = 0;
      break;
    case PPPIOCSFLAGS:
      if(get_user(val, (int __user *) argp))
	break;
      ap->flags = val & ~SC_RCV_BITS;
      ap->rbits = val & SC_RCV_BITS;
      err = 0;
      break;

      /* Async map stuff - all dummy to please pppd */
    case PPPIOCGASYNCMAP:
      if(put_user(ap->xaccm[0], (u32 __user *) argp))
	break;
      err = 0;
      break;
    case PPPIOCSASYNCMAP:
      if(get_user(ap->xaccm[0], (u32 __user *) argp))
	break;
      err = 0;
      break;
    case PPPIOCGRASYNCMAP:
      if(put_user(ap->raccm, (u32 __user *) argp))
	break;
      err = 0;
      break;
    case PPPIOCSRASYNCMAP:
      if(get_user(ap->raccm, (u32 __user *) argp))
	break;
      err = 0;
      break;
    case PPPIOCGXASYNCMAP:
      if(copy_to_user(argp, ap->xaccm, sizeof(ap->xaccm)))
	break;
      err = 0;
      break;
    case PPPIOCSXASYNCMAP:
      if(copy_from_user(accm, argp, sizeof(accm)))
	break;
      accm[2] &= ~0x40000000U;		/* can't escape 0x5e */
      accm[3] |= 0x60000000U;		/* must escape 0x7d, 0x7e */
      memcpy(ap->xaccm, accm, sizeof(ap->xaccm));
      err = 0;
      break;

      /* Max PPP frame size */
    case PPPIOCGMRU:
      if(put_user(ap->mru, (int __user *) argp))
	break;
      err = 0;
      break;
    case PPPIOCSMRU:
      if(get_user(val, (int __user *) argp))
	break;
      if(val < PPP_MRU)
	val = PPP_MRU;
      ap->mru = val;
      err = 0;
      break;

    default:
      DEBUG(PPP_INFO, "Unsupported ioctl (0x%X)\n", cmd);
      err = -ENOIOCTLCMD;
    }

  DEXIT(PPP_TRACE, " - err = 0x%X\n", err);
  return err;
}

/************************** INITIALISATION **************************/
/*
 * Module initialisation and all that jazz...
 */

/*------------------------------------------------------------------*/
/*
 * Hook our device callbacks in the filesystem, to connect our code
 * to /dev/irnet
 */
static inline int __init
ppp_irnet_init(void)
{
  int err = 0;

  DENTER(MODULE_TRACE, "()\n");

  /* Allocate ourselves as a minor in the misc range */
  err = misc_register(&irnet_misc_device);

  DEXIT(MODULE_TRACE, "\n");
  return err;
}

/*------------------------------------------------------------------*/
/*
 * Cleanup at exit...
 */
static inline void __exit
ppp_irnet_cleanup(void)
{
  DENTER(MODULE_TRACE, "()\n");

  /* De-allocate /dev/irnet minor in misc range */
  misc_deregister(&irnet_misc_device);

  DEXIT(MODULE_TRACE, "\n");
}

/*------------------------------------------------------------------*/
/*
 * Module main entry point
 */
static int __init
irnet_init(void)
{
  int err;

  /* Initialise both parts... */
  err = irda_irnet_init();
  if(!err)
    err = ppp_irnet_init();
  return err;
}

/*------------------------------------------------------------------*/
/*
 * Module exit
 */
static void __exit
irnet_cleanup(void)
{
  irda_irnet_cleanup();
  ppp_irnet_cleanup();
}

/*------------------------------------------------------------------*/
/*
 * Module magic
 */
module_init(irnet_init);
module_exit(irnet_cleanup);
MODULE_AUTHOR("Jean Tourrilhes <jt@hpl.hp.com>");
MODULE_DESCRIPTION("IrNET : Synchronous PPP over IrDA");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV(10, 187);
