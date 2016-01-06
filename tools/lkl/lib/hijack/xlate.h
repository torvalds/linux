#ifndef _LKL_HIJACK_XLATE_H
#define _LKL_HIJACK_XLATE_H

long lkl_set_errno(long err);
int lkl_soname_xlate(int soname);
int lkl_solevel_xlate(int solevel);
unsigned long lkl_ioctl_req_xlate(unsigned long req);
int lkl_fcntl_cmd_xlate(int cmd);

#define LKL_FD_OFFSET (FD_SETSIZE/2)

#endif /* _LKL_HIJACK_XLATE_H */
