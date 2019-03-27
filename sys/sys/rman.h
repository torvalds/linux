/*-
 * SPDX-License-Identifier: MIT
 *
 * Copyright 1998 Massachusetts Institute of Technology
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

#ifndef _SYS_RMAN_H_
#define	_SYS_RMAN_H_	1

#ifndef	_KERNEL
#include <sys/queue.h>
#else
#include <machine/_bus.h>
#include <machine/resource.h>
#endif

#define	RF_ALLOCATED	0x0001	/* resource has been reserved */
#define	RF_ACTIVE	0x0002	/* resource allocation has been activated */
#define	RF_SHAREABLE	0x0004	/* resource permits contemporaneous sharing */
#define	RF_SPARE1	0x0008
#define	RF_SPARE2	0x0010
#define	RF_FIRSTSHARE	0x0020	/* first in sharing list */
#define	RF_PREFETCHABLE	0x0040	/* resource is prefetchable */
#define	RF_OPTIONAL	0x0080	/* for bus_alloc_resources() */
#define	RF_UNMAPPED	0x0100	/* don't map resource when activating */

#define	RF_ALIGNMENT_SHIFT	10 /* alignment size bit starts bit 10 */
#define	RF_ALIGNMENT_MASK	(0x003F << RF_ALIGNMENT_SHIFT)
				/* resource address alignment size bit mask */
#define	RF_ALIGNMENT_LOG2(x)	((x) << RF_ALIGNMENT_SHIFT)
#define	RF_ALIGNMENT(x)		(((x) & RF_ALIGNMENT_MASK) >> RF_ALIGNMENT_SHIFT)

enum	rman_type { RMAN_UNINIT = 0, RMAN_GAUGE, RMAN_ARRAY };

/*
 * String length exported to userspace for resource names, etc.
 */
#define RM_TEXTLEN	32

#define	RM_MAX_END	(~(rman_res_t)0)

#define	RMAN_IS_DEFAULT_RANGE(s,e)	((s) == 0 && (e) == RM_MAX_END)

/*
 * Userspace-exported structures.
 */
struct u_resource {
	uintptr_t	r_handle;		/* resource uniquifier */
	uintptr_t	r_parent;		/* parent rman */
	uintptr_t	r_device;		/* device owning this resource */
	char		r_devname[RM_TEXTLEN];	/* device name XXX obsolete */

	rman_res_t	r_start;		/* offset in resource space */
	rman_res_t	r_size;			/* size in resource space */
	u_int		r_flags;		/* RF_* flags */
};

struct u_rman {
	uintptr_t	rm_handle;		/* rman uniquifier */
	char		rm_descr[RM_TEXTLEN];	/* rman description */

	rman_res_t	rm_start;		/* base of managed region */
	rman_res_t	rm_size;		/* size of managed region */
	enum rman_type	rm_type;		/* region type */
};

#ifdef _KERNEL

/*
 * The public (kernel) view of struct resource
 *
 * NB: Changing the offset/size/type of existing fields in struct resource
 * NB: breaks the device driver ABI and is strongly FORBIDDEN.
 * NB: Appending new fields is probably just misguided.
 */

struct resource {
	struct resource_i	*__r_i;
	bus_space_tag_t		r_bustag; /* bus_space tag */
	bus_space_handle_t	r_bushandle;	/* bus_space handle */
};

struct resource_i;
struct resource_map;

TAILQ_HEAD(resource_head, resource_i);

struct rman {
	struct	resource_head 	rm_list;
	struct	mtx *rm_mtx;	/* mutex used to protect rm_list */
	TAILQ_ENTRY(rman)	rm_link; /* link in list of all rmans */
	rman_res_t	rm_start;	/* index of globally first entry */
	rman_res_t	rm_end;	/* index of globally last entry */
	enum	rman_type rm_type; /* what type of resource this is */
	const	char *rm_descr;	/* text descripion of this resource */
};
TAILQ_HEAD(rman_head, rman);

int	rman_activate_resource(struct resource *r);
int	rman_adjust_resource(struct resource *r, rman_res_t start, rman_res_t end);
int	rman_first_free_region(struct rman *rm, rman_res_t *start, rman_res_t *end);
bus_space_handle_t rman_get_bushandle(struct resource *);
bus_space_tag_t rman_get_bustag(struct resource *);
rman_res_t	rman_get_end(struct resource *);
device_t rman_get_device(struct resource *);
u_int	rman_get_flags(struct resource *);
void   *rman_get_irq_cookie(struct resource *);
void	rman_get_mapping(struct resource *, struct resource_map *);
int	rman_get_rid(struct resource *);
rman_res_t	rman_get_size(struct resource *);
rman_res_t	rman_get_start(struct resource *);
void   *rman_get_virtual(struct resource *);
int	rman_deactivate_resource(struct resource *r);
int	rman_fini(struct rman *rm);
int	rman_init(struct rman *rm);
int	rman_init_from_resource(struct rman *rm, struct resource *r);
int	rman_last_free_region(struct rman *rm, rman_res_t *start, rman_res_t *end);
uint32_t rman_make_alignment_flags(uint32_t size);
int	rman_manage_region(struct rman *rm, rman_res_t start, rman_res_t end);
int	rman_is_region_manager(struct resource *r, struct rman *rm);
int	rman_release_resource(struct resource *r);
struct resource *rman_reserve_resource(struct rman *rm, rman_res_t start,
					rman_res_t end, rman_res_t count,
					u_int flags, device_t dev);
struct resource *rman_reserve_resource_bound(struct rman *rm, rman_res_t start,
					rman_res_t end, rman_res_t count, rman_res_t bound,
					u_int flags, device_t dev);
void	rman_set_bushandle(struct resource *_r, bus_space_handle_t _h);
void	rman_set_bustag(struct resource *_r, bus_space_tag_t _t);
void	rman_set_device(struct resource *_r, device_t _dev);
void	rman_set_end(struct resource *_r, rman_res_t _end);
void	rman_set_irq_cookie(struct resource *_r, void *_c);
void	rman_set_mapping(struct resource *, struct resource_map *);
void	rman_set_rid(struct resource *_r, int _rid);
void	rman_set_start(struct resource *_r, rman_res_t _start);
void	rman_set_virtual(struct resource *_r, void *_v);

extern	struct rman_head rman_head;

#endif /* _KERNEL */

#endif /* !_SYS_RMAN_H_ */
