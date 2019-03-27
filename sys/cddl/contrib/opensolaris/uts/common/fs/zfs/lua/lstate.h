/*
** $Id: lstate.h,v 2.82.1.1 2013/04/12 18:48:47 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"


/*

** Some notes about garbage-collected objects:  All objects in Lua must
** be kept somehow accessible until being freed.
**
** Lua keeps most objects linked in list g->allgc. The link uses field
** 'next' of the CommonHeader.
**
** Strings are kept in several lists headed by the array g->strt.hash.
**
** Open upvalues are not subject to independent garbage collection. They
** are collected together with their respective threads. Lua keeps a
** double-linked list with all open upvalues (g->uvhead) so that it can
** mark objects referred by them. (They are always gray, so they must
** be remarked in the atomic step. Usually their contents would be marked
** when traversing the respective threads, but the thread may already be
** dead, while the upvalue is still accessible through closures.)
**
** Objects with finalizers are kept in the list g->finobj.
**
** The list g->tobefnz links all objects being finalized.

*/


struct lua_longjmp;  /* defined in ldo.c */



/* extra stack space to handle TM calls and some other extras */
#define EXTRA_STACK   5


#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)


/* kinds of Garbage Collection */
#define KGC_NORMAL	0
#define KGC_EMERGENCY	1	/* gc was forced by an allocation failure */
#define KGC_GEN		2	/* generational collection */


typedef struct stringtable {
  GCObject **hash;
  lu_int32 nuse;  /* number of elements */
  int size;
} stringtable;


/*
** information about a call
*/
typedef struct CallInfo {
  StkId func;  /* function index in the stack */
  StkId	top;  /* top for this function */
  struct CallInfo *previous, *next;  /* dynamic call link */
  short nresults;  /* expected number of results from this function */
  lu_byte callstatus;
  ptrdiff_t extra;
  union {
    struct {  /* only for Lua functions */
      StkId base;  /* base for this function */
      const Instruction *savedpc;
    } l;
    struct {  /* only for C functions */
      int ctx;  /* context info. in case of yields */
      lua_CFunction k;  /* continuation in case of yields */
      ptrdiff_t old_errfunc;
      lu_byte old_allowhook;
      lu_byte status;
    } c;
  } u;
} CallInfo;


/*
** Bits in CallInfo status
*/
#define CIST_LUA	(1<<0)	/* call is running a Lua function */
#define CIST_HOOKED	(1<<1)	/* call is running a debug hook */
#define CIST_REENTRY	(1<<2)	/* call is running on same invocation of
                                   luaV_execute of previous call */
#define CIST_YIELDED	(1<<3)	/* call reentered after suspension */
#define CIST_YPCALL	(1<<4)	/* call is a yieldable protected call */
#define CIST_STAT	(1<<5)	/* call has an error status (pcall) */
#define CIST_TAIL	(1<<6)	/* call was tail called */
#define CIST_HOOKYIELD	(1<<7)	/* last hook called yielded */


#define isLua(ci)	((ci)->callstatus & CIST_LUA)


/*
** `global state', shared by all threads of this state
*/
typedef struct global_State {
  lua_Alloc frealloc;  /* function to reallocate memory */
  void *ud;         /* auxiliary data to `frealloc' */
  lu_mem totalbytes;  /* number of bytes currently allocated - GCdebt */
  l_mem GCdebt;  /* bytes allocated not yet compensated by the collector */
  lu_mem GCmemtrav;  /* memory traversed by the GC */
  lu_mem GCestimate;  /* an estimate of the non-garbage memory in use */
  stringtable strt;  /* hash table for strings */
  TValue l_registry;
  unsigned int seed;  /* randomized seed for hashes */
  lu_byte currentwhite;
  lu_byte gcstate;  /* state of garbage collector */
  lu_byte gckind;  /* kind of GC running */
  lu_byte gcrunning;  /* true if GC is running */
  int sweepstrgc;  /* position of sweep in `strt' */
  GCObject *allgc;  /* list of all collectable objects */
  GCObject *finobj;  /* list of collectable objects with finalizers */
  GCObject **sweepgc;  /* current position of sweep in list 'allgc' */
  GCObject **sweepfin;  /* current position of sweep in list 'finobj' */
  GCObject *gray;  /* list of gray objects */
  GCObject *grayagain;  /* list of objects to be traversed atomically */
  GCObject *weak;  /* list of tables with weak values */
  GCObject *ephemeron;  /* list of ephemeron tables (weak keys) */
  GCObject *allweak;  /* list of all-weak tables */
  GCObject *tobefnz;  /* list of userdata to be GC */
  UpVal uvhead;  /* head of double-linked list of all open upvalues */
  Mbuffer buff;  /* temporary buffer for string concatenation */
  int gcpause;  /* size of pause between successive GCs */
  int gcmajorinc;  /* pause between major collections (only in gen. mode) */
  int gcstepmul;  /* GC `granularity' */
  lua_CFunction panic;  /* to be called in unprotected errors */
  struct lua_State *mainthread;
  const lua_Number *version;  /* pointer to version number */
  TString *memerrmsg;  /* memory-error message */
  TString *tmname[TM_N];  /* array with tag-method names */
  struct Table *mt[LUA_NUMTAGS];  /* metatables for basic types */
} global_State;


/*
** `per thread' state
*/
struct lua_State {
  CommonHeader;
  lu_byte status;
  StkId top;  /* first free slot in the stack */
  global_State *l_G;
  CallInfo *ci;  /* call info for current function */
  const Instruction *oldpc;  /* last pc traced */
  StkId stack_last;  /* last free slot in the stack */
  StkId stack;  /* stack base */
  int stacksize;
  unsigned short nny;  /* number of non-yieldable calls in stack */
  unsigned short nCcalls;  /* number of nested C calls */
  lu_byte hookmask;
  lu_byte allowhook;
  int basehookcount;
  int hookcount;
  lua_Hook hook;
  GCObject *openupval;  /* list of open upvalues in this stack */
  GCObject *gclist;
  struct lua_longjmp *errorJmp;  /* current error recover point */
  ptrdiff_t errfunc;  /* current error handling function (stack index) */
  CallInfo base_ci;  /* CallInfo for first level (C calling Lua) */
};


#define G(L)	(L->l_G)


/*
** Union of all collectable objects
*/
union GCObject {
  GCheader gch;  /* common header */
  union TString ts;
  union Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct UpVal uv;
  struct lua_State th;  /* thread */
};


#define gch(o)		(&(o)->gch)

/* macros to convert a GCObject into a specific value */
#define rawgco2ts(o)  \
	check_exp(novariant((o)->gch.tt) == LUA_TSTRING, &((o)->ts))
#define gco2ts(o)	(&rawgco2ts(o)->tsv)
#define rawgco2u(o)	check_exp((o)->gch.tt == LUA_TUSERDATA, &((o)->u))
#define gco2u(o)	(&rawgco2u(o)->uv)
#define gco2lcl(o)	check_exp((o)->gch.tt == LUA_TLCL, &((o)->cl.l))
#define gco2ccl(o)	check_exp((o)->gch.tt == LUA_TCCL, &((o)->cl.c))
#define gco2cl(o)  \
	check_exp(novariant((o)->gch.tt) == LUA_TFUNCTION, &((o)->cl))
#define gco2t(o)	check_exp((o)->gch.tt == LUA_TTABLE, &((o)->h))
#define gco2p(o)	check_exp((o)->gch.tt == LUA_TPROTO, &((o)->p))
#define gco2uv(o)	check_exp((o)->gch.tt == LUA_TUPVAL, &((o)->uv))
#define gco2th(o)	check_exp((o)->gch.tt == LUA_TTHREAD, &((o)->th))

/* macro to convert any Lua object into a GCObject */
#define obj2gco(v)	(cast(GCObject *, (v)))


/* actual number of total bytes allocated */
#define gettotalbytes(g)	((g)->totalbytes + (g)->GCdebt)

LUAI_FUNC void luaE_setdebt (global_State *g, l_mem debt);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);
LUAI_FUNC CallInfo *luaE_extendCI (lua_State *L);
LUAI_FUNC void luaE_freeCI (lua_State *L);


#endif

