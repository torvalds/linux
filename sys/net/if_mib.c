/*-
 * Copyright 1996 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 * 
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_mib.h>
#include <net/vnet.h>

/*
 * A sysctl(3) MIB for generic interface information.  This information
 * is exported in the net.link.generic branch, which has the following
 * structure:
 *
 * net.link.generic	.system			- system-wide control variables
 *						  and statistics (node)
 *			.ifdata.<ifindex>.general
 *						- what's in `struct ifdata'
 *						  plus some other info
 *			.ifdata.<ifindex>.linkspecific
 *						- a link-type-specific data
 *						  structure (as might be used
 *						  by an SNMP agent
 *
 * Perhaps someday we will make addresses accessible via this interface
 * as well (then there will be four such...).  The reason that the
 * index comes before the last element in the name is because it
 * seems more orthogonal that way, particularly with the possibility
 * of other per-interface data living down here as well (e.g., integrated
 * services stuff).
 */

SYSCTL_DECL(_net_link_generic);
static SYSCTL_NODE(_net_link_generic, IFMIB_SYSTEM, system, CTLFLAG_RW, 0,
	    "Variables global to all interfaces");

SYSCTL_INT(_net_link_generic_system, IFMIB_IFCOUNT, ifcount,
	CTLFLAG_VNET | CTLFLAG_RD, &VNET_NAME(if_index), 0,
	"Number of configured interfaces");

static int
sysctl_ifdata(SYSCTL_HANDLER_ARGS) /* XXX bad syntax! */
{
	int *name = (int *)arg1;
	int error;
	u_int namelen = arg2;
	struct ifnet *ifp;
	struct ifmibdata ifmd;
	size_t dlen;
	char *dbuf;

	if (namelen != 2)
		return EINVAL;
	if (name[0] <= 0)
		return (ENOENT);
	ifp = ifnet_byindex_ref(name[0]);
	if (ifp == NULL)
		return (ENOENT);

	switch(name[1]) {
	default:
		error = ENOENT;
		goto out;

	case IFDATA_GENERAL:
		bzero(&ifmd, sizeof(ifmd));
		strlcpy(ifmd.ifmd_name, ifp->if_xname, sizeof(ifmd.ifmd_name));

		ifmd.ifmd_pcount = ifp->if_pcount;
		if_data_copy(ifp, &ifmd.ifmd_data);

		ifmd.ifmd_flags = ifp->if_flags | ifp->if_drv_flags;
		ifmd.ifmd_snd_len = ifp->if_snd.ifq_len;
		ifmd.ifmd_snd_maxlen = ifp->if_snd.ifq_maxlen;
		ifmd.ifmd_snd_drops =
		    ifp->if_get_counter(ifp, IFCOUNTER_OQDROPS);

		error = SYSCTL_OUT(req, &ifmd, sizeof ifmd);
		if (error)
			goto out;
		break;

	case IFDATA_LINKSPECIFIC:
		error = SYSCTL_OUT(req, ifp->if_linkmib, ifp->if_linkmiblen);
		if (error || !req->newptr)
			goto out;

		error = SYSCTL_IN(req, ifp->if_linkmib, ifp->if_linkmiblen);
		if (error)
			goto out;
		break;

	case IFDATA_DRIVERNAME:
		/* 20 is enough for 64bit ints */
		dlen = strlen(ifp->if_dname) + 20 + 1;
		if ((dbuf = malloc(dlen, M_TEMP, M_NOWAIT)) == NULL) {
			error = ENOMEM;
			goto out;
		}
		if (ifp->if_dunit == IF_DUNIT_NONE)
			strcpy(dbuf, ifp->if_dname);
		else
			sprintf(dbuf, "%s%d", ifp->if_dname, ifp->if_dunit);

		error = SYSCTL_OUT(req, dbuf, strlen(dbuf) + 1);
		if (error == 0 && req->newptr != NULL)
			error = EPERM;
		free(dbuf, M_TEMP);
		goto out;
	}
out:
	if_rele(ifp);
	return error;
}

static SYSCTL_NODE(_net_link_generic, IFMIB_IFDATA, ifdata, CTLFLAG_RW,
	    sysctl_ifdata, "Interface table");

