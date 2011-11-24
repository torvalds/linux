/*
 * wpa_supplicant/hostapd / Empty OS specific functions
 * Copyright (c) 2005-2006, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * This file can be used as a starting point when adding a new OS target. The
 * functions here do not really work as-is since they are just empty or only
 * return an error value. os_internal.c can be used as another starting point
 * or reference since it has example implementation of many of these functions.
 */

#include "includes.h"

#include "os.h"

#define UNIMPL() DE_BUG_ON(1, "%s: NOT IMPLEMENTED\n", __func__)

void os_sleep(os_time_t sec, os_time_t usec)
{
	DE_TRACE_STATIC(TR_WPA, "os_sleep() called but NOT IMPLEMENTED");
}


int os_get_time(struct os_time *t)
{
	DriverEnvironment_GetTimestamp_wall(&t->sec, &t->usec);
	
	return 0;
}


int os_mktime(int year, int month, int day, int hour, int min, int sec,
	      os_time_t *t)
{
	int days;
	/* days up to, but not including month n */
	int m[] = { 0, 31, 59, 90, 120, 151,
		    181, 212, 243, 273, 304, 334 };
	
	/* This isn't quite mktime, since it's supposed to take a
	   local time and convert to time_t. But the time we get is
	   not local but UTC, so this works correct. */
	
	/* XXX check parameters more thoroughly */
 	if (year < 1970 || year > 2037 ||
	    month < 1 || month > 12 || 
	    day < 1 || day > 31 ||
 	    hour < 0 || hour > 23 || 
	    min < 0 || min > 59 || 
	    sec < 0 || sec > 60)
 		return -1;
	
	days = 0;
	days += (year - 1970) * 365;
	days += (year - 1969) / 4; /* leap years */
	days += m[month - 1];
	days += day - 1;
	if(month > 2 && year % 4 == 0)
		days += 1;
	*t = days * 86400 + hour * 3600 + min * 60 + sec;
 	
	
	DE_TRACE9(TR_WPA, "%s: %04d-%02d-%02d %02d:%02d:%02d = %lu\n", 
		  __func__, year, month, day, hour, min, sec, *t);
	
	return 0;
}


int os_daemonize(const char *pid_file)
{
	UNIMPL();
	return -1;
}


void os_daemonize_terminate(const char *pid_file)
{
}


int os_get_random(unsigned char *buf, size_t len)
{
	DriverEnvironment_RandomData(buf, len);
	return 0;
}


/* TODO: Replace this dummy code with something more sophisticated. */
unsigned long os_random(void)
{
   unsigned long test = 10;
   driver_tick_t tick = DriverEnvironment_GetTicks();
   test  = (unsigned long)tick;
   return test;
}


char * os_rel2abs_path(const char *rel_path)
{
	return NULL;
}


int os_program_init(void)
{
	UNIMPL();
	return 0;
}


void os_program_deinit(void)
{
	UNIMPL();
}


int os_setenv(const char *name, const char *value, int overwrite)
{
	UNIMPL();
	return -1;
}


int os_unsetenv(const char *name)
{
	UNIMPL();
	return -1;
}


char * os_readfile(const char *name, size_t *len)
{
	UNIMPL();
	return NULL;
}


void * os_zalloc(size_t size)
{
	void *ptr = os_malloc(size);
	if(ptr != NULL)
		memset(ptr, 0, size);
	return ptr;
}


void *os_malloc(size_t size)
{
	size_t n;
	struct _nrx_meminfo *mi;
	if(size == 0) {
		return NULL;
	}
#define BLOCK_SIZE (1 << 6)
	n = (size + sizeof(*mi) + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);
#undef BLOCK_SIZE
	mi = DriverEnvironment_Malloc(n);
	if(mi == NULL)
		return NULL;
	mi->magic1 = 0x4e616e4f;
	mi->magic2 = 0x4f6e614e;
	mi->block_size = n;
	mi->real_size = size;
	return (void*)(mi + 1);
}

void os_free(void *ptr)
{
	struct _nrx_meminfo *mi = ptr;
	if(mi == NULL) {
		return;
	}
	mi--;
	DE_ASSERT(mi->magic1 == 0x4e616e4f);
	DE_ASSERT(mi->magic2 == 0x4f6e614e);
	DE_ASSERT(mi->real_size <= mi->block_size - sizeof(*mi));
	DriverEnvironment_Free(mi);
}

void *os_realloc(void *ptr, size_t len)
{
	struct _nrx_meminfo *mi = ptr;
	void *new;
	if(len == 0) {
		os_free(ptr);
		return NULL;
	}
	if(ptr == NULL) {
		return os_malloc(len);
	}
	mi--;
	DE_ASSERT(mi->magic1 == 0x4e616e4f);
	DE_ASSERT(mi->magic2 == 0x4f6e614e);
	DE_ASSERT(mi->real_size <= mi->block_size - sizeof(*mi));
	if(mi->block_size - sizeof(*mi) >= len) {
		mi->real_size = len;
		return ptr;
	}
	new = os_malloc(len);
	if(new != NULL) {
		os_memcpy(new, ptr, mi->real_size);
		os_free(ptr);
	}
	return new;
}

char *os_strdup(const char *s)
{
   char *ret = os_malloc(strlen(s) + 1);
   if(ret != NULL)
      os_strcpy(ret, s);
   return ret;
}

#ifdef OS_NO_C_LIB_DEFINES
void * os_malloc(size_t size)
{
	return NULL;
}


void * os_realloc(void *ptr, size_t size)
{
	return NULL;
}


void os_free(void *ptr)
{
}


void * os_memcpy(void *dest, const void *src, size_t n)
{
	return dest;
}


void * os_memmove(void *dest, const void *src, size_t n)
{
	return dest;
}


void * os_memset(void *s, int c, size_t n)
{
	return s;
}


int os_memcmp(const void *s1, const void *s2, size_t n)
{
	return 0;
}


char * os_strdup(const char *s)
{
	return NULL;
}


size_t os_strlen(const char *s)
{
	return 0;
}


int os_strcasecmp(const char *s1, const char *s2)
{
	/*
	 * Ignoring case is not required for main functionality, so just use
	 * the case sensitive version of the function.
	 */
	return os_strcmp(s1, s2);
}


int os_strncasecmp(const char *s1, const char *s2, size_t n)
{
	/*
	 * Ignoring case is not required for main functionality, so just use
	 * the case sensitive version of the function.
	 */
	return os_strncmp(s1, s2, n);
}


char * os_strchr(const char *s, int c)
{
	return NULL;
}


char * os_strrchr(const char *s, int c)
{
	return NULL;
}


int os_strcmp(const char *s1, const char *s2)
{
	return 0;
}


int os_strncmp(const char *s1, const char *s2, size_t n)
{
	return 0;
}


char * os_strncpy(char *dest, const char *src, size_t n)
{
	return dest;
}


char * os_strstr(const char *haystack, const char *needle)
{
	return NULL;
}


int os_snprintf(char *str, size_t size, const char *format, ...)
{
	return 0;
}
#endif /* OS_NO_C_LIB_DEFINES */

/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */
