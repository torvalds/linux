#include <linux/mm.h>
#include <linux/mm_event.h>

void mm_event_count(enum mm_event_type event, int count)
{
}
EXPORT_SYMBOL_GPL(mm_event_count);

void mm_event_end(enum mm_event_type event, ktime_t start)
{
}
EXPORT_SYMBOL_GPL(mm_event_end);
