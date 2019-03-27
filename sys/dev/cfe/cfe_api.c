/* from: Broadcom Id: cfe_api.c,v 1.18 2006/08/24 02:13:56 binh Exp $ */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2000, 2001, 2002
 * Broadcom Corporation. All rights reserved.
 *
 * This software is furnished under license and may be used and copied only
 * in accordance with the following terms and conditions.  Subject to these
 * conditions, you may download, copy, install, use, modify and distribute
 * modified or unmodified copies of this software in source and/or binary
 * form. No title or ownership is transferred hereby.
 *
 * 1) Any source code used, modified or distributed must reproduce and
 *    retain this copyright notice and list of conditions as they appear in
 *    the source file.
 *
 * 2) No right is granted to use any trade name, trademark, or logo of
 *    Broadcom Corporation.  The "Broadcom Corporation" name may not be
 *    used to endorse or promote products derived from this software
 *    without the prior written permission of Broadcom Corporation.
 *
 * 3) THIS SOFTWARE IS PROVIDED "AS-IS" AND ANY EXPRESS OR IMPLIED
 *    WARRANTIES, INCLUDING BUT NOT LIMITED TO, ANY IMPLIED WARRANTIES OF
 *    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR
 *    NON-INFRINGEMENT ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM BE LIABLE
 *    FOR ANY DAMAGES WHATSOEVER, AND IN PARTICULAR, BROADCOM SHALL NOT BE
 *    LIABLE FOR DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *    BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 *    OR OTHERWISE), EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*  *********************************************************************
    *
    *  Broadcom Common Firmware Environment (CFE)
    *
    *  Device Function stubs			File: cfe_api.c
    *
    *  This module contains device function stubs (small routines to
    *  call the standard "iocb" interface entry point to CFE).
    *  There should be one routine here per iocb function call.
    *
    *  Authors:  Mitch Lichtenberg, Chris Demetriou
    *
    ********************************************************************* */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/cfe/cfe_api.h>
#include <dev/cfe/cfe_api_int.h>

/* Cast from a native pointer to a cfe_xptr_t and back.  */
#define XPTR_FROM_NATIVE(n)	((cfe_xptr_t) (intptr_t) (n))
#define NATIVE_FROM_XPTR(x)	((void *) (intptr_t) (x))

#ifdef CFE_API_IMPL_NAMESPACE
#define cfe_iocb_dispatch(a)		__cfe_iocb_dispatch(a)
#endif
int cfe_iocb_dispatch(cfe_xiocb_t *xiocb);

#if defined(CFE_API_common) || defined(CFE_API_ALL)
/*
 * Declare the dispatch function with args of "intptr_t".
 * This makes sure whatever model we're compiling in
 * puts the pointers in a single register.  For example,
 * combining -mlong64 and -mips1 or -mips2 would lead to
 * trouble, since the handle and IOCB pointer will be
 * passed in two registers each, and CFE expects one.
 */

static int (*cfe_dispfunc)(intptr_t handle, intptr_t xiocb) = 0;
static cfe_xuint_t cfe_handle = 0;

int
cfe_init(cfe_xuint_t handle, cfe_xuint_t ept)
{
    cfe_dispfunc = NATIVE_FROM_XPTR(ept);
    cfe_handle = handle;
    return 0;
}

int
cfe_iocb_dispatch(cfe_xiocb_t *xiocb)
{
    if (!cfe_dispfunc) return -1;
    return (*cfe_dispfunc)((intptr_t)cfe_handle, (intptr_t)xiocb);
}
#endif /* CFE_API_common || CFE_API_ALL */

#if defined(CFE_API_close) || defined(CFE_API_ALL)
int
cfe_close(int handle)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_DEV_CLOSE;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = handle;
    xiocb.xiocb_flags = 0;
    xiocb.xiocb_psize = 0;

    cfe_iocb_dispatch(&xiocb);

    return xiocb.xiocb_status;

}
#endif /* CFE_API_close || CFE_API_ALL */

#if defined(CFE_API_cpu_start) || defined(CFE_API_ALL)
int
cfe_cpu_start(int cpu, void (*fn)(void), long sp, long gp, long a1)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_FW_CPUCTL;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = 0;
    xiocb.xiocb_flags  = 0;
    xiocb.xiocb_psize = sizeof(xiocb_cpuctl_t);
    xiocb.plist.xiocb_cpuctl.cpu_number = cpu;
    xiocb.plist.xiocb_cpuctl.cpu_command = CFE_CPU_CMD_START;
    xiocb.plist.xiocb_cpuctl.gp_val = gp;
    xiocb.plist.xiocb_cpuctl.sp_val = sp;
    xiocb.plist.xiocb_cpuctl.a1_val = a1;
    xiocb.plist.xiocb_cpuctl.start_addr = (long)fn;

    cfe_iocb_dispatch(&xiocb);

    return xiocb.xiocb_status;
}
#endif /* CFE_API_cpu_start || CFE_API_ALL */

#if defined(CFE_API_cpu_stop) || defined(CFE_API_ALL)
int
cfe_cpu_stop(int cpu)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_FW_CPUCTL;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = 0;
    xiocb.xiocb_flags  = 0;
    xiocb.xiocb_psize = sizeof(xiocb_cpuctl_t);
    xiocb.plist.xiocb_cpuctl.cpu_number = cpu;
    xiocb.plist.xiocb_cpuctl.cpu_command = CFE_CPU_CMD_STOP;

    cfe_iocb_dispatch(&xiocb);

    return xiocb.xiocb_status;
}
#endif /* CFE_API_cpu_stop || CFE_API_ALL */

#if defined(CFE_API_enumenv) || defined(CFE_API_ALL)
int
cfe_enumenv(int idx, char *name, int namelen, char *val, int vallen)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_ENV_ENUM;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = 0;
    xiocb.xiocb_flags = 0;
    xiocb.xiocb_psize = sizeof(xiocb_envbuf_t);
    xiocb.plist.xiocb_envbuf.enum_idx = idx;
    xiocb.plist.xiocb_envbuf.name_ptr = XPTR_FROM_NATIVE(name);
    xiocb.plist.xiocb_envbuf.name_length = namelen;
    xiocb.plist.xiocb_envbuf.val_ptr = XPTR_FROM_NATIVE(val);
    xiocb.plist.xiocb_envbuf.val_length = vallen;

    cfe_iocb_dispatch(&xiocb);

    return xiocb.xiocb_status;
}
#endif /* CFE_API_enumenv || CFE_API_ALL */

#if defined(CFE_API_enumdev) || defined(CFE_API_ALL)
int
cfe_enumdev(int idx, char *name, int namelen)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_DEV_ENUM;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = 0;
    xiocb.xiocb_flags = 0;
    xiocb.xiocb_psize = sizeof(xiocb_envbuf_t);
    xiocb.plist.xiocb_envbuf.enum_idx = idx;
    xiocb.plist.xiocb_envbuf.name_ptr = XPTR_FROM_NATIVE(name);
    xiocb.plist.xiocb_envbuf.name_length = namelen;

    cfe_iocb_dispatch(&xiocb);

    return xiocb.xiocb_status;
}
#endif /* CFE_API_enumdev || CFE_API_ALL */

#if defined(CFE_API_enummem) || defined(CFE_API_ALL)
int
cfe_enummem(int idx, int flags, cfe_xuint_t *start, cfe_xuint_t *length,
	    cfe_xuint_t *type)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_FW_MEMENUM;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = 0;
    xiocb.xiocb_flags = flags;
    xiocb.xiocb_psize = sizeof(xiocb_meminfo_t);
    xiocb.plist.xiocb_meminfo.mi_idx = idx;

    cfe_iocb_dispatch(&xiocb);

    if (xiocb.xiocb_status < 0)
	return xiocb.xiocb_status;

    *start = xiocb.plist.xiocb_meminfo.mi_addr;
    *length = xiocb.plist.xiocb_meminfo.mi_size;
    *type = xiocb.plist.xiocb_meminfo.mi_type;

    return 0;
}
#endif /* CFE_API_enummem || CFE_API_ALL */

#if defined(CFE_API_exit) || defined(CFE_API_ALL)
int
cfe_exit(int warm, int status)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_FW_RESTART;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = 0;
    xiocb.xiocb_flags = warm ? CFE_FLG_WARMSTART : 0;
    xiocb.xiocb_psize = sizeof(xiocb_exitstat_t);
    xiocb.plist.xiocb_exitstat.status = status;

    cfe_iocb_dispatch(&xiocb);

    return xiocb.xiocb_status;
}
#endif /* CFE_API_exit || CFE_API_ALL */

#if defined(CFE_API_flushcache) || defined(CFE_API_ALL)
int
cfe_flushcache(int flg)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_FW_FLUSHCACHE;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = 0;
    xiocb.xiocb_flags = flg;
    xiocb.xiocb_psize = 0;

    cfe_iocb_dispatch(&xiocb);

    return xiocb.xiocb_status;
}
#endif /* CFE_API_flushcache || CFE_API_ALL */

#if defined(CFE_API_getdevinfo) || defined(CFE_API_ALL)
int
cfe_getdevinfo(char *name)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_DEV_GETINFO;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = 0;
    xiocb.xiocb_flags = 0;
    xiocb.xiocb_psize = sizeof(xiocb_buffer_t);
    xiocb.plist.xiocb_buffer.buf_offset = 0;
    xiocb.plist.xiocb_buffer.buf_ptr = XPTR_FROM_NATIVE(name);
    xiocb.plist.xiocb_buffer.buf_length = cfe_strlen(name);

    cfe_iocb_dispatch(&xiocb);

    if (xiocb.xiocb_status < 0)
	return xiocb.xiocb_status;
    return xiocb.plist.xiocb_buffer.buf_devflags;
}
#endif /* CFE_API_getdevinfo || CFE_API_ALL */

#if defined(CFE_API_getenv) || defined(CFE_API_ALL)
int
cfe_getenv(char *name, char *dest, int destlen)
{
    cfe_xiocb_t xiocb;

    *dest = 0;

    xiocb.xiocb_fcode = CFE_CMD_ENV_GET;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = 0;
    xiocb.xiocb_flags = 0;
    xiocb.xiocb_psize = sizeof(xiocb_envbuf_t);
    xiocb.plist.xiocb_envbuf.enum_idx = 0;
    xiocb.plist.xiocb_envbuf.name_ptr = XPTR_FROM_NATIVE(name);
    xiocb.plist.xiocb_envbuf.name_length = cfe_strlen(name);
    xiocb.plist.xiocb_envbuf.val_ptr = XPTR_FROM_NATIVE(dest);
    xiocb.plist.xiocb_envbuf.val_length = destlen;

    cfe_iocb_dispatch(&xiocb);

    return xiocb.xiocb_status;
}
#endif /* CFE_API_getenv || CFE_API_ALL */

#if defined(CFE_API_getfwinfo) || defined(CFE_API_ALL)
int
cfe_getfwinfo(cfe_fwinfo_t *info)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_FW_GETINFO;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = 0;
    xiocb.xiocb_flags = 0;
    xiocb.xiocb_psize = sizeof(xiocb_fwinfo_t);

    cfe_iocb_dispatch(&xiocb);

    if (xiocb.xiocb_status < 0)
	return xiocb.xiocb_status;

    info->fwi_version = xiocb.plist.xiocb_fwinfo.fwi_version;
    info->fwi_totalmem = xiocb.plist.xiocb_fwinfo.fwi_totalmem;
    info->fwi_flags = xiocb.plist.xiocb_fwinfo.fwi_flags;
    info->fwi_boardid = xiocb.plist.xiocb_fwinfo.fwi_boardid;
    info->fwi_bootarea_va = xiocb.plist.xiocb_fwinfo.fwi_bootarea_va;
    info->fwi_bootarea_pa = xiocb.plist.xiocb_fwinfo.fwi_bootarea_pa;
    info->fwi_bootarea_size = xiocb.plist.xiocb_fwinfo.fwi_bootarea_size;
#if 0
    info->fwi_reserved1 = xiocb.plist.xiocb_fwinfo.fwi_reserved1;
    info->fwi_reserved2 = xiocb.plist.xiocb_fwinfo.fwi_reserved2;
    info->fwi_reserved3 = xiocb.plist.xiocb_fwinfo.fwi_reserved3;
#endif

    return 0;
}
#endif /* CFE_API_getfwinfo || CFE_API_ALL */

#if defined(CFE_API_getstdhandle) || defined(CFE_API_ALL)
int
cfe_getstdhandle(int flg)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_DEV_GETHANDLE;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = 0;
    xiocb.xiocb_flags = flg;
    xiocb.xiocb_psize = 0;

    cfe_iocb_dispatch(&xiocb);

    if (xiocb.xiocb_status < 0)
	return xiocb.xiocb_status;
    return xiocb.xiocb_handle;

}
#endif /* CFE_API_getstdhandle || CFE_API_ALL */

#if defined(CFE_API_getticks) || defined(CFE_API_ALL)
int64_t
#ifdef CFE_API_IMPL_NAMESPACE
__cfe_getticks(void)
#else
cfe_getticks(void)
#endif
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_FW_GETTIME;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = 0;
    xiocb.xiocb_flags = 0;
    xiocb.xiocb_psize = sizeof(xiocb_time_t);
    xiocb.plist.xiocb_time.ticks = 0;

    cfe_iocb_dispatch(&xiocb);

    return xiocb.plist.xiocb_time.ticks;

}
#endif /* CFE_API_getticks || CFE_API_ALL */

#if defined(CFE_API_inpstat) || defined(CFE_API_ALL)
int
cfe_inpstat(int handle)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_DEV_INPSTAT;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = handle;
    xiocb.xiocb_flags = 0;
    xiocb.xiocb_psize = sizeof(xiocb_inpstat_t);
    xiocb.plist.xiocb_inpstat.inp_status = 0;

    cfe_iocb_dispatch(&xiocb);

    if (xiocb.xiocb_status < 0)
	return xiocb.xiocb_status;
    return xiocb.plist.xiocb_inpstat.inp_status;

}
#endif /* CFE_API_inpstat || CFE_API_ALL */

#if defined(CFE_API_ioctl) || defined(CFE_API_ALL)
int
cfe_ioctl(int handle, unsigned int ioctlnum, unsigned char *buffer, int length,
	  int *retlen, cfe_xuint_t offset)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_DEV_IOCTL;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = handle;
    xiocb.xiocb_flags = 0;
    xiocb.xiocb_psize = sizeof(xiocb_buffer_t);
    xiocb.plist.xiocb_buffer.buf_offset = offset;
    xiocb.plist.xiocb_buffer.buf_ioctlcmd = ioctlnum;
    xiocb.plist.xiocb_buffer.buf_ptr = XPTR_FROM_NATIVE(buffer);
    xiocb.plist.xiocb_buffer.buf_length = length;

    cfe_iocb_dispatch(&xiocb);

    if (retlen)
	*retlen = xiocb.plist.xiocb_buffer.buf_retlen;
    return xiocb.xiocb_status;
}
#endif /* CFE_API_ioctl || CFE_API_ALL */

#if defined(CFE_API_open) || defined(CFE_API_ALL)
int
cfe_open(char *name)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_DEV_OPEN;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = 0;
    xiocb.xiocb_flags = 0;
    xiocb.xiocb_psize = sizeof(xiocb_buffer_t);
    xiocb.plist.xiocb_buffer.buf_offset = 0;
    xiocb.plist.xiocb_buffer.buf_ptr = XPTR_FROM_NATIVE(name);
    xiocb.plist.xiocb_buffer.buf_length = cfe_strlen(name);

    cfe_iocb_dispatch(&xiocb);

    if (xiocb.xiocb_status < 0)
	return xiocb.xiocb_status;
    return xiocb.xiocb_handle;
}
#endif /* CFE_API_open || CFE_API_ALL */

#if defined(CFE_API_read) || defined(CFE_API_ALL)
int
cfe_read(int handle, unsigned char *buffer, int length)
{
    return cfe_readblk(handle, 0, buffer, length);
}
#endif /* CFE_API_read || CFE_API_ALL */

#if defined(CFE_API_readblk) || defined(CFE_API_ALL)
int
cfe_readblk(int handle, cfe_xint_t offset, unsigned char *buffer, int length)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_DEV_READ;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = handle;
    xiocb.xiocb_flags = 0;
    xiocb.xiocb_psize = sizeof(xiocb_buffer_t);
    xiocb.plist.xiocb_buffer.buf_offset = offset;
    xiocb.plist.xiocb_buffer.buf_ptr = XPTR_FROM_NATIVE(buffer);
    xiocb.plist.xiocb_buffer.buf_length = length;

    cfe_iocb_dispatch(&xiocb);

    if (xiocb.xiocb_status < 0)
	return xiocb.xiocb_status;
    return xiocb.plist.xiocb_buffer.buf_retlen;
}
#endif /* CFE_API_readblk || CFE_API_ALL */

#if defined(CFE_API_setenv) || defined(CFE_API_ALL)
int
cfe_setenv(char *name, char *val)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_ENV_SET;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = 0;
    xiocb.xiocb_flags = 0;
    xiocb.xiocb_psize = sizeof(xiocb_envbuf_t);
    xiocb.plist.xiocb_envbuf.enum_idx = 0;
    xiocb.plist.xiocb_envbuf.name_ptr = XPTR_FROM_NATIVE(name);
    xiocb.plist.xiocb_envbuf.name_length = cfe_strlen(name);
    xiocb.plist.xiocb_envbuf.val_ptr = XPTR_FROM_NATIVE(val);
    xiocb.plist.xiocb_envbuf.val_length = cfe_strlen(val);

    cfe_iocb_dispatch(&xiocb);

    return xiocb.xiocb_status;
}
#endif /* CFE_API_setenv || CFE_API_ALL */

#if (defined(CFE_API_strlen) || defined(CFE_API_ALL)) \
    && !defined(CFE_API_STRLEN_CUSTOM)
int
cfe_strlen(char *name)
{
    int count = 0;

    while (*name++)
	count++;

    return count;
}
#endif /* CFE_API_strlen || CFE_API_ALL */

#if defined(CFE_API_write) || defined(CFE_API_ALL)
int
cfe_write(int handle, unsigned char *buffer, int length)
{
    return cfe_writeblk(handle, 0, buffer, length);
}
#endif /* CFE_API_write || CFE_API_ALL */

#if defined(CFE_API_writeblk) || defined(CFE_API_ALL)
int
cfe_writeblk(int handle, cfe_xint_t offset, unsigned char *buffer, int length)
{
    cfe_xiocb_t xiocb;

    xiocb.xiocb_fcode = CFE_CMD_DEV_WRITE;
    xiocb.xiocb_status = 0;
    xiocb.xiocb_handle = handle;
    xiocb.xiocb_flags = 0;
    xiocb.xiocb_psize = sizeof(xiocb_buffer_t);
    xiocb.plist.xiocb_buffer.buf_offset = offset;
    xiocb.plist.xiocb_buffer.buf_ptr = XPTR_FROM_NATIVE(buffer);
    xiocb.plist.xiocb_buffer.buf_length = length;

    cfe_iocb_dispatch(&xiocb);

    if (xiocb.xiocb_status < 0)
	return xiocb.xiocb_status;
    return xiocb.plist.xiocb_buffer.buf_retlen;
}
#endif /* CFE_API_writeblk || CFE_API_ALL */
