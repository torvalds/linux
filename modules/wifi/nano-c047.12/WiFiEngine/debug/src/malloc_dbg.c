#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "driverenv.h"
#include "malloc_dbg.h"

#if (DE_DEBUG_MODE & CFG_MEMORY)

static struct malloc_head_s malloc_head = 
	WEI_TQ_HEAD_INITIALIZER(malloc_head);

#ifdef (DE_RUNTIME_INTEGRATION == CFG_MULTI_THREADED)
static driver_trylock_t malloc_lock = 0;

/* Implement protection of the linked list with a spinn lock. */
#define MALLOC_DBG_LOCK    while(DriverEnvironment_acquire_trylock(&malloc_lock))
#define MALLOC_DBG_UNLOCK  DriverEnvironment_release_trylock(&malloc_lock)
#else 
#define MALLOC_DBG_LOCK
#define MALLOC_DBG_UNLOCK
#endif /* (DE_RUNTIME_INTEGRATION != CFG_MULTI_THREADED) */

int malloc_sum(void) 
{
	size_t m = 0;
	struct malloc_s* info_p = NULL;

	MALLOC_DBG_LOCK;
	WEI_TQ_FOREACH(info_p, &malloc_head, entry) {
		m += info_p->size;
	}
	MALLOC_DBG_UNLOCK;

	return m;
}

void malloc_list(void) 
{
	struct malloc_s* info_p = NULL;
	int i = 0;
  	driver_msec_t ts;

	MALLOC_DBG_LOCK;
   
 	ts = DriverEnvironment_GetTimestamp_msec();

	WEI_TQ_FOREACH(info_p, &malloc_head, entry) {
		DE_TRACE6(TR_ALL, "%u %s:%d %s [%p] %d\n",
			ts - info_p->ts, 
			info_p->file,info_p->line, info_p->func,
			info_p->ptr, info_p->size);
		i++;
	}
	MALLOC_DBG_UNLOCK;
	DE_TRACE2(TR_ALL, "%d entrys in malloc_head\n",i);
}

void free_dbg(void (*real_free)(void *ptr), 
		        void* ptr) 
{
	struct malloc_s* info_p;
  	info_p = (struct malloc_s*)ptr - 1;

	MALLOC_DBG_LOCK;
		WEI_TQ_REMOVE(&malloc_head, info_p, entry);
	MALLOC_DBG_UNLOCK;
   ptr = (void*)info_p;

	real_free(ptr);
}

void *malloc_dbg( void* (*real_malloc)(uint32_t  size),
		size_t size, char* file, int line, const char* func) 
{
   void* ptr;

	ptr = real_malloc(size + sizeof(struct malloc_s)); 
	if(ptr) 
   { 
		struct malloc_s* info_p;
    	info_p = (struct malloc_s*)ptr;
      ptr = (void*)&info_p[1];

		info_p->ptr  = ptr;
		info_p->size = size;
		info_p->file = file;
		info_p->line = line;
		info_p->func = func;
		info_p->ts = DriverEnvironment_GetTimestamp_msec();

	   MALLOC_DBG_LOCK;
   		WEI_TQ_INSERT_TAIL(&malloc_head, info_p , entry);
		MALLOC_DBG_UNLOCK;
	}

	return ptr;
}

#endif /* (DE_DEBUG_MODE & CFG_MEMORY) */
