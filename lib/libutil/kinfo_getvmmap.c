#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <stdlib.h>
#include <string.h>

#include "libutil.h"

struct kinfo_vmentry *
kinfo_getvmmap(pid_t pid, int *cntp)
{
	int mib[4];
	int error;
	int cnt;
	size_t len;
	char *buf, *bp, *eb;
	struct kinfo_vmentry *kiv, *kp, *kv;

	*cntp = 0;
	len = 0;
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_VMMAP;
	mib[3] = pid;

	error = sysctl(mib, nitems(mib), NULL, &len, NULL, 0);
	if (error)
		return (NULL);
	len = len * 4 / 3;
	buf = malloc(len);
	if (buf == NULL)
		return (NULL);
	error = sysctl(mib, nitems(mib), buf, &len, NULL, 0);
	if (error) {
		free(buf);
		return (NULL);
	}
	/* Pass 1: count items */
	cnt = 0;
	bp = buf;
	eb = buf + len;
	while (bp < eb) {
		kv = (struct kinfo_vmentry *)(uintptr_t)bp;
		if (kv->kve_structsize == 0)
			break;
		bp += kv->kve_structsize;
		cnt++;
	}

	kiv = calloc(cnt, sizeof(*kiv));
	if (kiv == NULL) {
		free(buf);
		return (NULL);
	}
	bp = buf;
	eb = buf + len;
	kp = kiv;
	/* Pass 2: unpack */
	while (bp < eb) {
		kv = (struct kinfo_vmentry *)(uintptr_t)bp;
		if (kv->kve_structsize == 0)
			break;
		/* Copy/expand into pre-zeroed buffer */
		memcpy(kp, kv, kv->kve_structsize);
		/* Advance to next packed record */
		bp += kv->kve_structsize;
		/* Set field size to fixed length, advance */
		kp->kve_structsize = sizeof(*kp);
		kp++;
	}
	free(buf);
	*cntp = cnt;
	return (kiv);	/* Caller must free() return value */
}
