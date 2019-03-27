/*
** $Id: lmem.h,v 1.40.1.1 2013/04/12 18:48:47 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#ifndef lmem_h
#define lmem_h


#include <sys/zfs_context.h>

#include "llimits.h"
#include "lua.h"


/*
** This macro avoids the runtime division MAX_SIZET/(e), as 'e' is
** always constant.
** The macro is somewhat complex to avoid warnings:
** +1 avoids warnings of "comparison has constant result";
** cast to 'void' avoids warnings of "value unused".
*/
#define luaM_reallocv(L,b,on,n,e) \
  (cast(void, \
     (cast(size_t, (n)+1) > MAX_SIZET/(e)) ? (luaM_toobig(L), 0) : 0), \
   luaM_realloc_(L, (b), (on)*(e), (n)*(e)))

#define luaM_freemem(L, b, s)	luaM_realloc_(L, (b), (s), 0)
#define luaM_free(L, b)		luaM_realloc_(L, (b), sizeof(*(b)), 0)
#define luaM_freearray(L, b, n)   luaM_reallocv(L, (b), n, 0, sizeof((b)[0]))

#define luaM_malloc(L,s)	luaM_realloc_(L, NULL, 0, (s))
#define luaM_new(L,t)		cast(t *, luaM_malloc(L, sizeof(t)))
#define luaM_newvector(L,n,t) \
		cast(t *, luaM_reallocv(L, NULL, 0, n, sizeof(t)))

#define luaM_newobject(L,tag,s)	luaM_realloc_(L, NULL, tag, (s))

#define luaM_growvector(L,v,nelems,size,t,limit,e) \
          if ((nelems)+1 > (size)) \
            ((v)=cast(t *, luaM_growaux_(L,v,&(size),sizeof(t),limit,e)))

#define luaM_reallocvector(L, v,oldn,n,t) \
   ((v)=cast(t *, luaM_reallocv(L, v, oldn, n, sizeof(t))))

LUAI_FUNC l_noret luaM_toobig (lua_State *L);

/* not to be called directly */
LUAI_FUNC void *luaM_realloc_ (lua_State *L, void *block, size_t oldsize,
                                                          size_t size);
LUAI_FUNC void *luaM_growaux_ (lua_State *L, void *block, int *size,
                               size_t size_elem, int limit,
                               const char *what);

#endif

