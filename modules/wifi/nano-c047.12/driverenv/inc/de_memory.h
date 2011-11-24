
/*!
  * @file de_memory.h
  *
  * Adaptation layer for memory handling functions
  *
  */

#ifndef DE_MEMORY_H
#define DE_MEMORY_H


/******************************************************************************
T Y P E D E F ' S
******************************************************************************/

/** @defgroup de_memory_api Driver Environment API memory allocation functions
 *  @{
 */

/* REMARK: if all memory is identical, all allocation routines can
 * probably be identical */
/*!
 * \brief Allocate nonpaged memory.
 * 
 * Allocate memory in that is safe to access while holding a lock/trylock
 * as defined by DriverEnvironment_*_lock/trylock. In environments where
 * locking is unnecessary or where thread preemption cannot be disabled
 * this function can call the same memory allocator as DriverEnvironment_Malloc().
 * @param size (int)
 * @return (void *) Allocated buffer.
 */
#define DriverEnvironment_Nonpaged_Malloc(SIZE)         \
	({                                              \
           size_t __s = (SIZE);                         \
           void *__p = kmalloc(__s, GFP_ATOMIC);        \
           MTALLOC(__s, __p, 'N');                      \
	   __p;                                         \
        })
/*! \brief Free nonpaged memory.
 *
 * Free memory allocated with DriverEnvironment_Nonpaged_Malloc().
 * @param buf (void *)
 */
#define DriverEnvironment_Nonpaged_Free(BUF)	\
	({					\
           void *__p = (BUF);			\
           MTFREE(__p, 'N', 1);			\
           kfree(__p);				\
	})
/*!
 * \brief Allocate memory.
 */
#define DriverEnvironment_Malloc(SIZE)                  \
	({                                              \
           size_t __s = (SIZE);                         \
           void *__p = kmalloc(__s, GFP_KERNEL);        \
           MTALLOC(__s, __p, 'R');                      \
	   __p;                                         \
        })
/*!
 * \brief Free memory allocated with DriverEnvironment_Malloc().
 */
#define DriverEnvironment_Free(BUF)		\
	({					\
           void *__p = (BUF);			\
           MTFREE(__p, 'R', 1);			\
           kfree(__p);				\
	})
/*!
 * \brief Allocate memory for data transmission buffers.
 *
 * WiFiEngine uses this function to allocate memory for
 * messages that will be transfered to the device.
 * An example of when this should be separate from the
 * normal memory allocation is if the host system
 * uses DMA transfers to the device, this function
 * could then allocate memory in a DMA-accessible region
 * to avoid copying the message buffers on transmission.
 *
 * @param size       (int) Request size
 * @return (void *) A pointer to the allocated memory region, or NULL on failure.
*/
void* DriverEnvironment__TX_Alloc(int size);
#define DriverEnvironment_TX_Alloc(SIZE)                        \
	({                                                      \
           size_t __s = (SIZE);                                 \
           void *__p = DriverEnvironment__TX_Alloc(__s);        \
           MTALLOC(__s, __p, 'T');                              \
	   __p;                                                 \
        })
/*!
 * \brief Free memory allocated with DriverEnvironment_TX_Alloc().
 * @param buf (void *)
 */
void DriverEnvironment__TX_Free(void *p);
#define DriverEnvironment_TX_Free(BUF)		\
	({					\
           void *__p = (BUF);			\
           MTFREE(__p, 'T', 1);			\
           DriverEnvironment__TX_Free(__p);	\
	})





/** @} */ /* End of de_memory_api group */

#endif /* DE_MEMORY_H */
