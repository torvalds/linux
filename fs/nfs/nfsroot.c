/*
 *  Copyright (C) 1995, 1996  Gero Kuhlmann <gero@gkminix.han.de>
 *
 *  Allow an NFS filesystem to be mounted as root. The way this works is:
 *     (1) Use the IP autoconfig mechanism to set local IP addresses and routes.
 *     (2) Construct the device string and the options string using DHCP
 *         option 17 and/or kernel command line options.
 *     (3) When mount_root() sets up the root file system, pass these strings
 *         to the NFS client's regular mount interface via sys_mount().
 *
 *
 *	Changes:
 *
 *	Alan Cox	:	Removed get_address name clash with FPU.
 *	Alan Cox	:	Reformatted a bit.
 *	Gero Kuhlmann	:	Code cleanup
 *	Michael Rausch  :	Fixed recognition of an incoming RARP answer.
 *	Martin Mares	: (2.0)	Auto-configuration via BOOTP supported.
 *	Martin Mares	:	Manual selection of interface & BOOTP/RARP.
 *	Martin Mares	:	Using network routes instead of host routes,
 *				allowing the default configuration to be used
 *				for normal operation of the host.
 *	Martin Mares	:	Randomized timer with exponential backoff
 *				installed to minimize network congestion.
 *	Martin Mares	:	Code cleanup.
 *	Martin Mares	: (2.1)	BOOTP and RARP made configuration options.
 *	Martin Mares	:	Server hostname generation fixed.
 *	Gerd Knorr	:	Fixed wired inode handling
 *	Martin Mares	: (2.2)	"0.0.0.0" addresses from command line ignored.
 *	Martin Mares	:	RARP replies not tested for server address.
 *	Gero Kuhlmann	: (2.3) Some bug fixes and code cleanup again (please
 *				send me your new patches _before_ bothering
 *				Linus so that I don' always have to cleanup
 *				_afterwards_ - thanks)
 *	Gero Kuhlmann	:	Last changes of Martin Mares undone.
 *	Gero Kuhlmann	: 	RARP replies are tested for specified server
 *				again. However, it's now possible to have
 *				different RARP and NFS servers.
 *	Gero Kuhlmann	:	"0.0.0.0" addresses from command line are
 *				now mapped to INADDR_NONE.
 *	Gero Kuhlmann	:	Fixed a bug which prevented BOOTP path name
 *				from being used (thanks to Leo Spiekman)
 *	Andy Walker	:	Allow to specify the NFS server in nfs_root
 *				without giving a path name
 *	Swen Thümmler	:	Allow to specify the NFS options in nfs_root
 *				without giving a path name. Fix BOOTP request
 *				for domainname (domainname is NIS domain, not
 *				DNS domain!). Skip dummy devices for BOOTP.
 *	Jacek Zapala	:	Fixed a bug which prevented server-ip address
 *				from nfsroot parameter from being used.
 *	Olaf Kirch	:	Adapted to new NFS code.
 *	Jakub Jelinek	:	Free used code segment.
 *	Marko Kohtala	:	Fixed some bugs.
 *	Martin Mares	:	Debug message cleanup
 *	Martin Mares	:	Changed to use the new generic IP layer autoconfig
 *				code. BOOTP and RARP moved there.
 *	Martin Mares	:	Default path now contains host name instead of
 *				host IP address (but host name defaults to IP
 *				address anyway).
 *	Martin Mares	:	Use root_server_addr appropriately during setup.
 *	Martin Mares	:	Rewrote parameter parsing, now hopefully giving
 *				correct overriding.
 *	Trond Myklebust :	Add in preliminary support for NFSv3 and TCP.
 *				Fix bug in root_nfs_addr(). nfs_data.namlen
 *				is NOT for the length of the hostname.
 *	Hua Qin		:	Support for mounting root file system via
 *				NFS over TCP.
 *	Fabian Frederick:	Option parser rebuilt (using parser lib)
 *	Chuck Lever	:	Use super.c's text-based mount option parsing
 *	Chuck Lever	:	Add "nfsrootdebug".
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/nfs.h>
#include <linux/nfs_fs.h>
#include <linux/utsname.h>
#include <linux/root_dev.h>
#include <net/ipconfig.h>

#include "internal.h"

#define NFSDBG_FACILITY NFSDBG_ROOT

/* Default path we try to mount. "%s" gets replaced by our IP address */
#define NFS_ROOT		"/tftpboot/%s"

/* Parameters passed from the kernel command line */
static char nfs_root_parms[256] __initdata = "";

/* Text-based mount options passed to super.c */
static char nfs_root_options[256] __initdata = "";

/* Address of NFS server */
static __be32 servaddr __initdata = htonl(INADDR_NONE);

/* Name of directory to mount */
static char nfs_export_path[NFS_MAXPATHLEN + 1] __initdata = "";

/* server:export path string passed to super.c */
static char nfs_root_device[NFS_MAXPATHLEN + 1] __initdata = "";

#ifdef RPC_DEBUG
/*
 * When the "nfsrootdebug" kernel command line option is specified,
 * enable debugging messages for NFSROOT.
 */
static int __init nfs_root_debug(char *__unused)
{
	nfs_debug |= NFSDBG_ROOT | NFSDBG_MOUNT;
	return 1;
}

__setup("nfsrootdebug", nfs_root_debug);
#endif

/*
 *  Parse NFS server and directory information passed on the kernel
 *  command line.
 *
 *  nfsroot=[<server-ip>:]<root-dir>[,<nfs-options>]
 *
 *  If there is a "%s" token in the <root-dir> string, it is replaced
 *  by the ASCII-representation of the client's IP address.
 */
static int __init nfs_root_setup(char *line)
{
	ROOT_DEV = Root_NFS;

	if (line[0] == '/' || line[0] == ',' || (line[0] >= '0' && line[0] <= '9')) {
		strlcpy(nfs_root_parms, line, sizeof(nfs_root_parms));
	} else {
		size_t n = strlen(line) + sizeof(NFS_ROOT) - 1;
		if (n >= sizeof(nfs_root_parms))
			line[sizeof(nfs_root_parms) - sizeof(NFS_ROOT) - 2] = '\0';
		sprintf(nfs_root_parms, NFS_ROOT, line);
	}

	/*
	 * Extract the IP address of the NFS server containing our
	 * root file system, if one was specified.
	 *
	 * Note: root_nfs_parse_addr() removes the server-ip from
	 *	 nfs_root_parms, if it exists.
	 */
	root_server_addr = root_nfs_parse_addr(nfs_root_parms);

	return 1;
}

__setup("nfsroot=", nfs_root_setup);

static int __init root_nfs_copy(char *dest, const char *src,
				     const size_t destlen)
{
	if (strlcpy(dest, src, destlen) > destlen)
		return -1;
	return 0;
}

static int __init root_nfs_cat(char *dest, const char *src,
				  const size_t destlen)
{
	if (strlcat(dest, src, destlen) > destlen)
		return -1;
	return 0;
}

/*
 * Parse out root export path and mount options from
 * passed-in string @incoming.
 *
 * Copy the export path into @exppath.
 */
static int __init root_nfs_parse_options(char *incoming, char *exppath,
					 const size_t exppathlen)
{
	char *p;

	/*
	 * Set the NFS remote path
	 */
	p = strsep(&incoming, ",");
	if (*p != '\0' && strcmp(p, "default") != 0)
		if (root_nfs_copy(exppath, p, exppathlen))
			return -1;

	/*
	 * @incoming now points to the rest of the string; if it
	 * contains something, append it to our root options buffer
	 */
	if (incoming != NULL && *incoming != '\0')
		if (root_nfs_cat(nfs_root_options, incoming,
						sizeof(nfs_root_options)))
			return -1;

	/*
	 * Possibly prepare for more options to be appended
	 */
	if (nfs_root_options[0] != '\0' &&
	    nfs_root_options[strlen(nfs_root_options)] != ',')
		if (root_nfs_cat(nfs_root_options, ",",
						sizeof(nfs_root_options)))
			return -1;

	return 0;
}

/*
 *  Decode the export directory path name and NFS options from
 *  the kernel command line.  This has to be done late in order to
 *  use a dynamically acquired client IP address for the remote
 *  root directory path.
 *
 *  Returns zero if successful; otherwise -1 is returned.
 */
static int __init root_nfs_data(char *cmdline)
{
	char addr_option[sizeof("nolock,addr=") + INET_ADDRSTRLEN + 1];
	int len, retval = -1;
	char *tmp = NULL;
	const size_t tmplen = sizeof(nfs_export_path);

	tmp = kzalloc(tmplen, GFP_KERNEL);
	if (tmp == NULL)
		goto out_nomem;
	strcpy(tmp, NFS_ROOT);

	if (root_server_path[0] != '\0') {
		dprintk("Root-NFS: DHCPv4 option 17: %s\n",
			root_server_path);
		if (root_nfs_parse_options(root_server_path, tmp, tmplen))
			goto out_optionstoolong;
	}

	if (cmdline[0] != '\0') {
		dprintk("Root-NFS: nfsroot=%s\n", cmdline);
		if (root_nfs_parse_options(cmdline, tmp, tmplen))
			goto out_optionstoolong;
	}

	/*
	 * Append mandatory options for nfsroot so they override
	 * what has come before
	 */
	snprintf(addr_option, sizeof(addr_option), "nolock,addr=%pI4",
			&servaddr);
	if (root_nfs_cat(nfs_root_options, addr_option,
						sizeof(nfs_root_options)))
		goto out_optionstoolong;

	/*
	 * Set up nfs_root_device.  For NFS mounts, this looks like
	 *
	 *	server:/path
	 *
	 * At this point, utsname()->nodename contains our local
	 * IP address or hostname, set by ipconfig.  If "%s" exists
	 * in tmp, substitute the nodename, then shovel the whole
	 * mess into nfs_root_device.
	 */
	len = snprintf(nfs_export_path, sizeof(nfs_export_path),
				tmp, utsname()->nodename);
	if (len > (int)sizeof(nfs_export_path))
		goto out_devnametoolong;
	len = snprintf(nfs_root_device, sizeof(nfs_root_device),
				"%pI4:%s", &servaddr, nfs_export_path);
	if (len > (int)sizeof(nfs_root_device))
		goto out_devnametoolong;

	retval = 0;

out:
	kfree(tmp);
	return retval;
out_nomem:
	printk(KERN_ERR "Root-NFS: could not allocate memory\n");
	goto out;
out_optionstoolong:
	printk(KERN_ERR "Root-NFS: mount options string too long\n");
	goto out;
out_devnametoolong:
	printk(KERN_ERR "Root-NFS: root device name too long.\n");
	goto out;
}

/**
 * nfs_root_data - Return prepared 'data' for NFSROOT mount
 * @root_device: OUT: address of string containing NFSROOT device
 * @root_data: OUT: address of string containing NFSROOT mount options
 *
 * Returns zero and sets @root_device and @root_data if successful,
 * otherwise -1 is returned.
 */
int __init nfs_root_data(char **root_device, char **root_data)
{
	servaddr = root_server_addr;
	if (servaddr == htonl(INADDR_NONE)) {
		printk(KERN_ERR "Root-NFS: no NFS server address\n");
		return -1;
	}

	if (root_nfs_data(nfs_root_parms) < 0)
		return -1;

	*root_device = nfs_root_device;
	*root_data = nfs_root_options;
	return 0;
}
