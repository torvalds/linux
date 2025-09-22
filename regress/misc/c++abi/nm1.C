/* $OpenBSD: nm1.C,v 1.3 2022/08/06 13:31:13 semarie Exp $ */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define D(T)	void witness_##T(T) {}

D(cpuid_t)
D(dev_t)
D(fixpt_t)
D(fsblkcnt_t)
D(gid_t)
D(id_t)
D(in_addr_t)
D(in_port_t)
D(ino_t)
D(key_t)
D(mode_t)
D(nlink_t)
D(pid_t)
D(rlim_t)
D(sa_family_t)
D(segsz_t)
D(socklen_t)
D(suseconds_t)
D(uid_t)
D(uint64_t)
D(uint32_t)
D(size_t)
D(off_t)
D(useconds_t)
