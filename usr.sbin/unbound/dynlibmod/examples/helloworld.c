/**
 * \file
 *
 * This is an example to show how dynamic libraries can be made to work with
 * unbound. To build a .so file simply run:
 *   gcc -I../.. -shared -Wall -Werror -fpic  -o helloworld.so helloworld.c
 * And to build for windows, first make unbound with the --with-dynlibmod
 * switch, then use this command:
 *   x86_64-w64-mingw32-gcc -m64 -I../.. -shared -Wall -Werror -fpic
 *      -o helloworld.dll helloworld.c -L../.. -l:libunbound.dll.a
 * to cross-compile a 64-bit Windows DLL.  The libunbound.dll.a is produced
 * by the compile step that makes unbound.exe and allows the dynlib dll to
 * access definitions in unbound.exe.
 */

#include "../../config.h"
#include "../../util/module.h"
#include "../../sldns/parseutil.h"
#include "../dynlibmod.h"

/* Declare the EXPORT macro that expands to exporting the symbol for DLLs when
 * compiling for Windows. All procedures marked with EXPORT in this example are
 * called directly by the dynlib module and must be present for the module to
 * load correctly. */
#ifdef HAVE_WINDOWS_H
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

/* Forward declare a callback, implemented at the bottom of this file */
int reply_callback(struct query_info* qinfo,
    struct module_qstate* qstate, struct reply_info* rep, int rcode,
    struct edns_data* edns, struct edns_option** opt_list_out,
    struct comm_reply* repinfo, struct regional* region,
    struct timeval* start_time, int id, void* callback);

/* Init is called when the module is first loaded. It should be used to set up
 * the environment for this module and do any other initialisation required. */
EXPORT int init(struct module_env* env, int id) {
    log_info("dynlib: hello world from init");
    struct dynlibmod_env* de = (struct dynlibmod_env*) env->modinfo[id];
    de->inplace_cb_register_wrapped(&reply_callback,
                                    inplace_cb_reply,
                                    NULL, env, id);
    struct dynlibmod_env* local_env = env->modinfo[id];
    local_env->dyn_env = NULL;
    return 1;
}

/* Deinit is run as the program is shutting down. It should be used to clean up
 * the environment and any left over data. */
EXPORT void deinit(struct module_env* env, int id) {
    log_info("dynlib: hello world from deinit");
    struct dynlibmod_env* de = (struct dynlibmod_env*) env->modinfo[id];
    de->inplace_cb_delete_wrapped(env, inplace_cb_reply, id);
    if (de->dyn_env != NULL) free(de->dyn_env);
}

/* Operate is called every time a query passes by this module. The event can be
 * used to determine which direction in the module chain it came from. */
EXPORT void operate(struct module_qstate* qstate, enum module_ev event,
                    int id, struct outbound_entry* entry) {
    log_info("dynlib: hello world from operate");
    log_info("dynlib: incoming query: %s %s(%d) %s(%d)",
            qstate->qinfo.qname,
            sldns_lookup_by_id(sldns_rr_classes, qstate->qinfo.qclass)->name,
            qstate->qinfo.qclass,
            sldns_rr_descript(qstate->qinfo.qtype)->_name,
            qstate->qinfo.qtype);
    if (event == module_event_new || event == module_event_pass) {
        qstate->ext_state[id] = module_wait_module;
        struct dynlibmod_env* env = qstate->env->modinfo[id];
        if (env->dyn_env == NULL) {
            env->dyn_env = calloc(3, sizeof(int));
            ((int *)env->dyn_env)[0] = 42;
            ((int *)env->dyn_env)[1] = 102;
            ((int *)env->dyn_env)[2] = 192;
        } else {
            log_err("dynlib: already has data!");
            qstate->ext_state[id] = module_error;
        }
    } else if (event == module_event_moddone) {
        qstate->ext_state[id] = module_finished;
    } else {
        qstate->ext_state[id] = module_error;
    }
}

/* Inform super is called when a query is completed or errors out, but only if
 * a sub-query has been registered to it by this module. Look at
 * mesh_attach_sub in services/mesh.h to see how this is done. */
EXPORT void inform_super(struct module_qstate* qstate, int id,
                         struct module_qstate* super) {
    log_info("dynlib: hello world from inform_super");
}

/* Clear is called once a query is complete and the response has been sent
 * back. It is used to clear up any per-query allocations. */
EXPORT void clear(struct module_qstate* qstate, int id) {
    log_info("dynlib: hello world from clear");
    struct dynlibmod_env* env = qstate->env->modinfo[id];
    if (env->dyn_env != NULL) {
        free(env->dyn_env);
        env->dyn_env = NULL;
    }
}

/* Get mem is called when Unbound is printing performance information. This
 * only happens explicitly and is only used to show memory usage to the user. */
EXPORT size_t get_mem(struct module_env* env, int id) {
    log_info("dynlib: hello world from get_mem");
    return 0;
}

/* The callback that was forward declared earlier. It is registered in the init
 * procedure to run when a query is being replied to. */
int reply_callback(struct query_info* qinfo,
    struct module_qstate* qstate, struct reply_info* rep, int rcode,
    struct edns_data* edns, struct edns_option** opt_list_out,
    struct comm_reply* repinfo, struct regional* region,
    struct timeval* start_time, int id, void* callback) {
    log_info("dynlib: hello world from callback");
    struct dynlibmod_env* env = qstate->env->modinfo[id];
    if (env->dyn_env != NULL) {
        log_info("dynlib: numbers gotten from query: %d, %d, and %d",
            ((int *)env->dyn_env)[0],
            ((int *)env->dyn_env)[1],
            ((int *)env->dyn_env)[2]);
    }
    return 0;
}
