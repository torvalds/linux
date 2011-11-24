#include <linux/module.h>
#include <linux/kernel.h>
#include <nanoutil.h>

void __cyg_profile_func_enter(void*, void*) 
   __attribute__ ((no_instrument_function));
void __cyg_profile_func_exit(void*, void*) 
   __attribute__ ((no_instrument_function));

void __cyg_profile_func_enter (void *this_fn, void *call_site)
{
   KDEBUG(CALL, "call %p > %p", call_site, this_fn);
} 

void __cyg_profile_func_exit  (void *this_fn, void *call_site)
{
   KDEBUG(CALL, "return %p < %p", call_site, this_fn);
}

