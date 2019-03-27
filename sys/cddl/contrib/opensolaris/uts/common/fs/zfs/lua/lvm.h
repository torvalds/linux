/*
** $Id: lvm.h,v 2.18.1.1 2013/04/12 18:48:47 roberto Exp $
** Lua virtual machine
** See Copyright Notice in lua.h
*/

#ifndef lvm_h
#define lvm_h


#include "ldo.h"
#include "lobject.h"
#include "ltm.h"


#define tostring(L,o) (ttisstring(o) || (luaV_tostring(L, o)))

#define tonumber(o,n)	(ttisnumber(o) || (((o) = luaV_tonumber(o,n)) != NULL))

#define equalobj(L,o1,o2)  (ttisequal(o1, o2) && luaV_equalobj_(L, o1, o2))

#define luaV_rawequalobj(o1,o2)		equalobj(NULL,o1,o2)


/* not to called directly */
LUAI_FUNC int luaV_equalobj_ (lua_State *L, const TValue *t1, const TValue *t2);


LUAI_FUNC int luaV_lessthan (lua_State *L, const TValue *l, const TValue *r);
LUAI_FUNC int luaV_lessequal (lua_State *L, const TValue *l, const TValue *r);
LUAI_FUNC const TValue *luaV_tonumber (const TValue *obj, TValue *n);
LUAI_FUNC int luaV_tostring (lua_State *L, StkId obj);
LUAI_FUNC void luaV_gettable (lua_State *L, const TValue *t, TValue *key,
                                            StkId val);
LUAI_FUNC void luaV_settable (lua_State *L, const TValue *t, TValue *key,
                                            StkId val);
LUAI_FUNC void luaV_finishOp (lua_State *L);
LUAI_FUNC void luaV_execute (lua_State *L);
LUAI_FUNC void luaV_concat (lua_State *L, int total);
LUAI_FUNC void luaV_arith (lua_State *L, StkId ra, const TValue *rb,
                           const TValue *rc, TMS op);
LUAI_FUNC void luaV_objlen (lua_State *L, StkId ra, const TValue *rb);

#endif
