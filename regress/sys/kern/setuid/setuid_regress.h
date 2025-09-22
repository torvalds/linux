/*	$OpenBSD: setuid_regress.h,v 1.1 2014/08/27 07:36:14 blambert Exp $	*/
/*
 *	Written by Bret Stephen Lambert <blambert@openbsd.org> 2014
 *	Public Domain.
 */

#ifndef	_SETUID_REGRESS_H_
#define	_SETUID_REGRESS_H_

#define	_SETUID_REGRESS_USER	"nobody"

static inline int
read_kproc_pid(struct kinfo_proc *kproc, pid_t pid)
{
	int			 args[6];
	size_t			 size;

	args[0] = CTL_KERN;
	args[1] = KERN_PROC;
	args[2] = KERN_PROC_PID;
	args[3] = pid;
	args[4] = sizeof(*kproc);
	args[5] = 1;

	size = sizeof(*kproc);
	return (sysctl(args, 6, kproc, &size, NULL, 0));
}

static inline void
checkuids(uid_t truid, uid_t teuid, uid_t tsuid, const char *str)
{
	uid_t			 ruid, euid, suid;

	if (getresuid(&ruid, &euid, &suid) == -1)
		err(1, "getresuid %s", str);

	if (ruid != truid)
		errx(1, "real uid incorrectly set %s: is %u should be %u",
		    str, ruid, truid);
	if (euid != teuid)
		errx(1, "effective uid incorrectly set %s: is %u should be %u",
		    str, euid, teuid);
	if (suid != tsuid)
		errx(1, "saved uid incorrectly set %s: is %u should be %u",
		    str, suid, tsuid);
}

void
checkgids(gid_t trgid, gid_t tegid, gid_t tsgid, const char *str)
{
	gid_t			rgid, egid, sgid;

	if (getresgid(&rgid, &egid, &sgid) == -1)
		err(1, "getresgid %s", str);

	if (rgid != trgid)
		errx(1, "real gid incorrectly set %s: is %u should be %u",
		    str, rgid, trgid);
	if (egid != tegid)
		errx(1, "effective gid incorrectly set %s: is %u should be %u",
		    str, egid, tegid);
	if (sgid != tsgid)
		errx(1, "saved gid incorrectly set %s: is %u should be %u",
		    str, sgid, tsgid);
}
#endif
