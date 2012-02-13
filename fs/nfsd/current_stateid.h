#ifndef _NFSD4_CURRENT_STATE_H
#define _NFSD4_CURRENT_STATE_H

#include "state.h"
#include "xdr4.h"

extern void nfsd4_set_openstateid(struct nfsd4_compound_state *, struct nfsd4_open *);
extern void nfsd4_get_closestateid(struct nfsd4_compound_state *, struct nfsd4_close *);
extern void nfsd4_set_closestateid(struct nfsd4_compound_state *, struct nfsd4_close *);

#endif   /* _NFSD4_CURRENT_STATE_H */
