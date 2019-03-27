/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * @(#)ip_compat.h	1.8 1/14/96
 * $FreeBSD$
 * Id: ip_compat.h,v 2.142.2.57 2007/10/10 09:51:42 darrenr Exp $
 */

#ifndef	__IP_COMPAT_H__
#define	__IP_COMPAT_H__

#ifndef	__P
# ifdef	__STDC__
#  define	__P(x)  x
# else
#  define	__P(x)  ()
# endif
#endif
#ifndef	__STDC__
# undef		const
# define	const
#endif

#if defined(_KERNEL) || defined(KERNEL) || defined(__KERNEL__)
# undef	KERNEL
# undef	_KERNEL
# undef 	__KERNEL__
# define	KERNEL
# define	_KERNEL
# define 	__KERNEL__
#endif

#ifndef	SOLARIS
# if defined(sun) && defined(__SVR4)
#  define	SOLARIS		1
# else
#  define	SOLARIS		0
# endif
#endif


#if defined(__SVR4)
# define index   strchr
# if !defined(_KERNEL)
#  define	bzero(a,b)	memset(a,0,b)
#  define	bcmp		memcmp
#  define	bcopy(a,b,c)	memmove(b,a,c)
# endif
#endif

#ifndef LIFNAMSIZ
# ifdef IF_NAMESIZE
#  define	LIFNAMSIZ	IF_NAMESIZE
# else
#  ifdef	IFNAMSIZ
#   define	LIFNAMSIZ	IFNAMSIZ
#  else
#   define	LIFNAMSIZ	16
#  endif
# endif
#endif


# ifdef __STDC__
#  define IPL_EXTERN(ep) ipl##ep
# else
#  define IPL_EXTERN(ep) ipl/**/ep
# endif

/*
 * This is a workaround for <sys/uio.h> troubles on FreeBSD and OpenBSD.
 */
# ifndef _KERNEL
#  define ADD_KERNEL
#  define _KERNEL
#  define KERNEL
# endif
# include <sys/uio.h>
# ifdef ADD_KERNEL
#  undef _KERNEL
#  undef KERNEL
# endif

#define	NETBSD_GE_REV(x)	(defined(__NetBSD_Version__) && \
				 (__NetBSD_Version__ >= (x)))
#define	NETBSD_GT_REV(x)	(defined(__NetBSD_Version__) && \
				 (__NetBSD_Version__ > (x)))
#define	NETBSD_LT_REV(x)	(defined(__NetBSD_Version__) && \
				 (__NetBSD_Version__ < (x)))
#define	FREEBSD_GE_REV(x)	(defined(__FreeBSD_version) && \
				 (__FreeBSD_version >= (x)))
#define	FREEBSD_GT_REV(x)	(defined(__FreeBSD_version) && \
				 (__FreeBSD_version > (x)))
#define	FREEBSD_LT_REV(x)	(defined(__FreeBSD_version) && \
				 (__FreeBSD_version < (x)))
#define	BSD_GE_YEAR(x)		(defined(BSD) && (BSD >= (x)))
#define	BSD_GT_YEAR(x)		(defined(BSD) && (BSD > (x)))
#define	BSD_LT_YEAR(x)		(defined(BSD) && (BSD < (x)))


/* ----------------------------------------------------------------------- */
/*                                F R E E B S D                            */
/* ----------------------------------------------------------------------- */
# define HAS_SYS_MD5_H	1
# if defined(_KERNEL)
#   include "opt_bpf.h"
#   include "opt_inet6.h"
#  if defined(INET6) && !defined(USE_INET6)
#   define USE_INET6
#  endif
# else
#  if !defined(USE_INET6) && !defined(NOINET6)
#   define	USE_INET6
#  endif
# endif

# if defined(_KERNEL)
#  include <netinet/ip_var.h>
#    define	p_cred	td_ucred
#    define	p_uid	td_ucred->cr_ruid

/*
 * When #define'd, the 5.2.1 kernel panics when used with the ftp proxy.
 * There may be other, safe, kernels but this is not extensively tested yet.
 */
#   define HAVE_M_PULLDOWN
#  if !defined(IPFILTER_LKM) && (__FreeBSD_version >= 300000)
#   include "opt_ipfilter.h"
#  endif
#  define	COPYIN(a,b,c)	copyin((caddr_t)(a), (caddr_t)(b), (c))
#  define	COPYOUT(a,b,c)	copyout((caddr_t)(a), (caddr_t)(b), (c))

# else
#  include <inttypes.h>
# endif /* _KERNEL */

#  include <sys/selinfo.h>
#  include <sys/lock.h>
#  include <sys/malloc.h>
#  include <sys/mutex.h>
#    define	KRWLOCK_FILL_SZ		56
#    define	KMUTEX_FILL_SZ		56
#   include <sys/rwlock.h>
#   define	KMUTEX_T		struct mtx
#   define	KRWLOCK_T		struct rwlock

#ifdef _KERNEL
#    define	READ_ENTER(x)		rw_rlock(&(x)->ipf_lk)
#    define	WRITE_ENTER(x)		rw_wlock(&(x)->ipf_lk)
#    define	MUTEX_DOWNGRADE(x)	rw_downgrade(&(x)->ipf_lk)
#    define	MUTEX_TRY_UPGRADE(x)	rw_try_upgrade(&(x)->ipf_lk)
#    define	RWLOCK_INIT(x,y)	rw_init(&(x)->ipf_lk, (y))
#    define	RW_DESTROY(x)		rw_destroy(&(x)->ipf_lk)
#    define	RWLOCK_EXIT(x)		do { \
					    if (rw_wowned(&(x)->ipf_lk)) \
					    	rw_wunlock(&(x)->ipf_lk); \
					    else \
						rw_runlock(&(x)->ipf_lk); \
					} while (0)
#  include <net/if_var.h>
#  define	GETKTIME(x)	microtime((struct timeval *)x)
#  define	if_addrlist	if_addrhead

#   include <netinet/in_systm.h>
#   include <netinet/ip.h>
#   include <machine/in_cksum.h>

#   define	USE_MUTEXES
#   define	MUTEX_ENTER(x)		mtx_lock(&(x)->ipf_lk)
#   define	MUTEX_EXIT(x)		mtx_unlock(&(x)->ipf_lk)
#   define	MUTEX_INIT(x,y)		mtx_init(&(x)->ipf_lk, (y), NULL,\
						 MTX_DEF)
#   define	MUTEX_DESTROY(x)	mtx_destroy(&(x)->ipf_lk)
#   define	MUTEX_NUKE(x)		bzero((x), sizeof(*(x)))
/*
 * Whilst the sx(9) locks on FreeBSD have the right semantics and interface
 * for what we want to use them for, despite testing showing they work -
 * with a WITNESS kernel, it generates LOR messages.
 */
#   include <machine/atomic.h>
#   define	ATOMIC_INC(x)		{ mtx_lock(&softc->ipf_rw.ipf_lk); (x)++; \
					  mtx_unlock(&softc->ipf_rw.ipf_lk); }
#   define	ATOMIC_DEC(x)		{ mtx_lock(&softc->ipf_rw.ipf_lk); (x)--; \
					  mtx_unlock(&softc->ipf_rw.ipf_lk); }
#   define	ATOMIC_INCL(x)		atomic_add_long(&(x), 1)
#   define	ATOMIC_INC64(x)		ATOMIC_INC(x)
#   define	ATOMIC_INC32(x)		atomic_add_32((u_int *)&(x), 1)
#   define	ATOMIC_DECL(x)		atomic_add_long(&(x), -1)
#   define	ATOMIC_DEC64(x)		ATOMIC_DEC(x)
#   define	ATOMIC_DEC32(x)		atomic_add_32((u_int *)&(x), -1)
#   define	SPL_X(x)	;
#   define	SPL_NET(x)	;
#   define	SPL_IMP(x)	;
#   define	SPL_SCHED(x)	;
#   define	GET_MINOR		dev2unit
#  define	MSGDSIZE(m)	mbufchainlen(m)
#  define	M_LEN(m)	(m)->m_len
#  define	M_ADJ(m,x)	m_adj(m, x)
#  define	M_COPY(x)	m_copym((x), 0, M_COPYALL, M_NOWAIT)
#  define	M_DUP(m)	m_dup(m, M_NOWAIT)
#  define	IPF_PANIC(x,y)	if (x) { printf y; panic("ipf_panic"); }
typedef struct mbuf mb_t;

#else	/* !_KERNEL */
#ifndef _NET_IF_VAR_H_
/*
 * Userland emulation of struct ifnet.
 */
struct route;
struct mbuf;
struct ifnet {
	char			if_xname[IFNAMSIZ];
	STAILQ_HEAD(, ifaddr)	if_addrlist;
	int	(*if_output)(struct ifnet *, struct mbuf *,
	    const struct sockaddr *, struct route *);
};
#endif /* _NET_IF_VAR_H_ */
#endif /* _KERNEL */

#  define	IFNAME(x)	((struct ifnet *)x)->if_xname
#  define	COPYIFNAME(v, x, b) \
				(void) strncpy(b, \
					       ((struct ifnet *)x)->if_xname, \
					       LIFNAMSIZ)

typedef	u_long		ioctlcmd_t;
typedef	struct uio	uio_t;
typedef	int		minor_t;
typedef	u_int32_t	u_32_t;
# define	U_32_T	1


/* ----------------------------------------------------------------------- */
/*                           G E N E R I C                                 */
/* ----------------------------------------------------------------------- */

/*
 * For BSD kernels, if bpf is in the kernel, enable ipfilter to use bpf in
 * filter rules.
 */
#if !defined(IPFILTER_BPF)
# if (defined(NBPF) && (NBPF > 0)) || (defined(DEV_BPF) && (DEV_BPF > 0)) || \
     (defined(NBPFILTER) && (NBPFILTER > 0))
#  define	IPFILTER_BPF
# endif
#endif

/*
 * Userland locking primitives
 */
#ifndef _KERNEL
#if !defined(KMUTEX_FILL_SZ)
# define	KMUTEX_FILL_SZ	1
#endif
#if !defined(KRWLOCK_FILL_SZ)
# define	KRWLOCK_FILL_SZ	1
#endif
#endif

typedef	struct	{
	char	*eMm_owner;
	char	*eMm_heldin;
	u_int	eMm_magic;
	int	eMm_held;
	int	eMm_heldat;
} eMmutex_t;

typedef	struct	{
	char	*eMrw_owner;
	char	*eMrw_heldin;
	u_int	eMrw_magic;
	short	eMrw_read;
	short	eMrw_write;
	int	eMrw_heldat;
} eMrwlock_t;

typedef union {
	char	_fill[KMUTEX_FILL_SZ];
#ifdef KMUTEX_T
	struct	{
		KMUTEX_T	ipf_slk;
		const char	*ipf_lname;
	} ipf_lkun_s;
#endif
	eMmutex_t	ipf_emu;
} ipfmutex_t;

typedef union {
	char	_fill[KRWLOCK_FILL_SZ];
#ifdef KRWLOCK_T
	struct	{
		KRWLOCK_T	ipf_slk;
		const char	*ipf_lname;
		int		ipf_sr;
		int		ipf_sw;
		u_int		ipf_magic;
	} ipf_lkun_s;
#endif
	eMrwlock_t	ipf_emu;
} ipfrwlock_t;

#define	ipf_lk		ipf_lkun_s.ipf_slk
#define	ipf_lname	ipf_lkun_s.ipf_lname
#define	ipf_isr		ipf_lkun_s.ipf_sr
#define	ipf_isw		ipf_lkun_s.ipf_sw
#define	ipf_magic	ipf_lkun_s.ipf_magic

#if !defined(__GNUC__) || defined(__FreeBSD_version)
# ifndef	INLINE
#  define	INLINE
# endif
#else
# define	INLINE	__inline__
#endif

#if defined(__FreeBSD_version) && defined(_KERNEL)
     CTASSERT(sizeof(ipfrwlock_t) == KRWLOCK_FILL_SZ);
     CTASSERT(sizeof(ipfmutex_t) == KMUTEX_FILL_SZ);
#endif


/*
 * In a non-kernel environment, there are a lot of macros that need to be
 * filled in to be null-ops or to point to some compatibility function,
 * somewhere in userland.
 */
#ifndef _KERNEL
typedef	struct	mb_s	{
	struct	mb_s	*mb_next;
	char		*mb_data;
	void		*mb_ifp;
	int		mb_len;
	int		mb_flags;
	u_long		mb_buf[2048];
} mb_t;
# undef		m_next
# define	m_next		mb_next
# undef		m_len
# define	m_len		mb_len
# undef		m_flags
# define	m_flags		mb_flags
# undef		m_data
# define	m_data		mb_data
# undef		M_MCAST
# define	M_MCAST		0x01
# undef		M_BCAST
# define	M_BCAST		0x02
# undef		M_MBCAST
# define	M_MBCAST	0x04
# define	MSGDSIZE(m)	msgdsize(m)
# define	M_LEN(m)	(m)->mb_len
# define	M_ADJ(m,x)	(m)->mb_len += x
# define	M_COPY(m)	dupmbt(m)
# define	M_DUP(m)	dupmbt(m)
# define	GETKTIME(x)	gettimeofday((struct timeval *)(x), NULL)
# define	MTOD(m, t)	((t)(m)->mb_data)
# define	FREE_MB_T(m)	freembt(m)
# define	ALLOC_MB_T(m,l)	(m) = allocmbt(l)
# define	PREP_MB_T(f, m)	do { \
						(m)->mb_next = *(f)->fin_mp; \
						*(fin)->fin_mp = (m); \
						(f)->fin_m = (m); \
					} while (0)
# define	SLEEP(x,y)	1;
# define	WAKEUP(x,y)	;
# define	POLLWAKEUP(y)	;
# define	IPF_PANIC(x,y)	;
# define	PANIC(x,y)	;
# define	SPL_SCHED(x)	;
# define	SPL_NET(x)	;
# define	SPL_IMP(x)	;
# define	SPL_X(x)	;
# define	KMALLOC(a,b)	(a) = (b)malloc(sizeof(*a))
# define	KMALLOCS(a,b,c)	(a) = (b)malloc(c)
# define	KFREE(x)	free(x)
# define	KFREES(x,s)	free(x)
# define	GETIFP(x, v)	get_unit(x,v)
# define	GETIFMTU_4(x)	2048
# define	GETIFMTU_6(x)	2048
# define	COPYIN(a,b,c)	bcopywrap((a), (b), (c))
# define	COPYOUT(a,b,c)	bcopywrap((a), (b), (c))
# define	COPYDATA(m, o, l, b)	bcopy(MTOD((mb_t *)m, char *) + (o), \
					      (b), (l))
# define	COPYBACK(m, o, l, b)	bcopy((b), \
					      MTOD((mb_t *)m, char *) + (o), \
					      (l))
# define	UIOMOVE(a,b,c,d)	ipfuiomove((caddr_t)a,b,c,d)
extern	void	m_copydata __P((mb_t *, int, int, caddr_t));
extern	int	ipfuiomove __P((caddr_t, int, int, struct uio *));
extern	int	bcopywrap __P((void *, void *, size_t));
extern	mb_t	*allocmbt __P((size_t));
extern	mb_t	*dupmbt __P((mb_t *));
extern	void	freembt __P((mb_t *));

# define	MUTEX_DESTROY(x)	eMmutex_destroy(&(x)->ipf_emu, \
							__FILE__, __LINE__)
# define	MUTEX_ENTER(x)		eMmutex_enter(&(x)->ipf_emu, \
						      __FILE__, __LINE__)
# define	MUTEX_EXIT(x)		eMmutex_exit(&(x)->ipf_emu, \
						     __FILE__, __LINE__)
# define	MUTEX_INIT(x,y)		eMmutex_init(&(x)->ipf_emu, y, \
						     __FILE__, __LINE__)
# define	MUTEX_NUKE(x)		bzero((x), sizeof(*(x)))

# define	MUTEX_DOWNGRADE(x)	eMrwlock_downgrade(&(x)->ipf_emu, \
							   __FILE__, __LINE__)
# define	MUTEX_TRY_UPGRADE(x)	eMrwlock_try_upgrade(&(x)->ipf_emu, \
							   __FILE__, __LINE__)
# define	READ_ENTER(x)		eMrwlock_read_enter(&(x)->ipf_emu, \
							    __FILE__, __LINE__)
# define	RWLOCK_INIT(x, y)	eMrwlock_init(&(x)->ipf_emu, y)
# define	RWLOCK_EXIT(x)		eMrwlock_exit(&(x)->ipf_emu)
# define	RW_DESTROY(x)		eMrwlock_destroy(&(x)->ipf_emu)
# define	WRITE_ENTER(x)		eMrwlock_write_enter(&(x)->ipf_emu, \
							     __FILE__, \
							     __LINE__)

# define	USE_MUTEXES		1

extern void eMmutex_destroy __P((eMmutex_t *, char *, int));
extern void eMmutex_enter __P((eMmutex_t *, char *, int));
extern void eMmutex_exit __P((eMmutex_t *, char *, int));
extern void eMmutex_init __P((eMmutex_t *, char *, char *, int));
extern void eMrwlock_destroy __P((eMrwlock_t *));
extern void eMrwlock_exit __P((eMrwlock_t *));
extern void eMrwlock_init __P((eMrwlock_t *, char *));
extern void eMrwlock_read_enter __P((eMrwlock_t *, char *, int));
extern void eMrwlock_write_enter __P((eMrwlock_t *, char *, int));
extern void eMrwlock_downgrade __P((eMrwlock_t *, char *, int));

#endif

extern	mb_t	*allocmbt(size_t);

#define	MAX_IPV4HDR	((0xf << 2) + sizeof(struct icmp) + sizeof(ip_t) + 8)

#ifndef	IP_OFFMASK
# define	IP_OFFMASK	0x1fff
#endif


/*
 * On BSD's use quad_t as a guarantee for getting at least a 64bit sized
 * object.
 */
#if !defined(__amd64__) && BSD_GT_YEAR(199306)
# define	USE_QUAD_T
# define	U_QUAD_T	unsigned long long
# define	QUAD_T		long long
#else /* BSD > 199306 */
# if !defined(U_QUAD_T)
#  define	U_QUAD_T	u_long
#  define	QUAD_T		long
# endif
#endif /* BSD > 199306 */


#ifdef	USE_INET6
# if defined(__NetBSD__) || defined(__FreeBSD__)
#  include <netinet/ip6.h>
#  include <netinet/icmp6.h>
#   if defined(_KERNEL)
#    include <netinet6/ip6_var.h>
#   endif
typedef	struct ip6_hdr	ip6_t;
# endif
#endif

#ifndef	MAX
# define	MAX(a,b)	(((a) > (b)) ? (a) : (b))
#endif

#if defined(_KERNEL)
# if defined(MENTAT) && !defined(INSTANCES)
#  define	COPYDATA	mb_copydata
#  define	COPYBACK	mb_copyback
# else
#  define	COPYDATA	m_copydata
#  define	COPYBACK	m_copyback
# endif
#  if (defined(__NetBSD_Version__) && (__NetBSD_Version__ < 105180000)) || \
       defined(__FreeBSD__)
#   include <vm/vm.h>
#  endif
#   if NETBSD_GE_REV(105180000)
#    include <uvm/uvm_extern.h>
#   else
#    include <vm/vm_extern.h>
extern  vm_map_t        kmem_map;
#   endif
#   include <sys/proc.h>

#  ifdef IPFILTER_M_IPFILTER
#    include <sys/malloc.h>
MALLOC_DECLARE(M_IPFILTER);
#    define	_M_IPF		M_IPFILTER
#  else /* IPFILTER_M_IPFILTER */
#   ifdef M_PFIL
#    define	_M_IPF		M_PFIL
#   else
#    ifdef M_IPFILTER
#     define	_M_IPF		M_IPFILTER
#    else
#     define	_M_IPF		M_TEMP
#    endif /* M_IPFILTER */
#   endif /* M_PFIL */
#  endif /* IPFILTER_M_IPFILTER */
#  if !defined(KMALLOC)
#   define	KMALLOC(a, b)		(a) = (b)malloc(sizeof(*(a)), _M_IPF, M_NOWAIT)
#  endif
#  if !defined(KMALLOCS)
#   define	KMALLOCS(a, b, c)	(a) = (b)malloc((c), _M_IPF, M_NOWAIT)
#  endif
#  if !defined(KFREE)
#   define	KFREE(x)	free((x), _M_IPF)
#  endif
#   if !defined(KFREES)
#  define	KFREES(x,s)	free((x), _M_IPF)
#  endif
#  define	UIOMOVE(a,b,c,d)	uiomove((caddr_t)a,b,d)
#  define	SLEEP(id, n)	tsleep((id), PPAUSE|PCATCH, n, 0)
#  define	WAKEUP(id,x)	wakeup(id+x)
#  if !defined(POLLWAKEUP)
#   define	POLLWAKEUP(x)	selwakeup(softc->ipf_selwait+x)
#  endif
#  define	GETIFP(n, v)	ifunit(n)
#  define	GETIFMTU_4(x)	((struct ifnet *)x)->if_mtu
#  define	GETIFMTU_6(x)	((struct ifnet *)x)->if_mtu

# if !defined(USE_MUTEXES) && !defined(SPL_NET)
#  define	SPL_IMP(x)	x = splimp()
#  define	SPL_NET(x)	x = splnet()
#  if !defined(SPL_SCHED)
#   define	SPL_SCHED(x)	x = splsched()
#  endif
#  define	SPL_X(x)	(void) splx(x)
# endif /* !USE_MUTEXES */

# ifndef FREE_MB_T
#  define	FREE_MB_T(m)	m_freem(m)
# endif
# ifndef ALLOC_MB_T
#  ifdef MGETHDR
#   define	ALLOC_MB_T(m,l)	do { \
					MGETHDR((m), M_NOWAIT, MT_HEADER); \
					if ((m) != NULL) { \
						(m)->m_len = (l); \
						(m)->m_pkthdr.len = (l); \
					} \
				} while (0)
#  else
#   define	ALLOC_MB_T(m,l)	do { \
					MGET((m), M_NOWAIT, MT_HEADER); \
					if ((m) != NULL) { \
						(m)->m_len = (l); \
						(m)->m_pkthdr.len = (l); \
					} \
				} while (0)
#  endif
# endif
# ifndef PREP_MB_T
#  define	PREP_MB_T(f, m)	do { \
						mb_t *_o = *(f)->fin_mp; \
						(m)->m_next = _o; \
						*(fin)->fin_mp = (m); \
						if (_o->m_flags & M_PKTHDR) { \
							(m)->m_pkthdr.len += \
							    _o->m_pkthdr.len; \
							(m)->m_pkthdr.rcvif = \
							  _o->m_pkthdr.rcvif; \
						} \
					} while (0)
# endif
# ifndef M_DUP
#  ifdef M_COPYALL
#   define	M_DUP(m)	m_dup(m, 0, M_COPYALL, 0)
#  else
#   define	M_DUP(m)	m_dup(m)
#  endif
# endif

# ifndef MTOD
#  define	MTOD(m,t)	mtod(m,t)
# endif

# ifndef COPYIN
#  define	COPYIN(a,b,c)	(bcopy((caddr_t)(a), (caddr_t)(b), (c)), 0)
#  define	COPYOUT(a,b,c)	(bcopy((caddr_t)(a), (caddr_t)(b), (c)), 0)
# endif

# if SOLARIS && !defined(KMALLOC)
#  define	KMALLOC(a,b)	(a) = (b)new_kmem_alloc(sizeof(*(a)), \
							KMEM_NOSLEEP)
#  define	KMALLOCS(a,b,c)	(a) = (b)new_kmem_alloc((c), KMEM_NOSLEEP)
# endif

# ifndef	GET_MINOR
#  define	GET_MINOR(x)	dev2unit(x)
# endif
# define	PANIC(x,y)	if (x) panic y
#endif /* _KERNEL */

#if !defined(IFNAME) && !defined(_KERNEL)
# define	IFNAME(x)	get_ifname((struct ifnet *)x)
#endif
#ifndef	COPYIFNAME
# define	NEED_FRGETIFNAME
extern	char	*ipf_getifname __P((struct ifnet *, char *));
# define	COPYIFNAME(v, x, b) \
				ipf_getifname((struct ifnet *)x, b)
#endif

#ifndef ASSERT
# ifdef _KERNEL
#  define	ASSERT(x)
# else
#  define	ASSERT(x)	do { if (!(x)) abort(); } while (0)
# endif
#endif

#ifndef BCOPYIN
#  define	BCOPYIN(a,b,c)	(bcopy((caddr_t)(a), (caddr_t)(b), (c)), 0)
#  define	BCOPYOUT(a,b,c)	(bcopy((caddr_t)(a), (caddr_t)(b), (c)), 0)
#endif

/*
 * Because the ctype(3) posix definition, if used "safely" in code everywhere,
 * would mean all normal code that walks through strings needed casts.  Yuck.
 */
#define	ISALNUM(x)	isalnum((u_char)(x))
#define	ISALPHA(x)	isalpha((u_char)(x))
#define	ISDIGIT(x)	isdigit((u_char)(x))
#define	ISSPACE(x)	isspace((u_char)(x))
#define	ISUPPER(x)	isupper((u_char)(x))
#define	ISXDIGIT(x)	isxdigit((u_char)(x))
#define	ISLOWER(x)	islower((u_char)(x))
#define	TOUPPER(x)	toupper((u_char)(x))
#define	TOLOWER(x)	tolower((u_char)(x))

/*
 * If mutexes aren't being used, turn all the mutex functions into null-ops.
 */
#if !defined(USE_MUTEXES)
# define	USE_SPL			1
# undef		RW_DESTROY
# undef		MUTEX_INIT
# undef		MUTEX_NUKE
# undef		MUTEX_DESTROY
# define	MUTEX_ENTER(x)		;
# define	READ_ENTER(x)		;
# define	WRITE_ENTER(x)		;
# define	MUTEX_DOWNGRADE(x)	;
# define	MUTEX_TRY_UPGRADE(x)	;
# define	RWLOCK_INIT(x, y)	;
# define	RWLOCK_EXIT(x)		;
# define	RW_DESTROY(x)		;
# define	MUTEX_EXIT(x)		;
# define	MUTEX_INIT(x,y)		;
# define	MUTEX_DESTROY(x)	;
# define	MUTEX_NUKE(x)		;
#endif /* !USE_MUTEXES */
#ifndef	ATOMIC_INC
# define	ATOMIC_INC(x)		(x)++
# define	ATOMIC_DEC(x)		(x)--
#endif

#if defined(USE_SPL) && defined(_KERNEL)
# define	SPL_INT(x)	int x
#else
# define	SPL_INT(x)
#endif

/*
 * If there are no atomic operations for bit sizes defined, define them to all
 * use a generic one that works for all sizes.
 */
#ifndef	ATOMIC_INCL
# define	ATOMIC_INCL		ATOMIC_INC
# define	ATOMIC_INC64		ATOMIC_INC
# define	ATOMIC_INC32		ATOMIC_INC
# define	ATOMIC_DECL		ATOMIC_DEC
# define	ATOMIC_DEC64		ATOMIC_DEC
# define	ATOMIC_DEC32		ATOMIC_DEC
#endif

#ifndef HDR_T_PRIVATE
typedef	struct	tcphdr	tcphdr_t;
typedef	struct	udphdr	udphdr_t;
#endif
typedef	struct	icmp	icmphdr_t;
typedef	struct	ip	ip_t;
typedef	struct	ether_header	ether_header_t;
typedef	struct	tcpiphdr	tcpiphdr_t;

#ifndef	FR_GROUPLEN
# define	FR_GROUPLEN	16
#endif

#ifndef offsetof
# define offsetof(t,m) (size_t)((&((t *)0L)->m))
#endif
#ifndef stsizeof
# define stsizeof(t,m)	sizeof(((t *)0L)->m)
#endif

/*
 * This set of macros has been brought about because on Tru64 it is not
 * possible to easily assign or examine values in a structure that are
 * bit fields.
 */
#ifndef IP_V
# define	IP_V(x)		(x)->ip_v
#endif
#ifndef	IP_V_A
# define	IP_V_A(x,y)	(x)->ip_v = (y)
#endif
#ifndef	IP_HL
# define	IP_HL(x)	(x)->ip_hl
#endif
#ifndef	IP_HL_A
# define	IP_HL_A(x,y)	(x)->ip_hl = ((y) & 0xf)
#endif
#ifndef	TCP_X2
# define	TCP_X2(x)	(x)->th_x2
#endif
#ifndef	TCP_X2_A
# define	TCP_X2_A(x,y)	(x)->th_x2 = (y)
#endif
#ifndef	TCP_OFF
# define	TCP_OFF(x)	(x)->th_off
#endif
#ifndef	TCP_OFF_A
# define	TCP_OFF_A(x,y)	(x)->th_off = (y)
#endif
#define	IPMINLEN(i, h)	((i)->ip_len >= (IP_HL(i) * 4 + sizeof(struct h)))

#define	TCPF_ALL	(TH_FIN|TH_SYN|TH_RST|TH_PUSH|TH_ACK|TH_URG|\
			 TH_ECN|TH_CWR)

#if BSD_GE_YEAR(199306) && !defined(m_act)
# define	m_act	m_nextpkt
#endif

/*
 * Security Options for Intenet Protocol (IPSO) as defined in RFC 1108.
 *
 * Basic Option
 *
 * 00000001   -   (Reserved 4)
 * 00111101   -   Top Secret
 * 01011010   -   Secret
 * 10010110   -   Confidential
 * 01100110   -   (Reserved 3)
 * 11001100   -   (Reserved 2)
 * 10101011   -   Unclassified
 * 11110001   -   (Reserved 1)
 */
#define	IPSO_CLASS_RES4		0x01
#define	IPSO_CLASS_TOPS		0x3d
#define	IPSO_CLASS_SECR		0x5a
#define	IPSO_CLASS_CONF		0x96
#define	IPSO_CLASS_RES3		0x66
#define	IPSO_CLASS_RES2		0xcc
#define	IPSO_CLASS_UNCL		0xab
#define	IPSO_CLASS_RES1		0xf1

#define	IPSO_AUTH_GENSER	0x80
#define	IPSO_AUTH_ESI		0x40
#define	IPSO_AUTH_SCI		0x20
#define	IPSO_AUTH_NSA		0x10
#define	IPSO_AUTH_DOE		0x08
#define	IPSO_AUTH_UN		0x06
#define	IPSO_AUTH_FTE		0x01

/*
 * IP option #defines
 */
#undef	IPOPT_RR
#define	IPOPT_RR	7
#undef	IPOPT_ZSU
#define	IPOPT_ZSU	10	/* ZSU */
#undef	IPOPT_MTUP
#define	IPOPT_MTUP	11	/* MTUP */
#undef	IPOPT_MTUR
#define	IPOPT_MTUR	12	/* MTUR */
#undef	IPOPT_ENCODE
#define	IPOPT_ENCODE	15	/* ENCODE */
#undef	IPOPT_TS
#define	IPOPT_TS	68
#undef	IPOPT_TR
#define	IPOPT_TR	82	/* TR */
#undef	IPOPT_SECURITY
#define	IPOPT_SECURITY	130
#undef	IPOPT_LSRR
#define	IPOPT_LSRR	131
#undef	IPOPT_E_SEC
#define	IPOPT_E_SEC	133	/* E-SEC */
#undef	IPOPT_CIPSO
#define	IPOPT_CIPSO	134	/* CIPSO */
#undef	IPOPT_SATID
#define	IPOPT_SATID	136
#ifndef	IPOPT_SID
# define	IPOPT_SID	IPOPT_SATID
#endif
#undef	IPOPT_SSRR
#define	IPOPT_SSRR	137
#undef	IPOPT_ADDEXT
#define	IPOPT_ADDEXT	147	/* ADDEXT */
#undef	IPOPT_VISA
#define	IPOPT_VISA	142	/* VISA */
#undef	IPOPT_IMITD
#define	IPOPT_IMITD	144	/* IMITD */
#undef	IPOPT_EIP
#define	IPOPT_EIP	145	/* EIP */
#undef	IPOPT_RTRALRT
#define	IPOPT_RTRALRT	148	/* RTRALRT */
#undef	IPOPT_SDB
#define	IPOPT_SDB	149
#undef	IPOPT_NSAPA
#define	IPOPT_NSAPA	150
#undef	IPOPT_DPS
#define	IPOPT_DPS	151
#undef	IPOPT_UMP
#define	IPOPT_UMP	152
#undef	IPOPT_FINN
#define	IPOPT_FINN	205	/* FINN */
#undef	IPOPT_AH
#define	IPOPT_AH	256+IPPROTO_AH

#ifndef TCPOPT_EOL
# define TCPOPT_EOL		0
#endif
#ifndef TCPOPT_NOP
# define TCPOPT_NOP		1
#endif
#ifndef TCPOPT_MAXSEG
# define TCPOPT_MAXSEG		2
#endif
#ifndef TCPOLEN_MAXSEG
# define TCPOLEN_MAXSEG		4
#endif
#ifndef TCPOPT_WINDOW
# define TCPOPT_WINDOW		3
#endif
#ifndef TCPOLEN_WINDOW
# define TCPOLEN_WINDOW		3
#endif
#ifndef TCPOPT_SACK_PERMITTED
# define TCPOPT_SACK_PERMITTED	4
#endif
#ifndef TCPOLEN_SACK_PERMITTED
# define TCPOLEN_SACK_PERMITTED	2
#endif
#ifndef TCPOPT_SACK
# define TCPOPT_SACK		5
#endif
#ifndef TCPOPT_TIMESTAMP
# define TCPOPT_TIMESTAMP	8
#endif

#ifndef	ICMP_MINLEN
# define	ICMP_MINLEN	8
#endif
#ifndef	ICMP_ECHOREPLY
# define	ICMP_ECHOREPLY	0
#endif
#ifndef	ICMP_UNREACH
# define	ICMP_UNREACH	3
#endif
#ifndef	ICMP_UNREACH_NET
# define	ICMP_UNREACH_NET	0
#endif
#ifndef	ICMP_UNREACH_HOST
# define	ICMP_UNREACH_HOST	1
#endif
#ifndef	ICMP_UNREACH_PROTOCOL
# define	ICMP_UNREACH_PROTOCOL	2
#endif
#ifndef	ICMP_UNREACH_PORT
# define	ICMP_UNREACH_PORT	3
#endif
#ifndef	ICMP_UNREACH_NEEDFRAG
# define	ICMP_UNREACH_NEEDFRAG	4
#endif
#ifndef	ICMP_UNREACH_SRCFAIL
# define	ICMP_UNREACH_SRCFAIL	5
#endif
#ifndef	ICMP_UNREACH_NET_UNKNOWN
# define	ICMP_UNREACH_NET_UNKNOWN	6
#endif
#ifndef	ICMP_UNREACH_HOST_UNKNOWN
# define	ICMP_UNREACH_HOST_UNKNOWN	7
#endif
#ifndef	ICMP_UNREACH_ISOLATED
# define	ICMP_UNREACH_ISOLATED	8
#endif
#ifndef	ICMP_UNREACH_NET_PROHIB
# define	ICMP_UNREACH_NET_PROHIB	9
#endif
#ifndef	ICMP_UNREACH_HOST_PROHIB
# define	ICMP_UNREACH_HOST_PROHIB	10
#endif
#ifndef	ICMP_UNREACH_TOSNET
# define	ICMP_UNREACH_TOSNET	11
#endif
#ifndef	ICMP_UNREACH_TOSHOST
# define	ICMP_UNREACH_TOSHOST	12
#endif
#ifndef	ICMP_UNREACH_ADMIN_PROHIBIT
# define	ICMP_UNREACH_ADMIN_PROHIBIT	13
#endif
#ifndef	ICMP_UNREACH_FILTER
# define	ICMP_UNREACH_FILTER	13
#endif
#ifndef	ICMP_UNREACH_HOST_PRECEDENCE
# define	ICMP_UNREACH_HOST_PRECEDENCE	14
#endif
#ifndef	ICMP_UNREACH_PRECEDENCE_CUTOFF
# define	ICMP_UNREACH_PRECEDENCE_CUTOFF	15
#endif
#ifndef	ICMP_SOURCEQUENCH
# define	ICMP_SOURCEQUENCH	4
#endif
#ifndef	ICMP_REDIRECT_NET
# define	ICMP_REDIRECT_NET	0
#endif
#ifndef	ICMP_REDIRECT_HOST
# define	ICMP_REDIRECT_HOST	1
#endif
#ifndef	ICMP_REDIRECT_TOSNET
# define	ICMP_REDIRECT_TOSNET	2
#endif
#ifndef	ICMP_REDIRECT_TOSHOST
# define	ICMP_REDIRECT_TOSHOST	3
#endif
#ifndef	ICMP_ALTHOSTADDR
# define	ICMP_ALTHOSTADDR	6
#endif
#ifndef	ICMP_TIMXCEED
# define	ICMP_TIMXCEED	11
#endif
#ifndef	ICMP_TIMXCEED_INTRANS
# define	ICMP_TIMXCEED_INTRANS	0
#endif
#ifndef	ICMP_TIMXCEED_REASS
# define		ICMP_TIMXCEED_REASS	1
#endif
#ifndef	ICMP_PARAMPROB
# define	ICMP_PARAMPROB	12
#endif
#ifndef	ICMP_PARAMPROB_ERRATPTR
# define	ICMP_PARAMPROB_ERRATPTR	0
#endif
#ifndef	ICMP_PARAMPROB_OPTABSENT
# define	ICMP_PARAMPROB_OPTABSENT	1
#endif
#ifndef	ICMP_PARAMPROB_LENGTH
# define	ICMP_PARAMPROB_LENGTH	2
#endif
#ifndef ICMP_TSTAMP
# define	ICMP_TSTAMP	13
#endif
#ifndef ICMP_TSTAMPREPLY
# define	ICMP_TSTAMPREPLY	14
#endif
#ifndef ICMP_IREQ
# define	ICMP_IREQ	15
#endif
#ifndef ICMP_IREQREPLY
# define	ICMP_IREQREPLY	16
#endif
#ifndef	ICMP_MASKREQ
# define	ICMP_MASKREQ	17
#endif
#ifndef ICMP_MASKREPLY
# define	ICMP_MASKREPLY	18
#endif
#ifndef	ICMP_TRACEROUTE
# define	ICMP_TRACEROUTE	30
#endif
#ifndef	ICMP_DATACONVERR
# define	ICMP_DATACONVERR	31
#endif
#ifndef	ICMP_MOBILE_REDIRECT
# define	ICMP_MOBILE_REDIRECT	32
#endif
#ifndef	ICMP_IPV6_WHEREAREYOU
# define	ICMP_IPV6_WHEREAREYOU	33
#endif
#ifndef	ICMP_IPV6_IAMHERE
# define	ICMP_IPV6_IAMHERE	34
#endif
#ifndef	ICMP_MOBILE_REGREQUEST
# define	ICMP_MOBILE_REGREQUEST	35
#endif
#ifndef	ICMP_MOBILE_REGREPLY
# define	ICMP_MOBILE_REGREPLY	36
#endif
#ifndef	ICMP_SKIP
# define	ICMP_SKIP	39
#endif
#ifndef	ICMP_PHOTURIS
# define	ICMP_PHOTURIS	40
#endif
#ifndef	ICMP_PHOTURIS_UNKNOWN_INDEX
# define	ICMP_PHOTURIS_UNKNOWN_INDEX	1
#endif
#ifndef	ICMP_PHOTURIS_AUTH_FAILED
# define	ICMP_PHOTURIS_AUTH_FAILED	2
#endif
#ifndef	ICMP_PHOTURIS_DECRYPT_FAILED
# define	ICMP_PHOTURIS_DECRYPT_FAILED	3
#endif
#ifndef	IPVERSION
# define	IPVERSION	4
#endif
#ifndef	IPOPT_MINOFF
# define	IPOPT_MINOFF	4
#endif
#ifndef	IPOPT_COPIED
# define	IPOPT_COPIED(x)	((x)&0x80)
#endif
#ifndef	IPOPT_EOL
# define	IPOPT_EOL	0
#endif
#ifndef	IPOPT_NOP
# define	IPOPT_NOP	1
#endif
#ifndef	IP_MF
# define	IP_MF	((u_short)0x2000)
#endif
#ifndef	ETHERTYPE_IP
# define	ETHERTYPE_IP	((u_short)0x0800)
#endif
#ifndef	TH_FIN
# define	TH_FIN	0x01
#endif
#ifndef	TH_SYN
# define	TH_SYN	0x02
#endif
#ifndef	TH_RST
# define	TH_RST	0x04
#endif
#ifndef	TH_PUSH
# define	TH_PUSH	0x08
#endif
#ifndef	TH_ACK
# define	TH_ACK	0x10
#endif
#ifndef	TH_URG
# define	TH_URG	0x20
#endif
#undef	TH_ACKMASK
#define	TH_ACKMASK	(TH_FIN|TH_SYN|TH_RST|TH_ACK)

#ifndef	IPOPT_EOL
# define	IPOPT_EOL	0
#endif
#ifndef	IPOPT_NOP
# define	IPOPT_NOP	1
#endif
#ifndef	IPOPT_RR
# define	IPOPT_RR	7
#endif
#ifndef	IPOPT_TS
# define	IPOPT_TS	68
#endif
#ifndef	IPOPT_SECURITY
# define	IPOPT_SECURITY	130
#endif
#ifndef	IPOPT_LSRR
# define	IPOPT_LSRR	131
#endif
#ifndef	IPOPT_SATID
# define	IPOPT_SATID	136
#endif
#ifndef	IPOPT_SSRR
# define	IPOPT_SSRR	137
#endif
#ifndef	IPOPT_SECUR_UNCLASS
# define	IPOPT_SECUR_UNCLASS	((u_short)0x0000)
#endif
#ifndef	IPOPT_SECUR_CONFID
# define	IPOPT_SECUR_CONFID	((u_short)0xf135)
#endif
#ifndef	IPOPT_SECUR_EFTO
# define	IPOPT_SECUR_EFTO	((u_short)0x789a)
#endif
#ifndef	IPOPT_SECUR_MMMM
# define	IPOPT_SECUR_MMMM	((u_short)0xbc4d)
#endif
#ifndef	IPOPT_SECUR_RESTR
# define	IPOPT_SECUR_RESTR	((u_short)0xaf13)
#endif
#ifndef	IPOPT_SECUR_SECRET
# define	IPOPT_SECUR_SECRET	((u_short)0xd788)
#endif
#ifndef IPOPT_SECUR_TOPSECRET
# define	IPOPT_SECUR_TOPSECRET	((u_short)0x6bc5)
#endif
#ifndef IPOPT_OLEN
# define	IPOPT_OLEN	1
#endif
#ifndef	IPPROTO_HOPOPTS
# define	IPPROTO_HOPOPTS	0
#endif
#ifndef	IPPROTO_IPIP
# define	IPPROTO_IPIP	4
#endif
#ifndef	IPPROTO_ENCAP
# define	IPPROTO_ENCAP	98
#endif
#ifndef	IPPROTO_IPV6
# define	IPPROTO_IPV6	41
#endif
#ifndef	IPPROTO_ROUTING
# define	IPPROTO_ROUTING	43
#endif
#ifndef	IPPROTO_FRAGMENT
# define	IPPROTO_FRAGMENT	44
#endif
#ifndef	IPPROTO_GRE
# define	IPPROTO_GRE	47	/* GRE encaps RFC 1701 */
#endif
#ifndef	IPPROTO_ESP
# define	IPPROTO_ESP	50
#endif
#ifndef	IPPROTO_AH
# define	IPPROTO_AH	51
#endif
#ifndef	IPPROTO_ICMPV6
# define	IPPROTO_ICMPV6	58
#endif
#ifndef	IPPROTO_NONE
# define	IPPROTO_NONE	59
#endif
#ifndef	IPPROTO_DSTOPTS
# define	IPPROTO_DSTOPTS	60
#endif
#ifndef	IPPROTO_MOBILITY
# define	IPPROTO_MOBILITY	135
#endif

#ifndef	ICMP_ROUTERADVERT
# define	ICMP_ROUTERADVERT	9
#endif
#ifndef	ICMP_ROUTERSOLICIT
# define	ICMP_ROUTERSOLICIT	10
#endif
#ifndef	ICMP6_DST_UNREACH
# define	ICMP6_DST_UNREACH	1
#endif
#ifndef	ICMP6_PACKET_TOO_BIG
# define	ICMP6_PACKET_TOO_BIG	2
#endif
#ifndef	ICMP6_TIME_EXCEEDED
# define	ICMP6_TIME_EXCEEDED	3
#endif
#ifndef	ICMP6_PARAM_PROB
# define	ICMP6_PARAM_PROB	4
#endif

#ifndef	ICMP6_ECHO_REQUEST
# define	ICMP6_ECHO_REQUEST	128
#endif
#ifndef	ICMP6_ECHO_REPLY
# define	ICMP6_ECHO_REPLY	129
#endif
#ifndef	ICMP6_MEMBERSHIP_QUERY
# define	ICMP6_MEMBERSHIP_QUERY	130
#endif
#ifndef	MLD6_LISTENER_QUERY
# define	MLD6_LISTENER_QUERY	130
#endif
#ifndef	ICMP6_MEMBERSHIP_REPORT
# define	ICMP6_MEMBERSHIP_REPORT	131
#endif
#ifndef	MLD6_LISTENER_REPORT
# define	MLD6_LISTENER_REPORT	131
#endif
#ifndef	ICMP6_MEMBERSHIP_REDUCTION
# define	ICMP6_MEMBERSHIP_REDUCTION	132
#endif
#ifndef	MLD6_LISTENER_DONE
# define	MLD6_LISTENER_DONE	132
#endif
#ifndef	ND_ROUTER_SOLICIT
# define	ND_ROUTER_SOLICIT	133
#endif
#ifndef	ND_ROUTER_ADVERT
# define	ND_ROUTER_ADVERT	134
#endif
#ifndef	ND_NEIGHBOR_SOLICIT
# define	ND_NEIGHBOR_SOLICIT	135
#endif
#ifndef	ND_NEIGHBOR_ADVERT
# define	ND_NEIGHBOR_ADVERT	136
#endif
#ifndef	ND_REDIRECT
# define	ND_REDIRECT	137
#endif
#ifndef	ICMP6_ROUTER_RENUMBERING
# define	ICMP6_ROUTER_RENUMBERING	138
#endif
#ifndef	ICMP6_WRUREQUEST
# define	ICMP6_WRUREQUEST	139
#endif
#ifndef	ICMP6_WRUREPLY
# define	ICMP6_WRUREPLY		140
#endif
#ifndef	ICMP6_FQDN_QUERY
# define	ICMP6_FQDN_QUERY	139
#endif
#ifndef	ICMP6_FQDN_REPLY
# define	ICMP6_FQDN_REPLY	140
#endif
#ifndef	ICMP6_NI_QUERY
# define	ICMP6_NI_QUERY		139
#endif
#ifndef	ICMP6_NI_REPLY
# define	ICMP6_NI_REPLY		140
#endif
#ifndef	MLD6_MTRACE_RESP
# define	MLD6_MTRACE_RESP	200
#endif
#ifndef	MLD6_MTRACE
# define	MLD6_MTRACE		201
#endif
#ifndef	ICMP6_HADISCOV_REQUEST
# define	ICMP6_HADISCOV_REQUEST	202
#endif
#ifndef	ICMP6_HADISCOV_REPLY
# define	ICMP6_HADISCOV_REPLY	203
#endif
#ifndef	ICMP6_MOBILEPREFIX_SOLICIT
# define	ICMP6_MOBILEPREFIX_SOLICIT	204
#endif
#ifndef	ICMP6_MOBILEPREFIX_ADVERT
# define	ICMP6_MOBILEPREFIX_ADVERT	205
#endif
#ifndef	ICMP6_MAXTYPE
# define	ICMP6_MAXTYPE		205
#endif

#ifndef	ICMP6_DST_UNREACH_NOROUTE
# define	ICMP6_DST_UNREACH_NOROUTE	0
#endif
#ifndef	ICMP6_DST_UNREACH_ADMIN
# define	ICMP6_DST_UNREACH_ADMIN		1
#endif
#ifndef	ICMP6_DST_UNREACH_NOTNEIGHBOR
# define	ICMP6_DST_UNREACH_NOTNEIGHBOR	2
#endif
#ifndef	ICMP6_DST_UNREACH_BEYONDSCOPE
# define	ICMP6_DST_UNREACH_BEYONDSCOPE	2
#endif
#ifndef	ICMP6_DST_UNREACH_ADDR
# define	ICMP6_DST_UNREACH_ADDR		3
#endif
#ifndef	ICMP6_DST_UNREACH_NOPORT
# define	ICMP6_DST_UNREACH_NOPORT	4
#endif
#ifndef	ICMP6_TIME_EXCEED_TRANSIT
# define	ICMP6_TIME_EXCEED_TRANSIT	0
#endif
#ifndef	ICMP6_TIME_EXCEED_REASSEMBLY
# define	ICMP6_TIME_EXCEED_REASSEMBLY	1
#endif

#ifndef	ICMP6_NI_SUCCESS
# define	ICMP6_NI_SUCCESS	0
#endif
#ifndef	ICMP6_NI_REFUSED
# define	ICMP6_NI_REFUSED	1
#endif
#ifndef	ICMP6_NI_UNKNOWN
# define	ICMP6_NI_UNKNOWN	2
#endif

#ifndef	ICMP6_ROUTER_RENUMBERING_COMMAND
# define	ICMP6_ROUTER_RENUMBERING_COMMAND	0
#endif
#ifndef	ICMP6_ROUTER_RENUMBERING_RESULT
# define	ICMP6_ROUTER_RENUMBERING_RESULT	1
#endif
#ifndef	ICMP6_ROUTER_RENUMBERING_SEQNUM_RESET
# define	ICMP6_ROUTER_RENUMBERING_SEQNUM_RESET	255
#endif

#ifndef	ICMP6_PARAMPROB_HEADER
# define	ICMP6_PARAMPROB_HEADER	0
#endif
#ifndef	ICMP6_PARAMPROB_NEXTHEADER
# define	ICMP6_PARAMPROB_NEXTHEADER	1
#endif
#ifndef	ICMP6_PARAMPROB_OPTION
# define	ICMP6_PARAMPROB_OPTION	2
#endif

#ifndef	ICMP6_NI_SUBJ_IPV6
# define	ICMP6_NI_SUBJ_IPV6	0
#endif
#ifndef	ICMP6_NI_SUBJ_FQDN
# define	ICMP6_NI_SUBJ_FQDN	1
#endif
#ifndef	ICMP6_NI_SUBJ_IPV4
# define	ICMP6_NI_SUBJ_IPV4	2
#endif

#ifndef	MLD_MTRACE_RESP
# define	MLD_MTRACE_RESP		200
#endif
#ifndef	MLD_MTRACE
# define	MLD_MTRACE		201
#endif
#ifndef	MLD6_MTRACE_RESP
# define	MLD6_MTRACE_RESP	MLD_MTRACE_RESP
#endif
#ifndef	MLD6_MTRACE
# define	MLD6_MTRACE		MLD_MTRACE
#endif

#if !defined(IPV6_FLOWINFO_MASK)
# if (BYTE_ORDER == BIG_ENDIAN) || defined(_BIG_ENDIAN)
#  define IPV6_FLOWINFO_MASK	0x0fffffff	/* flow info (28 bits) */
# else
#  if(BYTE_ORDER == LITTLE_ENDIAN) || !defined(_BIG_ENDIAN)
#   define IPV6_FLOWINFO_MASK	0xffffff0f	/* flow info (28 bits) */
#  endif /* LITTLE_ENDIAN */
# endif
#endif
#if !defined(IPV6_FLOWLABEL_MASK)
# if (BYTE_ORDER == BIG_ENDIAN) || defined(_BIG_ENDIAN)
#  define IPV6_FLOWLABEL_MASK	0x000fffff	/* flow label (20 bits) */
# else
#  if (BYTE_ORDER == LITTLE_ENDIAN) || !defined(_BIG_ENDIAN)
#   define IPV6_FLOWLABEL_MASK	0xffff0f00	/* flow label (20 bits) */
#  endif /* LITTLE_ENDIAN */
# endif
#endif

/*
 * ECN is a new addition to TCP - RFC 2481
 */
#ifndef TH_ECN
# define	TH_ECN	0x40
#endif
#ifndef TH_CWR
# define	TH_CWR	0x80
#endif
#define	TH_ECNALL	(TH_ECN|TH_CWR)

/*
 * TCP States
 */
#define IPF_TCPS_LISTEN		0	/* listening for connection */
#define IPF_TCPS_SYN_SENT	1	/* active, have sent syn */
#define IPF_TCPS_SYN_RECEIVED	2	/* have send and received syn */
#define IPF_TCPS_HALF_ESTAB	3	/* for connections not fully "up" */
/* states < IPF_TCPS_ESTABLISHED are those where connections not established */
#define IPF_TCPS_ESTABLISHED	4	/* established */
#define IPF_TCPS_CLOSE_WAIT	5	/* rcvd fin, waiting for close */
/* states > IPF_TCPS_CLOSE_WAIT are those where user has closed */
#define IPF_TCPS_FIN_WAIT_1	6	/* have closed, sent fin */
#define IPF_TCPS_CLOSING	7	/* closed xchd FIN; await FIN ACK */
#define IPF_TCPS_LAST_ACK	8	/* had fin and close; await FIN ACK */
/* states > IPF_TCPS_CLOSE_WAIT && < IPF_TCPS_FIN_WAIT_2 await ACK of FIN */
#define IPF_TCPS_FIN_WAIT_2	9	/* have closed, fin is acked */
#define IPF_TCPS_TIME_WAIT	10	/* in 2*msl quiet wait after close */
#define IPF_TCPS_CLOSED		11	/* closed */
#define IPF_TCP_NSTATES		12

#define	TCP_MSL			120

#undef	ICMP_MAX_UNREACH
#define	ICMP_MAX_UNREACH	14
#undef	ICMP_MAXTYPE
#define	ICMP_MAXTYPE		18

#ifndef	LOG_FTP
# define	LOG_FTP		(11<<3)
#endif
#ifndef	LOG_AUTHPRIV
# define	LOG_AUTHPRIV	(10<<3)
#endif
#ifndef	LOG_AUDIT
# define	LOG_AUDIT	(13<<3)
#endif
#ifndef	LOG_NTP
# define	LOG_NTP		(12<<3)
#endif
#ifndef	LOG_SECURITY
# define	LOG_SECURITY	(13<<3)
#endif
#ifndef	LOG_LFMT
# define	LOG_LFMT	(14<<3)
#endif
#ifndef	LOG_CONSOLE
# define	LOG_CONSOLE	(14<<3)
#endif

/*
 * ICMP error replies have an IP header (20 bytes), 8 bytes of ICMP data,
 * another IP header and then 64 bits of data, totalling 56.  Of course,
 * the last 64 bits is dependent on that being available.
 */
#define	ICMPERR_ICMPHLEN	8
#define	ICMPERR_IPICMPHLEN	(20 + 8)
#define	ICMPERR_MINPKTLEN	(20 + 8 + 20)
#define	ICMPERR_MAXPKTLEN	(20 + 8 + 20 + 8)
#define ICMP6ERR_MINPKTLEN	(40 + 8)
#define ICMP6ERR_IPICMPHLEN	(40 + 8 + 40)

#ifndef MIN
# define	MIN(a,b)	(((a)<(b))?(a):(b))
#endif

#ifdef RESCUE
# undef IPFILTER_BPF
#endif

#ifdef IPF_DEBUG
# define	DPRINT(x)	printf x
#else
# define	DPRINT(x)
#endif

#ifdef DTRACE_PROBE
# ifdef _KERNEL
#  define	DT(_n)			DTRACE_PROBE(_n)
#  define	DT1(_n,_a,_b)		DTRACE_PROBE1(_n,_a,_b)
#  define	DT2(_n,_a,_b,_c,_d)	DTRACE_PROBE2(_n,_a,_b,_c,_d)
#  define	DT3(_n,_a,_b,_c,_d,_e,_f)	\
					DTRACE_PROBE3(_n,_a,_b,_c,_d,_e,_f)
#  define	DT4(_n,_a,_b,_c,_d,_e,_f,_g,_h) \
				DTRACE_PROBE4(_n,_a,_b,_c,_d,_e,_f,_g,_h)
# else
#  define	DT(_n)
#  define	DT1(_n,_a,_b)
#  define	DT2(_n,_a,_b,_c,_d)
#  define	DT3(_n,_a,_b,_c,_d,_e,_f)
#  define	DT4(_n,_a,_b,_c,_d,_e,_f,_g,_h)
# endif
#else
# define	DT(_n)
# define	DT1(_n,_a,_b)
# define	DT2(_n,_a,_b,_c,_d)
# define	DT3(_n,_a,_b,_c,_d,_e,_f)
# define	DT4(_n,_a,_b,_c,_d,_e,_f,_g,_h)
#endif

struct ip6_routing {
	u_char	ip6r_nxt;	/* next header */
	u_char	ip6r_len;	/* length in units of 8 octets */
	u_char	ip6r_type;	/* always zero */
	u_char	ip6r_segleft;	/* segments left */
	u_32_t	ip6r_reserved;	/* reserved field */
};

#endif	/* __IP_COMPAT_H__ */
