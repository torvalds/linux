/*	$OpenBSD: vm_agentx.c,v 1.5 2025/08/13 10:26:31 dv Exp $ */

/*
 * Copyright (c) 2022 Martijn van Duren <martijn@openbsd.org>
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

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/un.h>

#include <agentx.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "proc.h"
#include "vmd.h"

struct conn {
	struct event ev;
	struct agentx *agentx;
};

void vm_agentx_run(struct privsep *, struct privsep_proc *, void *);
int vm_agentx_dispatch_parent(int, struct privsep_proc *, struct imsg *);
void vm_agentx_configure(struct vmd_agentx *);
static void vm_agentx_nofd(struct agentx *, void *, int);
static void vm_agentx_tryconnect(int, short, void *);
static void vm_agentx_read(int, short, void *);
static void vm_agentx_flush_pending(void);
static int vm_agentx_sortvir(const void *, const void *);
static int vm_agentx_adminstate(int);
static int vm_agentx_operstate(int);
static void vm_agentx_vmHvSoftware(struct agentx_varbind *);
static void vm_agentx_vmHvVersion(struct agentx_varbind *);
static void vm_agentx_vmHvObjectID(struct agentx_varbind *);
static void vm_agentx_vminfo(struct agentx_varbind *);

static struct agentx_index *vmIndex;
static struct agentx_object *vmNumber, *vmName, *vmUUID, *vmOSType;
static struct agentx_object *vmAdminState, *vmOperState, *vmAutoStart;
static struct agentx_object *vmPersistent, *vmCurCpuNumber, *vmMinCpuNumber;
static struct agentx_object *vmMaxCpuNumber, *vmMemUnit, *vmCurMem, *vmMinMem;
static struct agentx_object *vmMaxMem;

static struct vmd_agentx *vmd_agentx;

static struct agentx_varbind **vminfo = NULL;
static size_t vminfolen = 0;
static size_t vminfosize = 0;
static int vmcollecting = 0;

#define VMMIB AGENTX_MIB2, 236
#define VMOBJECTS VMMIB, 1
#define VMHVSOFTWARE VMOBJECTS, 1, 1
#define VMHVVERSION VMOBJECTS, 1, 2
#define VMHVOBJECTID VMOBJECTS, 1, 3
#define VMNUMBER VMOBJECTS, 2
#define VMENTRY VMOBJECTS, 4, 1
#define VMINDEX VMENTRY, 1
#define VMNAME VMENTRY, 2
#define VMUUID VMENTRY, 3
#define VMOSTYPE VMENTRY, 4
#define VMADMINSTATE VMENTRY, 5
#define VMOPERSTATE VMENTRY, 6
#define VMAUTOSTART VMENTRY, 7
#define VMPERSISTENT VMENTRY, 8
#define VMCURCPUNUMBER VMENTRY, 9
#define VMMINCPUNUMBER VMENTRY, 10
#define VMMAXCPUNUMBER VMENTRY, 11
#define VMMEMUNIT VMENTRY, 12
#define VMCURMEM VMENTRY, 13
#define VMMINMEM VMENTRY, 14
#define VMMAXMEM VMENTRY, 15

#define AGENTX_GROUP "_agentx"
#define MEM_SCALE (1024 * 1024)

static struct privsep_proc procs[] = {
	{ "parent",	PROC_PARENT,	vm_agentx_dispatch_parent  },
};

void
vm_agentx(struct privsep *ps, struct privsep_proc *p)
{
	struct group *grp;

	/*
	 * Make sure we can connect to /var/agentx/master with the correct
	 * group permissions.
	 */
	if ((grp = getgrnam(AGENTX_GROUP)) == NULL)
		fatal("failed to get group: %s", AGENTX_GROUP);
	ps->ps_pw->pw_gid = grp->gr_gid;

	proc_run(ps, p, procs, nitems(procs), vm_agentx_run, NULL);
}

void
vm_agentx_shutdown(void)
{
}

void
vm_agentx_run(struct privsep *ps, struct privsep_proc *p, void *arg)
{
	/*
	 * pledge in agentx process
	 * stdio - for malloc and basic I/O including events.
	 * recvfd - for the proc fd exchange.
	 * unix - for access to the agentx master socket.
	 */
	if (pledge("stdio recvfd unix", NULL) == -1)
		fatal("pledge");
}

int
vm_agentx_dispatch_parent(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	static struct vmop_info_result *vir = NULL;
	static struct vmop_info_result *mvir = NULL;
	struct vmd *env = p->p_ps->ps_env;
	struct vmop_info_result *tvir;
	struct agentx_object *reqObject;
	static size_t nvir = 0;
	static size_t virlen = 0;
	static int error = 0;
	size_t i, j, index;
	enum agentx_request_type rtype;
	uint32_t type;

	type = imsg_get_type(imsg);
	switch (type) {
	case IMSG_VMDOP_GET_INFO_VM_DATA:
		if (error)
			break;
		if (nvir + 1 > virlen) {
			tvir = reallocarray(vir, virlen + 10, sizeof(*vir));
			if (tvir == NULL) {
				log_warn("%s: Couldn't dispatch vm information",
				    __func__);
				error = 1;
				break;
			}
			virlen += 10;
			vir = tvir;
		}
		vmop_info_result_read(imsg, &(vir[nvir++]));
		break;
	case IMSG_VMDOP_GET_INFO_VM_END_DATA:
		vmcollecting = 0;
		if (error) {
			for (i = 0; i < vminfolen; i++)
				agentx_varbind_error(vminfo[i]);
			vminfolen = 0;
			error = 0;
			nvir = 0;
			return (0);
		}

		qsort(vir, nvir, sizeof(*vir), vm_agentx_sortvir);
		for (i = 0; i < vminfolen; i++) {
			reqObject = agentx_varbind_get_object(vminfo[i]);
			if (reqObject == vmNumber) {
				agentx_varbind_integer(vminfo[i],
				    (int32_t)nvir);
				continue;
			}
			index = agentx_varbind_get_index_integer(vminfo[i],
			    vmIndex);
			rtype = agentx_varbind_request(vminfo[i]);
			for (j = 0; j < nvir; j++) {
				if (vir[j].vir_info.vir_id < index)
					continue;
				if (vir[j].vir_info.vir_id == index &&
				    (rtype == AGENTX_REQUEST_TYPE_GET ||
				    rtype ==
				    AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE))
					break;
				if (vir[j].vir_info.vir_id > index &&
				    (rtype == AGENTX_REQUEST_TYPE_GETNEXT ||
				    rtype ==
				    AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE))
					break;
			}
			if (j == nvir) {
				agentx_varbind_notfound(vminfo[i]);
				continue;
			}
			mvir = &(vir[j]);
			agentx_varbind_set_index_integer(vminfo[i], vmIndex,
			    mvir->vir_info.vir_id);
			if (reqObject == vmName)
				agentx_varbind_string(vminfo[i],
				    mvir->vir_info.vir_name);
			else if (reqObject == vmUUID)
				agentx_varbind_string(vminfo[i], "");
			else if (reqObject == vmOSType)
				agentx_varbind_string(vminfo[i], "");
			else if (reqObject == vmAdminState)
				agentx_varbind_integer(vminfo[i],
				    vm_agentx_adminstate(mvir->vir_state));
			else if (reqObject == vmOperState)
				agentx_varbind_integer(vminfo[i],
				    vm_agentx_operstate(mvir->vir_state));
			else if (reqObject == vmAutoStart)
				agentx_varbind_integer(vminfo[i],
				    mvir->vir_state & VM_STATE_DISABLED ?
				    3 : 2);
/* XXX We can dynamically create vm's but I don't know how to differentiate */
			else if (reqObject == vmPersistent)
				agentx_varbind_integer(vminfo[i], 1);
/* We currently only support a single CPU */
			else if (reqObject == vmCurCpuNumber ||
			    reqObject == vmMinCpuNumber ||
			    reqObject == vmMaxCpuNumber)
				agentx_varbind_integer(vminfo[i],
				    mvir->vir_info.vir_ncpus);
			else if (reqObject == vmMemUnit)
				agentx_varbind_integer(vminfo[i], MEM_SCALE);
			else if (reqObject == vmCurMem)
				agentx_varbind_integer(vminfo[i],
				    mvir->vir_info.vir_used_size / MEM_SCALE);
			else if (reqObject == vmMinMem)
				agentx_varbind_integer(vminfo[i], -1);
			else if (reqObject == vmMaxMem)
				agentx_varbind_integer(vminfo[i],
				    mvir->vir_info.vir_memory_size / MEM_SCALE);
/* We probably had a reload */
			else
				agentx_varbind_notfound(vminfo[i]);
		}
		vminfolen = 0;
		nvir = 0;
		break;
	case IMSG_VMDOP_CONFIG:
		config_getconfig(env, imsg);
		vm_agentx_configure(&env->vmd_cfg.cfg_agentx);
		break;
	default:
		return (-1);
	}
	return (0);
}

void
vm_agentx_configure(struct vmd_agentx *env)
{
	static char curpath[sizeof(env->ax_path)];
	static char curcontext[sizeof(env->ax_context)];
	static struct conn *conn;
	static struct agentx_session *sess;
	static struct agentx_context *ctx;
	struct agentx_region *vmMIB;
	char *context = env->ax_context[0] == '\0' ? NULL : env->ax_context;
	int changed = 0;

	vmd_agentx = env;

	agentx_log_fatal = fatalx;
	agentx_log_warn = log_warnx;
	agentx_log_info = log_info;
	agentx_log_debug = log_debug;

	if (!vmd_agentx->ax_enabled) {
		if (conn != NULL) {
			agentx_free(conn->agentx);
			conn = NULL;
			sess = NULL;
			ctx = NULL;
			vm_agentx_flush_pending();
		}
		return;
	}

	if (strcmp(curpath, vmd_agentx->ax_path) != 0 || conn == NULL) {
		if (conn != NULL) {
			agentx_free(conn->agentx);
			conn = NULL;
			sess = NULL;
			ctx = NULL;
			vm_agentx_flush_pending();
		}

		if ((conn = malloc(sizeof(*conn))) == NULL)
			fatal(NULL);
		/* Set to something so we can safely event_del */
		evtimer_set(&conn->ev, vm_agentx_tryconnect, conn);
		/* result assigned in vm_agentx_nofd */
		if (agentx(vm_agentx_nofd, conn) == NULL)
			fatal("Can't setup agentx");
		sess = agentx_session(conn->agentx, NULL, 0, "vmd", 0);
		if (sess == NULL)
			fatal("Can't setup agentx session");
		(void) strlcpy(curpath, vmd_agentx->ax_path, sizeof(curpath));
		changed = 1;
	}

	if (strcmp(curcontext, vmd_agentx->ax_context) != 0 || changed) {
		if (!changed) {
			agentx_context_free(ctx);
			vm_agentx_flush_pending();
		}
		if ((ctx = agentx_context(sess, context)) == NULL)
			fatal("Can't setup agentx context");
		strlcpy(curcontext, vmd_agentx->ax_context, sizeof(curcontext));
		changed = 1;
	}

	if (!changed)
		return;

	if ((vmMIB = agentx_region(ctx, AGENTX_OID(VMMIB), 1)) == NULL)
		fatal("agentx_region vmMIB");

	if ((vmIndex = agentx_index_integer_dynamic(vmMIB,
	    AGENTX_OID(VMINDEX))) == NULL)
		fatal("agentx_index_integer_dynamic");
        if ((agentx_object(vmMIB, AGENTX_OID(VMHVSOFTWARE), NULL, 0, 0,
	    vm_agentx_vmHvSoftware)) == NULL ||
            (agentx_object(vmMIB, AGENTX_OID(VMHVVERSION), NULL, 0, 0,
	    vm_agentx_vmHvVersion)) == NULL ||
            (agentx_object(vmMIB, AGENTX_OID(VMHVOBJECTID), NULL, 0, 0,
	    vm_agentx_vmHvObjectID)) == NULL ||
            (vmNumber = agentx_object(vmMIB, AGENTX_OID(VMNUMBER), NULL, 0, 0,
	    vm_agentx_vminfo)) == NULL ||
            (vmName = agentx_object(vmMIB, AGENTX_OID(VMNAME), &vmIndex, 1, 0,
	    vm_agentx_vminfo)) == NULL ||
            (vmUUID = agentx_object(vmMIB, AGENTX_OID(VMUUID), &vmIndex, 1, 0,
	    vm_agentx_vminfo)) == NULL ||
            (vmOSType = agentx_object(vmMIB, AGENTX_OID(VMOSTYPE), &vmIndex, 1,
	    0, vm_agentx_vminfo)) == NULL ||
            (vmAdminState = agentx_object(vmMIB, AGENTX_OID(VMADMINSTATE),
	    &vmIndex, 1, 0, vm_agentx_vminfo)) == NULL ||
            (vmOperState = agentx_object(vmMIB, AGENTX_OID(VMOPERSTATE),
	    &vmIndex, 1, 0, vm_agentx_vminfo)) == NULL ||
            (vmAutoStart = agentx_object(vmMIB, AGENTX_OID(VMAUTOSTART),
	    &vmIndex, 1, 0, vm_agentx_vminfo)) == NULL ||
            (vmPersistent = agentx_object(vmMIB, AGENTX_OID(VMPERSISTENT),
	    &vmIndex, 1, 0, vm_agentx_vminfo)) == NULL ||
            (vmCurCpuNumber = agentx_object(vmMIB, AGENTX_OID(VMCURCPUNUMBER),
	    &vmIndex, 1, 0, vm_agentx_vminfo)) == NULL ||
            (vmMinCpuNumber = agentx_object(vmMIB, AGENTX_OID(VMMINCPUNUMBER),
	    &vmIndex, 1, 0, vm_agentx_vminfo)) == NULL ||
            (vmMaxCpuNumber = agentx_object(vmMIB, AGENTX_OID(VMMAXCPUNUMBER),
	    &vmIndex, 1, 0, vm_agentx_vminfo)) == NULL ||
            (vmMemUnit = agentx_object(vmMIB, AGENTX_OID(VMMEMUNIT),
	    &vmIndex, 1, 0, vm_agentx_vminfo)) == NULL ||
            (vmCurMem = agentx_object(vmMIB, AGENTX_OID(VMCURMEM),
	    &vmIndex, 1, 0, vm_agentx_vminfo)) == NULL ||
            (vmMinMem = agentx_object(vmMIB, AGENTX_OID(VMMINMEM),
	    &vmIndex, 1, 0, vm_agentx_vminfo)) == NULL ||
            (vmMaxMem = agentx_object(vmMIB, AGENTX_OID(VMMAXMEM),
	    &vmIndex, 1, 0, vm_agentx_vminfo)) == NULL)
                fatal("agentx_object_ro");
}

static void
vm_agentx_nofd(struct agentx *agentx, void *cookie, int close)
{
	struct conn *conn = cookie;

	conn->agentx = agentx;
	event_del(&conn->ev);
	if (close)
		free(conn);
	else
		vm_agentx_tryconnect(-1, 0, conn);
}

static void
vm_agentx_tryconnect(int fd, short event, void *cookie)
{
	struct sockaddr_un sun;
	struct timeval timeout = {3, 0};
	struct conn *conn = cookie;

	sun.sun_len = sizeof(sun);
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, vmd_agentx->ax_path, sizeof(sun.sun_path));
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		log_warn("socket");
		goto fail;
	} else if (connect(fd, (struct sockaddr *)&sun, sun.sun_len) == -1) {
		log_warn("connect");
		close(fd);
		goto fail;
	}
	agentx_connect(conn->agentx, fd);

	event_set(&conn->ev, fd, EV_READ|EV_PERSIST, vm_agentx_read, conn);
	event_add(&conn->ev, NULL);

	return;
fail:
	evtimer_set(&conn->ev, vm_agentx_tryconnect, conn);
	evtimer_add(&conn->ev, &timeout);
}

static void
vm_agentx_read(int fd, short event, void *cookie)
{
	struct conn *conn = cookie;

	agentx_read(conn->agentx);
}

static void
vm_agentx_flush_pending(void)
{
	size_t i;

	for (i = 0; i < vminfolen; i++)
		agentx_varbind_notfound(vminfo[i]);
	vminfolen = 0;
}

static int
vm_agentx_sortvir(const void *c1, const void *c2)
{
	const struct vmop_info_result *v1 = c1, *v2 = c2;

	return (v1->vir_info.vir_id < v2->vir_info.vir_id ? -1 :
	    v1->vir_info.vir_id > v2->vir_info.vir_id);
}

static int
vm_agentx_adminstate(int mask)
{
	if (mask & VM_STATE_PAUSED)
                return (3);
        else if (mask & VM_STATE_RUNNING)
                return (1);
        else if (mask & VM_STATE_SHUTDOWN)
                return (4);
        /* Presence of absence of other flags */
        else if (!mask || (mask & VM_STATE_DISABLED))
                return (4);

        return 4;
}

static int
vm_agentx_operstate(int mask)
{
	if (mask & VM_STATE_PAUSED)
                return (8);
        else if (mask & VM_STATE_RUNNING)
                return (4);
        else if (mask & VM_STATE_SHUTDOWN)
                return (11);
        /* Presence of absence of other flags */
        else if (!mask || (mask & VM_STATE_DISABLED))
                return (11);

        return (11);
}

static void
vm_agentx_vmHvSoftware(struct agentx_varbind *vb)
{
	agentx_varbind_string(vb, "OpenBSD VMM");
}

static void
vm_agentx_vmHvVersion(struct agentx_varbind *vb)
{
	int osversid[] = {CTL_KERN, KERN_OSRELEASE};
	static char osvers[10] = "";
	size_t osverslen;

	if (osvers[0] == '\0') {
		osverslen = sizeof(osvers);
		if (sysctl(osversid, 2, osvers, &osverslen, NULL,
		    0) == -1) {
			log_warn("Failed vmHvVersion sysctl");
			agentx_varbind_string(vb, "unknown");
			return;
		}
		if (osverslen >= sizeof(osvers))
			osverslen = sizeof(osvers) - 1;
		osvers[osverslen] = '\0';
	}
	agentx_varbind_string(vb, osvers);
}

static void
vm_agentx_vmHvObjectID(struct agentx_varbind *vb)
{
	agentx_varbind_oid(vb, AGENTX_OID(0, 0));
}

static void
vm_agentx_vminfo(struct agentx_varbind *vb)
{
	extern struct vmd *env;
	struct agentx_varbind **tvminfo;

	if (vminfolen >= vminfosize) {
		if ((tvminfo = reallocarray(vminfo, vminfosize + 10,
		    sizeof(*vminfo))) == NULL) {
			log_warn("%s: Couldn't retrieve vm information",
			    __func__);
			agentx_varbind_error(vb);
			return;
		}
		vminfo = tvminfo;
		vminfosize += 10;
	}

	if (!vmcollecting) {
		if (proc_compose_imsg(&(env->vmd_ps), PROC_PARENT,
		    IMSG_VMDOP_GET_INFO_VM_REQUEST, IMSG_AGENTX_PEERID,
		    -1, NULL, 0) == -1) {
			log_warn("%s: Couldn't retrieve vm information",
			    __func__);
			agentx_varbind_error(vb);
			return;
		}
		vmcollecting = 1;
	}

	vminfo[vminfolen++] = vb;
}
