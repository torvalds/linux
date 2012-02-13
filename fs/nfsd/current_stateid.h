#ifndef _NFSD4_CURRENT_STATE_H
#define _NFSD4_CURRENT_STATE_H

#include "state.h"
#include "xdr4.h"

/*
 * functions to set current state id
 */
extern void nfsd4_set_openstateid(struct nfsd4_compound_state *, struct nfsd4_open *);
extern void nfsd4_set_lockstateid(struct nfsd4_compound_state *, struct nfsd4_lock *);
extern void nfsd4_set_closestateid(struct nfsd4_compound_state *, struct nfsd4_close *);

/*
 * functions to consume current state id
 */
extern void nfsd4_get_closestateid(struct nfsd4_compound_state *, struct nfsd4_close *);
extern void nfsd4_get_lockustateid(struct nfsd4_compound_state *, struct nfsd4_locku *);

#endif   /* _NFSD4_CURRENT_STATE_H */
