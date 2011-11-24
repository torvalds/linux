
#include "driverenv.h"
#include "we_dump.h"
#include "logger_io.h"

#define CORE_BASE_NAME    "core-%u"
#define LOG_BASE_NAME     "log-%u-%u.%u"

#define CORE_FILE_NAME "" CORE_BASE_NAME
#define LOG_FILE_NAME  "" LOG_BASE_NAME

struct dump_s {
   int objId;
   int errCode;
   int nr;
   de_file_ref_t file;
   int written;
   int expected_size;

   char* cache;
   int cache_off;
};

static struct dump_s dump_state;

static de_file_ref_t prep_new_core_file(struct dump_s *s)
{
    char name[80];
    int i;
    de_file_ref_t file;

    for(i=1 ; i <= DE_COREDUMP_MAX_COUNT ; i++) 
    {
        DE_SNPRINTF(name, sizeof(name), CORE_FILE_NAME, i);
        file = de_fopen(name, DE_FRDONLY );
        if(de_f_is_open(file)) 
        {
            de_fclose(file);
        } 
        else
        { 
            s->nr = i;
            return de_fopen(name, DE_FCREATE|DE_FWRONLY|DE_FTRUNC);
        }
    }
    return NULL;
}

void* core_dump_start(int objId, int errCode, int expected_size)
{
    struct dump_s *s = &dump_state;

    DE_MEMSET(s,0,sizeof(struct dump_s));

    s->objId = objId;
    s->errCode = errCode;
    s->expected_size = expected_size;
    s->written = 0;

    s->file = prep_new_core_file(s);
    if(!de_f_is_open(s->file))
    {
       DE_TRACE_INT2(TR_ALL, "max count of coredump with, objId; %d, errCode %d reatched\n"
            , objId, errCode);
       return NULL;
    }

#if (DE_COREDUMP_CACHE_SIZE > 0)
    s->cache = DriverEnvironment_Malloc(DE_COREDUMP_CACHE_SIZE);
    if(!s->cache)
    {
       return NULL;
    }
#endif

    DE_TRACE_INT3(TR_ALL, "coredump started, objId; %d, errCode %d, nr %d\n",
                     objId, errCode, s->nr);
    return s;
}

int core_dump_append(void *ctx, void *data, size_t len)
{
    struct dump_s *s = (struct dump_s*)ctx;

    if(s==NULL)
    {
       return -1;
    }

    if( s->cache )
	 { 
		 /* Coredump cache must be larger then each packet */
       DE_ASSERT(len < DE_COREDUMP_CACHE_SIZE);
       if(s->cache_off + len >= DE_COREDUMP_CACHE_SIZE)
		 { 
          s->written += de_fwrite(s->file, s->cache, s->cache_off);
          s->cache_off = 0;
       }

       DE_MEMCPY(s->cache + s->cache_off, data, len);
       s->cache_off += len;
    } else { 
       s->written += de_fwrite(s->file, (char*)data, len);
    }

    DE_TRACE_INT2(TR_NOISE, "wrote %d file at %d\n", len, s->written);

    return len;
}

int core_dump_abort(void **ctx)
{
    struct dump_s *s = (struct dump_s*)*ctx;

    if(s==NULL)
    {
       return 0;
    }

    if(s->cache)
    {
       DriverEnvironment_Free(s->cache);
    }

    de_fclose(s->file);

    if(s->written < s->expected_size) 
    {
        DE_TRACE_INT(TR_ALL, "coredump aborted at size %d\n", s->written);
    }
    /* This will complete the coredump */
    /* No more calls to _write|_abort|_complete will be done if ctx==NULL */
    *ctx = NULL;
    return 0;
}

int core_dump_complete(void **ctx)
{
    char name[80];
    struct dump_s *s = (struct dump_s*)*ctx;

    if(s==NULL)
    {
       return 0;
    }

    if( s->cache) 
    { 
       if(s->cache_off > 0) 
       { 
          s->written += de_fwrite(s->file, s->cache, s->cache_off);
       }
       DriverEnvironment_Free(s->cache);
    }

    de_fclose(s->file);

    if(s->written < s->expected_size) 
    {
        DE_TRACE_INT2(TR_ALL, "coredump wrote short, expected %d but found %d\n", 
              s->expected_size, s->written);
    }
    DE_SNPRINTF(name, sizeof(name), LOG_FILE_NAME, s->nr, s->objId, s->errCode);
    log_to_file(name,0);
    
    /* This will complete the coredump */
    /* No more calls to _write|_abort|_complete will be done if ctx==NULL */
    *ctx = NULL;
    return 0;
}

