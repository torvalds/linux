/*
 * dynlibmod.h: module header file
 * 
 * Copyright (c) 2019, Peter Munch-Ellingsen (peterme AT peterme.net)
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 *    * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 * 
 *    * Redistributions in binary form must reproduce the above copyright notice,
 *      this list of conditions and the following disclaimer in the documentation
 *      and/or other materials provided with the distribution.
 * 
 *    * Neither the name of the organization nor the names of its
 *      contributors may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * \file
 * Dynamic loading module for unbound.  Loads dynamic library.
 */
#ifndef DYNLIBMOD_H
#define DYNLIBMOD_H
#include "util/module.h"
#include "services/outbound_list.h"

/**
 * Get the module function block.
 * @return: function block with function pointers to module methods.
 */
struct module_func_block* dynlibmod_get_funcblock(void);

/** dynlib module init */
int dynlibmod_init(struct module_env* env, int id);

/** dynlib module deinit */
void dynlibmod_deinit(struct module_env* env, int id);

/** dynlib module operate on a query */
void dynlibmod_operate(struct module_qstate* qstate, enum module_ev event,
	int id, struct outbound_entry* outbound);

/** dynlib module  */
void dynlibmod_inform_super(struct module_qstate* qstate, int id,
	struct module_qstate* super);

/** dynlib module cleanup query state */
void dynlibmod_clear(struct module_qstate* qstate, int id);

/** dynlib module alloc size routine */
size_t dynlibmod_get_mem(struct module_env* env, int id);

int dynlib_inplace_cb_reply_generic(struct query_info* qinfo,
    struct module_qstate* qstate, struct reply_info* rep, int rcode,
    struct edns_data* edns, struct edns_option** opt_list_out,
    struct comm_reply* repinfo, struct regional* region,
	struct timeval* start_time, int id, void* callback);

int dynlib_inplace_cb_query_generic(struct query_info* qinfo, uint16_t flags,
    struct module_qstate* qstate, struct sockaddr_storage* addr,
    socklen_t addrlen, uint8_t* zone, size_t zonelen, struct regional* region,
    int id, void* callback);

int dynlib_inplace_cb_edns_back_parsed(struct module_qstate* qstate,
    int id, void* cb_args);

int dynlib_inplace_cb_query_response(struct module_qstate* qstate,
    struct dns_msg* response, int id, void* cb_args);

int
inplace_cb_register_wrapped(void* cb, enum inplace_cb_list_type type, void* cbarg,
  struct module_env* env, int id);

void
inplace_cb_delete_wrapped(struct module_env* env, enum inplace_cb_list_type type,
  int id);

struct cb_pair {
    void *cb;
    void *cb_arg;
};

/**
 * Global state for the module.
 */

typedef int (*func_init_t)(struct module_env*, int);
typedef void (*func_deinit_t)(struct module_env*, int);
typedef void (*func_operate_t)(struct module_qstate*, enum module_ev, int, struct outbound_entry*);
typedef void (*func_inform_t)(struct module_qstate*, int, struct module_qstate*);
typedef void (*func_clear_t)(struct module_qstate*, int);
typedef size_t (*func_get_mem_t)(struct module_env*, int);
typedef void (*inplace_cb_delete_wrapped_t)(struct module_env*, enum inplace_cb_list_type, int);
typedef int (*inplace_cb_register_wrapped_t)(void*, enum inplace_cb_list_type, void*, struct module_env*, int);


struct dynlibmod_env {
	/** Dynamic library filename. */
	const char* fname;
	/** dynamic library handle */
	void* dynamic_library;
	/** Module init function */
	func_init_t func_init;
	/** Module deinit function */
	func_deinit_t func_deinit;
	/** Module operate function */
	func_operate_t func_operate;
	/** Module super_inform function */
	func_inform_t func_inform;
	/** Module clear function */
	func_clear_t func_clear;
	/** Module get_mem function */
	func_get_mem_t func_get_mem;
  /** Wrapped inplace callback functions to circumvent callback whitelisting */
  inplace_cb_delete_wrapped_t inplace_cb_delete_wrapped;
  inplace_cb_register_wrapped_t inplace_cb_register_wrapped;
  /** Pointer to any data the dynamic library might want to keep */
  void *dyn_env;
};


#endif /* DYNLIBMOD_H */
