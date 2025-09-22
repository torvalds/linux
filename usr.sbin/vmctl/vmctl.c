/*	$OpenBSD: vmctl.c,v 1.94 2025/06/09 18:43:01 dv Exp $	*/

/*
 * Copyright (c) 2014 Mike Larkin <mlarkin@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>
#include <pwd.h>
#include <grp.h>

#include "vmd.h"
#include "virtio.h"
#include "vmctl.h"
#include "atomicio.h"

extern char *__progname;
uint32_t info_id;
char info_name[VMM_MAX_NAME_LEN];
enum actions info_action;
unsigned int info_flags;

struct imsgbuf *ibuf;

/*
 * vm_start
 *
 * Request vmd to start the VM defined by the supplied parameters
 *
 * Parameters:
 *  start_id: optional ID of the VM
 *  name: optional name of the VM
 *  memsize: memory size (in bytes) of the VM to create
 *  nnics: number of vionet network interfaces to create
 *  nics: switch names of the network interfaces to create
 *  ndisks: number of disk images
 *  disks: disk image file names
 *  kernel: kernel image to load
 *  iso: iso image file
 *  instance: create instance from vm
 *
 * Return:
 *  0 if the request to start the VM was sent successfully.
 *  ENOMEM if a memory allocation failure occurred.
 */
int
vm_start(uint32_t start_id, const char *name, size_t memsize, int nnics,
    char **nics, int ndisks, char **disks, int *disktypes, char *kernel,
    char *iso, char *instance, unsigned int bootdevice)
{
	struct vmop_create_params *vmc;
	struct vm_create_params *vcp;
	struct stat sb;
	unsigned int flags = 0;
	int i;
	const char *s;

	if (kernel) {
		if (unveil(kernel, "r") == -1)
			err(1, "unveil boot kernel");
	} else {
		/* We can drop sendfd promise. */
		if (pledge("stdio rpath exec unix getpw unveil", NULL) == -1)
			err(1, "pledge");
	}

	if (memsize)
		flags |= VMOP_CREATE_MEMORY;
	if (nnics)
		flags |= VMOP_CREATE_NETWORK;
	if (ndisks)
		flags |= VMOP_CREATE_DISK;
	if (kernel)
		flags |= VMOP_CREATE_KERNEL;
	if (iso)
		flags |= VMOP_CREATE_CDROM;
	if (instance)
		flags |= VMOP_CREATE_INSTANCE;
	else if (flags != 0) {
		if (memsize < 1)
			memsize = VM_DEFAULT_MEMORY;
		if (ndisks > VM_MAX_DISKS_PER_VM)
			errx(1, "too many disks");
		else if (kernel == NULL && ndisks == 0)
			warnx("starting without disks");
		if (kernel == NULL && ndisks == 0 && !iso)
			errx(1, "no kernel or disk/cdrom specified");
		if (nnics == -1)
			nnics = 0;
		if (nnics > VM_MAX_NICS_PER_VM)
			errx(1, "too many network interfaces");
		if (kernel == NULL && nnics == 0)
			warnx("starting without network interfaces");
	}

	if ((vmc = calloc(1, sizeof(struct vmop_create_params))) == NULL)
		return (ENOMEM);
	vmc->vmc_kernel = -1;
	vmc->vmc_flags = flags;

	/* vcp includes configuration that is shared with the kernel */
	vcp = &vmc->vmc_params;

	/*
	 * XXX: vmd(8) fills in the actual memory ranges. vmctl(8)
	 * just passes in the actual memory size here.
	 */
	vcp->vcp_nmemranges = 1;
	vcp->vcp_memranges[0].vmr_size = memsize;

	vcp->vcp_ncpus = 1;
	vcp->vcp_id = start_id;

	vmc->vmc_ndisks = ndisks;
	vmc->vmc_nnics = nnics;

	for (i = 0 ; i < ndisks; i++) {
		if (strlcpy(vmc->vmc_disks[i], disks[i],
		    sizeof(vmc->vmc_disks[i])) >=
		    sizeof(vmc->vmc_disks[i]))
			errx(1, "disk path too long");
		vmc->vmc_disktypes[i] = disktypes[i];
	}
	for (i = 0 ; i < nnics; i++) {
		vmc->vmc_ifflags[i] = VMIFF_UP;

		if (strcmp(".", nics[i]) == 0) {
			/* Add a "local" interface */
			(void)strlcpy(vmc->vmc_ifswitch[i], "",
			    sizeof(vmc->vmc_ifswitch[i]));
			vmc->vmc_ifflags[i] |= VMIFF_LOCAL;
		} else {
			/* Add an interface to a switch */
			if (strlcpy(vmc->vmc_ifswitch[i], nics[i],
			    sizeof(vmc->vmc_ifswitch[i])) >=
			    sizeof(vmc->vmc_ifswitch[i]))
				errx(1, "interface name too long");
		}
	}
	if (name != NULL) {
		/*
		 * Allow VMs names with alphanumeric characters, dot, hyphen
		 * and underscore. But disallow dot, hyphen and underscore at
		 * the start.
		 */
		if (*name == '-' || *name == '.' || *name == '_')
			errx(1, "invalid VM name");

		for (s = name; *s != '\0'; ++s) {
			if (!(isalnum(*s) || *s == '.' || *s == '-' ||
			    *s == '_'))
				errx(1, "invalid VM name");
		}

		if (strlcpy(vcp->vcp_name, name,
		    sizeof(vcp->vcp_name)) >= sizeof(vcp->vcp_name))
			errx(1, "vm name too long");
	}
	if (kernel != NULL) {
		if (strnlen(kernel, PATH_MAX) == PATH_MAX)
			errx(1, "kernel name too long");
		vmc->vmc_kernel = open(kernel, O_RDONLY);
		if (vmc->vmc_kernel == -1)
			err(1, "cannot open kernel '%s'", kernel);
		memset(&sb, 0, sizeof(sb));
		if (fstat(vmc->vmc_kernel, &sb) == -1)
			err(1, "fstat kernel");
		if (!S_ISREG(sb.st_mode))
			errx(1, "kernel must be a regular file");
	}
	if (iso != NULL)
		if (strlcpy(vmc->vmc_cdrom, iso,
		    sizeof(vmc->vmc_cdrom)) >= sizeof(vmc->vmc_cdrom))
			errx(1, "cdrom name too long");
	if (instance != NULL)
		if (strlcpy(vmc->vmc_instance, instance,
		    sizeof(vmc->vmc_instance)) >= sizeof(vmc->vmc_instance))
			errx(1, "instance vm name too long");
	vmc->vmc_bootdevice = bootdevice;

	imsg_compose(ibuf, IMSG_VMDOP_START_VM_REQUEST, 0, 0, vmc->vmc_kernel,
	    vmc, sizeof(struct vmop_create_params));

	free(vmc);
	return (0);
}

/*
 * vm_start_complete
 *
 * Callback function invoked when we are expecting an
 * IMSG_VMDOP_START_VMM_RESPONSE message indicating the completion of
 * a start vm operation.
 *
 * Parameters:
 *  imsg : response imsg received from vmd
 *  ret  : return value
 *  autoconnect : open the console after startup
 *
 * Return:
 *  Always 1 to indicate we have processed the return message (even if it
 *  was an incorrect/failure message)
 *
 *  The function also sets 'ret' to the error code as follows:
 *   0     : Message successfully processed
 *   EINVAL: Invalid or unexpected response from vmd
 *   EIO   : vm_start command failed
 *   ENOENT: a specified component of the VM could not be found (disk image,
 *    BIOS firmware image, etc)
 */
int
vm_start_complete(struct imsg *imsg, int *ret, int autoconnect)
{
	struct vmop_result vmr;
	uint32_t type;
	int res;

	type = imsg_get_type(imsg);
	if (type == IMSG_VMDOP_START_VM_RESPONSE) {
		vmop_result_read(imsg, &vmr);
		res = vmr.vmr_result;
		if (res) {
			switch (res) {
			case VMD_BIOS_MISSING:
				warnx("vmm bios firmware file not found.");
				*ret = ENOENT;
				break;
			case VMD_DISK_MISSING:
				warnx("could not open disk image(s)");
				*ret = ENOENT;
				break;
			case VMD_CDROM_MISSING:
				warnx("could not find specified iso image");
				*ret = ENOENT;
				break;
			case VMD_CDROM_INVALID:
				warnx("specified iso image is not a regular "
				    "file");
				*ret = ENOENT;
				break;
			case VMD_PARENT_INVALID:
				warnx("invalid template");
				*ret = EINVAL;
				break;
			default:
				errno = res;
				warn("start vm command failed");
				*ret = EIO;
			}
		} else if (autoconnect) {
			/* does not return */
			ctl_openconsole(vmr.vmr_ttyname);
		} else {
			warnx("started vm %d successfully, tty %s",
			    vmr.vmr_id, vmr.vmr_ttyname);
			*ret = 0;
		}
	} else {
		warnx("unexpected response received from vmd");
		*ret = EINVAL;
	}

	return (1);
}

void
pause_vm(uint32_t pause_id, const char *name)
{
	struct vmop_id vid;

	memset(&vid, 0, sizeof(vid));
	vid.vid_id = pause_id;
	if (name != NULL)
		(void)strlcpy(vid.vid_name, name, sizeof(vid.vid_name));

	imsg_compose(ibuf, IMSG_VMDOP_PAUSE_VM, 0, 0, -1,
	    &vid, sizeof(vid));
}

int
pause_vm_complete(struct imsg *imsg, int *ret)
{
	struct vmop_result vmr;
	uint32_t type;
	int res;

	type = imsg_get_type(imsg);
	if (type == IMSG_VMDOP_PAUSE_VM_RESPONSE) {
		vmop_result_read(imsg, &vmr);
		res = vmr.vmr_result;
		if (res) {
			errno = res;
			warn("pause vm command failed");
			*ret = EIO;
		} else {
			warnx("paused vm %d successfully", vmr.vmr_id);
			*ret = 0;
		}
	} else {
		warnx("unexpected response received from vmd");
		*ret = EINVAL;
	}

	return (1);
}

void
unpause_vm(uint32_t pause_id, const char *name)
{
	struct vmop_id vid;

	memset(&vid, 0, sizeof(vid));
	vid.vid_id = pause_id;
	if (name != NULL)
		(void)strlcpy(vid.vid_name, name, sizeof(vid.vid_name));

	imsg_compose(ibuf, IMSG_VMDOP_UNPAUSE_VM, 0, 0, -1,
	    &vid, sizeof(vid));
}

int
unpause_vm_complete(struct imsg *imsg, int *ret)
{
	struct vmop_result vmr;
	uint32_t type;
	int res;

	type = imsg_get_type(imsg);
	if (type == IMSG_VMDOP_UNPAUSE_VM_RESPONSE) {
		vmop_result_read(imsg, &vmr);
		res = vmr.vmr_result;
		if (res) {
			errno = res;
			warn("unpause vm command failed");
			*ret = EIO;
		} else {
			warnx("unpaused vm %d successfully", vmr.vmr_id);
			*ret = 0;
		}
	} else {
		warnx("unexpected response received from vmd");
		*ret = EINVAL;
	}

	return (1);
}

/*
 * terminate_vm
 *
 * Request vmd to stop the VM indicated
 *
 * Parameters:
 *  terminate_id: ID of the vm to be terminated
 *  name: optional name of the VM to be terminated
 *  flags: VMOP_FORCE or VMOP_WAIT flags
 */
void
terminate_vm(uint32_t terminate_id, const char *name, unsigned int flags)
{
	struct vmop_id vid;

	memset(&vid, 0, sizeof(vid));
	vid.vid_id = terminate_id;
	if (name != NULL) {
		(void)strlcpy(vid.vid_name, name, sizeof(vid.vid_name));
		fprintf(stderr, "stopping vm %s: ", name);
	} else {
		fprintf(stderr, "stopping vm: ");
	}

	vid.vid_flags = flags & (VMOP_FORCE|VMOP_WAIT);

	imsg_compose(ibuf, IMSG_VMDOP_TERMINATE_VM_REQUEST,
	    0, 0, -1, &vid, sizeof(vid));
}

/*
 * terminate_vm_complete
 *
 * Callback function invoked when we are waiting for the response from an
 * IMSG_VMDOP_TERMINATE_VM_REQUEST. We expect a reply of either an
 * IMSG_VMDOP_TERMINATE_VM_EVENT indicating the termination of a vm or an
 * IMSG_VMDOP_TERMINATE_VM_RESPONSE with a success/failure result.
 *
 * Parameters:
 *  imsg : response imsg received from vmd
 *  ret  : return value
 *  flags: VMOP_FORCE or VMOP_WAIT flags
 *
 * Return:
 *  Always 1 to indicate we have processed the return message (even if it
 *  was an incorrect/failure message)
 *
 *  The function also sets 'ret' to the error code as follows:
 *   0     : Message successfully processed
 *   EINVAL: Invalid or unexpected response from vmd
 *   EIO   : terminate_vm command failed
 */
int
terminate_vm_complete(struct imsg *imsg, int *ret, unsigned int flags)
{
	struct vmop_result vmr;
	uint32_t type;
	int res;

	type = imsg_get_type(imsg);
	switch (type) {
	case IMSG_VMDOP_TERMINATE_VM_RESPONSE:
		vmop_result_read(imsg, &vmr);
		res = vmr.vmr_result;

		switch (res) {
		case 0:
			fprintf(stderr, "requested to shutdown vm %d\n",
			    vmr.vmr_id);
			*ret = 0;
			break;
		case VMD_VM_STOP_INVALID:
			fprintf(stderr,
			    "cannot stop vm that is not running\n");
			*ret = EINVAL;
			break;
		case ENOENT:
			fprintf(stderr, "vm not found\n");
			*ret = EIO;
			break;
		case EINTR:
			fprintf(stderr, "interrupted call\n");
			*ret = EIO;
			break;
		default:
			errno = res;
			fprintf(stderr, "failed: %s\n",
			    strerror(res));
			*ret = EIO;
		}
		break;
	case IMSG_VMDOP_TERMINATE_VM_EVENT:
		vmop_result_read(imsg, &vmr);
		if (flags & VMOP_WAIT) {
			fprintf(stderr, "terminated vm %d\n", vmr.vmr_id);
		} else if (flags & VMOP_FORCE) {
			fprintf(stderr, "forced to terminate vm %d\n",
			    vmr.vmr_id);
		}
		*ret = 0;
		break;
	default:
		fprintf(stderr, "unexpected response received from vmd\n");
		*ret = EINVAL;
	}
	errno = *ret;

	return (1);
}

/*
 * terminate_all
 *
 * Request to stop all VMs gracefully
 *
 * Parameters
 *  list: the vm information (consolidated) returned from vmd via imsg
 *  ct  : the size (number of elements in 'list') of the result
 *  flags: VMOP_FORCE or VMOP_WAIT flags
 */
void
terminate_all(struct vmop_info_result *list, size_t ct, unsigned int flags)
{
	struct vm_info_result *vir;
	struct vmop_info_result *vmi;
	struct parse_result res;
	size_t i;

	for (i = 0; i < ct; i++) {
		vmi = &list[i];
		vir = &vmi->vir_info;

		/* The VM is already stopped */
		if (vir->vir_creator_pid == 0 || vir->vir_id == 0)
			continue;

		memset(&res, 0, sizeof(res));
		res.action = CMD_STOP;
		res.id = 0;
		res.flags = info_flags;

		if ((res.name = strdup(vir->vir_name)) == NULL)
			errx(1, "strdup");

		vmmaction(&res);
	}
}

/*
 * waitfor_vm
 *
 * Wait until vmd stopped the indicated VM
 *
 * Parameters:
 *  terminate_id: ID of the vm to be terminated
 *  name: optional name of the VM to be terminated
 */
void
waitfor_vm(uint32_t terminate_id, const char *name)
{
	struct vmop_id vid;

	memset(&vid, 0, sizeof(vid));
	vid.vid_id = terminate_id;
	if (name != NULL) {
		(void)strlcpy(vid.vid_name, name, sizeof(vid.vid_name));
		fprintf(stderr, "waiting for vm %s: ", name);
	} else {
		fprintf(stderr, "waiting for vm: ");
	}

	imsg_compose(ibuf, IMSG_VMDOP_WAIT_VM_REQUEST,
	    0, 0, -1, &vid, sizeof(vid));
}

/*
 * get_info_vm
 *
 * Return the list of all running VMs or find a specific VM by ID or name.
 *
 * Parameters:
 *  id: optional ID of a VM to list
 *  name: optional name of a VM to list
 *  action: if CMD_CONSOLE or CMD_STOP open a console or terminate the VM.
 *  flags: optional flags used by the CMD_STOP action.
 *
 * Request a list of running VMs from vmd
 */
void
get_info_vm(uint32_t id, const char *name, enum actions action,
    unsigned int flags)
{
	info_id = id;
	if (name != NULL)
		(void)strlcpy(info_name, name, sizeof(info_name));
	info_action = action;
	info_flags = flags;
	imsg_compose(ibuf, IMSG_VMDOP_GET_INFO_VM_REQUEST, 0, 0, -1, NULL, 0);
}

/*
 * check_info_id
 *
 * Check if requested name or ID of a VM matches specified arguments
 *
 * Parameters:
 *  name: name of the VM
 *  id: ID of the VM
 */
int
check_info_id(const char *name, uint32_t id)
{
	if (info_id == 0 && *info_name == '\0')
		return (-1);
	if (info_id != 0 && info_id == id)
		return (1);
	if (*info_name != '\0' && name && strcmp(info_name, name) == 0)
		return (1);
	return (0);
}

/*
 * add_info
 *
 * Callback function invoked when we are expecting an
 * IMSG_VMDOP_GET_INFO_VM_DATA message indicating the receipt of additional
 * "list vm" data, or an IMSG_VMDOP_GET_INFO_VM_END_DATA message indicating
 * that no additional "list vm" data will be forthcoming.
 *
 * Parameters:
 *  imsg : response imsg received from vmd
 *  ret  : return value
 *
 * Return:
 *  0     : the returned data was successfully added to the "list vm" data.
 *          The caller can expect more data.
 *  1     : IMSG_VMDOP_GET_INFO_VM_END_DATA received (caller should not call
 *          add_info again), or an error occurred adding the returned data
 *          to the "list vm" data. The caller should check the value of
 *          'ret' to determine which case occurred.
 *
 * This function does not return if a VM is found and info_action is CMD_CONSOLE
 *
 *  The function also sets 'ret' to the error code as follows:
 *   0     : Message successfully processed
 *   EINVAL: Invalid or unexpected response from vmd
 *   ENOMEM: memory allocation failure
 *   ENOENT: no entries
 */
int
add_info(struct imsg *imsg, int *ret)
{
	static size_t ct = 0;
	static struct vmop_info_result *vir = NULL;
	uint32_t type;

	*ret = 0;

	type = imsg_get_type(imsg);
	if (type == IMSG_VMDOP_GET_INFO_VM_DATA) {
		vir = reallocarray(vir, ct + 1,
		    sizeof(struct vmop_info_result));
		if (vir == NULL) {
			*ret = ENOMEM;
			return (1);
		}
		vmop_info_result_read(imsg, &vir[ct]);
		ct++;
		return (0);
	} else if (type == IMSG_VMDOP_GET_INFO_VM_END_DATA) {
		switch (info_action) {
		case CMD_CONSOLE:
			vm_console(vir, ct);
			break;
		case CMD_STOPALL:
			terminate_all(vir, ct, info_flags);
			break;
		default:
			*ret = print_vm_info(vir, ct);
			break;
		}
		free(vir);
		return (1);
	} else {
		*ret = EINVAL;
		return (1);
	}
}

/*
 * vm_state
 *
 * Returns a string representing the current VM state, note that the order
 * matters. A paused VM does have the VM_STATE_RUNNING bit set, but
 * VM_STATE_PAUSED is more significant to report. Same goes for stopping VMs.
 *
 * Parameters
 *  vm_state: mask indicating the vm state
 */
const char *
vm_state(unsigned int mask)
{
	if (mask & VM_STATE_PAUSED)
		return "paused";
	else if (mask & VM_STATE_WAITING)
		return "waiting";
	else if (mask & VM_STATE_SHUTDOWN)
		return "stopping";
	else if (mask & VM_STATE_RUNNING)
		return "running";
	/* Presence of absence of other flags */
	else if (!mask || (mask & VM_STATE_DISABLED))
		return "stopped";

	return "unknown";
}

/*
 * print_vm_info
 *
 * Prints the vm information returned from vmd in 'list' to stdout.
 *
 * Parameters
 *  list: the vm information (consolidated) returned from vmd via imsg
 *  ct  : the size (number of elements in 'list') of the result
 *
 * Return values:
 *  0: no error
 *  ENOENT: no entries printed
 */
int
print_vm_info(struct vmop_info_result *list, size_t ct)
{
	struct vm_info_result *vir;
	struct vmop_info_result *vmi;
	size_t i;
	char *tty;
	char curmem[FMT_SCALED_STRSIZE];
	char maxmem[FMT_SCALED_STRSIZE];
	char user[16], group[16];
	const char *name;
	int running, found_running;
	extern int stat_rflag;

	found_running = 0;

	printf("%5s %5s %5s %7s %7s %7s %12s %8s %s\n", "ID", "PID", "VCPUS",
	    "MAXMEM", "CURMEM", "TTY", "OWNER", "STATE", "NAME");

	for (i = 0; i < ct; i++) {
		vmi = &list[i];
		vir = &vmi->vir_info;
		running = (vir->vir_creator_pid != 0 && vir->vir_id != 0);
		if (!running && stat_rflag)
			continue;

		found_running++;

		if (check_info_id(vir->vir_name, vir->vir_id)) {
			/* get user name */
			name = user_from_uid(vmi->vir_uid, 1);
			if (name == NULL)
				(void)snprintf(user, sizeof(user),
				    "%d", vmi->vir_uid);
			else
				(void)strlcpy(user, name, sizeof(user));
			/* get group name */
			if (vmi->vir_gid != -1) {
				name = group_from_gid(vmi->vir_gid, 1);
				if (name == NULL)
					(void)snprintf(group, sizeof(group),
					    ":%lld", vmi->vir_gid);
				else
					(void)snprintf(group, sizeof(group),
					    ":%s", name);
				(void)strlcat(user, group, sizeof(user));
			}

			(void)strlcpy(curmem, "-", sizeof(curmem));
			(void)strlcpy(maxmem, "-", sizeof(maxmem));

			(void)fmt_scaled(vir->vir_memory_size, maxmem);

			if (running) {
				if (*vmi->vir_ttyname == '\0')
					tty = "-";
				/* get tty - skip /dev/ path */
				else if ((tty = strrchr(vmi->vir_ttyname,
				    '/')) == NULL || *++tty == '\0')
					tty = list[i].vir_ttyname;

				(void)fmt_scaled(vir->vir_used_size, curmem);

				/* running vm */
				printf("%5u %5u %5zd %7s %7s %7s %12s %8s %s\n",
				    vir->vir_id, vir->vir_creator_pid,
				    vir->vir_ncpus, maxmem, curmem,
				    tty, user, vm_state(vmi->vir_state),
				    vir->vir_name);
			} else {
				/* disabled vm */
				printf("%5u %5s %5zd %7s %7s %7s %12s %8s %s\n",
				    vir->vir_id, "-",
				    vir->vir_ncpus, maxmem, curmem,
				    "-", user, vm_state(vmi->vir_state),
				    vir->vir_name);
			}
		}
	}

	if (found_running)
		return (0);
	else
		return (ENOENT);
}

/*
 * vm_console
 *
 * Connects to the vm console returned from vmd in 'list'.
 *
 * Parameters
 *  list: the vm information (consolidated) returned from vmd via imsg
 *  ct  : the size (number of elements in 'list') of the result
 */
__dead void
vm_console(struct vmop_info_result *list, size_t ct)
{
	struct vmop_info_result *vir;
	size_t i;

	for (i = 0; i < ct; i++) {
		vir = &list[i];
		if ((check_info_id(vir->vir_info.vir_name,
		    vir->vir_info.vir_id) > 0) &&
			(vir->vir_ttyname[0] != '\0')) {
			/* does not return */
			ctl_openconsole(vir->vir_ttyname);
		}
	}

	errx(1, "console not found");
}

/*
 * open_imagefile
 *
 * Open an imagefile with the specified type, path and size.
 *
 * Parameters:
 *  type        : format of the image file
 *  imgfile_path: path to the image file to create
 *  flags       : flags for open(2), e.g. O_RDONLY
 *  file        : file structure
 *  sz		: size of the image file
 *
 * Return:
 *  fd          : Returns file descriptor of the new image file
 *  -1          : Operation failed.  errno is set.
 */
int
open_imagefile(int type, const char *imgfile_path, int flags,
    struct virtio_backing *file, off_t *sz)
{
	int	 fd, ret, basefd[VM_MAX_BASE_PER_DISK], nfd, i;
	char	 path[PATH_MAX];

	*sz = 0;
	if ((fd = open(imgfile_path, flags)) == -1)
		return (-1);

	basefd[0] = fd;
	nfd = 1;

	errno = 0;
	switch (type) {
	case VMDF_QCOW2:
		if (strlcpy(path, imgfile_path, sizeof(path)) >= sizeof(path))
			return (-1);
		for (i = 0; i < VM_MAX_BASE_PER_DISK - 1; i++, nfd++) {
			if ((ret = virtio_qcow2_get_base(basefd[i],
			    path, sizeof(path), imgfile_path)) == -1) {
				log_debug("%s: failed to get base %d", __func__, i);
				return -1;
			} else if (ret == 0)
				break;

			if ((basefd[i + 1] = open(path, O_RDONLY)) == -1) {
				log_warn("%s: failed to open base %s",
				    __func__, path);
				return (-1);
			}
		}
		ret = virtio_qcow2_init(file, sz, basefd, nfd);
		break;
	default:
		ret = virtio_raw_init(file, sz, &fd, 1);
		break;
	}

	if (ret == -1) {
		for (i = 0; i < nfd; i++)
			close(basefd[i]);
		return (-1);
	}

	return (fd);
}

/*
 * create_imagefile
 *
 * Create an empty imagefile with the specified type, path and size.
 *
 * Parameters:
 *  type        : format of the image file
 *  imgfile_path: path to the image file to create
 *  base_path   : path to the qcow2 base image
 *  imgsize     : size of the image file to create (in bytes)
 *  format      : string identifying the format
 *
 * Return:
 *  EEXIST: The requested image file already exists
 *  0     : Image file successfully created
 *  Exxxx : Various other Exxxx errno codes due to other I/O errors
 */
int
create_imagefile(int type, const char *imgfile_path, const char *base_path,
    uint64_t imgsize, const char **format)
{
	int	 ret;

	switch (type) {
	case VMDF_QCOW2:
		*format = "qcow2";
		ret = virtio_qcow2_create(imgfile_path, base_path, imgsize);
		break;
	default:
		*format = "raw";
		ret = virtio_raw_create(imgfile_path, imgsize);
		break;
	}

	return (ret);
}

void
vmop_result_read(struct imsg *imsg, struct vmop_result *vmr)
{
	if (imsg_get_data(imsg, vmr, sizeof(*vmr)))
		fatal("%s", __func__);

	vmr->vmr_ttyname[sizeof(vmr->vmr_ttyname) - 1] = '\0';
}

void
vmop_info_result_read(struct imsg *imsg, struct vmop_info_result *vir)
{
	struct vm_info_result *r;

	if (imsg_get_data(imsg, vir, sizeof(*vir)))
		fatal("%s", __func__);

	r = &vir->vir_info;
	r->vir_name[sizeof(r->vir_name) - 1] = '\0';

	vir->vir_ttyname[sizeof(vir->vir_ttyname) - 1] = '\0';
}

int
imsg_int_read(struct imsg *imsg)
{
	int val;

	if (imsg_get_data(imsg, &val, sizeof(val)))
		fatal("%s", __func__);

	return (val);
}
