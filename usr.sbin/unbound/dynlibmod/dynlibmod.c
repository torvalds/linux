/**
 * \file
 * This file contains the dynamic library module for Unbound.
 * This loads a dynamic library (.dll, .so) and calls that for the
 * module actions.
 */
#include "config.h"
#include "dynlibmod/dynlibmod.h"
#include "util/module.h"
#include "util/config_file.h"

#if HAVE_WINDOWS_H
#include <windows.h>
#define __DYNMOD HMODULE
#define __DYNSYM FARPROC
#define __LOADSYM GetProcAddress
static void log_dlerror() {
    DWORD dwLastError = GetLastError();
    LPSTR MessageBuffer;
    DWORD dwBufferLength;
    DWORD dwFormatFlags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_IGNORE_INSERTS |
        FORMAT_MESSAGE_FROM_SYSTEM ;
    if((dwBufferLength = FormatMessageA(
        dwFormatFlags,
        NULL, // module to get message from (NULL == system)
        dwLastError,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // default language
        (LPSTR) &MessageBuffer,
        0,
        NULL
        )))
    {
        log_err("dynlibmod: %s (%ld)", MessageBuffer, dwLastError);
        LocalFree(MessageBuffer);
    }

}

static HMODULE open_library(const char* fname) {
    return LoadLibrary(fname);
}

static void close_library(const char* fname, __DYNMOD handle) {
	(void)fname;
	(void)handle;
}
#else
#include <dlfcn.h>
#define __DYNMOD void*
#define __DYNSYM void*
#define __LOADSYM dlsym
static void log_dlerror() {
    log_err("dynlibmod: %s", dlerror());
}

static void* open_library(const char* fname) {
    return dlopen(fname, RTLD_LAZY | RTLD_GLOBAL);
}

static void close_library(const char* fname, __DYNMOD handle) {
	if(!handle) return;
	if(dlclose(handle) != 0) {
		log_err("dlclose %s: %s", fname, strerror(errno));
	}
}
#endif

/** module counter for multiple dynlib modules */
static int dynlib_mod_count = 0;

/** dynlib module init */
int dynlibmod_init(struct module_env* env, int id) {
    int dynlib_mod_idx = dynlib_mod_count++;
    struct config_strlist* cfg_item = env->cfg->dynlib_file;
    struct dynlibmod_env* de = (struct dynlibmod_env*)calloc(1, sizeof(struct dynlibmod_env));
    __DYNMOD dynamic_library;
    int i;
    if (!de)
    {
        log_err("dynlibmod[%d]: malloc failure", dynlib_mod_idx);
        return 0;
    }

    env->modinfo[id] = (void*) de;

    de->fname = NULL;
    for(i = dynlib_mod_idx;
        i != 0 && cfg_item != NULL;
        i--, cfg_item = cfg_item->next) {}

    if (cfg_item == NULL || cfg_item->str == NULL || cfg_item->str[0] == 0) {
        log_err("dynlibmod[%d]: no dynamic library given.", dynlib_mod_idx);
        return 0;
    } else {
        de->fname = cfg_item->str;
    }
    verbose(VERB_ALGO, "dynlibmod[%d]: Trying to load library %s", dynlib_mod_idx, de->fname);
    dynamic_library = open_library(de->fname);
    de->dynamic_library = (void*)dynamic_library;
    if (dynamic_library == NULL) {
        log_dlerror();
        log_err("dynlibmod[%d]: unable to load dynamic library \"%s\".", dynlib_mod_idx, de->fname);
        return 0;
    } else {
	__DYNSYM initializer;
	__DYNSYM deinitializer;
	__DYNSYM operate;
	__DYNSYM inform;
	__DYNSYM clear;
	__DYNSYM get_mem;
        initializer = __LOADSYM(dynamic_library,"init");
        if (initializer == NULL) {
            log_dlerror();
            log_err("dynlibmod[%d]: unable to load init procedure from dynamic library \"%s\".", dynlib_mod_idx, de->fname);
            return 0;
        } else {
            de->func_init = (func_init_t)(void*)initializer;
        }
        deinitializer = __LOADSYM(dynamic_library,"deinit");
        if (deinitializer == NULL) {
            log_dlerror();
            log_err("dynlibmod[%d]: unable to load deinit procedure from dynamic library \"%s\".", dynlib_mod_idx, de->fname);
            return 0;
        } else {
            de->func_deinit = (func_deinit_t)(void*)deinitializer;
        }
        operate = __LOADSYM(dynamic_library,"operate");
        if (operate == NULL) {
            log_dlerror();
            log_err("dynlibmod[%d]: unable to load operate procedure from dynamic library \"%s\".", dynlib_mod_idx, de->fname);
            return 0;
        } else {
            de->func_operate = (func_operate_t)(void*)operate;
        }
        inform = __LOADSYM(dynamic_library,"inform_super");
        if (inform == NULL) {
            log_dlerror();
            log_err("dynlibmod[%d]: unable to load inform_super procedure from dynamic library \"%s\".", dynlib_mod_idx, de->fname);
            return 0;
        } else {
            de->func_inform = (func_inform_t)(void*)inform;
        }
        clear = __LOADSYM(dynamic_library,"clear");
        if (clear == NULL) {
            log_dlerror();
            log_err("dynlibmod[%d]: unable to load clear procedure from dynamic library \"%s\".", dynlib_mod_idx, de->fname);
            return 0;
        } else {
            de->func_clear = (func_clear_t)(void*)clear;
        }
        get_mem = __LOADSYM(dynamic_library,"get_mem");
        if (get_mem == NULL) {
            log_dlerror();
            log_err("dynlibmod[%d]: unable to load get_mem procedure from dynamic library \"%s\".", dynlib_mod_idx, de->fname);
            return 0;
        } else {
            de->func_get_mem = (func_get_mem_t)(void*)get_mem;
        }
    }
    de->inplace_cb_delete_wrapped = &inplace_cb_delete_wrapped;
    de->inplace_cb_register_wrapped = &inplace_cb_register_wrapped;
    return de->func_init(env, id);
}

/** dynlib module deinit */
void dynlibmod_deinit(struct module_env* env, int id) {
    struct dynlibmod_env* de = env->modinfo[id];
    if(de == NULL)
        return;
    de->func_deinit(env, id);
    close_library(de->fname, (__DYNMOD)de->dynamic_library);
    dynlib_mod_count--;
    de->fname = NULL;
    free(de);
}

/** dynlib module operate on a query */
void dynlibmod_operate(struct module_qstate* qstate, enum module_ev event,
    int id, struct outbound_entry* outbound) {
    struct dynlibmod_env* de = qstate->env->modinfo[id];

    de->func_operate(qstate, event, id, outbound);
}

/** dynlib module  */
void dynlibmod_inform_super(struct module_qstate* qstate, int id,
    struct module_qstate* super) {
    struct dynlibmod_env* de = qstate->env->modinfo[id];

    de->func_inform(qstate, id, super);
}

/** dynlib module cleanup query state */
void dynlibmod_clear(struct module_qstate* qstate, int id) {
    struct dynlibmod_env* de = qstate->env->modinfo[id];

    de->func_clear(qstate, id);
}

/** dynlib module alloc size routine */
size_t dynlibmod_get_mem(struct module_env* env, int id) {
    struct dynlibmod_env* de = (struct dynlibmod_env*)env->modinfo[id];
    size_t size;
    verbose(VERB_ALGO, "dynlibmod: get_mem, id: %d, de:%p", id, de);
    if(!de)
        return 0;

    size = de->func_get_mem(env, id);
    return size + sizeof(*de);
}

int dynlib_inplace_cb_reply_generic(struct query_info* qinfo,
    struct module_qstate* qstate, struct reply_info* rep, int rcode,
    struct edns_data* edns, struct edns_option** opt_list_out,
    struct comm_reply* repinfo, struct regional* region,
    struct timeval* start_time, int id, void* callback) {
    struct cb_pair* cb_pair = (struct cb_pair*) callback;
    return ((inplace_cb_reply_func_type*) cb_pair->cb)(qinfo, qstate, rep, rcode, edns, opt_list_out, repinfo, region, start_time, id, cb_pair->cb_arg);
}

int dynlib_inplace_cb_query_generic(struct query_info* qinfo, uint16_t flags,
    struct module_qstate* qstate, struct sockaddr_storage* addr,
    socklen_t addrlen, uint8_t* zone, size_t zonelen, struct regional* region,
    int id, void* callback) {
    struct cb_pair* cb_pair = (struct cb_pair*) callback;
    return ((inplace_cb_query_func_type*) cb_pair->cb)(qinfo, flags, qstate, addr, addrlen, zone, zonelen, region, id, cb_pair->cb_arg);
}

int dynlib_inplace_cb_edns_back_parsed(struct module_qstate* qstate,
    int id, void* cb_args) {
    struct cb_pair* cb_pair = (struct cb_pair*) cb_args;
    return ((inplace_cb_edns_back_parsed_func_type*) cb_pair->cb)(qstate, id, cb_pair->cb_arg);
}

int dynlib_inplace_cb_query_response(struct module_qstate* qstate,
    struct dns_msg* response, int id, void* cb_args) {
    struct cb_pair* cb_pair = (struct cb_pair*) cb_args;
    return ((inplace_cb_query_response_func_type*) cb_pair->cb)(qstate, response, id, cb_pair->cb_arg);
}

int
inplace_cb_register_wrapped(void* cb, enum inplace_cb_list_type type, void* cbarg,
    struct module_env* env, int id) {
    struct cb_pair* cb_pair = malloc(sizeof(struct cb_pair));
    if(cb_pair == NULL) {
	log_err("dynlibmod[%d]: malloc failure", id);
        return 0;
    }
    cb_pair->cb = cb;
    cb_pair->cb_arg = cbarg;
    if(type >= inplace_cb_reply && type <= inplace_cb_reply_servfail) {
        return inplace_cb_register(&dynlib_inplace_cb_reply_generic, type, (void*) cb_pair, env, id);
    } else if(type == inplace_cb_query) {
        return inplace_cb_register(&dynlib_inplace_cb_query_generic, type, (void*) cb_pair, env, id);
    } else if(type == inplace_cb_query_response) {
        return inplace_cb_register(&dynlib_inplace_cb_query_response, type, (void*) cb_pair, env, id);
    } else if(type == inplace_cb_edns_back_parsed) {
        return inplace_cb_register(&dynlib_inplace_cb_edns_back_parsed, type, (void*) cb_pair, env, id);
    } else {
        free(cb_pair);
        return 0;
    }
}

void
inplace_cb_delete_wrapped(struct module_env* env, enum inplace_cb_list_type type,
    int id) {
    struct inplace_cb* temp = env->inplace_cb_lists[type];
    struct inplace_cb* prev = NULL;

    while(temp) {
        if(temp->id == id) {
            if(!prev) {
                env->inplace_cb_lists[type] = temp->next;
                free(temp->cb_arg);
                free(temp);
                temp = env->inplace_cb_lists[type];
            }
            else {
                prev->next = temp->next;
                free(temp->cb_arg);
                free(temp);
                temp = prev->next;
            }
        }
        else {
            prev = temp;
            temp = temp->next;
        }
    }
}


/**
 * The module function block
 */
static struct module_func_block dynlibmod_block = {
   "dynlib",
   NULL, NULL, &dynlibmod_init, &dynlibmod_deinit, &dynlibmod_operate,
   &dynlibmod_inform_super, &dynlibmod_clear, &dynlibmod_get_mem
};

struct module_func_block* dynlibmod_get_funcblock(void)
{
   return &dynlibmod_block;
}
