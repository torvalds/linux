/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001-2007, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef _NETINET_SCTP_LOCK_BSD_H_
#define _NETINET_SCTP_LOCK_BSD_H_

/*
 * General locking concepts: The goal of our locking is to of course provide
 * consistency and yet minimize overhead. We will attempt to use
 * non-recursive locks which are supposed to be quite inexpensive. Now in
 * order to do this the goal is that most functions are not aware of locking.
 * Once we have a TCB we lock it and unlock when we are through. This means
 * that the TCB lock is kind-of a "global" lock when working on an
 * association. Caution must be used when asserting a TCB_LOCK since if we
 * recurse we deadlock.
 *
 * Most other locks (INP and INFO) attempt to localize the locking i.e. we try
 * to contain the lock and unlock within the function that needs to lock it.
 * This sometimes mean we do extra locks and unlocks and lose a bit of
 * efficiency, but if the performance statements about non-recursive locks are
 * true this should not be a problem.  One issue that arises with this only
 * lock when needed is that if an implicit association setup is done we have
 * a problem. If at the time I lookup an association I have NULL in the tcb
 * return, by the time I call to create the association some other processor
 * could have created it. This is what the CREATE lock on the endpoint.
 * Places where we will be implicitly creating the association OR just
 * creating an association (the connect call) will assert the CREATE_INP
 * lock. This will assure us that during all the lookup of INP and INFO if
 * another creator is also locking/looking up we can gate the two to
 * synchronize. So the CREATE_INP lock is also another one we must use
 * extreme caution in locking to make sure we don't hit a re-entrancy issue.
 *
 * For non FreeBSD 5.x we provide a bunch of EMPTY lock macros so we can
 * blatantly put locks everywhere and they reduce to nothing on
 * NetBSD/OpenBSD and FreeBSD 4.x
 *
 */

/*
 * When working with the global SCTP lists we lock and unlock the INP_INFO
 * lock. So when we go to lookup an association we will want to do a
 * SCTP_INP_INFO_RLOCK() and then when we want to add a new association to
 * the SCTP_BASE_INFO() list's we will do a SCTP_INP_INFO_WLOCK().
 */

extern struct sctp_foo_stuff sctp_logoff[];
extern int sctp_logoff_stuff;

#define SCTP_IPI_COUNT_INIT()

#define SCTP_STATLOG_INIT_LOCK()
#define SCTP_STATLOG_LOCK()
#define SCTP_STATLOG_UNLOCK()
#define SCTP_STATLOG_DESTROY()

#define SCTP_INP_INFO_LOCK_DESTROY() do { \
        if(rw_wowned(&SCTP_BASE_INFO(ipi_ep_mtx))) { \
             rw_wunlock(&SCTP_BASE_INFO(ipi_ep_mtx)); \
        } \
        rw_destroy(&SCTP_BASE_INFO(ipi_ep_mtx)); \
      }  while (0)

#define SCTP_INP_INFO_LOCK_INIT() \
        rw_init(&SCTP_BASE_INFO(ipi_ep_mtx), "sctp-info");


#define SCTP_INP_INFO_RLOCK()	do { 					\
             rw_rlock(&SCTP_BASE_INFO(ipi_ep_mtx));                         \
} while (0)

#define SCTP_MCORE_QLOCK_INIT(cpstr) do { \
		mtx_init(&(cpstr)->que_mtx,	      \
			 "sctp-mcore_queue","queue_lock",	\
			 MTX_DEF|MTX_DUPOK);		\
} while (0)

#define SCTP_MCORE_QLOCK(cpstr)  do { \
		mtx_lock(&(cpstr)->que_mtx);	\
} while (0)

#define SCTP_MCORE_QUNLOCK(cpstr)  do { \
		mtx_unlock(&(cpstr)->que_mtx);	\
} while (0)

#define SCTP_MCORE_QDESTROY(cpstr)  do { \
	if(mtx_owned(&(cpstr)->core_mtx)) {	\
		mtx_unlock(&(cpstr)->que_mtx);	\
        } \
	mtx_destroy(&(cpstr)->que_mtx);	\
} while (0)


#define SCTP_MCORE_LOCK_INIT(cpstr) do { \
		mtx_init(&(cpstr)->core_mtx,	      \
			 "sctp-cpulck","cpu_proc_lock",	\
			 MTX_DEF|MTX_DUPOK);		\
} while (0)

#define SCTP_MCORE_LOCK(cpstr)  do { \
		mtx_lock(&(cpstr)->core_mtx);	\
} while (0)

#define SCTP_MCORE_UNLOCK(cpstr)  do { \
		mtx_unlock(&(cpstr)->core_mtx);	\
} while (0)

#define SCTP_MCORE_DESTROY(cpstr)  do { \
	if(mtx_owned(&(cpstr)->core_mtx)) {	\
		mtx_unlock(&(cpstr)->core_mtx);	\
        } \
	mtx_destroy(&(cpstr)->core_mtx);	\
} while (0)

#define SCTP_INP_INFO_WLOCK()	do { 					\
            rw_wlock(&SCTP_BASE_INFO(ipi_ep_mtx));                         \
} while (0)


#define SCTP_INP_INFO_RUNLOCK()		rw_runlock(&SCTP_BASE_INFO(ipi_ep_mtx))
#define SCTP_INP_INFO_WUNLOCK()		rw_wunlock(&SCTP_BASE_INFO(ipi_ep_mtx))


#define SCTP_IPI_ADDR_INIT()								\
        rw_init(&SCTP_BASE_INFO(ipi_addr_mtx), "sctp-addr")
#define SCTP_IPI_ADDR_DESTROY() do  { \
        if(rw_wowned(&SCTP_BASE_INFO(ipi_addr_mtx))) { \
             rw_wunlock(&SCTP_BASE_INFO(ipi_addr_mtx)); \
        } \
	rw_destroy(&SCTP_BASE_INFO(ipi_addr_mtx)); \
      }  while (0)
#define SCTP_IPI_ADDR_RLOCK()	do { 					\
             rw_rlock(&SCTP_BASE_INFO(ipi_addr_mtx));                         \
} while (0)
#define SCTP_IPI_ADDR_WLOCK()	do { 					\
             rw_wlock(&SCTP_BASE_INFO(ipi_addr_mtx));                         \
} while (0)

#define SCTP_IPI_ADDR_RUNLOCK()		rw_runlock(&SCTP_BASE_INFO(ipi_addr_mtx))
#define SCTP_IPI_ADDR_WUNLOCK()		rw_wunlock(&SCTP_BASE_INFO(ipi_addr_mtx))


#define SCTP_IPI_ITERATOR_WQ_INIT() \
        mtx_init(&sctp_it_ctl.ipi_iterator_wq_mtx, "sctp-it-wq", "sctp_it_wq", MTX_DEF)

#define SCTP_IPI_ITERATOR_WQ_DESTROY() \
	mtx_destroy(&sctp_it_ctl.ipi_iterator_wq_mtx)

#define SCTP_IPI_ITERATOR_WQ_LOCK()	do { 					\
             mtx_lock(&sctp_it_ctl.ipi_iterator_wq_mtx);                \
} while (0)

#define SCTP_IPI_ITERATOR_WQ_UNLOCK()		mtx_unlock(&sctp_it_ctl.ipi_iterator_wq_mtx)


#define SCTP_IP_PKTLOG_INIT() \
        mtx_init(&SCTP_BASE_INFO(ipi_pktlog_mtx), "sctp-pktlog", "packetlog", MTX_DEF)


#define SCTP_IP_PKTLOG_LOCK()	do { 			\
             mtx_lock(&SCTP_BASE_INFO(ipi_pktlog_mtx));     \
} while (0)

#define SCTP_IP_PKTLOG_UNLOCK()	mtx_unlock(&SCTP_BASE_INFO(ipi_pktlog_mtx))

#define SCTP_IP_PKTLOG_DESTROY() \
	mtx_destroy(&SCTP_BASE_INFO(ipi_pktlog_mtx))





/*
 * The INP locks we will use for locking an SCTP endpoint, so for example if
 * we want to change something at the endpoint level for example random_store
 * or cookie secrets we lock the INP level.
 */

#define SCTP_INP_READ_INIT(_inp) \
	mtx_init(&(_inp)->inp_rdata_mtx, "sctp-read", "inpr", MTX_DEF | MTX_DUPOK)

#define SCTP_INP_READ_DESTROY(_inp) \
	mtx_destroy(&(_inp)->inp_rdata_mtx)

#define SCTP_INP_READ_LOCK(_inp)	do { \
        mtx_lock(&(_inp)->inp_rdata_mtx);    \
} while (0)


#define SCTP_INP_READ_UNLOCK(_inp) mtx_unlock(&(_inp)->inp_rdata_mtx)


#define SCTP_INP_LOCK_INIT(_inp) \
	mtx_init(&(_inp)->inp_mtx, "sctp-inp", "inp", MTX_DEF | MTX_DUPOK)
#define SCTP_ASOC_CREATE_LOCK_INIT(_inp) \
	mtx_init(&(_inp)->inp_create_mtx, "sctp-create", "inp_create", \
		 MTX_DEF | MTX_DUPOK)

#define SCTP_INP_LOCK_DESTROY(_inp) \
	mtx_destroy(&(_inp)->inp_mtx)

#define SCTP_INP_LOCK_CONTENDED(_inp) ((_inp)->inp_mtx.mtx_lock & MTX_CONTESTED)

#define SCTP_INP_READ_CONTENDED(_inp) ((_inp)->inp_rdata_mtx.mtx_lock & MTX_CONTESTED)

#define SCTP_ASOC_CREATE_LOCK_CONTENDED(_inp) ((_inp)->inp_create_mtx.mtx_lock & MTX_CONTESTED)


#define SCTP_ASOC_CREATE_LOCK_DESTROY(_inp) \
	mtx_destroy(&(_inp)->inp_create_mtx)


#ifdef SCTP_LOCK_LOGGING
#define SCTP_INP_RLOCK(_inp)	do { 					\
	if(SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOCK_LOGGING_ENABLE) sctp_log_lock(_inp, (struct sctp_tcb *)NULL, SCTP_LOG_LOCK_INP);\
        mtx_lock(&(_inp)->inp_mtx);                                     \
} while (0)

#define SCTP_INP_WLOCK(_inp)	do { 					\
	if(SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOCK_LOGGING_ENABLE) sctp_log_lock(_inp, (struct sctp_tcb *)NULL, SCTP_LOG_LOCK_INP);\
        mtx_lock(&(_inp)->inp_mtx);                                     \
} while (0)

#else

#define SCTP_INP_RLOCK(_inp)	do { 					\
        mtx_lock(&(_inp)->inp_mtx);                                     \
} while (0)

#define SCTP_INP_WLOCK(_inp)	do { 					\
        mtx_lock(&(_inp)->inp_mtx);                                     \
} while (0)

#endif


#define SCTP_TCB_SEND_LOCK_INIT(_tcb) \
	mtx_init(&(_tcb)->tcb_send_mtx, "sctp-send-tcb", "tcbs", MTX_DEF | MTX_DUPOK)

#define SCTP_TCB_SEND_LOCK_DESTROY(_tcb) mtx_destroy(&(_tcb)->tcb_send_mtx)

#define SCTP_TCB_SEND_LOCK(_tcb)  do { \
	mtx_lock(&(_tcb)->tcb_send_mtx); \
} while (0)

#define SCTP_TCB_SEND_UNLOCK(_tcb) mtx_unlock(&(_tcb)->tcb_send_mtx)

#define SCTP_INP_INCR_REF(_inp) atomic_add_int(&((_inp)->refcount), 1)
#define SCTP_INP_DECR_REF(_inp) atomic_add_int(&((_inp)->refcount), -1)


#ifdef SCTP_LOCK_LOGGING
#define SCTP_ASOC_CREATE_LOCK(_inp) \
	do {								\
	if(SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOCK_LOGGING_ENABLE) sctp_log_lock(_inp, (struct sctp_tcb *)NULL, SCTP_LOG_LOCK_CREATE); \
		mtx_lock(&(_inp)->inp_create_mtx);			\
	} while (0)
#else

#define SCTP_ASOC_CREATE_LOCK(_inp) \
	do {								\
		mtx_lock(&(_inp)->inp_create_mtx);			\
	} while (0)
#endif

#define SCTP_INP_RUNLOCK(_inp)		mtx_unlock(&(_inp)->inp_mtx)
#define SCTP_INP_WUNLOCK(_inp)		mtx_unlock(&(_inp)->inp_mtx)
#define SCTP_ASOC_CREATE_UNLOCK(_inp)	mtx_unlock(&(_inp)->inp_create_mtx)

/*
 * For the majority of things (once we have found the association) we will
 * lock the actual association mutex. This will protect all the assoiciation
 * level queues and streams and such. We will need to lock the socket layer
 * when we stuff data up into the receiving sb_mb. I.e. we will need to do an
 * extra SOCKBUF_LOCK(&so->so_rcv) even though the association is locked.
 */

#define SCTP_TCB_LOCK_INIT(_tcb) \
	mtx_init(&(_tcb)->tcb_mtx, "sctp-tcb", "tcb", MTX_DEF | MTX_DUPOK)

#define SCTP_TCB_LOCK_DESTROY(_tcb)	mtx_destroy(&(_tcb)->tcb_mtx)

#ifdef SCTP_LOCK_LOGGING
#define SCTP_TCB_LOCK(_tcb)  do {					\
	if(SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOCK_LOGGING_ENABLE)  sctp_log_lock(_tcb->sctp_ep, _tcb, SCTP_LOG_LOCK_TCB);          \
	mtx_lock(&(_tcb)->tcb_mtx);                                     \
} while (0)

#else
#define SCTP_TCB_LOCK(_tcb)  do {					\
	mtx_lock(&(_tcb)->tcb_mtx);                                     \
} while (0)

#endif


#define SCTP_TCB_TRYLOCK(_tcb) 	mtx_trylock(&(_tcb)->tcb_mtx)

#define SCTP_TCB_UNLOCK(_tcb)		mtx_unlock(&(_tcb)->tcb_mtx)

#define SCTP_TCB_UNLOCK_IFOWNED(_tcb)	      do { \
                                                if (mtx_owned(&(_tcb)->tcb_mtx)) \
                                                     mtx_unlock(&(_tcb)->tcb_mtx); \
                                              } while (0)



#ifdef INVARIANTS
#define SCTP_TCB_LOCK_ASSERT(_tcb) do { \
                            if (mtx_owned(&(_tcb)->tcb_mtx) == 0) \
                                panic("Don't own TCB lock"); \
                            } while (0)
#else
#define SCTP_TCB_LOCK_ASSERT(_tcb)
#endif

#define SCTP_ITERATOR_LOCK_INIT() \
        mtx_init(&sctp_it_ctl.it_mtx, "sctp-it", "iterator", MTX_DEF)

#ifdef INVARIANTS
#define SCTP_ITERATOR_LOCK() \
	do {								\
		if (mtx_owned(&sctp_it_ctl.it_mtx))			\
			panic("Iterator Lock");				\
		mtx_lock(&sctp_it_ctl.it_mtx);				\
	} while (0)
#else
#define SCTP_ITERATOR_LOCK() \
	do {								\
		mtx_lock(&sctp_it_ctl.it_mtx);				\
	} while (0)

#endif

#define SCTP_ITERATOR_UNLOCK()	        mtx_unlock(&sctp_it_ctl.it_mtx)
#define SCTP_ITERATOR_LOCK_DESTROY()	mtx_destroy(&sctp_it_ctl.it_mtx)


#define SCTP_WQ_ADDR_INIT() do { \
        mtx_init(&SCTP_BASE_INFO(wq_addr_mtx), "sctp-addr-wq","sctp_addr_wq",MTX_DEF); \
 } while (0)

#define SCTP_WQ_ADDR_DESTROY() do  { \
        if(mtx_owned(&SCTP_BASE_INFO(wq_addr_mtx))) { \
             mtx_unlock(&SCTP_BASE_INFO(wq_addr_mtx)); \
        } \
	    mtx_destroy(&SCTP_BASE_INFO(wq_addr_mtx)); \
      }  while (0)

#define SCTP_WQ_ADDR_LOCK()	do { \
             mtx_lock(&SCTP_BASE_INFO(wq_addr_mtx));  \
} while (0)
#define SCTP_WQ_ADDR_UNLOCK() do { \
		mtx_unlock(&SCTP_BASE_INFO(wq_addr_mtx)); \
} while (0)



#define SCTP_INCR_EP_COUNT() \
                do { \
		       atomic_add_int(&SCTP_BASE_INFO(ipi_count_ep), 1); \
	        } while (0)

#define SCTP_DECR_EP_COUNT() \
                do { \
		       atomic_subtract_int(&SCTP_BASE_INFO(ipi_count_ep), 1); \
	        } while (0)

#define SCTP_INCR_ASOC_COUNT() \
                do { \
	               atomic_add_int(&SCTP_BASE_INFO(ipi_count_asoc), 1); \
	        } while (0)

#define SCTP_DECR_ASOC_COUNT() \
                do { \
	               atomic_subtract_int(&SCTP_BASE_INFO(ipi_count_asoc), 1); \
	        } while (0)

#define SCTP_INCR_LADDR_COUNT() \
                do { \
	               atomic_add_int(&SCTP_BASE_INFO(ipi_count_laddr), 1); \
	        } while (0)

#define SCTP_DECR_LADDR_COUNT() \
                do { \
	               atomic_subtract_int(&SCTP_BASE_INFO(ipi_count_laddr), 1); \
	        } while (0)

#define SCTP_INCR_RADDR_COUNT() \
                do { \
 	               atomic_add_int(&SCTP_BASE_INFO(ipi_count_raddr), 1); \
	        } while (0)

#define SCTP_DECR_RADDR_COUNT() \
                do { \
 	               atomic_subtract_int(&SCTP_BASE_INFO(ipi_count_raddr),1); \
	        } while (0)

#define SCTP_INCR_CHK_COUNT() \
                do { \
  	               atomic_add_int(&SCTP_BASE_INFO(ipi_count_chunk), 1); \
	        } while (0)
#ifdef INVARIANTS
#define SCTP_DECR_CHK_COUNT() \
                do { \
                       if(SCTP_BASE_INFO(ipi_count_chunk) == 0) \
                             panic("chunk count to 0?");    \
  	               atomic_subtract_int(&SCTP_BASE_INFO(ipi_count_chunk), 1); \
	        } while (0)
#else
#define SCTP_DECR_CHK_COUNT() \
                do { \
                       if(SCTP_BASE_INFO(ipi_count_chunk) != 0) \
  	               atomic_subtract_int(&SCTP_BASE_INFO(ipi_count_chunk), 1); \
	        } while (0)
#endif
#define SCTP_INCR_READQ_COUNT() \
                do { \
		       atomic_add_int(&SCTP_BASE_INFO(ipi_count_readq),1); \
	        } while (0)

#define SCTP_DECR_READQ_COUNT() \
                do { \
		       atomic_subtract_int(&SCTP_BASE_INFO(ipi_count_readq), 1); \
	        } while (0)

#define SCTP_INCR_STRMOQ_COUNT() \
                do { \
		       atomic_add_int(&SCTP_BASE_INFO(ipi_count_strmoq), 1); \
	        } while (0)

#define SCTP_DECR_STRMOQ_COUNT() \
                do { \
		       atomic_subtract_int(&SCTP_BASE_INFO(ipi_count_strmoq), 1); \
	        } while (0)


#if defined(SCTP_SO_LOCK_TESTING)
#define SCTP_INP_SO(sctpinp)	(sctpinp)->ip_inp.inp.inp_socket
#define SCTP_SOCKET_LOCK(so, refcnt)
#define SCTP_SOCKET_UNLOCK(so, refcnt)
#endif

#endif
