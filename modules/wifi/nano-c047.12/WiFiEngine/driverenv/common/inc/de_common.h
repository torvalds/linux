/* $Id $ */
/*!
 * @file de_common.h
 * @brief Common driverenv definitions.
 */

#ifndef DE_COMMON_H
#define DE_COMMON_H

#include "sysdef.h" /* Must be first include */
#include "wifi_engine.h"
#include "wei_tailq.h"
 
#define DRIVERENVIRONMENT_FAILURE_NOT_ALLOWED -3
#define DRIVERENVIRONMENT_FAILURE_DEFER -2
#define DRIVERENVIRONMENT_BUSY -1
#define DRIVERENVIRONMENT_FAILURE 0
#define DRIVERENVIRONMENT_SUCCESS 1
#define DRIVERENVIRONMENT_SUCCESS_TIMEOUT 2

/******************************************************************************
M A C R O ' S
******************************************************************************/

/* c.f mac_api_defs.h */
#define DE_GET_UBE16(_ptr) (((const unsigned char*)(_ptr))[1]		\
                            | (((const unsigned char*)(_ptr))[0] << 8))

/* trace argument conversion macros.
   usage example:
     int x;
     size_t y;
     DE_TRACE_INT(TR_XYZ, "x = %d, y = " TR_FSIZE_T() "\n", 
                  x, TR_ASIZE_T(y));
*/
#ifndef TR_FSIZE_T
#define TR_FSIZE_T "%zu"    /* format string */
#endif
#ifndef TR_ASIZE_T
#define TR_ASIZE_T(N) (N)   /* format argument */
#endif

#ifndef TR_FPTR
#define TR_FPTR "%p"
#endif
#ifndef TR_APTR
#define TR_APTR(N) (N)
#endif


/******************************************************************************
T Y P E D E F ' S
******************************************************************************/
#ifdef packed
#undef packed
#endif
typedef struct
{
   void     *raw;
   int      raw_size;
   char     *packed;
   int      packed_size;  
   int      msg_id;
   int      msg_type;
} hic_message_context_t;

#define HIC_STATIC_MESSAGE -1

/* Note that x = HIC_ALLOCATE_RAW_CONTEXT() actually does the right thing here
 * since the first statement returns the pointer, or NULL. */
#define HIC_ALLOCATE_RAW_CONTEXT(_blob, _context_p, _type)         \
   (_type*)(_context_p->raw = WrapperAllocStructure((Blob_t*)(_blob), (_context_p->raw_size = sizeof(_type))))

#define HIC_GET_RAW_FROM_CONTEXT(C, T) ((T*)(C)->raw)

/* Casting macros for buffer-to-type conversions */
#define AS_UINT8(x) (*((uint8_t *)x))
#define AS_UINT16(x) (*((uint16_t *)x))

/* @brief Evaluates to the number of elements in ARRAY */
#define DE_ARRAY_SIZE(ARRAY) (sizeof(ARRAY) / sizeof((ARRAY)[0]))

#ifdef WIFI_DEBUG_ON
#define DE_TRACE_DO(_level, _task) \
   do { if(TRACE_ENABLED(_level)) _task; } while(0)
#else /* !WIFI_DEBUG_ON */
#define DE_TRACE_DO(_level, _task) {}
#endif /* WIFI_DEBUG_ON */

/* struct de_timer must be declared and contain a member declared as
 * WEI_TQ_ENTRY(de_timer) tq;
 */
struct de_timer;
WEI_TQ_HEAD(timers_head, de_timer);

extern struct timers_head active_timers;

void DriverEnvironment_DisableTimers(void);
void DriverEnvironment_EnableTimers(void);
int __de_stop_timer(struct de_timer *timer);
int __de_start_timer(struct de_timer *timer);

#ifdef WITH_TIMESTAMPS
#define DE_TIMESTAMP(str) DriverEnvironment_DeltaTimestamp(str)
#else
#define DE_TIMESTAMP(str)
#endif


#define DE_HASH_SIZE_SHA1 20 /* 160 bits */
#define DE_HASH_SIZE_MD5  16 /* 128 bits */

#if (DE_ENABLE_HASH_FUNCTIONS == CFG_ON)
int DriverEnvironment_HMAC_MD5(const void *key, size_t key_len,
                               const void *data, size_t data_len,
                               void *result, size_t result_len);
int DriverEnvironment_HMAC_SHA1(const void *key, size_t key_len,
                                const void *data, size_t data_len,
                                void *result, size_t result_len);
#else
#define DriverEnvironment_HMAC_MD5(K, KL, D, DL, R, RL)  (WIFI_ENGINE_FAILURE)
#define DriverEnvironment_HMAC_SHA1(K, KL, D, DL, R, RL) (WIFI_ENGINE_FAILURE)
#endif

#if (DE_ENABLE_FILE_ACCESS == CFG_OFF)

/*!
 * @brief Read data from a previously open file
 *
 * @param file  reference to the file of the opaque type de_file_ref_t
 * @param buf   bufer that will receive the data
 * @param size  maximum size to read
 *
 * @return the number of bytes actualy read
 */
#define de_fread(_file, _buf, _size)   (0)

/*!
 * @brief writes data to a previously open file
 *
 * @param file  reference to the file of the opaque type de_file_ref_t
 * @param buf   bufer that contains the data
 * @param size  size to write
 *
 * @return the number of bytes actualy written
 */
#define de_fwrite(_file, _buf, _size)  (0)

/*!
 * @brief tries to open a file
 *
 * @param name  name and path to the file
 * @param mode  one of, or a combination of: DE_FCREATE, DE_FWRONLY, DE_FRDONLY, DE_FTRUNC, DE_FAPPEND
 *
 * @return   the opaque type de_file_ref_t if successfull or INVALID_FILE_HANDLE on failure
 */
#define de_fopen(_name, _mode)         (NULL)

/*!
 * @brief closing a file (or closes the handle)
 *
 * @param file  reference to the open file of the opaque type de_file_ref_t
 *
 * @return   none
 */
#define de_fclose(file)                (0)

#define DE_FCREATE  0
#define DE_FWRONLY  1
#define DE_FRDONLY  2
#define DE_FTRUNC   4
#define DE_FAPPEND  5

#define INVALID_FILE_HANDLE NULL

typedef void* de_file_ref_t;

#else
#include "de_file.h"
#endif

#endif /* DE_COMMON_H */
