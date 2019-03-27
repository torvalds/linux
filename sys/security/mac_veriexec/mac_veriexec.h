/*
 * $FreeBSD$
 *
 * Copyright (c) 2011, 2012, 2013, 2015, 2016, Juniper Networks, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef	_SECURITY_MAC_VERIEXEC_H
#define	_SECURITY_MAC_VERIEXEC_H

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/module.h>
#endif

/**
 * Name of the MAC module
 */
#define	MAC_VERIEXEC_NAME	"mac_veriexec"

/* MAC/veriexec syscalls */
#define	MAC_VERIEXEC_CHECK_FD_SYSCALL	1
#define	MAC_VERIEXEC_CHECK_PATH_SYSCALL	2

/**
 * Enough room for the largest signature...
 */
#define MAXFINGERPRINTLEN	64	/* enough room for largest signature */

/*
 * Types of veriexec inodes we can have
 */
#define VERIEXEC_INDIRECT	(1<<0)  /* Only allow indirect execution */
#define VERIEXEC_FILE		(1<<1)  /* Fingerprint of a plain file */
#define VERIEXEC_NOTRACE	(1<<2)	/**< PTRACE not allowed */
#define VERIEXEC_TRUSTED	(1<<3)	/**< Safe to write /dev/mem */
/* XXX these are currently unimplemented */
#define VERIEXEC_NOFIPS		(1<<4)	/**< Not allowed in FIPS mode */

#define VERIEXEC_STATE_INACTIVE	0	/**< Ignore */
#define VERIEXEC_STATE_LOADED	(1<<0)	/**< Sigs have been loaded */
#define VERIEXEC_STATE_ACTIVE	(1<<1)	/**< Pay attention to it */
#define VERIEXEC_STATE_ENFORCE	(1<<2)	/**< Fail execs for files that do not
					     match signature */
#define VERIEXEC_STATE_LOCKED	(1<<3)	/**< Do not allow further changes */

#ifdef _KERNEL
/**
 * Version of the MAC/veriexec module
 */
#define	MAC_VERIEXEC_VERSION	1

/* Valid states for the fingerprint flag - if signed exec is being used */
typedef enum fingerprint_status {
	FINGERPRINT_INVALID,	/**< Fingerprint has not been evaluated */
	FINGERPRINT_VALID,	/**< Fingerprint evaluated and matches list */
	FINGERPRINT_INDIRECT,	/**< Fingerprint eval'd/matched but only
				     indirect execs allowed */
	FINGERPRINT_FILE,	/**< Fingerprint evaluated/matched but
				     not executable */
	FINGERPRINT_NOMATCH,	/**< Fingerprint evaluated but does not match */
	FINGERPRINT_NOENTRY,	/**< Fingerprint evaluated but no list entry */
	FINGERPRINT_NODEV,	/**< Fingerprint evaluated but no dev list */
} fingerprint_status_t;

typedef void (*mac_veriexec_fpop_init_t)(void *);
typedef void (*mac_veriexec_fpop_update_t)(void *, const uint8_t *, size_t);
typedef void (*mac_veriexec_fpop_final_t)(uint8_t *, void *);

struct mac_veriexec_fpops {
	const char *type;
	size_t digest_len;
	size_t context_size;
	mac_veriexec_fpop_init_t init;
	mac_veriexec_fpop_update_t update;
	mac_veriexec_fpop_final_t final;
	LIST_ENTRY(mac_veriexec_fpops) entries;
};

/**
 * Verified execution subsystem debugging level
 */
extern int	mac_veriexec_debug;

/**
 * @brief Define a fingerprint module.
 *
 * @param _name		Name of the fingerprint module
 * @param _digest_len	Length of the digest string, in number of characters
 * @param _context_size	Size of the context structure, in bytes
 * @param _init		Initialization function of type
 * 			mac_veriexec_fpop_init_t
 * @param _update	Update function of type mac_veriexec_fpop_update_t
 * @param _final	Finalize function of type mac_veriexec_fpop_final_t
 * @param _vers		Module version
 */
#define MAC_VERIEXEC_FPMOD(_name, _digest_len, _context_size, _init,	\
	    _update, _final, _vers)					\
	static struct mac_veriexec_fpops				\
	    mac_veriexec_##_name##_fpops = {				\
		.type = #_name,						\
		.digest_len = _digest_len,				\
		.context_size = _context_size,				\
		.init = _init,						\
		.update = _update,					\
		.final = _final,					\
	};								\
	static moduledata_t mac_veriexec_##_name##_mod = {		\
		"mac_veriexec/" #_name,					\
		mac_veriexec_fingerprint_modevent,			\
		&(mac_veriexec_##_name##_fpops)				\
	};								\
	MODULE_VERSION(mac_veriexec_##_name, _vers);			\
	DECLARE_MODULE(mac_veriexec_##_name,				\
	    mac_veriexec_##_name##_mod, SI_SUB_MAC_POLICY,		\
	    SI_ORDER_ANY);						\
	MODULE_DEPEND(mac_veriexec_##_name, mac_veriexec,		\
	    MAC_VERIEXEC_VERSION, MAC_VERIEXEC_VERSION,			\
	    MAC_VERIEXEC_VERSION)

/*
 * The following function should not be called directly. The prototype is
 * included here to satisfy the compiler when using the macro above.
 */
int	mac_veriexec_fingerprint_modevent(module_t mod, int type, void *data);

/*
 * Public functions
 */
int	mac_veriexec_metadata_add_file(int file_dev, dev_t fsid, long fileid, 
	    unsigned long gen, unsigned char fingerprint[MAXFINGERPRINTLEN], 
	    int flags, const char *fp_type, int override);
int	mac_veriexec_metadata_has_file(dev_t fsid, long fileid, 
	    unsigned long gen);
int	mac_veriexec_proc_is_trusted(struct ucred *cred, struct proc *p);
#endif

#endif	/* _SECURITY_MAC_VERIEXEC_H */
