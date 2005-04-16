/*
 * linux/include/net/sunrpc/msg_prot.h
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_MSGPROT_H_
#define _LINUX_SUNRPC_MSGPROT_H_

#ifdef __KERNEL__ /* user programs should get these from the rpc header files */

#define RPC_VERSION 2

/* spec defines authentication flavor as an unsigned 32 bit integer */
typedef u32	rpc_authflavor_t;

enum rpc_auth_flavors {
	RPC_AUTH_NULL  = 0,
	RPC_AUTH_UNIX  = 1,
	RPC_AUTH_SHORT = 2,
	RPC_AUTH_DES   = 3,
	RPC_AUTH_KRB   = 4,
	RPC_AUTH_GSS   = 6,
	RPC_AUTH_MAXFLAVOR = 8,
	/* pseudoflavors: */
	RPC_AUTH_GSS_KRB5  = 390003,
	RPC_AUTH_GSS_KRB5I = 390004,
	RPC_AUTH_GSS_KRB5P = 390005,
	RPC_AUTH_GSS_LKEY  = 390006,
	RPC_AUTH_GSS_LKEYI = 390007,
	RPC_AUTH_GSS_LKEYP = 390008,
	RPC_AUTH_GSS_SPKM  = 390009,
	RPC_AUTH_GSS_SPKMI = 390010,
	RPC_AUTH_GSS_SPKMP = 390011,
};

enum rpc_msg_type {
	RPC_CALL = 0,
	RPC_REPLY = 1
};

enum rpc_reply_stat {
	RPC_MSG_ACCEPTED = 0,
	RPC_MSG_DENIED = 1
};

enum rpc_accept_stat {
	RPC_SUCCESS = 0,
	RPC_PROG_UNAVAIL = 1,
	RPC_PROG_MISMATCH = 2,
	RPC_PROC_UNAVAIL = 3,
	RPC_GARBAGE_ARGS = 4,
	RPC_SYSTEM_ERR = 5
};

enum rpc_reject_stat {
	RPC_MISMATCH = 0,
	RPC_AUTH_ERROR = 1
};

enum rpc_auth_stat {
	RPC_AUTH_OK = 0,
	RPC_AUTH_BADCRED = 1,
	RPC_AUTH_REJECTEDCRED = 2,
	RPC_AUTH_BADVERF = 3,
	RPC_AUTH_REJECTEDVERF = 4,
	RPC_AUTH_TOOWEAK = 5,
	/* RPCSEC_GSS errors */
	RPCSEC_GSS_CREDPROBLEM = 13,
	RPCSEC_GSS_CTXPROBLEM = 14
};

#define RPC_PMAP_PROGRAM	100000
#define RPC_PMAP_VERSION	2
#define RPC_PMAP_PORT		111

#define RPC_MAXNETNAMELEN	256

#endif /* __KERNEL__ */
#endif /* _LINUX_SUNRPC_MSGPROT_H_ */
