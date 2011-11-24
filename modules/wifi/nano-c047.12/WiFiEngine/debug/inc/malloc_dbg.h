#ifndef __MALLOC_DBG__
#define __MALLOC_DBG__

#if (DE_DEBUG_MODE & CFG_MEMORY)

struct malloc_s {
    void*         ptr;
    size_t        size;
    const char *  file;
    int           line;
    const char *  func;
    driver_msec_t ts;
    WEI_TQ_ENTRY(malloc_s) entry;
};

WEI_TQ_HEAD(malloc_head_s, malloc_s);

int malloc_sum(void);
void malloc_list(void);
void free_dbg(void (*real_free)(void *ptr), 
		void* ptr);
void *malloc_dbg( void* (*real_malloc)(uint32_t  size),
		size_t size, char* file, int line, const char* func);


#else

#define malloc_sum()                                           0 
#define malloc_list() 
#define free_dbg(_real_free, _ptr)                             _real_free(_ptr)
#define malloc_dbg(_real_malloc, _size, _file, _line, _func)   _real_malloc(_size)

#endif /* (DE_DEBUG_MODE & CFG_MEMORY) */


#endif /* __MALLOC_DBG__ */
