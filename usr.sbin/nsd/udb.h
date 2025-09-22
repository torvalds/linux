/* udb.h - u(micro) data base, stores data and index information in mmap file.
 * By W.C.A. Wijngaards
 * Copyright 2010, NLnet Labs.
 * BSD, see LICENSE.
 */
#ifndef UDB_H
#define UDB_H
#include <assert.h>

/**
 * The micro data base UDB.
 *
 * File data.udb is mmapped and used to lookup and edit.
 * it contains a header with space-allocation-info, and a reference to the
 * base information, an object that is the entry point for the file.
 * Then it contains a lot of data and index objects.
 *
 * The space allocator is 'buddy system', 1megareas, larger get own area.
 * So worst case is 2xdata filesize (+header).  Growth semi-linear.
 * Chunks have size and type (for recovery).  Call to reserve space.
 * Call to 'realloc-in-place', if space permits.
 *
 * Usually you want a record-type and its indexes (sorted) to be stored in
 * the file.  This is a table (named by string).  The record is opaque
 * data.
 *
 * To be able to use pointers in the mmapped file, there is conversion of
 * relative-pointers(to file base) to system-pointers.
 *
 * If an item is moved its internal pointers need to be recalculated.
 * Thus a recordtype (that has internal pointers) must provide a routine.
 * Structures that are 'on-disk', are denoted with _d. Except rel_ptr which
 * is also on-disk.
 *
 * About 64-bit trouble.  The pointer-size which which the application is
 * compiled determines the file layout, because this makes it perform well
 * in a mmap.  It could in theory be converted if you really wanted to.
 * Nonpointer data is best stored as a fixed bitsize (uint8, 16, 32, 64).
 */
typedef struct udb_base udb_base;
typedef struct udb_alloc udb_alloc;

/** these checks are very slow, disabled by default */
#if 0
/** perform extra checks (when --enable-checking is used) */
#ifndef NDEBUG
#define UDB_CHECK 1
#endif
#endif

/** pointers are stored like this */
typedef uint64_t udb_void;

/** convert relptr to usable pointer */
#define UDB_REL(base, relptr) ((void*)((char*)(base) + (relptr)))
/** from system pointer to relative pointer */
#define UDB_SYSTOREL(base, ptr) ((udb_void)((char*)(ptr) - (char*)(base)))

/** MAX 2**x exponent of alloced chunks, for 1Mbytes.  The smallest
 * chunk is 16bytes (8preamble+8data), so 0-3 is unused. */
#define UDB_ALLOC_CHUNKS_MAX 20
/** size of areas that are subdivided */
#define UDB_ALLOC_CHUNK_SIZE ((uint64_t)1<<UDB_ALLOC_CHUNKS_MAX)
/** the minimum alloc in exp, 2**x.  32bytes because of chunk_free_d size (8aligned) */
#define UDB_ALLOC_CHUNK_MINEXP 5
/** size of minimum alloc */
#define UDB_ALLOC_CHUNK_MINSIZE ((uint64_t)1<<UDB_ALLOC_CHUNK_MINEXP)
/** exp size used to mark the header (cannot be reallocated) */
#define UDB_EXP_HEADER 0
/** exp size used to mark XL(extralarge) allocations (in whole mbs) */
#define UDB_EXP_XL 1

typedef struct udb_ptr udb_ptr;
/**
 * This structure is there for when you want to have a pointer into
 * the mmap-ed file.  It is kept track of.  Set it to NULL to unlink it.
 * For pointers to the mmap-ed file from within the mmap-ed file, use the
 * rel_pre construct below.
 */
struct udb_ptr {
	/** the data segment it points to (relative file offset) */
	uint64_t data;
	/** pointer to the base pointer (for convenience) */
	void** base;
	/** prev in udb_ptr list for this data segment */
	udb_ptr* prev;
	/** next in udb_ptr list for this data segment */
	udb_ptr* next;
};

typedef struct udb_rel_ptr udb_rel_ptr;
/**
 * A relative pointer that keeps track of the list of pointers,
 * so that it can be reallocated.
 */
struct udb_rel_ptr {
	/** the relative pointer to the data itself (subtract chunk_d size
	 * to get the chunk_d type, this is for usage speed in dereferencing
	 * to the userdata). */
	udb_void data;
	/** udb_rel_ptr* prev in relptr list */
	udb_void prev;
	/** udb_rel_ptr* next in relptr list */
	udb_void next;
};

/** 
 * This is the routine that is called for every relptr
 * @param base: the baseptr for REL.
 * @param p: the relptr, a real pointer to it.
 * @param arg: user argument.
 */
typedef void udb_walk_relptr_cb(void*, udb_rel_ptr*, void*);

/** 
 * This routine calls the callback for every relptr in a datablock
 * params in order:
 * base: the baseptr for REL macro.
 * warg: the walkfunc user argument.
 * t: the type of the chunk.
 * d: pointer to the data part of the chunk (real pointer).
 * s: max size of the data part.
 * cb: the callback to call for every element.
 * arg: user argument to pass to the callback.
 */
typedef void udb_walk_relptr_func(void*, void*, uint8_t, void*, uint64_t,
	udb_walk_relptr_cb*, void*);

/** What sort of salvage should be performed by alloc */
enum udb_dirty_alloc {
	udb_dirty_clean = 0, /* all clean */
	udb_dirty_fl,        /* allocs, freelists are messed up */
	udb_dirty_fsize,     /* file size and fsize are messed up */
	udb_dirty_compact    /* allocs, freelists and relptrs are messed up */
};

typedef struct udb_glob_d udb_glob_d;
/**
 * The UDB global data for a file.  This structure is mmapped.
 * Make sure it has no structure-padding problems.
 */
struct udb_glob_d {
	/** size of header in the file (offset to the first alloced chunk) */
	uint64_t hsize;
	/** version number of this file */
	uint8_t version;
	/** was the file cleanly closed, 0 is not clean, 1 is clean */
	uint8_t clean_close;
	/** an allocation operation was in progress, file needs to be salvaged
	 * type enum udb_dirty_alloc */
	uint8_t dirty_alloc;
	/** user flags */
	uint8_t userflags;
	/** padding to 8-bytes alignment */
	uint8_t pad1[4];
	/** size to mmap */
	uint64_t fsize;
	/** chunk move rollback info: oldchunk (0 is nothing).
	 * volatile because these values prevent dataloss, they need to be
	 * written immediately. */
	volatile udb_void rb_old;
	/** chunk move rollback info: newchunk (0 is nothing) */
	volatile udb_void rb_new;
	/** size of move rollback chunks */
	volatile uint64_t rb_size;
	/** segment of move rollback, for an XL chunk that overlaps. */
	volatile uint64_t rb_seg;
	/** linked list for content-listing, 0 if empty;
	 * this pointer is unused; and could be removed if the database
	 * format is modified or updated. */
	udb_rel_ptr content_list;
	/** user global data pointer */
	udb_rel_ptr user_global;
};

/**
 * The UDB database file.  Contains all the data
 */
struct udb_base {
	/** name of the file, alloced */
	char* fname;

	/** mmap base pointer (or NULL) */
	void* base;
	/** size of mmap */
	size_t base_size;
	/** fd of mmap (if -1, closed). */
	int fd;

	/** space allocator that is used for this base */
	udb_alloc* alloc;
	/** real pointer to the global data in the file */
	udb_glob_d* glob_data;

	/** store all linked udb_ptrs in this table, by hash(offset).
	 * then a linked list of ptrs (all that match the hash).
	 * this avoids buckets, and thus memory allocation. */
	udb_ptr** ram_hash;
	/** size of the current udb_ptr hashtable array */
	size_t ram_size;
	/** mask for the current udb_ptr hashtable lookups */
	int ram_mask;
	/** number of ptrs in ram, used to decide when to grow */
	size_t ram_num;
	/** for relocation, this walks through all relptrs in chunk */
	udb_walk_relptr_func* walkfunc;
	/** user data for walkfunc */
	void* walkarg;

	/** compaction is inhibited */
	int inhibit_compact;
	/** compaction is useful; deletions performed. */
	int useful_compact;
};

typedef enum udb_chunk_type udb_chunk_type;
/** chunk type enum, setting these types help recovery and debug */
enum udb_chunk_type {
	udb_chunk_type_free = 0,
	udb_chunk_type_data, /* alloced data */
	udb_chunk_type_task,
	udb_chunk_type_internal
};

typedef struct udb_chunk_d udb_chunk_d;
/**
 * UDB chunk info (prepended for every allocated chunk).
 * The chunks are in doublelinkedlists per size.
 * At the end of the chunk another exp uint8 is stored (to walk backwards).
 * 17 bytes overhead, datasize for 32byte chunk is 15.
 */
struct udb_chunk_d {
	/** the size of this chunk (i.e. 2**x) */
	uint8_t exp;
	/** type for this chunk (enum chunktype; free, data or index) */
	uint8_t type;
	/** flags for this chunk */
	uint8_t flags;
	/** padding onto 8-alignment */
	uint8_t pad[5];
	/** udb_rel_ptr* first in list of rel-ptrs that point back here 
	 * In the free chunk this is the previous pointer. */
	udb_void ptrlist;
	/* user data space starts here, 64-bit aligned */
	uint8_t data[0];
	/* last octet: exp of chunk */
};

typedef struct udb_free_chunk_d udb_free_chunk_d;
/**
 * A free chunk.  Same start as the udb_chunk_d. minsize is 32 bytes.
 */
struct udb_free_chunk_d {
	/** the size of this chunk (i.e. 2**x) */
	uint8_t exp;
	/** type for this chunk (enum chunktype; free, data or index) */
	uint8_t type;
	/** flags for this chunk */
	uint8_t flags;
	/** padding onto 8-alignment */
	uint8_t pad[5];
	/** udb_chunk_d* prev of free list for this size */
	udb_void prev;
	/** udb_chunk_d* next of free list for this size */
	udb_void next;
	/* empty stuff */
	/* last octet: exp of chunk */
};

typedef struct udb_xl_chunk_d udb_xl_chunk_d;
/**
 * an Extra Large (XL) chunk.  Same start as the udb_chunk_d.  Allocated in whole
 * MAX_CHUNK_SIZE parts, whole megabytes.  overhead is 5x8=40 bytes.
 */
struct udb_xl_chunk_d {
	/** the size of this chunk (i.e. 2**x): special XL value */
	uint8_t exp;
	/** type for this chunk (enum chunktype; free, data or index) */
	uint8_t type;
	/** flags for this chunk */
	uint8_t flags;
	/** padding onto 8-alignment */
	uint8_t pad[5];
	/** udb_rel_ptr* first in list of rel-ptrs that point back here 
	 * In the free chunk this is the previous pointer. */
	udb_void ptrlist;
	/** size of this chunk in bytes */
	uint64_t size;
	/** data of the XL chunk */
	uint8_t data[0];
	/* uint64_t endsize: before last octet the size again. */
	/* uint8_t pad[7]: padding to make last octet last. */
	/* last octet: exp of chunk: special XL value */
};

typedef struct udb_alloc_d udb_alloc_d;
/**
 * UDB alloc info on disk.
 */
struct udb_alloc_d {
	/** stats: number of data bytes allocated, sum of sizes passed to alloc */
	uint64_t stat_data;
	/** stats: number of bytes in free chunks, sum of their 2**x size */
	uint64_t stat_free;
	/** stats: number of bytes in alloced chunks, sum of their 2**x size */
	uint64_t stat_alloc;
	/** offset to create next chunk at. can be before file-end, or be 
	 * fsize, volatile because it is used as a 'commit', and thus we want
	 * this to be written to memory (and thus disk) immediately. */
	volatile uint64_t nextgrow;
	/** fixed size array the points to the 2**x size chunks in the file,
	 * This is the start of the doublelinked list, ptr to udb_free_chunk_d.
	 * array starts at UDB_ALLOC_CHUNK_MINEXP entry as [0]. */
	udb_void free[UDB_ALLOC_CHUNKS_MAX-UDB_ALLOC_CHUNK_MINEXP+1];
};

/**
 * The UDB space allocator.  Assigns space in the file.
 */
struct udb_alloc {
	/** the base this is part of */
	udb_base* udb;
	/** real pointer to space allocation info on disk; fixedsize struct */
	udb_alloc_d* disk;
};

/** 
 * file header length, the file start with
 * 64bit: magic number to identify file (and prevent stupid mistakes)
 * globdata: global data. Fixed size segment. (starts with size uint64)
 * allocdata: alloc global data. Fixed size segment.
 * size and 0 byte: end marker for reverse search.
 */
#define UDB_HEADER_SIZE (sizeof(uint64_t)+sizeof(udb_glob_d)+ \
	sizeof(udb_alloc_d)+sizeof(uint64_t)*2)
/** magic string that starts an UDB file, uint64_t, note first byte=0, to mark
 * header start as a chunk. */
#define UDB_MAGIC (((uint64_t)'u'<<48)|((uint64_t)'d'<<40)|((uint64_t)'b' \
	<<32)|((uint64_t)'v'<<24)|((uint64_t)'0'<<16)|((uint64_t)'b'<<8))

/* UDB BASE */
/**
 * Create udb base structure and attempt to read the file.
 * @param fname: file name.
 * @param walkfunc: function to walk through relptrs in chunk.
 * @param arg: user argument to pass to walkfunc
 * @return base structure or NULL on failure.
 */
udb_base* udb_base_create_read(const char* fname, udb_walk_relptr_func walkfunc,
	void* arg);

/**
 * Create udb base structure and create a new file.
 * @param fname: file name.
 * @param walkfunc: function to walk through relptrs in chunk.
 * @param arg: user argument to pass to walkfunc
 * @return base structure or NULL on failure.
 */
udb_base* udb_base_create_new(const char* fname, udb_walk_relptr_func walkfunc,
	void* arg);

/**
 * Create udb from (O_RDWR) fd.
 * @param fname: file name.
 * @param fd: file descriptor.
 * @param walkfunc: function to walk through relptrs in chunk.
 * @param arg: user argument to pass to walkfunc
 * @return base structure or NULL on failure.
 */
udb_base* udb_base_create_fd(const char* fname, int fd,
	udb_walk_relptr_func walkfunc, void* arg);

/**
 * Properly close the UDB base file.  Separate from delete so the
 * most important bits (write to disk, sockets) can be done first.
 * @param udb: the udb.
 */
void udb_base_close(udb_base* udb);

/**
 * Free the data structure (and close if not already) the udb.
 * @param udb: the udb.
 */
void udb_base_free(udb_base* udb);

/**
 * Free the udb, but keep mmap mapped for others.
 * @param udb: the udb.
 */
void udb_base_free_keep_mmap(udb_base* udb);

/**
 * Sync the mmap.
 * @param udb: the udb.
 * @param wait: if true, the call blocks until synced.
 */
void udb_base_sync(udb_base* udb, int wait);

/**
 * The mmap size is updated to reflect changes by another process.
 * @param udb: the udb.
 */
void udb_base_remap_process(udb_base* udb);

/**
 * get the user data (relative) pointer.
 * @param udb: the udb.
 * @return the userdata relative pointer, 0 means nothing.
 */
udb_rel_ptr* udb_base_get_userdata(udb_base* udb);

/**
 * Set the user data (relative) pointer.
 * @param udb: the udb.
 * @param user: user data. offset-pointer (or 0).
 */
void udb_base_set_userdata(udb_base* udb, udb_void user);

/** 
 * Set the user flags (to any value, uint8). 
 * @param udb: the udb.
 * @param v: new value.
 */
void udb_base_set_userflags(udb_base* udb, uint8_t v);

/** 
 * Get the user flags. 
 * @param udb: the udb.
 * @param v: new value.
 */
uint8_t udb_base_get_userflags(udb_base* udb);

/**
 * Not for users of udb_base, but for udb_ptr.
 * Link in a new ptr that references a data segment.
 * @param udb: the udb.
 * @param ptr: to link in.
 */
void udb_base_link_ptr(udb_base* udb, udb_ptr* ptr);

/**
 * Not for users of udb_base, but for udb_ptr.
 * Unlink a ptr that references a data segment.
 * @param udb: the udb.
 * @param ptr: to unlink.
 */
void udb_base_unlink_ptr(udb_base* udb, udb_ptr* ptr);

/* UDB ALLOC */
/**
 * Utility for alloc, find 2**x size that is bigger than the given size.
 * Does not work for amount==0.
 * @param amount: amount of memory.
 * @return x; the exponent where 2**x >= amount.
 */
int udb_exp_size(uint64_t amount);

/**
 * Utility for alloc, what is the size that the current offset supports
 * as a maximum 2**x chunk.
 * Does not work for offset = 0 (result is infinite).
 * @param offset: the offset into the memory region.
 * @return maximum exponent where 2**x is fits the offset, thus
 * 	offset % (2**x) == 0 and x cannot be larger.
 */
int udb_exp_offset(uint64_t offset);

/**
 * Convert pointer to the data part to a pointer to the base of the chunk.
 * @param data: data part.
 * @return pointer to the base of the chunk.
 */
udb_void chunk_from_dataptr_ext(udb_void data);

/**
 * Create empty UDB allocate structure to write to disk to initialize file.
 * @param a: allocation structure to initialize.  system pointer.
 */
void udb_alloc_init_new(udb_alloc_d* a);

/**
 * Create new udb allocator, with specific data on disk
 * @param udb: the udb.
 * @param disk: disk data.
 * @return udb allocator or NULL on (malloc) failure.
 */
udb_alloc* udb_alloc_create(udb_base* udb, udb_alloc_d* disk);

/**
 * Free the udb allocator from memory.
 * @param alloc: the udb space allocator.
 */
void udb_alloc_delete(udb_alloc* alloc);

/**
 * Allocate space on the disk.
 * This may involve closing and reopening the mmap.
 * @param alloc: the udb space allocator.
 * @param sz: size you want to use.
 * @return relative pointer (or 0 on alloc failure).
 */
udb_void udb_alloc_space(udb_alloc* alloc, size_t sz);

/**
 * Allocate space on disk, give already the data you want there.
 * This may involve closing and reopening the mmap.
 * @param alloc: the udb space allocator.
 * @param d: data you want there (system pointer).
 * @param sz: size you want to use.
 * @return relative pointer (or 0 on alloc failure).
 */
udb_void udb_alloc_init(udb_alloc* alloc, void* d, size_t sz);

/**
 * free allocated space.  It may shrink the file.
 * This may involve closing and reopening the mmap.
 * @param alloc: the udb space allocator.
 * @param r: relative pointer to data you want to free.
 * @param sz: the size of the data you stop using.
 * @return false if the free failed, it failed the close and mmap.
 */
int udb_alloc_free(udb_alloc* alloc, udb_void r, size_t sz);

/**
 * realloc an existing allocated space.  It may grow the file.
 * This may involve closing and reopening the mmap.
 * It could also use the existing space where it is now.
 * @param alloc: the udb space allocator.
 * @param r: relative pointer to data you want to realloc.
 *	if 0 then this is alloc_space(), and osz is ignored.
 * @param osz: the old size of the data.
 * @param sz: the size of the data you want to get.
 *	if this is 0 then a free() is done, but please do it directly,
 *	as you then get a returnvalue (file errors).
 * @return relative pointer (0 on alloc failure, same if not moved).
 */
udb_void udb_alloc_realloc(udb_alloc* alloc, udb_void r, size_t osz,
	size_t sz);

/**
 * Prepare for a lot of new entries.  Grow space for that.
 * This can involve closing and reopening the mmap.
 * This space (if large) is going to be released on next free() or close().
 * @param alloc: the udb space allocator.
 * @param sz: size of the entries.
 * @param num: number of entries.
 * @return false on failure to grow or re-mmap.
 */
int udb_alloc_grow(udb_alloc* alloc, size_t sz, size_t num);

/** 
 * attempt to compact the data and move free space to the end
 * can shrink the db, which calls sync on the db (for portability).
 * @param udb: the udb base.
 * @return 0 on failure (to remap the (possibly) changed udb base).
 */
int udb_compact(udb_base* udb);

/** 
 * set the udb to inhibit or uninhibit compaction.  Does not perform
 * the compaction itself if enabled, for that call udb_compact.
 * @param udb: the udb base
 * @param inhibit: 0 or 1.
 */
void udb_compact_inhibited(udb_base* udb, int inhibit);

/**
 * Set the alloc type for a newly alloced piece of data
 * @param alloc: the udb space allocator.
 * @param r: relativeptr to the data.
 * @param tp: the type of that block.
 */
void udb_alloc_set_type(udb_alloc* alloc, udb_void r, udb_chunk_type tp);

/**
 * See if a pointer could be valid (it points within valid space),
 * for the given type side.  For debug checks.
 * @param udb: the udb
 * @param to: the ptr (offset).
 * @param destsize: the size_of of the destination of the pointer.
 * @return true if it points to a valid region.
 */
int udb_valid_offset(udb_base* udb, udb_void to, size_t destsize);

/**
 * See if a pointer is valid (it points to a chunk).  For debug checks.
 * @param udb: the udb.
 * @param to: the ptr (offset).
 * @return true if it points to the start of a chunks data region.
 */
int udb_valid_dataptr(udb_base* udb, udb_void to);

/**
 * See if a pointer is on the relptrlist for dataptr.  For debug checks.
 * @param udb: the udb.
 * @param rptr: the rel_ptr (offset).
 * @param to: dataptr of the chunk on which ptrlist the rptr is searched.
 * @return true if rptr is valid and on the ptrlist.
 */
int udb_valid_rptr(udb_base* udb, udb_void rptr, udb_void to);

/*** UDB_REL_PTR ***/
/** 
 * Init a new UDB rel ptr at NULL.
 * @param ptr: sysptr, becomes inited.
 */
void udb_rel_ptr_init(udb_rel_ptr* ptr);

/** 
 * Unlink a UDB rel ptr.
 * @param base: the udb base
 * @param ptr: sysptr, unlinked
 */
void udb_rel_ptr_unlink(void* base, udb_rel_ptr* ptr);

/**
 * Link a UDB rel ptr to a new chunk
 * @param base: the udb base
 * @param ptr: sysptr, linked to new value.
 * @param to: the data to point to (relative ptr).
 */
void udb_rel_ptr_link(void* base, udb_rel_ptr* ptr, udb_void to);

/**
 * Change rel ptr to a new value (point to another record)
 * @param base: the udb base
 * @param ptr: sysptr, points to new value.
 * @param to: the data to point to (relative ptr).
 */
void udb_rel_ptr_set(void* base, udb_rel_ptr* ptr, udb_void to);

/**
 * A chunk has moved and now edit all the relptrs in list to fix them up
 * @param base: the udb base
 * @param list: start of the ptr list
 * @param to: where the chunk has moved to relptr to its userdata.
 */
void udb_rel_ptr_edit(void* base, udb_void list, udb_void to);

/**
 * Get system pointer.  Assumes there is a variable named 'base'
 * that points to the udb base.
 * @param ptr: the relative pointer (a sysptr to it).
 * @return void* to the data.
 */
#define UDB_SYSPTR(ptr) UDB_REL(base, (ptr)->data)

/** get sys ptr for char* string */
#define UDB_CHAR(ptr) ((char*)UDB_REL(base, ptr))
/** get sys ptr for udb_rel_ptr */
#define UDB_REL_PTR(ptr) ((udb_rel_ptr*)UDB_REL(base, ptr))
/** get sys ptr for udb_glob_d */
#define UDB_GLOB(ptr) ((udb_glob_d*)UDB_REL(base, ptr))
/** get sys ptr for udb_chunk_d */
#define UDB_CHUNK(ptr) ((udb_chunk_d*)UDB_REL(base, ptr))
/** get sys ptr for udb_free_chunk_d */
#define UDB_FREE_CHUNK(ptr) ((udb_free_chunk_d*)UDB_REL(base, ptr))
/** get sys ptr for udb_xl_chunk_d */
#define UDB_XL_CHUNK(ptr) ((udb_xl_chunk_d*)UDB_REL(base, ptr))

/* udb_ptr */
/**
 * Initialize an udb ptr.  Set to NULL.  (and thus not linked can be deleted).
 * You MUST set it to 0 before you stop using the ptr.
 * @param ptr: the ptr to initialise (caller has allocated it).
 * @param udb: the udb base to link it to.
 */
void udb_ptr_init(udb_ptr* ptr, udb_base* udb);

/**
 * Set udp ptr to a new value.  If set to NULL you can delete it.
 * @param ptr: the ptr.
 * @param udb: the udb base to link up with that data segment's administration.
 * @param newval: new value to point to (udb_void relative file offset to data).
 */
void udb_ptr_set(udb_ptr* ptr, udb_base* udb, udb_void newval);

/** dereference udb_ptr */
#define UDB_PTR(ptr) (UDB_REL(*((ptr)->base), (ptr)->data))

/**
 * Ease of use udb ptr, allocate space and return ptr to it
 * You MUST udb_ptr_set it to 0 before you stop using the ptr.
 * @param base: udb base to use.
 * @param ptr: ptr is overwritten, can be uninitialised.
 * @param type: type of the allocation.
 * 	You need a special type if the block contains udb_rel_ptr's.
 * 	You can use udb_type_data for plain data.
 * @param sz: amount to allocate.
 * @return 0 on alloc failure.
 */
int udb_ptr_alloc_space(udb_ptr* ptr, udb_base* udb, udb_chunk_type type,
	size_t sz);

/**
 * Ease of use udb ptr, free space and set ptr to NULL (to it can be deleted).
 * The space is freed on disk.
 * @param ptr: the ptr.
 * @param udb: udb base.
 * @param sz: the size of the data you stop using.
 */
void udb_ptr_free_space(udb_ptr* ptr, udb_base* udb, size_t sz);

/**
 * Get pointer to the data of the ptr.  or use a macro to cast UDB_PTR to
 * the type of your structure(.._d)
 */
static inline uint8_t* udb_ptr_data(udb_ptr* ptr) {
	return (uint8_t*)UDB_PTR(ptr);
}

/**
 * See if udb ptr is null
 */
static inline int udb_ptr_is_null(udb_ptr* ptr) {
	return (ptr->data == 0);
}

/**
 * Get the type of a udb_ptr chunk.
 * @param ptr: udb pointer
 * @return type of chunk */
udb_chunk_type udb_ptr_get_type(udb_ptr* ptr);

/** Ease of use, create new pointer to destination relptr
 * You MUST udb_ptr_set it to 0 before you stop using the ptr. */
static inline void udb_ptr_new(udb_ptr* ptr, udb_base* udb, udb_rel_ptr* d) {
	udb_ptr_init(ptr, udb);
	udb_ptr_set(ptr, udb, d->data);
}

/** Ease of use.  Stop using this ptr */
static inline void udb_ptr_unlink(udb_ptr* ptr, udb_base* udb) {
	if(ptr->data)
		udb_base_unlink_ptr(udb, ptr);
}

/* Ease of use.  Assign rptr from rptr */
static inline void udb_rptr_set_rptr(udb_rel_ptr* dest, udb_base* udb,
	udb_rel_ptr* p) {
#ifdef UDB_CHECK
	if(dest->data) { assert(udb_valid_rptr(udb,
		UDB_SYSTOREL(udb->base, dest), dest->data)); }
	if(p->data) { assert(udb_valid_rptr(udb,
		UDB_SYSTOREL(udb->base, p), p->data)); }
#endif
	udb_rel_ptr_set(udb->base, dest, p->data);
}

/* Ease of use.  Assign rptr from ptr */
static inline void udb_rptr_set_ptr(udb_rel_ptr* dest, udb_base* udb,
	udb_ptr* p) {
#ifdef UDB_CHECK
	if(dest->data) { assert(udb_valid_rptr(udb,
		UDB_SYSTOREL(udb->base, dest), dest->data)); }
	if(p->data) { assert(udb_valid_dataptr(udb, p->data)); }
#endif
	udb_rel_ptr_set(udb->base, dest, p->data);
}

/* Ease of use.  Assign ptr from rptr */
static inline void udb_ptr_set_rptr(udb_ptr* dest, udb_base* udb,
	udb_rel_ptr* p) {
#ifdef UDB_CHECK
	if(p->data) { assert(udb_valid_rptr(udb,
		UDB_SYSTOREL(udb->base, p), p->data)); }
#endif
	udb_ptr_set(dest, udb, p->data);
}

/* Ease of use.  Assign ptr from ptr */
static inline void udb_ptr_set_ptr(udb_ptr* dest, udb_base* udb, udb_ptr* p) {
	udb_ptr_set(dest, udb, p->data);
}

/* Ease of use, zero rptr.  You use this to zero an existing pointer.
 * A new rptr should be rel_ptr_init-ed before it is taken into use. */
static inline void udb_rptr_zero(udb_rel_ptr* dest, udb_base* udb) {
#ifdef UDB_CHECK
	if(dest->data) { assert(udb_valid_rptr(udb,
		UDB_SYSTOREL(udb->base, dest), dest->data)); }
#endif
	udb_rel_ptr_set(udb->base, dest, 0);
}

/* Ease of use, zero ptr */
static inline void udb_ptr_zero(udb_ptr* dest, udb_base* udb) {
	udb_ptr_set(dest, udb, 0);
}

/** ease of use, delete memory pointed at by relptr */
static inline void udb_rel_ptr_free_space(udb_rel_ptr* ptr, udb_base* udb,
	size_t sz) {
	udb_void d = ptr->data;
#ifdef UDB_CHECK
	if(d) { assert(udb_valid_rptr(udb, UDB_SYSTOREL(udb->base, ptr), d)); }
#endif
	udb_rel_ptr_set(udb->base, ptr, 0);
	udb_alloc_free(udb->alloc, d, sz);
}

#endif /* UDB_H */
