/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_UID16_H
#define LINUX_UID16_H

long __sys_setuid(uid_t uid);
long __sys_setgid(gid_t gid);
long __sys_setreuid(uid_t ruid, uid_t euid);
long __sys_setregid(gid_t rgid, gid_t egid);
long __sys_setresuid(uid_t ruid, uid_t euid, uid_t suid);
long __sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid);
long __sys_setfsuid(uid_t uid);
long __sys_setfsgid(gid_t gid);

#endif /* LINUX_UID16_H */
