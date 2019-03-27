/*
Copyright (c) 2001 Wolfram Gloger
Copyright (c) 2006 Cavium networks

Permission to use, copy, modify, distribute, and sell this software
and its documentation for any purpose is hereby granted without fee,
provided that (i) the above copyright notices and this permission
notice appear in all copies of the software and related documentation,
and (ii) the name of Wolfram Gloger may not be used in any advertising
or publicity relating to the software.

THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.

IN NO EVENT SHALL WOLFRAM GLOGER BE LIABLE FOR ANY SPECIAL,
INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND, OR ANY
DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY
OF LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
*/

/* $Id: arena.c 30481 2007-12-05 21:46:59Z rfranz $ */

/* Compile-time constants.  */

#define HEAP_MIN_SIZE (4096)   /* Must leave room for struct malloc_state, arena ptrs, etc., totals about 2400 bytes */

#ifndef THREAD_STATS
#define THREAD_STATS 0
#endif

/* If THREAD_STATS is non-zero, some statistics on mutex locking are
   computed.  */

/***************************************************************************/

// made static to avoid conflicts with newlib
static mstate         _int_new_arena __MALLOC_P ((size_t __ini_size));

/***************************************************************************/

#define top(ar_ptr) ((ar_ptr)->top)

/* A heap is a single contiguous memory region holding (coalesceable)
   malloc_chunks.    Not used unless compiling with
   USE_ARENAS. */

typedef struct _heap_info {
  mstate ar_ptr; /* Arena for this heap. */
  struct _heap_info *prev; /* Previous heap. */
  size_t size;   /* Current size in bytes. */
  size_t pad;    /* Make sure the following data is properly aligned. */
} heap_info;

/* Thread specific data */

static tsd_key_t arena_key;  // one per PP (thread)
static CVMX_SHARED mutex_t list_lock;  // shared...

#if THREAD_STATS
static int stat_n_heaps;
#define THREAD_STAT(x) x
#else
#define THREAD_STAT(x) do ; while(0)
#endif

/* Mapped memory in non-main arenas (reliable only for NO_THREADS). */
static unsigned long arena_mem;

/* Already initialized? */
int CVMX_SHARED cvmx__malloc_initialized = -1;

/**************************************************************************/

#if USE_ARENAS

/* find the heap and corresponding arena for a given ptr */

#define arena_for_chunk(ptr) ((ptr)->arena_ptr)
#define set_arena_for_chunk(ptr, arena) (ptr)->arena_ptr = (arena)


#endif /* USE_ARENAS */

/**************************************************************************/

#ifndef NO_THREADS

/* atfork support.  */

static __malloc_ptr_t (*save_malloc_hook) __MALLOC_P ((size_t __size,
						       __const __malloc_ptr_t));
static void           (*save_free_hook) __MALLOC_P ((__malloc_ptr_t __ptr,
						     __const __malloc_ptr_t));
static Void_t*        save_arena;

/* Magic value for the thread-specific arena pointer when
   malloc_atfork() is in use.  */

#define ATFORK_ARENA_PTR ((Void_t*)-1)

/* The following hooks are used while the `atfork' handling mechanism
   is active. */

static Void_t*
malloc_atfork(size_t sz, const Void_t *caller)
{
return(NULL);
}

static void
free_atfork(Void_t* mem, const Void_t *caller)
{
  Void_t *vptr = NULL;
  mstate ar_ptr;
  mchunkptr p;                          /* chunk corresponding to mem */

  if (mem == 0)                              /* free(0) has no effect */
    return;

  p = mem2chunk(mem);         /* do not bother to replicate free_check here */

#if HAVE_MMAP
  if (chunk_is_mmapped(p))                       /* release mmapped memory. */
  {
    munmap_chunk(p);
    return;
  }
#endif

  ar_ptr = arena_for_chunk(p);
  tsd_getspecific(arena_key, vptr);
  if(vptr != ATFORK_ARENA_PTR)
    (void)mutex_lock(&ar_ptr->mutex);
  _int_free(ar_ptr, mem);
  if(vptr != ATFORK_ARENA_PTR)
    (void)mutex_unlock(&ar_ptr->mutex);
}



#ifdef __linux__
#error   __linux__defined!
#endif

#endif /* !defined NO_THREADS */



/* Initialization routine. */
#ifdef _LIBC
#error  _LIBC is defined, and should not be
#endif /* _LIBC */

static CVMX_SHARED cvmx_spinlock_t malloc_init_spin_lock;




/* Managing heaps and arenas (for concurrent threads) */

#if USE_ARENAS

#if MALLOC_DEBUG > 1

/* Print the complete contents of a single heap to stderr. */

static void
#if __STD_C
dump_heap(heap_info *heap)
#else
dump_heap(heap) heap_info *heap;
#endif
{
  char *ptr;
  mchunkptr p;

  fprintf(stderr, "Heap %p, size %10lx:\n", heap, (long)heap->size);
  ptr = (heap->ar_ptr != (mstate)(heap+1)) ?
    (char*)(heap + 1) : (char*)(heap + 1) + sizeof(struct malloc_state);
  p = (mchunkptr)(((unsigned long)ptr + MALLOC_ALIGN_MASK) &
                  ~MALLOC_ALIGN_MASK);
  for(;;) {
    fprintf(stderr, "chunk %p size %10lx", p, (long)p->size);
    if(p == top(heap->ar_ptr)) {
      fprintf(stderr, " (top)\n");
      break;
    } else if(p->size == (0|PREV_INUSE)) {
      fprintf(stderr, " (fence)\n");
      break;
    }
    fprintf(stderr, "\n");
    p = next_chunk(p);
  }
}

#endif /* MALLOC_DEBUG > 1 */
/* Delete a heap. */


static mstate cvmx_new_arena(void *addr, size_t size)
{
  mstate a;
  heap_info *h;
  char *ptr;
  unsigned long misalign;
  int page_mask = malloc_getpagesize - 1;

  debug_printf("cvmx_new_arena called, addr: %p, size %ld\n", addr, size);
  debug_printf("heapinfo size: %ld, mstate size: %d\n", sizeof(heap_info), sizeof(struct malloc_state));

  if (!addr || (size < HEAP_MIN_SIZE))
  {
      return(NULL);
  }
  /* We must zero out the arena as the malloc code assumes this. */
  memset(addr, 0, size);

  h = (heap_info *)addr;
  h->size = size;

  a = h->ar_ptr = (mstate)(h+1);
  malloc_init_state(a);
  /*a->next = NULL;*/
  a->system_mem = a->max_system_mem = h->size;
  arena_mem += h->size;
  a->next = a;

  /* Set up the top chunk, with proper alignment. */
  ptr = (char *)(a + 1);
  misalign = (unsigned long)chunk2mem(ptr) & MALLOC_ALIGN_MASK;
  if (misalign > 0)
    ptr += MALLOC_ALIGNMENT - misalign;
  top(a) = (mchunkptr)ptr;
  set_head(top(a), (((char*)h + h->size) - ptr) | PREV_INUSE);

  return a;
}


int cvmx_add_arena(cvmx_arena_list_t *arena_list, void *ptr, size_t size)
{
  mstate a;

  /* Enforce required alignement, and adjust size */
  int misaligned = ((size_t)ptr) & (MALLOC_ALIGNMENT - 1);
  if (misaligned)
  {
      ptr = (char*)ptr + MALLOC_ALIGNMENT - misaligned;
      size -= MALLOC_ALIGNMENT - misaligned;
  }

  debug_printf("Adding arena at addr: %p, size %d\n", ptr, size);

  a = cvmx_new_arena(ptr, size);  /* checks ptr and size */
  if (!a)
  {
      return(-1);
  }

  debug_printf("cmvx_add_arena - arena_list: %p, *arena_list: %p\n", arena_list, *arena_list);
  debug_printf("cmvx_add_arena - list: %p, new: %p\n", *arena_list, a);
  mutex_init(&a->mutex);
  mutex_lock(&a->mutex);


  if (*arena_list)
  {
      mstate ar_ptr = *arena_list;
      (void)mutex_lock(&ar_ptr->mutex);
      a->next = ar_ptr->next;  // lock held on a and ar_ptr
      ar_ptr->next = a;
      (void)mutex_unlock(&ar_ptr->mutex);
  }
  else
  {
      *arena_list = a;
//      a->next = a;
  }

  debug_printf("cvmx_add_arena - list: %p, list->next: %p\n", *arena_list, ((mstate)*arena_list)->next);

  // unlock, since it is not going to be used immediately
  (void)mutex_unlock(&a->mutex);

  return(0);
}



#endif /* USE_ARENAS */
