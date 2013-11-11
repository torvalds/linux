#ifndef _UAPI_LINUX_SECUREBITS_H
#define _UAPI_LINUX_SECUREBITS_H

/* Each securesetting is implemented using two bits. One bit specifies
   whether the setting is on or off. The other bit specify whether the
   setting is locked or not. A setting which is locked cannot be
   changed from user-level. */
#define issecure_mask(X)	(1 << (X))

#define SECUREBITS_DEFAULT 0x00000000

/* When set UID 0 has no special privileges. When unset, we support
   inheritance of root-permissions and suid-root executable under
   compatibility mode. We raise the effective and inheritable bitmasks
   *of the executable file* if the effective uid of the new process is
   0. If the real uid is 0, we raise the effective (legacy) bit of the
   executable file. */
#define SECURE_NOROOT			0
#define SECURE_NOROOT_LOCKED		1  /* make bit-0 immutable */

#define SECBIT_NOROOT		(issecure_mask(SECURE_NOROOT))
#define SECBIT_NOROOT_LOCKED	(issecure_mask(SECURE_NOROOT_LOCKED))

/* When set, setuid to/from uid 0 does not trigger capability-"fixup".
   When unset, to provide compatiblility with old programs relying on
   set*uid to gain/lose privilege, transitions to/from uid 0 cause
   capabilities to be gained/lost. */
#define SECURE_NO_SETUID_FIXUP		2
#define SECURE_NO_SETUID_FIXUP_LOCKED	3  /* make bit-2 immutable */

#define SECBIT_NO_SETUID_FIXUP	(issecure_mask(SECURE_NO_SETUID_FIXUP))
#define SECBIT_NO_SETUID_FIXUP_LOCKED \
			(issecure_mask(SECURE_NO_SETUID_FIXUP_LOCKED))

/* When set, a process can retain its capabilities even after
   transitioning to a non-root user (the set-uid fixup suppressed by
   bit 2). Bit-4 is cleared when a process calls exec(); setting both
   bit 4 and 5 will create a barrier through exec that no exec()'d
   child can use this feature again. */
#define SECURE_KEEP_CAPS		4
#define SECURE_KEEP_CAPS_LOCKED		5  /* make bit-4 immutable */

#define SECBIT_KEEP_CAPS	(issecure_mask(SECURE_KEEP_CAPS))
#define SECBIT_KEEP_CAPS_LOCKED (issecure_mask(SECURE_KEEP_CAPS_LOCKED))

#define SECURE_ALL_BITS		(issecure_mask(SECURE_NOROOT) | \
				 issecure_mask(SECURE_NO_SETUID_FIXUP) | \
				 issecure_mask(SECURE_KEEP_CAPS))
#define SECURE_ALL_LOCKS	(SECURE_ALL_BITS << 1)

#endif /* _UAPI_LINUX_SECUREBITS_H */
