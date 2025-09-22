#if !defined(PEX_PROTOS_H)
#define PEX_PROTOS_H

struct pex_obj;

extern const char *
pex_run (struct pex_obj *obj, int flags, const char *executable,
       	 char * const * argv, const char *orig_outname, const char *errname,
         int *err);

extern int
pex_get_status (struct pex_obj *obj, int count, int *vector);

extern void
pex_free (struct pex_obj *obj);
#endif
