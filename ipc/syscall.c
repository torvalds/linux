// SPDX-License-Identifier: GPL-2.0
/*
 * sys_ipc() is the old de-multiplexer for the SysV IPC calls.
 *
 * This function handles System V IPC calls and routes them to appropriate kernel functions.
 * It supports backward compatibility and compatibility for different architectures.
 * New architectures should consider wiring up individual syscalls instead of using this.
 */

#include <linux/unistd.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/ipc_namespace.h>
#include "util.h"

#ifdef __ARCH_WANT_SYS_IPC
#include <linux/errno.h>
#include <linux/ipc.h>
#include <linux/shm.h>
#include <linux/uaccess.h>

struct ipc_kludge {
    void __user *msgp;
    long msgtyp;
};

/**
 * ksys_ipc - System V IPC syscall handler
 * @call: IPC call identifier
 * @first: First syscall argument
 * @second: Second syscall argument
 * @third: Third syscall argument
 * @ptr: Pointer to user data
 * @fifth: Fifth syscall argument
 *
 * This function handles System V IPC calls and routes them to appropriate kernel functions.
 */
int ksys_ipc(unsigned int call, int first, unsigned long second,
             unsigned long third, void __user *ptr, long fifth)
{
    int ret = -ENOSYS; // Default to ENOSYS if syscall not implemented

    switch (call) {
    case SEMOP:
        ret = ksys_semtimedop(first, (struct sembuf __user *)ptr, second, NULL);
        break;
    case SEMTIMEDOP:
        if (IS_ENABLED(CONFIG_64BIT))
            ret = ksys_semtimedop(first, ptr, second, (const struct __kernel_timespec __user *)fifth);
        else if (IS_ENABLED(CONFIG_COMPAT_32BIT_TIME))
            ret = compat_ksys_semtimedop(first, ptr, second, (const struct old_timespec32 __user *)fifth);
        break;
    case SEMGET:
        ret = ksys_semget(first, second, third);
        break;
    case SEMCTL:
        if (!ptr)
            ret = -EINVAL;
        else {
            unsigned long arg;
            if (get_user(arg, (unsigned long __user *)ptr))
                ret = -EFAULT;
            else
                ret = ksys_old_semctl(first, second, third, arg);
        }
        break;
    case MSGSND:
        ret = ksys_msgsnd(first, (struct msgbuf __user *)ptr, second, third);
        break;
    case MSGRCV: {
        struct ipc_kludge tmp;
        if (!ptr)
            return -EINVAL;
        if (copy_from_user(&tmp, (struct ipc_kludge __user *)ptr, sizeof(tmp)))
            return -EFAULT;
        return ksys_msgrcv(first, tmp.msgp, second, tmp.msgtyp, third);
    }
    case MSGGET:
        ret = ksys_msgget((key_t) first, second);
        break;
    case MSGCTL:
        ret = ksys_old_msgctl(first, second, (struct msqid_ds __user *)ptr);
        break;
    case SHMAT:
        ret = do_shmat_handler(call, first, second, third, ptr);
        break;
    case SHMDT:
        ret = ksys_shmdt((char __user *)ptr);
        break;
    case SHMGET:
        ret = ksys_shmget(first, (unsigned int)second, third);
        break;
    case SHMCTL:
        ret = ksys_old_shmctl(first, second, (struct shmid_ds __user *)ptr);
        break;
    default:
        ret = -ENOSYS;
        break;
    }

    return ret;
}

SYSCALL_DEFINE6(ipc, unsigned int, call, int, first, unsigned long, second,
                unsigned long, third, void __user *, ptr, long, fifth)
{
    return ksys_ipc(call, first, second, third, ptr, fifth);
}
#endif

#ifdef CONFIG_COMPAT
#include <linux/compat.h>

#ifndef COMPAT_SHMLBA
#define COMPAT_SHMLBA    SHMLBA
#endif

struct compat_ipc_kludge {
    compat_uptr_t msgp;
    compat_long_t msgtyp;
};

#ifdef CONFIG_ARCH_WANT_OLD_COMPAT_IPC
int compat_ksys_ipc(u32 call, int first, int second,
                    u32 third, compat_uptr_t ptr, u32 fifth)
{
    int ret = -ENOSYS;
    int version;
    u32 pad;

    version = call >> 16; /* hack for backward compatibility */
    call &= 0xffff;

    switch (call) {
    case SEMOP:
        ret = ksys_semtimedop(first, compat_ptr(ptr), second, NULL);
        break;
    case SEMTIMEDOP:
        if (!IS_ENABLED(CONFIG_COMPAT_32BIT_TIME))
            return -ENOSYS;
        ret = compat_ksys_semtimedop(first, compat_ptr(ptr), second,
                                     compat_ptr(fifth));
        break;
    case SEMGET:
        ret = ksys_semget(first, second, third);
        break;
    case SEMCTL:
        if (!ptr)
            ret = -EINVAL;
        else {
            if (get_user(pad, (u32 __user *) compat_ptr(ptr)))
                ret = -EFAULT;
            else
                ret = compat_ksys_old_semctl(first, second, third, pad);
        }
        break;
    case MSGSND:
        ret = compat_ksys_msgsnd(first, ptr, second, third);
        break;
    case MSGRCV: {
        void __user *uptr = compat_ptr(ptr);

        if (first < 0 || second < 0)
            return -EINVAL;

        if (!version) {
            struct compat_ipc_kludge ipck;
            if (!uptr)
                return -EINVAL;
            if (copy_from_user(&ipck, uptr, sizeof(ipck)))
                return -EFAULT;
            return compat_ksys_msgrcv(first, ipck.msgp, second,
                                      ipck.msgtyp, third);
        }
        return compat_ksys_msgrcv(first, ptr, second, fifth, third);
    }
    case MSGGET:
        ret = ksys_msgget(first, second);
        break;
    case MSGCTL:
        ret = compat_ksys_old_msgctl(first, second, compat_ptr(ptr));
        break;
    case SHMAT:
        if (version == 1)
            return -EINVAL;
        ret = do_shmat_handler(call, first, second, third, ptr);
        break;
    case SHMDT:
        ret = ksys_shmdt(compat_ptr(ptr));
        break;
    case SHMGET:
        ret = ksys_shmget(first, (unsigned int)second, third);
        break;
    case SHMCTL:
        ret = compat_ksys_old_shmctl(first, second, compat_ptr(ptr));
        break;
    default:
        ret = -ENOSYS;
        break;
    }

    return ret;
}

COMPAT_SYSCALL_DEFINE6(ipc, u32, call, int, first, int, second,
                       u32, third, compat_uptr_t, ptr, u32, fifth)
{
    return compat_ksys_ipc(call, first, second, third, ptr, fifth);
}
#endif
#endif
