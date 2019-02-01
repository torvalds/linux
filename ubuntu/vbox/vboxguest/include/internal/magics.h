/* $Id: magics.h $ */
/** @file
 * IPRT - Internal header defining The Magic Numbers.
 */

/*
 * Copyright (C) 2007-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef IPRT_INCLUDED_INTERNAL_magics_h
#define IPRT_INCLUDED_INTERNAL_magics_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** @name Magic Numbers.
 * @{ */

/** Magic number for RTAIOMGRINT::u32Magic. (Emil Erich Kaestner) */
#define RTAIOMGR_MAGIC                  UINT32_C(0x18990223)
/** Magic number for RTAIOMGRINTFILE::u32Magic. (Ephraim Kishon) */
#define RTAIOMGRFILE_MAGIC              UINT32_C(0x19240823)
/** Magic number for RTCRCIPHERINT::u32Magic. (Michael Wolff) */
#define RTCRCIPHERINT_MAGIC             UINT32_C(0x19530827)
/** Magic value for RTCRKEYINT::u32Magic. (Ronald Linn Rivest) */
#define RTCRKEYINT_MAGIC                UINT32_C(0x19470506)
/** Magic value for RTCRSSLINT::u32Magic. (Robert Upshur Woodward) */
#define RTCRSSLINT_MAGIC                UINT32_C(0x19430326)
/** Magic value for RTCRSSLSESSIONINT::u32Magic. (Carl Berstein) */
#define RTCRSSLSESSIONINT_MAGIC         UINT32_C(0x19440214)
/** Magic number for RTDBGMODINT::u32Magic. (Charles Lloyd) */
#define RTDBGAS_MAGIC                   UINT32_C(0x19380315)
/** Magic number for RTDBGCFGINT::u32Magic. (McCoy Tyner) */
#define RTDBGCFG_MAGIC                  UINT32_C(0x19381211)
/** Magic number for RTDBGMODINT::u32Magic. (Keith Jarrett) */
#define RTDBGMOD_MAGIC                  UINT32_C(0x19450508)
/** Magic number for RTDBGMODDEFERRED::u32Magic. (Chet Baker) */
#define RTDBGMODDEFERRED_MAGIC          UINT32_C(0x19291223)
/** Magic number for RTDBGMODDEFERRED::u32Magic after release. */
#define RTDBGMODDEFERRED_MAGIC_DEAD     UINT32_C(0x19880513)
/** Magic number for RTDBGMODLDR::u32Magic. (Gerry Mulligan) */
#define RTDBGMODLDR_MAGIC               UINT32_C(0x19270406)
/** Magic number for RTDBGMODLDR::u32Magic after close. */
#define RTDBGMODLDR_MAGIC_DEAD          UINT32_C(0x19960120)
/** Magic number for RTDBGMODVTIMG::u32Magic. (Jack DeJohnette) */
#define RTDBGMODVTDBG_MAGIC             UINT32_C(0x19420809)
/** Magic number for RTDBGMODVTIMG::u32Magic. (Cecil McBee) */
#define RTDBGMODVTIMG_MAGIC             UINT32_C(0x19350419)
/** Magic value for RTDBGKRNLINFOINT::u32Magic. (John Carmack) */
#define RTDBGKRNLINFO_MAGIC             UINT32_C(0x19700820)
/** The value of RTDIRINTERNAL::u32Magic. (Michael Ende) */
#define RTDIR_MAGIC                     UINT32_C(0x19291112)
/** The value of RTDIRINTERNAL::u32Magic after RTDirClose().  */
#define RTDIR_MAGIC_DEAD                UINT32_C(0x19950829)
/** The value of RTDVMINTERNAL::u32Magic. (Dan Brown) */
#define RTDVM_MAGIC                     UINT32_C(0x19640622)
/** The value of RTDVMINTERNAL::u32Magic after close. */
#define RTDVM_MAGIC_DEAD                (~RTDVM_MAGIC)
/** The value of RTDVMVOLUMEINTERNAL::u32Magic. (Daniel Defoe) */
#define RTDVMVOLUME_MAGIC               UINT32_C(0x16591961)
/** The value of RTDVMVOLUMEINTERNAL::u32Magic after close. */
#define RTDVMVOLUME_MAGIC_DEAD          UINT32_C(0x17310424)
/** The value of RTFILEAIOCTXINT::u32Magic. (Howard Phillips Lovecraft) */
#define RTFILEAIOCTX_MAGIC              UINT32_C(0x18900820)
/** The value of RTFILEAIOCTXINT::u32Magic after RTFileAioCtxDestroy(). */
#define RTFILEAIOCTX_MAGIC_DEAD         UINT32_C(0x19370315)
/** The value of RTFILEAIOREQINT::u32Magic. (Stephen Edwin King)  */
#define RTFILEAIOREQ_MAGIC              UINT32_C(0x19470921)
/** The value of RTENVINTERNAL::u32Magic. (Rumiko Takahashi) */
#define RTENV_MAGIC                     UINT32_C(0x19571010)
/** The value of RTERRVARS::ai32Vars[0]. (Ryuichi Sakamoto) */
#define RTERRVARS_MAGIC                 UINT32_C(0x19520117)
/** The value of RTFSISOMAKERINT::uMagic. (Brian Blade) */
#define RTFSISOMAKERINT_MAGIC           UINT32_C(0x19700725)
/** Magic number for RTHANDLETABLEINT::u32Magic. (Hitomi Kanehara) */
#define RTHANDLETABLE_MAGIC             UINT32_C(0x19830808)
/** Magic number for RTHEAPOFFSETINTERNAL::u32Magic. (Neal Town Stephenson) */
#define RTHEAPOFFSET_MAGIC              UINT32_C(0x19591031)
/** Magic number for RTHEAPSIMPLEINTERNAL::uMagic. (Kyoichi Katayama) */
#define RTHEAPSIMPLE_MAGIC              UINT32_C(0x19590105)
/** The magic value for RTHTTPINTERNAL::u32Magic. (Karl May) */
#define RTHTTP_MAGIC                    UINT32_C(0x18420225)
/** The value of RTHTTPINTERNAL::u32Magic after close. */
#define RTHTTP_MAGIC_DEAD               UINT32_C(0x19120330)
/** The value of RTINIFILEINT::u32Magic. (Jane Austen) */
#define RTINIFILE_MAGIC                 UINT32_C(0x17751216)
/** The value of RTINIFILEINT::u32Magic after close. */
#define RTINIFILE_MAGIC_DEAD            UINT32_C(0x18170718)
/** The magic value for RTLDRMODINTERNAL::u32Magic. (Alan Moore) */
#define RTLDRMOD_MAGIC                  UINT32_C(0x19531118)
/** The magic value for RTLOCALIPCSERVER::u32Magic. (Naoki Yamamoto) */
#define RTLOCALIPCSERVER_MAGIC          UINT32_C(0x19600201)
/** The magic value for RTLOCALIPCSERVER::u32Magic. (Katsuhiro Otomo) */
#define RTLOCALIPCSESSION_MAGIC         UINT32_C(0x19530414)
/** The magic value for RTLOCKVALCLASSINT::u32Magic. (Thomas Mann) */
#define RTLOCKVALCLASS_MAGIC            UINT32_C(0x18750605)
/** The magic value for RTLOCKVALCLASSINT::u32Magic after destruction. */
#define RTLOCKVALCLASS_MAGIC_DEAD       UINT32_C(0x19550812)
/** The magic value for RTLOCKVALRECEXCL::u32Magic. (Vladimir Vladimirovich Nabokov) */
#define RTLOCKVALRECEXCL_MAGIC          UINT32_C(0x18990422)
/** The dead magic value for RTLOCKVALRECEXCL::u32Magic. */
#define RTLOCKVALRECEXCL_MAGIC_DEAD     UINT32_C(0x19770702)
/** The magic value for RTLOCKVALRECSHRD::u32Magic. (Agnar Mykle) */
#define RTLOCKVALRECSHRD_MAGIC          UINT32_C(0x19150808)
/** The magic value for RTLOCKVALRECSHRD::u32Magic after deletion. */
#define RTLOCKVALRECSHRD_MAGIC_DEAD     UINT32_C(0x19940115)
/** The magic value for RTLOCKVALRECSHRDOWN::u32Magic. (Jens Ingvald Bjoerneboe) */
#define RTLOCKVALRECSHRDOWN_MAGIC       UINT32_C(0x19201009)
/** The magic value for RTLOCKVALRECSHRDOWN::u32Magic after deletion. */
#define RTLOCKVALRECSHRDOWN_MAGIC_DEAD  UINT32_C(0x19760509)
/** The magic value for RTLOCKVALRECNEST::u32Magic. (Anne Desclos) */
#define RTLOCKVALRECNEST_MAGIC          UINT32_C(0x19071123)
/** The magic value for RTLOCKVALRECNEST::u32Magic after deletion. */
#define RTLOCKVALRECNEST_MAGIC_DEAD     UINT32_C(0x19980427)
/** Magic number for RTMEMCACHEINT::u32Magic. (Joseph Weizenbaum) */
#define RTMEMCACHE_MAGIC                UINT32_C(0x19230108)
/** Dead magic number for RTMEMCACHEINT::u32Magic. */
#define RTMEMCACHE_MAGIC_DEAD           UINT32_C(0x20080305)
/** The magic value for RTMEMPOOL::u32Magic. (Jane Austin) */
#define RTMEMPOOL_MAGIC                 UINT32_C(0x17751216)
/** The magic value for RTMEMPOOL::u32Magic after RTMemPoolDestroy. */
#define RTMEMPOOL_MAGIC_DEAD            UINT32_C(0x18170718)
/** The magic value for heap blocks. (Edgar Allan Poe) */
#define RTMEMHDR_MAGIC                  UINT32_C(0x18090119)
/** The magic value for heap blocks after freeing. */
#define RTMEMHDR_MAGIC_DEAD             UINT32_C(0x18491007)
/** The value of RTPIPEINTERNAL::u32Magic. (Frank Schaetzing) */
#define RTPIPE_MAGIC                    UINT32_C(0x19570528)
/** The value of RTPOLLSETINTERNAL::u32Magic. (Ai Yazawa) */
#define RTPOLLSET_MAGIC                 UINT32_C(0x19670307)
/** RTR0MEMOBJ::u32Magic. (Masakazu Katsura) */
#define RTR0MEMOBJ_MAGIC                UINT32_C(0x19611210)
/** RTRANDINT::u32Magic. (Alan Moore) */
#define RTRANDINT_MAGIC                 UINT32_C(0x19531118)
/** The value of RTREQ::u32Magic. */
#define RTREQ_MAGIC                     UINT32_C(0xfeed0001) /**< @todo find a value */
/** The value of RTREQ::u32Magic of a freed request. */
#define RTREQ_MAGIC_DEAD                (~RTREQ_MAGIC)
/** The value of RTREQPOOLINT::u32Magic. */
#define RTREQPOOL_MAGIC                 UINT32_C(0xfeed0002)/**< @todo find a value */
/** The value of RTREQPOOLINT::u32Magic after destruction. */
#define RTREQPOOL_MAGIC_DEAD           (~RTREQPOOL_MAGIC)
/** The value of RTREQQUEUEINT::u32Magic. */
#define RTREQQUEUE_MAGIC                UINT32_C(0xfeed0003)/**< @todo find a value */
/** The value of RTREQQUEUEINT::u32Magic after destruction. */
#define RTREQQUEUE_MAGIC_DEAD           (~RTREQQUEUE_MAGIC)
/** The value of RTS3::u32Magic. (Edgar Wallace) */
#define RTS3_MAGIC                      UINT32_C(0x18750401)
/** The value of RTS3::u32Magic after RTS3Destroy().  */
#define RTS3_MAGIC_DEAD                 UINT32_C(0x19320210)
/** Magic for the event semaphore structure. (Neil Gaiman) */
#define RTSEMEVENT_MAGIC                UINT32_C(0x19601110)
/** Magic for the multiple release event semaphore structure. (Isaac Asimov) */
#define RTSEMEVENTMULTI_MAGIC           UINT32_C(0x19200102)
/** Dead magic value for multiple release event semaphore structures. */
#define RTSEMEVENTMULTI_MAGIC_DEAD      UINT32_C(0x19920406)
/** Magic value for RTSEMFASTMUTEXINTERNAL::u32Magic. (John Ronald Reuel Tolkien) */
#define RTSEMFASTMUTEX_MAGIC            UINT32_C(0x18920103)
/** Dead magic value for RTSEMFASTMUTEXINTERNAL::u32Magic. */
#define RTSEMFASTMUTEX_MAGIC_DEAD       UINT32_C(0x19730902)
/** Magic for the mutex semaphore structure. (Douglas Adams) */
#define RTSEMMUTEX_MAGIC                UINT32_C(0x19520311)
/** Dead magic for the mutex semaphore structure. */
#define RTSEMMUTEX_MAGIC_DEAD           UINT32_C(0x20010511)
/** Magic for the spinning mutex semaphore structure. (Natsume Soseki) */
#define RTSEMSPINMUTEX_MAGIC            UINT32_C(0x18670209)
/** Dead magic value for RTSEMSPINMUTEXINTERNAL::u32Magic. */
#define RTSEMSPINMUTEX_MAGIC_DEAD       UINT32_C(0x19161209)
/** RTSEMRWINTERNAL::u32Magic value. (Kosuke Fujishima) */
#define RTSEMRW_MAGIC                   UINT32_C(0x19640707)
/** RTSEMXROADSINTERNAL::u32Magic value. (Kenneth Elton "Ken" Kesey) */
#define RTSEMXROADS_MAGIC               UINT32_C(0x19350917)
/** RTSEMXROADSINTERNAL::u32Magic value after RTSemXRoadsDestroy. */
#define RTSEMXROADS_MAGIC_DEAD          UINT32_C(0x20011110)
/** RTSERIALPORTINTERNAL::u32Magic value (Jules-Gabriel Verne). */
#define RTSERIALPORT_MAGIC              UINT32_C(0x18280208)
/** RTSERIALPORTINTERNAL::u32Magic value after RTSerialPortClose. */
#define RTSERIALPORT_MAGIC_DEAD         UINT32_C(0x19050324)
/** RTSHMEMINT::u32Magic value (Stephen William Hawking) */
#define RTSHMEM_MAGIC                   UINT32_C(0x19420108)
/** RTSHMEMINT::u32Magic value after RTShMemClose */
#define RTSHMEM_MAGIC_DEAD              UINT32_C(0x20180314)
/** The magic value for RTSOCKETINT::u32Magic. (Stanislaw Lem) */
#define RTSOCKET_MAGIC                  UINT32_C(0x19210912)
/** The magic value for RTSOCKETINT::u32Magic after destruction. */
#define RTSOCKET_MAGIC_DEAD             UINT32_C(0x20060326)
/** Magic value for RTSPINLOCKINTERNAL::u32Magic. (Terry Pratchett) */
#define RTSPINLOCK_MAGIC                UINT32_C(0x19480428)
/** Magic value for generic RTSPINLOCKINTERNAL::u32Magic (Georges Prosper Remi). */
#define RTSPINLOCK_GEN_MAGIC            UINT32_C(0x10970522)
/** Magic value for RTSTRCACHE::u32Magic. (Sir Arthur Charles Clarke) */
#define RTSTRCACHE_MAGIC                UINT32_C(0x19171216)
/** Magic value for RTSTRCACHE::u32Magic after RTStrCacheDestroy. */
#define RTSTRCACHE_MAGIC_DEAD           UINT32_C(0x20080319)
/** The value of RTSTREAM::u32Magic for a valid stream. */
#define RTSTREAM_MAGIC                  UINT32_C(0xe44e44ee)
/** Magic value for RTTCPSERVER::u32Magic. (Jan Garbarek) */
#define RTTCPSERVER_MAGIC               UINT32_C(0x19470304)
/** Magic value for RTTCPSERVER::u32Magic. (Harlan Ellison) */
#define RTUDPSERVER_MAGIC               UINT32_C(0x19340527)
/** The value of RTTAR::u32Magic. (Donald Ervin Knuth) */
#define RTTAR_MAGIC                     UINT32_C(0x19380110)
/** The value of RTTAR::u32Magic after RTTarClose(). */
#define RTTAR_MAGIC_DEAD                ~RTTAR_MAGIC
/** The value of RTTARFILE::u32Magic. (Abraham Stoker) */
#define RTTARFILE_MAGIC                 UINT32_C(0x18471108)
/** The value of RTTARFILE::u32Magic after RTTarFileClose(). */
#define RTTARFILE_MAGIC_DEAD            UINT32_C(0x19120420)
/** RTTESTINT::u32Magic value. (Daniel Kehlmann) */
#define RTTESTINT_MAGIC                 UINT32_C(0x19750113)
/** RTTHREADCTXHOOKINT::u32Magic value. (Dennis MacAlistair Ritchie) */
#define RTTHREADCTXHOOKINT_MAGIC        UINT32_C(0x19410909)
/** RTTHREADINT::u32Magic value. (Gilbert Keith Chesterton) */
#define RTTHREADINT_MAGIC               UINT32_C(0x18740529)
/** RTTHREADINT::u32Magic value for a dead thread. */
#define RTTHREADINT_MAGIC_DEAD          UINT32_C(0x19360614)
/** Magic number for timer handles. (Jared Mason Diamond) */
#define RTTIMER_MAGIC                   UINT32_C(0x19370910)
/** Magic number for timer low resolution handles. (Saki Hiwatari) */
#define RTTIMERLR_MAGIC                 UINT32_C(0x19610715)
/** Magic value of RTTRACEBUFINT::u32Magic. (George Orwell) */
#define RTTRACEBUF_MAGIC                UINT32_C(0x19030625)
/** Magic value of RTTRACEBUFINT::u32Magic after the final release. */
#define RTTRACEBUF_MAGIC_DEAD           UINT32_C(0x19500121)
/** The value of RTTRACELOGRDRINT::u32Magic. (John Michael Scalzi) */
#define RTTRACELOGRDR_MAGIC             UINT32_C(0x19690510)
/** The value of RTTRACELOGRDRINT::u32Magic after RTTraceLogRdrDestroy(). */
#define RTTRACELOGRDR_MAGIC_DEAD        (~RTTRACELOGRDR_MAGIC)
/** The value of RTTRACELOGWRINT::u32Magic. (Herbert George Wells) */
#define RTTRACELOGWR_MAGIC              UINT32_C(0x18660921)
/** The value of RTTRACELOGWRINT::u32Magic after RTTraceLogWrDestroy(). */
#define RTTRACELOGWR_MAGIC_DEAD         UINT32_C(0x19460813)
/** The value of RTVFSOBJINTERNAL::u32Magic. (Yasunari Kawabata) */
#define RTVFSOBJ_MAGIC                  UINT32_C(0x18990614)
/** The value of RTVFSOBJINTERNAL::u32Magic after close. */
#define RTVFSOBJ_MAGIC_DEAD             UINT32_C(0x19720416)
/** The value of RTVFSINTERNAL::u32Magic. (Sir Kingsley William Amis) */
#define RTVFS_MAGIC                     UINT32_C(0x19220416)
/** The value of RTVFSINTERNAL::u32Magic after close.  */
#define RTVFS_MAGIC_DEAD                UINT32_C(0x19951022)
/** The value of RTVFSFSSTREAMINTERNAL::u32Magic. (William McGuire "Bill" Bryson) */
#define RTVFSFSSTREAM_MAGIC             UINT32_C(0x19511208)
/** The value of RTVFSFSSTREAMINTERNAL::u32Magic after close */
#define RTVFSFSSTREAM_MAGIC_DEAD        (~RTVFSFSSTREAM_MAGIC)
/** The value of RTVFSDIRINTERNAL::u32Magic. (Franklin Patrick Herbert, Jr.) */
#define RTVFSDIR_MAGIC                  UINT32_C(0x19201008)
/** The value of RTVFSDIRINTERNAL::u32Magic after close. */
#define RTVFSDIR_MAGIC_DEAD             UINT32_C(0x19860211)
/** The value of RTVFSFILEINTERNAL::u32Magic. (Charles John Huffam Dickens) */
#define RTVFSFILE_MAGIC                 UINT32_C(0x18120207)
/** The value of RTVFSFILEINTERNAL::u32Magic after close. */
#define RTVFSFILE_MAGIC_DEAD            UINT32_C(0x18700609)
/** The value of RTVFSIOSTREAMINTERNAL::u32Magic. (Ernest Miller Hemingway) */
#define RTVFSIOSTREAM_MAGIC             UINT32_C(0x18990721)
/** The value of RTVFSIOSTREAMINTERNAL::u32Magic after close. */
#define RTVFSIOSTREAM_MAGIC_DEAD        UINT32_C(0x19610702)
/** The value of RTVFSSYMLINKINTERNAL::u32Magic. (Francis Scott Key Fitzgerald) */
#define RTVFSSYMLINK_MAGIC              UINT32_C(0x18960924)
/** The value of RTVFSSYMLINKINTERNAL::u32Magic after close. */
#define RTVFSSYMLINK_MAGIC_DEAD         UINT32_C(0x19401221)

/** @} */

#endif /* !IPRT_INCLUDED_INTERNAL_magics_h */

