//------------------------------------------------------------------------------
// <copyright file="abtfilt_utils.c" company="Atheros">
//    Copyright (c) 2008 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

/*
 * Bluetooth Filter utils
 *
 */
static const char athId[] __attribute__ ((unused)) = "$Id: //depot/sw/releases/olca3.1-RC/host/tools/athbtfilter/bluez/abtfilt_utils.c#2 $";

#include "abtfilt_int.h"
#ifdef ANDROID
#include <cutils/log.h>
#endif

#ifdef ANDROID
#define LOG_TAGS "abtfilt"
#define A_LOG_INFO  ANDROID_LOG_INFO
#define A_LOG_ERR   ANDROID_LOG_ERROR
#define A_LOG_DEBUG ANDROID_LOG_DEBUG
#define A_VSYSLOG(level, msg, ap) __android_log_vprint(level, LOG_TAGS, msg, ap);
#define A_SYSLOG(level, msg, args...) __android_log_print(level, LOG_TAGS, msg, ##args);
#else
#define A_LOG_INFO  LOG_INFO
#define A_LOG_ERR   LOG_ERR
#define A_LOG_DEBUG LOG_DEBUG
#define A_VSYSLOG(level, msg, ap) vsyslog(LOG_ERR, msg, ap)
#define A_SYSLOG syslog
#endif

/* Task specific operations */
INLINE A_STATUS
A_TASK_CREATE(A_TASK_HANDLE *handle, void *(*func)(void *), void *arg)
{
    int ret;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    ret = pthread_create(handle, &attr, func, arg);
    if (ret) {
        A_ERR("%s Failed: %d\n", __FUNCTION__, ret);
        return A_ERROR;
    }

    pthread_attr_destroy(&attr);
    return A_OK;
}

INLINE A_STATUS
A_TASK_JOIN(A_TASK_HANDLE *handle)
{
    int ret;

    ret = pthread_join(*handle, NULL);
    if (ret) {
        A_ERR("%s Failed: %d\n", __FUNCTION__, ret);
        return A_ERROR;
    }

    return A_OK;
}

INLINE void
A_TASK_CLEANUP(void)
{
    pthread_exit(NULL);
}

/* Mutual exclusion operations */
INLINE A_STATUS
A_MUTEX_INIT(A_MUTEX_OBJECT *mutex)
{
    int ret;

    ret = pthread_mutex_init(mutex, NULL);
    if (ret) {
        A_ERR("%s Failed: %d\n", __FUNCTION__, ret);
        return A_ERROR;
    }

    return A_OK;
}

INLINE void
A_MUTEX_LOCK(A_MUTEX_OBJECT *mutex)
{
    pthread_mutex_lock(mutex);
}

INLINE void
A_MUTEX_UNLOCK(A_MUTEX_OBJECT *mutex)
{
    pthread_mutex_unlock(mutex);
}

INLINE void
A_MUTEX_DEINIT(A_MUTEX_OBJECT *mutex)
{
    pthread_mutex_destroy(mutex);
}

/* Conditional Variable operations */
INLINE A_STATUS
A_COND_INIT(A_COND_OBJECT *cond)
{
    int ret;

    ret = pthread_cond_init(cond, NULL);
    if (ret) {
        A_ERR("%s Failed: %d\n", __FUNCTION__, ret);
        return A_ERROR;
    }

    return A_OK;
}

INLINE A_STATUS
A_COND_WAIT(A_COND_OBJECT *cond, A_MUTEX_OBJECT *mutex, int timeout)
{
    int ret;
    struct timespec ts;

    if (timeout != WAITFOREVER) {
        /* TODO: support for values equal to or more than a second */
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += (timeout * 1000000);
        ret = pthread_cond_timedwait(cond, mutex, &ts);
    } else {
        ret = pthread_cond_wait(cond, mutex);
    }

    if (ret) {
        A_ERR("%s Failed: %d\n", __FUNCTION__, ret);
        return A_ERROR;
    }

    return A_OK;
}

INLINE void
A_COND_SIGNAL(A_COND_OBJECT *cond)
{
    pthread_cond_signal(cond);
}

INLINE void
A_COND_DEINIT(A_COND_OBJECT *cond)
{
    pthread_cond_destroy(cond);
}

INLINE A_STATUS
A_COND_RESET(A_COND_OBJECT *cond)
{
    A_COND_DEINIT(cond);
    return (A_COND_INIT(cond));
}

/* Debug Infrastructure */
#ifdef ABF_DEBUG
static volatile int debug_enabled = 0;
int dump_console = 0;

void A_DBG_SET_OUTPUT_TO_CONSOLE(void)
{
    dump_console = 1;    
}


INLINE void
A_DBG_INIT(const char *ident, const char *message, ...)
{
    va_list ap;

    openlog(ident, LOG_PID | LOG_NDELAY | LOG_PERROR, LOG_DAEMON);

    va_start(ap, message);
    A_VSYSLOG(A_LOG_INFO, message, ap);
    va_end(ap);
}

static void dump_to_console(const char *format, va_list args)
{
    char buffer[384];
    
    vsprintf(buffer,format,args);
    printf("%s", buffer);    
    
}

INLINE void
A_DEBUG(const char *format, ...)
{
    va_list ap;

    if (!debug_enabled)
        return;

    va_start(ap, format);
    if (dump_console) {
        dump_to_console(format,ap);
    } else {
        A_VSYSLOG(A_LOG_DEBUG, format, ap);
    }
    
    va_end(ap);
}

INLINE void
A_INFO(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    if (dump_console) {
        dump_to_console(format,ap);
    } else {
        A_VSYSLOG(A_LOG_INFO, format, ap);
    }
    va_end(ap);
}

INLINE void
A_ERR(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    if (dump_console) {
        dump_to_console(format,ap);
    } else {
        A_VSYSLOG(A_LOG_ERR, format, ap);
        A_SYSLOG(A_LOG_ERR, "Last Error: %s\n", strerror(errno));
    }
    va_end(ap);
}

INLINE void
A_SET_DEBUG(int enable)
{
    debug_enabled = enable;
}

INLINE void
A_DBG_DEINIT(void)
{
    A_SET_DEBUG(0);
    closelog();
}

void 
A_DUMP_BUFFER(A_UCHAR *buffer, int length, char *pDescription)
{
    A_CHAR    stream[60];
    int       i;
    int       offset, count;

    if (!debug_enabled) {
        return;    
    }
    
    A_DEBUG("<---------Dumping %d Bytes : %s ------>\n", length, pDescription);

    count = 0;
    offset = 0;
    for(i = 0; i < length; i++) {
        sprintf(stream + offset, "%2.2X ", buffer[i]);
        count ++;
        offset += 3;

        if (count == 16) {
            count = 0;
            offset = 0;
            A_DEBUG("[H]: %s\n", stream);
            A_MEMZERO(stream, sizeof(stream));
        }
    }

    if (offset != 0) {
        A_DEBUG("[H]: %s\n", stream);
    }
    
    A_DEBUG("<------------------------------------------------->\n");
}
#endif /* ABF_DEBUG */

INLINE void
A_STR2ADDR(const char *str, A_UINT8 *addr)
{
    const char *ptr = str;
    int i;

    for (i = 0; i < 6; i++) {
        addr[i] = (A_UINT8) strtol(ptr, NULL, 16);
        if (i != 5 && !(ptr = strchr(ptr, ':'))) {
            ptr = ":00:00:00:00:00";
        }
        ptr++;
    }
}
