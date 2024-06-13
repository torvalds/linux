// SPDX-License-Identifier: GPL-2.0
#include <linux/mm_types.h>
#include <linux/tracepoint.h>

#define CREATE_TRACE_POINTS
#include <trace/events/page_ref.h>

void __page_ref_set(struct page *page, int v)
{
	trace_page_ref_set(page, v);
}
EXPORT_SYMBOL(__page_ref_set);
EXPORT_TRACEPOINT_SYMBOL(page_ref_set);

void __page_ref_mod(struct page *page, int v)
{
	trace_page_ref_mod(page, v);
}
EXPORT_SYMBOL(__page_ref_mod);
EXPORT_TRACEPOINT_SYMBOL(page_ref_mod);

void __page_ref_mod_and_test(struct page *page, int v, int ret)
{
	trace_page_ref_mod_and_test(page, v, ret);
}
EXPORT_SYMBOL(__page_ref_mod_and_test);
EXPORT_TRACEPOINT_SYMBOL(page_ref_mod_and_test);

void __page_ref_mod_and_return(struct page *page, int v, int ret)
{
	trace_page_ref_mod_and_return(page, v, ret);
}
EXPORT_SYMBOL(__page_ref_mod_and_return);
EXPORT_TRACEPOINT_SYMBOL(page_ref_mod_and_return);

void __page_ref_mod_unless(struct page *page, int v, int u)
{
	trace_page_ref_mod_unless(page, v, u);
}
EXPORT_SYMBOL(__page_ref_mod_unless);
EXPORT_TRACEPOINT_SYMBOL(page_ref_mod_unless);

void __page_ref_freeze(struct page *page, int v, int ret)
{
	trace_page_ref_freeze(page, v, ret);
}
EXPORT_SYMBOL(__page_ref_freeze);
EXPORT_TRACEPOINT_SYMBOL(page_ref_freeze);

void __page_ref_unfreeze(struct page *page, int v)
{
	trace_page_ref_unfreeze(page, v);
}
EXPORT_SYMBOL(__page_ref_unfreeze);
EXPORT_TRACEPOINT_SYMBOL(page_ref_unfreeze);
