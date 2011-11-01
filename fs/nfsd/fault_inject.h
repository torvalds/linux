/*
 * Copyright (c) 2011 Bryan Schumaker <bjschuma@netapp.com>
 *
 * Function definitions for fault injection
 */

#ifndef LINUX_NFSD_FAULT_INJECT_H
#define LINUX_NFSD_FAULT_INJECT_H

#ifdef CONFIG_NFSD_FAULT_INJECTION
int nfsd_fault_inject_init(void);
void nfsd_fault_inject_cleanup(void);
void nfsd_forget_clients(u64);
void nfsd_forget_locks(u64);
void nfsd_forget_openowners(u64);
void nfsd_forget_delegations(u64);
void nfsd_recall_delegations(u64);
#else /* CONFIG_NFSD_FAULT_INJECTION */
static inline int nfsd_fault_inject_init(void) { return 0; }
static inline void nfsd_fault_inject_cleanup(void) {}
static inline void nfsd_forget_clients(u64 num) {}
static inline void nfsd_forget_locks(u64 num) {}
static inline void nfsd_forget_openowners(u64 num) {}
static inline void nfsd_forget_delegations(u64 num) {}
static inline void nfsd_recall_delegations(u64 num) {}
#endif /* CONFIG_NFSD_FAULT_INJECTION */

#endif /* LINUX_NFSD_FAULT_INJECT_H */
