//===-- sanitizer_platform_limits_freebsd.cpp -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of Sanitizer common code.
//
// Sizes and layouts of platform-specific FreeBSD data structures.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_FREEBSD

#include <sys/capsicum.h>
#include <sys/consio.h>
#include <sys/cpuset.h>
#include <sys/filio.h>
#include <sys/ipc.h>
#include <sys/kbio.h>
#include <sys/link_elf.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/mqueue.h>
#include <sys/msg.h>
#include <sys/mtio.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/soundcard.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#include <sys/timeb.h>
#pragma clang diagnostic pop
#include <sys/times.h>
#include <sys/timespec.h>
#include <sys/types.h>
#include <sys/ucontext.h>
#include <sys/utsname.h>
//
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/ppp_defs.h>
#include <net/route.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip_mroute.h>
//
#include <dirent.h>
#include <dlfcn.h>
#include <fstab.h>
#include <fts.h>
#include <glob.h>
#include <grp.h>
#include <ifaddrs.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <regex.h>
#include <semaphore.h>
#include <signal.h>
#include <stddef.h>
#include <md5.h>
#include <sha224.h>
#include <sha256.h>
#include <sha384.h>
#include <sha512.h>
#include <stdio.h>
#include <stringlist.h>
#include <term.h>
#include <termios.h>
#include <time.h>
#include <ttyent.h>
#include <utime.h>
#include <utmpx.h>
#include <vis.h>
#include <wchar.h>
#include <wordexp.h>

#define _KERNEL  // to declare 'shminfo' structure
#include <sys/shm.h>
#undef _KERNEL

#undef IOC_DIRMASK

// Include these after system headers to avoid name clashes and ambiguities.
#include "sanitizer_internal_defs.h"
#include "sanitizer_libc.h"
#include "sanitizer_platform_limits_freebsd.h"

namespace __sanitizer {
void *__sanitizer_get_link_map_by_dlopen_handle(void *handle) {
  void *p = nullptr;
  return internal_dlinfo(handle, RTLD_DI_LINKMAP, &p) == 0 ? p : nullptr;
}

unsigned struct_cpuset_sz = sizeof(cpuset_t);
unsigned struct_cap_rights_sz = sizeof(cap_rights_t);
unsigned struct_utsname_sz = sizeof(struct utsname);
unsigned struct_stat_sz = sizeof(struct stat);
unsigned struct_rusage_sz = sizeof(struct rusage);
unsigned struct_tm_sz = sizeof(struct tm);
unsigned struct_passwd_sz = sizeof(struct passwd);
unsigned struct_group_sz = sizeof(struct group);
unsigned siginfo_t_sz = sizeof(siginfo_t);
unsigned struct_sigaction_sz = sizeof(struct sigaction);
unsigned struct_stack_t_sz = sizeof(stack_t);
unsigned struct_itimerval_sz = sizeof(struct itimerval);
unsigned pthread_t_sz = sizeof(pthread_t);
unsigned pthread_mutex_t_sz = sizeof(pthread_mutex_t);
unsigned pthread_cond_t_sz = sizeof(pthread_cond_t);
unsigned pid_t_sz = sizeof(pid_t);
unsigned timeval_sz = sizeof(timeval);
unsigned uid_t_sz = sizeof(uid_t);
unsigned gid_t_sz = sizeof(gid_t);
unsigned fpos_t_sz = sizeof(fpos_t);
unsigned mbstate_t_sz = sizeof(mbstate_t);
unsigned sigset_t_sz = sizeof(sigset_t);
unsigned struct_timezone_sz = sizeof(struct timezone);
unsigned struct_tms_sz = sizeof(struct tms);
unsigned struct_sigevent_sz = sizeof(struct sigevent);
unsigned struct_sched_param_sz = sizeof(struct sched_param);
unsigned struct_statfs_sz = sizeof(struct statfs);
unsigned struct_sockaddr_sz = sizeof(struct sockaddr);
unsigned ucontext_t_sz(void *ctx) { return sizeof(ucontext_t); }
unsigned struct_rlimit_sz = sizeof(struct rlimit);
unsigned struct_timespec_sz = sizeof(struct timespec);
unsigned struct_utimbuf_sz = sizeof(struct utimbuf);
unsigned struct_itimerspec_sz = sizeof(struct itimerspec);
unsigned struct_timeb_sz = sizeof(struct timeb);
unsigned struct_msqid_ds_sz = sizeof(struct msqid_ds);
unsigned struct_mq_attr_sz = sizeof(struct mq_attr);
unsigned struct_statvfs_sz = sizeof(struct statvfs);
unsigned struct_shminfo_sz = sizeof(struct shminfo);
unsigned struct_shm_info_sz = sizeof(struct shm_info);
unsigned struct_regmatch_sz = sizeof(regmatch_t);
unsigned struct_regex_sz = sizeof(regex_t);
unsigned struct_fstab_sz = sizeof(struct fstab);
unsigned struct_FTS_sz = sizeof(FTS);
unsigned struct_FTSENT_sz = sizeof(FTSENT);
unsigned struct_StringList_sz = sizeof(StringList);

const uptr sig_ign = (uptr)SIG_IGN;
const uptr sig_dfl = (uptr)SIG_DFL;
const uptr sig_err = (uptr)SIG_ERR;
const uptr sa_siginfo = (uptr)SA_SIGINFO;

int shmctl_ipc_stat = (int)IPC_STAT;
int shmctl_ipc_info = (int)IPC_INFO;
int shmctl_shm_info = (int)SHM_INFO;
int shmctl_shm_stat = (int)SHM_STAT;
unsigned struct_utmpx_sz = sizeof(struct utmpx);

int map_fixed = MAP_FIXED;

int af_inet = (int)AF_INET;
int af_inet6 = (int)AF_INET6;

uptr __sanitizer_in_addr_sz(int af) {
  if (af == AF_INET)
    return sizeof(struct in_addr);
  else if (af == AF_INET6)
    return sizeof(struct in6_addr);
  else
    return 0;
}

// For FreeBSD the actual size of a directory entry is not always in d_reclen.
// Use the appropriate macro to get the correct size for all cases (e.g. NFS).
u16 __sanitizer_dirsiz(const __sanitizer_dirent *dp) {
  return _GENERIC_DIRSIZ(dp);
}

unsigned struct_ElfW_Phdr_sz = sizeof(Elf_Phdr);
int glob_nomatch = GLOB_NOMATCH;
int glob_altdirfunc = GLOB_ALTDIRFUNC;
const int wordexp_wrde_dooffs = WRDE_DOOFFS;

unsigned path_max = PATH_MAX;

int struct_ttyent_sz = sizeof(struct ttyent);

// ioctl arguments
unsigned struct_ifreq_sz = sizeof(struct ifreq);
unsigned struct_termios_sz = sizeof(struct termios);
unsigned struct_winsize_sz = sizeof(struct winsize);
#if SOUND_VERSION >= 0x040000
unsigned struct_copr_buffer_sz = 0;
unsigned struct_copr_debug_buf_sz = 0;
unsigned struct_copr_msg_sz = 0;
#else
unsigned struct_copr_buffer_sz = sizeof(struct copr_buffer);
unsigned struct_copr_debug_buf_sz = sizeof(struct copr_debug_buf);
unsigned struct_copr_msg_sz = sizeof(struct copr_msg);
#endif
unsigned struct_midi_info_sz = sizeof(struct midi_info);
unsigned struct_mtget_sz = sizeof(struct mtget);
unsigned struct_mtop_sz = sizeof(struct mtop);
unsigned struct_sbi_instrument_sz = sizeof(struct sbi_instrument);
unsigned struct_seq_event_rec_sz = sizeof(struct seq_event_rec);
unsigned struct_synth_info_sz = sizeof(struct synth_info);
unsigned struct_audio_buf_info_sz = sizeof(struct audio_buf_info);
unsigned struct_ppp_stats_sz = sizeof(struct ppp_stats);
unsigned struct_sioc_sg_req_sz = sizeof(struct sioc_sg_req);
unsigned struct_sioc_vif_req_sz = sizeof(struct sioc_vif_req);
unsigned struct_procctl_reaper_status_sz = sizeof(struct __sanitizer_procctl_reaper_status);
unsigned struct_procctl_reaper_pidinfo_sz = sizeof(struct __sanitizer_procctl_reaper_pidinfo);
unsigned struct_procctl_reaper_pids_sz = sizeof(struct __sanitizer_procctl_reaper_pids);
unsigned struct_procctl_reaper_kill_sz = sizeof(struct __sanitizer_procctl_reaper_kill);
const unsigned long __sanitizer_bufsiz = BUFSIZ;

const unsigned IOCTL_NOT_PRESENT = 0;

unsigned IOCTL_FIOASYNC = FIOASYNC;
unsigned IOCTL_FIOCLEX = FIOCLEX;
unsigned IOCTL_FIOGETOWN = FIOGETOWN;
unsigned IOCTL_FIONBIO = FIONBIO;
unsigned IOCTL_FIONCLEX = FIONCLEX;
unsigned IOCTL_FIOSETOWN = FIOSETOWN;
unsigned IOCTL_SIOCADDMULTI = SIOCADDMULTI;
unsigned IOCTL_SIOCATMARK = SIOCATMARK;
unsigned IOCTL_SIOCDELMULTI = SIOCDELMULTI;
unsigned IOCTL_SIOCGIFADDR = SIOCGIFADDR;
unsigned IOCTL_SIOCGIFBRDADDR = SIOCGIFBRDADDR;
unsigned IOCTL_SIOCGIFCONF = SIOCGIFCONF;
unsigned IOCTL_SIOCGIFDSTADDR = SIOCGIFDSTADDR;
unsigned IOCTL_SIOCGIFFLAGS = SIOCGIFFLAGS;
unsigned IOCTL_SIOCGIFMETRIC = SIOCGIFMETRIC;
unsigned IOCTL_SIOCGIFMTU = SIOCGIFMTU;
unsigned IOCTL_SIOCGIFNETMASK = SIOCGIFNETMASK;
unsigned IOCTL_SIOCGPGRP = SIOCGPGRP;
unsigned IOCTL_SIOCSIFADDR = SIOCSIFADDR;
unsigned IOCTL_SIOCSIFBRDADDR = SIOCSIFBRDADDR;
unsigned IOCTL_SIOCSIFDSTADDR = SIOCSIFDSTADDR;
unsigned IOCTL_SIOCSIFFLAGS = SIOCSIFFLAGS;
unsigned IOCTL_SIOCSIFMETRIC = SIOCSIFMETRIC;
unsigned IOCTL_SIOCSIFMTU = SIOCSIFMTU;
unsigned IOCTL_SIOCSIFNETMASK = SIOCSIFNETMASK;
unsigned IOCTL_SIOCSPGRP = SIOCSPGRP;
unsigned IOCTL_TIOCCONS = TIOCCONS;
unsigned IOCTL_TIOCEXCL = TIOCEXCL;
unsigned IOCTL_TIOCGETD = TIOCGETD;
unsigned IOCTL_TIOCGPGRP = TIOCGPGRP;
unsigned IOCTL_TIOCGWINSZ = TIOCGWINSZ;
unsigned IOCTL_TIOCMBIC = TIOCMBIC;
unsigned IOCTL_TIOCMBIS = TIOCMBIS;
unsigned IOCTL_TIOCMGET = TIOCMGET;
unsigned IOCTL_TIOCMSET = TIOCMSET;
unsigned IOCTL_TIOCNOTTY = TIOCNOTTY;
unsigned IOCTL_TIOCNXCL = TIOCNXCL;
unsigned IOCTL_TIOCOUTQ = TIOCOUTQ;
unsigned IOCTL_TIOCPKT = TIOCPKT;
unsigned IOCTL_TIOCSCTTY = TIOCSCTTY;
unsigned IOCTL_TIOCSETD = TIOCSETD;
unsigned IOCTL_TIOCSPGRP = TIOCSPGRP;
unsigned IOCTL_TIOCSTI = TIOCSTI;
unsigned IOCTL_TIOCSWINSZ = TIOCSWINSZ;
unsigned IOCTL_SIOCGETSGCNT = SIOCGETSGCNT;
unsigned IOCTL_SIOCGETVIFCNT = SIOCGETVIFCNT;
unsigned IOCTL_MTIOCGET = MTIOCGET;
unsigned IOCTL_MTIOCTOP = MTIOCTOP;
unsigned IOCTL_SNDCTL_DSP_GETBLKSIZE = SNDCTL_DSP_GETBLKSIZE;
unsigned IOCTL_SNDCTL_DSP_GETFMTS = SNDCTL_DSP_GETFMTS;
unsigned IOCTL_SNDCTL_DSP_NONBLOCK = SNDCTL_DSP_NONBLOCK;
unsigned IOCTL_SNDCTL_DSP_POST = SNDCTL_DSP_POST;
unsigned IOCTL_SNDCTL_DSP_RESET = SNDCTL_DSP_RESET;
unsigned IOCTL_SNDCTL_DSP_SETFMT = SNDCTL_DSP_SETFMT;
unsigned IOCTL_SNDCTL_DSP_SETFRAGMENT = SNDCTL_DSP_SETFRAGMENT;
unsigned IOCTL_SNDCTL_DSP_SPEED = SNDCTL_DSP_SPEED;
unsigned IOCTL_SNDCTL_DSP_STEREO = SNDCTL_DSP_STEREO;
unsigned IOCTL_SNDCTL_DSP_SUBDIVIDE = SNDCTL_DSP_SUBDIVIDE;
unsigned IOCTL_SNDCTL_DSP_SYNC = SNDCTL_DSP_SYNC;
unsigned IOCTL_SNDCTL_FM_4OP_ENABLE = SNDCTL_FM_4OP_ENABLE;
unsigned IOCTL_SNDCTL_FM_LOAD_INSTR = SNDCTL_FM_LOAD_INSTR;
unsigned IOCTL_SNDCTL_MIDI_INFO = SNDCTL_MIDI_INFO;
unsigned IOCTL_SNDCTL_MIDI_PRETIME = SNDCTL_MIDI_PRETIME;
unsigned IOCTL_SNDCTL_SEQ_CTRLRATE = SNDCTL_SEQ_CTRLRATE;
unsigned IOCTL_SNDCTL_SEQ_GETINCOUNT = SNDCTL_SEQ_GETINCOUNT;
unsigned IOCTL_SNDCTL_SEQ_GETOUTCOUNT = SNDCTL_SEQ_GETOUTCOUNT;
unsigned IOCTL_SNDCTL_SEQ_NRMIDIS = SNDCTL_SEQ_NRMIDIS;
unsigned IOCTL_SNDCTL_SEQ_NRSYNTHS = SNDCTL_SEQ_NRSYNTHS;
unsigned IOCTL_SNDCTL_SEQ_OUTOFBAND = SNDCTL_SEQ_OUTOFBAND;
unsigned IOCTL_SNDCTL_SEQ_PANIC = SNDCTL_SEQ_PANIC;
unsigned IOCTL_SNDCTL_SEQ_PERCMODE = SNDCTL_SEQ_PERCMODE;
unsigned IOCTL_SNDCTL_SEQ_RESET = SNDCTL_SEQ_RESET;
unsigned IOCTL_SNDCTL_SEQ_RESETSAMPLES = SNDCTL_SEQ_RESETSAMPLES;
unsigned IOCTL_SNDCTL_SEQ_SYNC = SNDCTL_SEQ_SYNC;
unsigned IOCTL_SNDCTL_SEQ_TESTMIDI = SNDCTL_SEQ_TESTMIDI;
unsigned IOCTL_SNDCTL_SEQ_THRESHOLD = SNDCTL_SEQ_THRESHOLD;
unsigned IOCTL_SNDCTL_SYNTH_INFO = SNDCTL_SYNTH_INFO;
unsigned IOCTL_SNDCTL_SYNTH_MEMAVL = SNDCTL_SYNTH_MEMAVL;
unsigned IOCTL_SNDCTL_TMR_CONTINUE = SNDCTL_TMR_CONTINUE;
unsigned IOCTL_SNDCTL_TMR_METRONOME = SNDCTL_TMR_METRONOME;
unsigned IOCTL_SNDCTL_TMR_SELECT = SNDCTL_TMR_SELECT;
unsigned IOCTL_SNDCTL_TMR_SOURCE = SNDCTL_TMR_SOURCE;
unsigned IOCTL_SNDCTL_TMR_START = SNDCTL_TMR_START;
unsigned IOCTL_SNDCTL_TMR_STOP = SNDCTL_TMR_STOP;
unsigned IOCTL_SNDCTL_TMR_TEMPO = SNDCTL_TMR_TEMPO;
unsigned IOCTL_SNDCTL_TMR_TIMEBASE = SNDCTL_TMR_TIMEBASE;
unsigned IOCTL_SOUND_MIXER_READ_ALTPCM = SOUND_MIXER_READ_ALTPCM;
unsigned IOCTL_SOUND_MIXER_READ_BASS = SOUND_MIXER_READ_BASS;
unsigned IOCTL_SOUND_MIXER_READ_CAPS = SOUND_MIXER_READ_CAPS;
unsigned IOCTL_SOUND_MIXER_READ_CD = SOUND_MIXER_READ_CD;
unsigned IOCTL_SOUND_MIXER_READ_DEVMASK = SOUND_MIXER_READ_DEVMASK;
unsigned IOCTL_SOUND_MIXER_READ_ENHANCE = SOUND_MIXER_READ_ENHANCE;
unsigned IOCTL_SOUND_MIXER_READ_IGAIN = SOUND_MIXER_READ_IGAIN;
unsigned IOCTL_SOUND_MIXER_READ_IMIX = SOUND_MIXER_READ_IMIX;
unsigned IOCTL_SOUND_MIXER_READ_LINE = SOUND_MIXER_READ_LINE;
unsigned IOCTL_SOUND_MIXER_READ_LINE1 = SOUND_MIXER_READ_LINE1;
unsigned IOCTL_SOUND_MIXER_READ_LINE2 = SOUND_MIXER_READ_LINE2;
unsigned IOCTL_SOUND_MIXER_READ_LINE3 = SOUND_MIXER_READ_LINE3;
unsigned IOCTL_SOUND_MIXER_READ_LOUD = SOUND_MIXER_READ_LOUD;
unsigned IOCTL_SOUND_MIXER_READ_MIC = SOUND_MIXER_READ_MIC;
unsigned IOCTL_SOUND_MIXER_READ_MUTE = SOUND_MIXER_READ_MUTE;
unsigned IOCTL_SOUND_MIXER_READ_OGAIN = SOUND_MIXER_READ_OGAIN;
unsigned IOCTL_SOUND_MIXER_READ_PCM = SOUND_MIXER_READ_PCM;
unsigned IOCTL_SOUND_MIXER_READ_RECLEV = SOUND_MIXER_READ_RECLEV;
unsigned IOCTL_SOUND_MIXER_READ_RECMASK = SOUND_MIXER_READ_RECMASK;
unsigned IOCTL_SOUND_MIXER_READ_RECSRC = SOUND_MIXER_READ_RECSRC;
unsigned IOCTL_SOUND_MIXER_READ_SPEAKER = SOUND_MIXER_READ_SPEAKER;
unsigned IOCTL_SOUND_MIXER_READ_STEREODEVS = SOUND_MIXER_READ_STEREODEVS;
unsigned IOCTL_SOUND_MIXER_READ_SYNTH = SOUND_MIXER_READ_SYNTH;
unsigned IOCTL_SOUND_MIXER_READ_TREBLE = SOUND_MIXER_READ_TREBLE;
unsigned IOCTL_SOUND_MIXER_READ_VOLUME = SOUND_MIXER_READ_VOLUME;
unsigned IOCTL_SOUND_MIXER_WRITE_ALTPCM = SOUND_MIXER_WRITE_ALTPCM;
unsigned IOCTL_SOUND_MIXER_WRITE_BASS = SOUND_MIXER_WRITE_BASS;
unsigned IOCTL_SOUND_MIXER_WRITE_CD = SOUND_MIXER_WRITE_CD;
unsigned IOCTL_SOUND_MIXER_WRITE_ENHANCE = SOUND_MIXER_WRITE_ENHANCE;
unsigned IOCTL_SOUND_MIXER_WRITE_IGAIN = SOUND_MIXER_WRITE_IGAIN;
unsigned IOCTL_SOUND_MIXER_WRITE_IMIX = SOUND_MIXER_WRITE_IMIX;
unsigned IOCTL_SOUND_MIXER_WRITE_LINE = SOUND_MIXER_WRITE_LINE;
unsigned IOCTL_SOUND_MIXER_WRITE_LINE1 = SOUND_MIXER_WRITE_LINE1;
unsigned IOCTL_SOUND_MIXER_WRITE_LINE2 = SOUND_MIXER_WRITE_LINE2;
unsigned IOCTL_SOUND_MIXER_WRITE_LINE3 = SOUND_MIXER_WRITE_LINE3;
unsigned IOCTL_SOUND_MIXER_WRITE_LOUD = SOUND_MIXER_WRITE_LOUD;
unsigned IOCTL_SOUND_MIXER_WRITE_MIC = SOUND_MIXER_WRITE_MIC;
unsigned IOCTL_SOUND_MIXER_WRITE_MUTE = SOUND_MIXER_WRITE_MUTE;
unsigned IOCTL_SOUND_MIXER_WRITE_OGAIN = SOUND_MIXER_WRITE_OGAIN;
unsigned IOCTL_SOUND_MIXER_WRITE_PCM = SOUND_MIXER_WRITE_PCM;
unsigned IOCTL_SOUND_MIXER_WRITE_RECLEV = SOUND_MIXER_WRITE_RECLEV;
unsigned IOCTL_SOUND_MIXER_WRITE_RECSRC = SOUND_MIXER_WRITE_RECSRC;
unsigned IOCTL_SOUND_MIXER_WRITE_SPEAKER = SOUND_MIXER_WRITE_SPEAKER;
unsigned IOCTL_SOUND_MIXER_WRITE_SYNTH = SOUND_MIXER_WRITE_SYNTH;
unsigned IOCTL_SOUND_MIXER_WRITE_TREBLE = SOUND_MIXER_WRITE_TREBLE;
unsigned IOCTL_SOUND_MIXER_WRITE_VOLUME = SOUND_MIXER_WRITE_VOLUME;
unsigned IOCTL_VT_ACTIVATE = VT_ACTIVATE;
unsigned IOCTL_VT_GETMODE = VT_GETMODE;
unsigned IOCTL_VT_OPENQRY = VT_OPENQRY;
unsigned IOCTL_VT_RELDISP = VT_RELDISP;
unsigned IOCTL_VT_SETMODE = VT_SETMODE;
unsigned IOCTL_VT_WAITACTIVE = VT_WAITACTIVE;
unsigned IOCTL_GIO_SCRNMAP = GIO_SCRNMAP;
unsigned IOCTL_KDDISABIO = KDDISABIO;
unsigned IOCTL_KDENABIO = KDENABIO;
unsigned IOCTL_KDGETLED = KDGETLED;
unsigned IOCTL_KDGETMODE = KDGETMODE;
unsigned IOCTL_KDGKBMODE = KDGKBMODE;
unsigned IOCTL_KDGKBTYPE = KDGKBTYPE;
unsigned IOCTL_KDMKTONE = KDMKTONE;
unsigned IOCTL_KDSETLED = KDSETLED;
unsigned IOCTL_KDSETMODE = KDSETMODE;
unsigned IOCTL_KDSKBMODE = KDSKBMODE;
unsigned IOCTL_KIOCSOUND = KIOCSOUND;
unsigned IOCTL_PIO_SCRNMAP = PIO_SCRNMAP;
unsigned IOCTL_SNDCTL_DSP_GETISPACE = SNDCTL_DSP_GETISPACE;

const int si_SEGV_MAPERR = SEGV_MAPERR;
const int si_SEGV_ACCERR = SEGV_ACCERR;
const int unvis_valid = UNVIS_VALID;
const int unvis_validpush = UNVIS_VALIDPUSH;

const unsigned MD5_CTX_sz = sizeof(MD5_CTX);
const unsigned MD5_return_length = MD5_DIGEST_STRING_LENGTH;

#define SHA2_CONST(LEN)                                                      \
  const unsigned SHA##LEN##_CTX_sz = sizeof(SHA##LEN##_CTX);                 \
  const unsigned SHA##LEN##_return_length = SHA##LEN##_DIGEST_STRING_LENGTH; \
  const unsigned SHA##LEN##_block_length = SHA##LEN##_BLOCK_LENGTH;          \
  const unsigned SHA##LEN##_digest_length = SHA##LEN##_DIGEST_LENGTH

SHA2_CONST(224);
SHA2_CONST(256);
SHA2_CONST(384);
SHA2_CONST(512);

#undef SHA2_CONST
}  // namespace __sanitizer

using namespace __sanitizer;

COMPILER_CHECK(sizeof(__sanitizer_pthread_attr_t) >= sizeof(pthread_attr_t));

COMPILER_CHECK(sizeof(socklen_t) == sizeof(unsigned));
CHECK_TYPE_SIZE(pthread_key_t);

// There are more undocumented fields in dl_phdr_info that we are not interested
// in.
COMPILER_CHECK(sizeof(__sanitizer_dl_phdr_info) <= sizeof(dl_phdr_info));
CHECK_SIZE_AND_OFFSET(dl_phdr_info, dlpi_addr);
CHECK_SIZE_AND_OFFSET(dl_phdr_info, dlpi_name);
CHECK_SIZE_AND_OFFSET(dl_phdr_info, dlpi_phdr);
CHECK_SIZE_AND_OFFSET(dl_phdr_info, dlpi_phnum);

CHECK_TYPE_SIZE(glob_t);
CHECK_SIZE_AND_OFFSET(glob_t, gl_pathc);
CHECK_SIZE_AND_OFFSET(glob_t, gl_pathv);
CHECK_SIZE_AND_OFFSET(glob_t, gl_offs);
CHECK_SIZE_AND_OFFSET(glob_t, gl_flags);
CHECK_SIZE_AND_OFFSET(glob_t, gl_closedir);
CHECK_SIZE_AND_OFFSET(glob_t, gl_readdir);
CHECK_SIZE_AND_OFFSET(glob_t, gl_opendir);
CHECK_SIZE_AND_OFFSET(glob_t, gl_lstat);
CHECK_SIZE_AND_OFFSET(glob_t, gl_stat);

CHECK_TYPE_SIZE(addrinfo);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_flags);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_family);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_socktype);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_protocol);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_protocol);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_addrlen);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_canonname);
CHECK_SIZE_AND_OFFSET(addrinfo, ai_addr);

CHECK_TYPE_SIZE(hostent);
CHECK_SIZE_AND_OFFSET(hostent, h_name);
CHECK_SIZE_AND_OFFSET(hostent, h_aliases);
CHECK_SIZE_AND_OFFSET(hostent, h_addrtype);
CHECK_SIZE_AND_OFFSET(hostent, h_length);
CHECK_SIZE_AND_OFFSET(hostent, h_addr_list);

CHECK_TYPE_SIZE(iovec);
CHECK_SIZE_AND_OFFSET(iovec, iov_base);
CHECK_SIZE_AND_OFFSET(iovec, iov_len);

CHECK_TYPE_SIZE(msghdr);
CHECK_SIZE_AND_OFFSET(msghdr, msg_name);
CHECK_SIZE_AND_OFFSET(msghdr, msg_namelen);
CHECK_SIZE_AND_OFFSET(msghdr, msg_iov);
CHECK_SIZE_AND_OFFSET(msghdr, msg_iovlen);
CHECK_SIZE_AND_OFFSET(msghdr, msg_control);
CHECK_SIZE_AND_OFFSET(msghdr, msg_controllen);
CHECK_SIZE_AND_OFFSET(msghdr, msg_flags);

CHECK_TYPE_SIZE(cmsghdr);
CHECK_SIZE_AND_OFFSET(cmsghdr, cmsg_len);
CHECK_SIZE_AND_OFFSET(cmsghdr, cmsg_level);
CHECK_SIZE_AND_OFFSET(cmsghdr, cmsg_type);

COMPILER_CHECK(sizeof(__sanitizer_dirent) <= sizeof(dirent));
CHECK_SIZE_AND_OFFSET(dirent, d_ino);
CHECK_SIZE_AND_OFFSET(dirent, d_reclen);

CHECK_TYPE_SIZE(ifconf);
CHECK_SIZE_AND_OFFSET(ifconf, ifc_len);
CHECK_SIZE_AND_OFFSET(ifconf, ifc_ifcu);

CHECK_TYPE_SIZE(pollfd);
CHECK_SIZE_AND_OFFSET(pollfd, fd);
CHECK_SIZE_AND_OFFSET(pollfd, events);
CHECK_SIZE_AND_OFFSET(pollfd, revents);

CHECK_TYPE_SIZE(nfds_t);

CHECK_TYPE_SIZE(sigset_t);

COMPILER_CHECK(sizeof(__sanitizer_sigaction) == sizeof(struct sigaction));
COMPILER_CHECK(sizeof(__sanitizer_siginfo) == sizeof(siginfo_t));
CHECK_SIZE_AND_OFFSET(siginfo_t, si_value);
// Can't write checks for sa_handler and sa_sigaction due to them being
// preprocessor macros.
CHECK_STRUCT_SIZE_AND_OFFSET(sigaction, sa_mask);

CHECK_TYPE_SIZE(wordexp_t);
CHECK_SIZE_AND_OFFSET(wordexp_t, we_wordc);
CHECK_SIZE_AND_OFFSET(wordexp_t, we_wordv);
CHECK_SIZE_AND_OFFSET(wordexp_t, we_offs);

CHECK_TYPE_SIZE(tm);
CHECK_SIZE_AND_OFFSET(tm, tm_sec);
CHECK_SIZE_AND_OFFSET(tm, tm_min);
CHECK_SIZE_AND_OFFSET(tm, tm_hour);
CHECK_SIZE_AND_OFFSET(tm, tm_mday);
CHECK_SIZE_AND_OFFSET(tm, tm_mon);
CHECK_SIZE_AND_OFFSET(tm, tm_year);
CHECK_SIZE_AND_OFFSET(tm, tm_wday);
CHECK_SIZE_AND_OFFSET(tm, tm_yday);
CHECK_SIZE_AND_OFFSET(tm, tm_isdst);
CHECK_SIZE_AND_OFFSET(tm, tm_gmtoff);
CHECK_SIZE_AND_OFFSET(tm, tm_zone);

CHECK_TYPE_SIZE(ether_addr);

CHECK_TYPE_SIZE(ipc_perm);
CHECK_SIZE_AND_OFFSET(ipc_perm, key);
CHECK_SIZE_AND_OFFSET(ipc_perm, seq);
CHECK_SIZE_AND_OFFSET(ipc_perm, uid);
CHECK_SIZE_AND_OFFSET(ipc_perm, gid);
CHECK_SIZE_AND_OFFSET(ipc_perm, cuid);
CHECK_SIZE_AND_OFFSET(ipc_perm, cgid);

CHECK_TYPE_SIZE(shmid_ds);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_perm);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_segsz);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_atime);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_dtime);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_ctime);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_cpid);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_lpid);
CHECK_SIZE_AND_OFFSET(shmid_ds, shm_nattch);

CHECK_TYPE_SIZE(clock_t);

CHECK_TYPE_SIZE(ifaddrs);
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_next);
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_name);
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_addr);
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_netmask);
#undef ifa_dstaddr
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_dstaddr);
CHECK_SIZE_AND_OFFSET(ifaddrs, ifa_data);

CHECK_TYPE_SIZE(timeb);
CHECK_SIZE_AND_OFFSET(timeb, time);
CHECK_SIZE_AND_OFFSET(timeb, millitm);
CHECK_SIZE_AND_OFFSET(timeb, timezone);
CHECK_SIZE_AND_OFFSET(timeb, dstflag);

CHECK_TYPE_SIZE(passwd);
CHECK_SIZE_AND_OFFSET(passwd, pw_name);
CHECK_SIZE_AND_OFFSET(passwd, pw_passwd);
CHECK_SIZE_AND_OFFSET(passwd, pw_uid);
CHECK_SIZE_AND_OFFSET(passwd, pw_gid);
CHECK_SIZE_AND_OFFSET(passwd, pw_dir);
CHECK_SIZE_AND_OFFSET(passwd, pw_shell);

CHECK_SIZE_AND_OFFSET(passwd, pw_gecos);

CHECK_TYPE_SIZE(group);
CHECK_SIZE_AND_OFFSET(group, gr_name);
CHECK_SIZE_AND_OFFSET(group, gr_passwd);
CHECK_SIZE_AND_OFFSET(group, gr_gid);
CHECK_SIZE_AND_OFFSET(group, gr_mem);

#if HAVE_RPC_XDR_H
CHECK_TYPE_SIZE(XDR);
CHECK_SIZE_AND_OFFSET(XDR, x_op);
CHECK_SIZE_AND_OFFSET(XDR, x_ops);
CHECK_SIZE_AND_OFFSET(XDR, x_public);
CHECK_SIZE_AND_OFFSET(XDR, x_private);
CHECK_SIZE_AND_OFFSET(XDR, x_base);
CHECK_SIZE_AND_OFFSET(XDR, x_handy);
COMPILER_CHECK(__sanitizer_XDR_ENCODE == XDR_ENCODE);
COMPILER_CHECK(__sanitizer_XDR_DECODE == XDR_DECODE);
COMPILER_CHECK(__sanitizer_XDR_FREE == XDR_FREE);
#endif

CHECK_TYPE_SIZE(sem_t);

COMPILER_CHECK(sizeof(__sanitizer_cap_rights_t) >= sizeof(cap_rights_t));
COMPILER_CHECK(sizeof(__sanitizer_cpuset_t) >= sizeof(cpuset_t));
#endif  // SANITIZER_FREEBSD
