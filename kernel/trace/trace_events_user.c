// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Microsoft Corporation.
 *
 * Authors:
 *   Beau Belgrave <beaub@linux.microsoft.com>
 */

#include <linux/bitmap.h>
#include <linux/cdev.h>
#include <linux/hashtable.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/uio.h>
#include <linux/ioctl.h>
#include <linux/jhash.h>
#include <linux/refcount.h>
#include <linux/trace_events.h>
#include <linux/tracefs.h>
#include <linux/types.h>
#include <linux/uaccess.h>
/* Reminder to move to uapi when everything works */
#ifdef CONFIG_COMPILE_TEST
#include <linux/user_events.h>
#else
#include <uapi/linux/user_events.h>
#endif
#include "trace.h"
#include "trace_dynevent.h"

#define USER_EVENTS_PREFIX_LEN (sizeof(USER_EVENTS_PREFIX)-1)

#define FIELD_DEPTH_TYPE 0
#define FIELD_DEPTH_NAME 1
#define FIELD_DEPTH_SIZE 2

/*
 * Limits how many trace_event calls user processes can create:
 * Must be a power of two of PAGE_SIZE.
 */
#define MAX_PAGE_ORDER 0
#define MAX_PAGES (1 << MAX_PAGE_ORDER)
#define MAX_BYTES (MAX_PAGES * PAGE_SIZE)
#define MAX_EVENTS (MAX_BYTES * 8)

/* Limit how long of an event name plus args within the subsystem. */
#define MAX_EVENT_DESC 512
#define EVENT_NAME(user_event) ((user_event)->tracepoint.name)
#define MAX_FIELD_ARRAY_SIZE 1024

/*
 * The MAP_STATUS_* macros are used for taking a index and determining the
 * appropriate byte and the bit in the byte to set/reset for an event.
 *
 * The lower 3 bits of the index decide which bit to set.
 * The remaining upper bits of the index decide which byte to use for the bit.
 *
 * This is used when an event has a probe attached/removed to reflect live
 * status of the event wanting tracing or not to user-programs via shared
 * memory maps.
 */
#define MAP_STATUS_BYTE(index) ((index) >> 3)
#define MAP_STATUS_MASK(index) BIT((index) & 7)

/*
 * Internal bits (kernel side only) to keep track of connected probes:
 * These are used when status is requested in text form about an event. These
 * bits are compared against an internal byte on the event to determine which
 * probes to print out to the user.
 *
 * These do not reflect the mapped bytes between the user and kernel space.
 */
#define EVENT_STATUS_FTRACE BIT(0)
#define EVENT_STATUS_PERF BIT(1)
#define EVENT_STATUS_OTHER BIT(7)

/*
 * Stores the pages, tables, and locks for a group of events.
 * Each logical grouping of events has its own group, with a
 * matching page for status checks within user programs. This
 * allows for isolation of events to user programs by various
 * means.
 */
struct user_event_group {
	struct page *pages;
	char *register_page_data;
	char *system_name;
	struct hlist_node node;
	struct mutex reg_mutex;
	DECLARE_HASHTABLE(register_table, 8);
	DECLARE_BITMAP(page_bitmap, MAX_EVENTS);
};

/* Group for init_user_ns mapping, top-most group */
static struct user_event_group *init_group;

/*
 * Stores per-event properties, as users register events
 * within a file a user_event might be created if it does not
 * already exist. These are globally used and their lifetime
 * is tied to the refcnt member. These cannot go away until the
 * refcnt reaches one.
 */
struct user_event {
	struct user_event_group *group;
	struct tracepoint tracepoint;
	struct trace_event_call call;
	struct trace_event_class class;
	struct dyn_event devent;
	struct hlist_node node;
	struct list_head fields;
	struct list_head validators;
	refcount_t refcnt;
	int index;
	int flags;
	int min_size;
	char status;
};

/*
 * Stores per-file events references, as users register events
 * within a file this structure is modified and freed via RCU.
 * The lifetime of this struct is tied to the lifetime of the file.
 * These are not shared and only accessible by the file that created it.
 */
struct user_event_refs {
	struct rcu_head rcu;
	int count;
	struct user_event *events[];
};

struct user_event_file_info {
	struct user_event_group *group;
	struct user_event_refs *refs;
};

#define VALIDATOR_ENSURE_NULL (1 << 0)
#define VALIDATOR_REL (1 << 1)

struct user_event_validator {
	struct list_head link;
	int offset;
	int flags;
};

typedef void (*user_event_func_t) (struct user_event *user, struct iov_iter *i,
				   void *tpdata, bool *faulted);

static int user_event_parse(struct user_event_group *group, char *name,
			    char *args, char *flags,
			    struct user_event **newuser);

static u32 user_event_key(char *name)
{
	return jhash(name, strlen(name), 0);
}

static void set_page_reservations(char *pages, bool set)
{
	int page;

	for (page = 0; page < MAX_PAGES; ++page) {
		void *addr = pages + (PAGE_SIZE * page);

		if (set)
			SetPageReserved(virt_to_page(addr));
		else
			ClearPageReserved(virt_to_page(addr));
	}
}

static void user_event_group_destroy(struct user_event_group *group)
{
	if (group->register_page_data)
		set_page_reservations(group->register_page_data, false);

	if (group->pages)
		__free_pages(group->pages, MAX_PAGE_ORDER);

	kfree(group->system_name);
	kfree(group);
}

static char *user_event_group_system_name(struct user_namespace *user_ns)
{
	char *system_name;
	int len = sizeof(USER_EVENTS_SYSTEM) + 1;

	if (user_ns != &init_user_ns) {
		/*
		 * Unexpected at this point:
		 * We only currently support init_user_ns.
		 * When we enable more, this will trigger a failure so log.
		 */
		pr_warn("user_events: Namespace other than init_user_ns!\n");
		return NULL;
	}

	system_name = kmalloc(len, GFP_KERNEL);

	if (!system_name)
		return NULL;

	snprintf(system_name, len, "%s", USER_EVENTS_SYSTEM);

	return system_name;
}

static inline struct user_event_group
*user_event_group_from_user_ns(struct user_namespace *user_ns)
{
	if (user_ns == &init_user_ns)
		return init_group;

	return NULL;
}

static struct user_event_group *current_user_event_group(void)
{
	struct user_namespace *user_ns = current_user_ns();
	struct user_event_group *group = NULL;

	while (user_ns) {
		group = user_event_group_from_user_ns(user_ns);

		if (group)
			break;

		user_ns = user_ns->parent;
	}

	return group;
}

static struct user_event_group
*user_event_group_create(struct user_namespace *user_ns)
{
	struct user_event_group *group;

	group = kzalloc(sizeof(*group), GFP_KERNEL);

	if (!group)
		return NULL;

	group->system_name = user_event_group_system_name(user_ns);

	if (!group->system_name)
		goto error;

	group->pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, MAX_PAGE_ORDER);

	if (!group->pages)
		goto error;

	group->register_page_data = page_address(group->pages);

	set_page_reservations(group->register_page_data, true);

	/* Zero all bits beside 0 (which is reserved for failures) */
	bitmap_zero(group->page_bitmap, MAX_EVENTS);
	set_bit(0, group->page_bitmap);

	mutex_init(&group->reg_mutex);
	hash_init(group->register_table);

	return group;
error:
	if (group)
		user_event_group_destroy(group);

	return NULL;
};

static __always_inline
void user_event_register_set(struct user_event *user)
{
	int i = user->index;

	user->group->register_page_data[MAP_STATUS_BYTE(i)] |= MAP_STATUS_MASK(i);
}

static __always_inline
void user_event_register_clear(struct user_event *user)
{
	int i = user->index;

	user->group->register_page_data[MAP_STATUS_BYTE(i)] &= ~MAP_STATUS_MASK(i);
}

static __always_inline __must_check
bool user_event_last_ref(struct user_event *user)
{
	return refcount_read(&user->refcnt) == 1;
}

static __always_inline __must_check
size_t copy_nofault(void *addr, size_t bytes, struct iov_iter *i)
{
	size_t ret;

	pagefault_disable();

	ret = copy_from_iter_nocache(addr, bytes, i);

	pagefault_enable();

	return ret;
}

static struct list_head *user_event_get_fields(struct trace_event_call *call)
{
	struct user_event *user = (struct user_event *)call->data;

	return &user->fields;
}

/*
 * Parses a register command for user_events
 * Format: event_name[:FLAG1[,FLAG2...]] [field1[;field2...]]
 *
 * Example event named 'test' with a 20 char 'msg' field with an unsigned int
 * 'id' field after:
 * test char[20] msg;unsigned int id
 *
 * NOTE: Offsets are from the user data perspective, they are not from the
 * trace_entry/buffer perspective. We automatically add the common properties
 * sizes to the offset for the user.
 *
 * Upon success user_event has its ref count increased by 1.
 */
static int user_event_parse_cmd(struct user_event_group *group,
				char *raw_command, struct user_event **newuser)
{
	char *name = raw_command;
	char *args = strpbrk(name, " ");
	char *flags;

	if (args)
		*args++ = '\0';

	flags = strpbrk(name, ":");

	if (flags)
		*flags++ = '\0';

	return user_event_parse(group, name, args, flags, newuser);
}

static int user_field_array_size(const char *type)
{
	const char *start = strchr(type, '[');
	char val[8];
	char *bracket;
	int size = 0;

	if (start == NULL)
		return -EINVAL;

	if (strscpy(val, start + 1, sizeof(val)) <= 0)
		return -EINVAL;

	bracket = strchr(val, ']');

	if (!bracket)
		return -EINVAL;

	*bracket = '\0';

	if (kstrtouint(val, 0, &size))
		return -EINVAL;

	if (size > MAX_FIELD_ARRAY_SIZE)
		return -EINVAL;

	return size;
}

static int user_field_size(const char *type)
{
	/* long is not allowed from a user, since it's ambigious in size */
	if (strcmp(type, "s64") == 0)
		return sizeof(s64);
	if (strcmp(type, "u64") == 0)
		return sizeof(u64);
	if (strcmp(type, "s32") == 0)
		return sizeof(s32);
	if (strcmp(type, "u32") == 0)
		return sizeof(u32);
	if (strcmp(type, "int") == 0)
		return sizeof(int);
	if (strcmp(type, "unsigned int") == 0)
		return sizeof(unsigned int);
	if (strcmp(type, "s16") == 0)
		return sizeof(s16);
	if (strcmp(type, "u16") == 0)
		return sizeof(u16);
	if (strcmp(type, "short") == 0)
		return sizeof(short);
	if (strcmp(type, "unsigned short") == 0)
		return sizeof(unsigned short);
	if (strcmp(type, "s8") == 0)
		return sizeof(s8);
	if (strcmp(type, "u8") == 0)
		return sizeof(u8);
	if (strcmp(type, "char") == 0)
		return sizeof(char);
	if (strcmp(type, "unsigned char") == 0)
		return sizeof(unsigned char);
	if (str_has_prefix(type, "char["))
		return user_field_array_size(type);
	if (str_has_prefix(type, "unsigned char["))
		return user_field_array_size(type);
	if (str_has_prefix(type, "__data_loc "))
		return sizeof(u32);
	if (str_has_prefix(type, "__rel_loc "))
		return sizeof(u32);

	/* Uknown basic type, error */
	return -EINVAL;
}

static void user_event_destroy_validators(struct user_event *user)
{
	struct user_event_validator *validator, *next;
	struct list_head *head = &user->validators;

	list_for_each_entry_safe(validator, next, head, link) {
		list_del(&validator->link);
		kfree(validator);
	}
}

static void user_event_destroy_fields(struct user_event *user)
{
	struct ftrace_event_field *field, *next;
	struct list_head *head = &user->fields;

	list_for_each_entry_safe(field, next, head, link) {
		list_del(&field->link);
		kfree(field);
	}
}

static int user_event_add_field(struct user_event *user, const char *type,
				const char *name, int offset, int size,
				int is_signed, int filter_type)
{
	struct user_event_validator *validator;
	struct ftrace_event_field *field;
	int validator_flags = 0;

	field = kmalloc(sizeof(*field), GFP_KERNEL);

	if (!field)
		return -ENOMEM;

	if (str_has_prefix(type, "__data_loc "))
		goto add_validator;

	if (str_has_prefix(type, "__rel_loc ")) {
		validator_flags |= VALIDATOR_REL;
		goto add_validator;
	}

	goto add_field;

add_validator:
	if (strstr(type, "char") != NULL)
		validator_flags |= VALIDATOR_ENSURE_NULL;

	validator = kmalloc(sizeof(*validator), GFP_KERNEL);

	if (!validator) {
		kfree(field);
		return -ENOMEM;
	}

	validator->flags = validator_flags;
	validator->offset = offset;

	/* Want sequential access when validating */
	list_add_tail(&validator->link, &user->validators);

add_field:
	field->type = type;
	field->name = name;
	field->offset = offset;
	field->size = size;
	field->is_signed = is_signed;
	field->filter_type = filter_type;

	list_add(&field->link, &user->fields);

	/*
	 * Min size from user writes that are required, this does not include
	 * the size of trace_entry (common fields).
	 */
	user->min_size = (offset + size) - sizeof(struct trace_entry);

	return 0;
}

/*
 * Parses the values of a field within the description
 * Format: type name [size]
 */
static int user_event_parse_field(char *field, struct user_event *user,
				  u32 *offset)
{
	char *part, *type, *name;
	u32 depth = 0, saved_offset = *offset;
	int len, size = -EINVAL;
	bool is_struct = false;

	field = skip_spaces(field);

	if (*field == '\0')
		return 0;

	/* Handle types that have a space within */
	len = str_has_prefix(field, "unsigned ");
	if (len)
		goto skip_next;

	len = str_has_prefix(field, "struct ");
	if (len) {
		is_struct = true;
		goto skip_next;
	}

	len = str_has_prefix(field, "__data_loc unsigned ");
	if (len)
		goto skip_next;

	len = str_has_prefix(field, "__data_loc ");
	if (len)
		goto skip_next;

	len = str_has_prefix(field, "__rel_loc unsigned ");
	if (len)
		goto skip_next;

	len = str_has_prefix(field, "__rel_loc ");
	if (len)
		goto skip_next;

	goto parse;
skip_next:
	type = field;
	field = strpbrk(field + len, " ");

	if (field == NULL)
		return -EINVAL;

	*field++ = '\0';
	depth++;
parse:
	name = NULL;

	while ((part = strsep(&field, " ")) != NULL) {
		switch (depth++) {
		case FIELD_DEPTH_TYPE:
			type = part;
			break;
		case FIELD_DEPTH_NAME:
			name = part;
			break;
		case FIELD_DEPTH_SIZE:
			if (!is_struct)
				return -EINVAL;

			if (kstrtou32(part, 10, &size))
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
	}

	if (depth < FIELD_DEPTH_SIZE || !name)
		return -EINVAL;

	if (depth == FIELD_DEPTH_SIZE)
		size = user_field_size(type);

	if (size == 0)
		return -EINVAL;

	if (size < 0)
		return size;

	*offset = saved_offset + size;

	return user_event_add_field(user, type, name, saved_offset, size,
				    type[0] != 'u', FILTER_OTHER);
}

static int user_event_parse_fields(struct user_event *user, char *args)
{
	char *field;
	u32 offset = sizeof(struct trace_entry);
	int ret = -EINVAL;

	if (args == NULL)
		return 0;

	while ((field = strsep(&args, ";")) != NULL) {
		ret = user_event_parse_field(field, user, &offset);

		if (ret)
			break;
	}

	return ret;
}

static struct trace_event_fields user_event_fields_array[1];

static const char *user_field_format(const char *type)
{
	if (strcmp(type, "s64") == 0)
		return "%lld";
	if (strcmp(type, "u64") == 0)
		return "%llu";
	if (strcmp(type, "s32") == 0)
		return "%d";
	if (strcmp(type, "u32") == 0)
		return "%u";
	if (strcmp(type, "int") == 0)
		return "%d";
	if (strcmp(type, "unsigned int") == 0)
		return "%u";
	if (strcmp(type, "s16") == 0)
		return "%d";
	if (strcmp(type, "u16") == 0)
		return "%u";
	if (strcmp(type, "short") == 0)
		return "%d";
	if (strcmp(type, "unsigned short") == 0)
		return "%u";
	if (strcmp(type, "s8") == 0)
		return "%d";
	if (strcmp(type, "u8") == 0)
		return "%u";
	if (strcmp(type, "char") == 0)
		return "%d";
	if (strcmp(type, "unsigned char") == 0)
		return "%u";
	if (strstr(type, "char[") != NULL)
		return "%s";

	/* Unknown, likely struct, allowed treat as 64-bit */
	return "%llu";
}

static bool user_field_is_dyn_string(const char *type, const char **str_func)
{
	if (str_has_prefix(type, "__data_loc ")) {
		*str_func = "__get_str";
		goto check;
	}

	if (str_has_prefix(type, "__rel_loc ")) {
		*str_func = "__get_rel_str";
		goto check;
	}

	return false;
check:
	return strstr(type, "char") != NULL;
}

#define LEN_OR_ZERO (len ? len - pos : 0)
static int user_dyn_field_set_string(int argc, const char **argv, int *iout,
				     char *buf, int len, bool *colon)
{
	int pos = 0, i = *iout;

	*colon = false;

	for (; i < argc; ++i) {
		if (i != *iout)
			pos += snprintf(buf + pos, LEN_OR_ZERO, " ");

		pos += snprintf(buf + pos, LEN_OR_ZERO, "%s", argv[i]);

		if (strchr(argv[i], ';')) {
			++i;
			*colon = true;
			break;
		}
	}

	/* Actual set, advance i */
	if (len != 0)
		*iout = i;

	return pos + 1;
}

static int user_field_set_string(struct ftrace_event_field *field,
				 char *buf, int len, bool colon)
{
	int pos = 0;

	pos += snprintf(buf + pos, LEN_OR_ZERO, "%s", field->type);
	pos += snprintf(buf + pos, LEN_OR_ZERO, " ");
	pos += snprintf(buf + pos, LEN_OR_ZERO, "%s", field->name);

	if (colon)
		pos += snprintf(buf + pos, LEN_OR_ZERO, ";");

	return pos + 1;
}

static int user_event_set_print_fmt(struct user_event *user, char *buf, int len)
{
	struct ftrace_event_field *field, *next;
	struct list_head *head = &user->fields;
	int pos = 0, depth = 0;
	const char *str_func;

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"");

	list_for_each_entry_safe_reverse(field, next, head, link) {
		if (depth != 0)
			pos += snprintf(buf + pos, LEN_OR_ZERO, " ");

		pos += snprintf(buf + pos, LEN_OR_ZERO, "%s=%s",
				field->name, user_field_format(field->type));

		depth++;
	}

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"");

	list_for_each_entry_safe_reverse(field, next, head, link) {
		if (user_field_is_dyn_string(field->type, &str_func))
			pos += snprintf(buf + pos, LEN_OR_ZERO,
					", %s(%s)", str_func, field->name);
		else
			pos += snprintf(buf + pos, LEN_OR_ZERO,
					", REC->%s", field->name);
	}

	return pos + 1;
}
#undef LEN_OR_ZERO

static int user_event_create_print_fmt(struct user_event *user)
{
	char *print_fmt;
	int len;

	len = user_event_set_print_fmt(user, NULL, 0);

	print_fmt = kmalloc(len, GFP_KERNEL);

	if (!print_fmt)
		return -ENOMEM;

	user_event_set_print_fmt(user, print_fmt, len);

	user->call.print_fmt = print_fmt;

	return 0;
}

static enum print_line_t user_event_print_trace(struct trace_iterator *iter,
						int flags,
						struct trace_event *event)
{
	/* Unsafe to try to decode user provided print_fmt, use hex */
	trace_print_hex_dump_seq(&iter->seq, "", DUMP_PREFIX_OFFSET, 16,
				 1, iter->ent, iter->ent_size, true);

	return trace_handle_return(&iter->seq);
}

static struct trace_event_functions user_event_funcs = {
	.trace = user_event_print_trace,
};

static int user_event_set_call_visible(struct user_event *user, bool visible)
{
	int ret;
	const struct cred *old_cred;
	struct cred *cred;

	cred = prepare_creds();

	if (!cred)
		return -ENOMEM;

	/*
	 * While by default tracefs is locked down, systems can be configured
	 * to allow user_event files to be less locked down. The extreme case
	 * being "other" has read/write access to user_events_data/status.
	 *
	 * When not locked down, processes may not have permissions to
	 * add/remove calls themselves to tracefs. We need to temporarily
	 * switch to root file permission to allow for this scenario.
	 */
	cred->fsuid = GLOBAL_ROOT_UID;

	old_cred = override_creds(cred);

	if (visible)
		ret = trace_add_event_call(&user->call);
	else
		ret = trace_remove_event_call(&user->call);

	revert_creds(old_cred);
	put_cred(cred);

	return ret;
}

static int destroy_user_event(struct user_event *user)
{
	int ret = 0;

	/* Must destroy fields before call removal */
	user_event_destroy_fields(user);

	ret = user_event_set_call_visible(user, false);

	if (ret)
		return ret;

	dyn_event_remove(&user->devent);

	user_event_register_clear(user);
	clear_bit(user->index, user->group->page_bitmap);
	hash_del(&user->node);

	user_event_destroy_validators(user);
	kfree(user->call.print_fmt);
	kfree(EVENT_NAME(user));
	kfree(user);

	return ret;
}

static struct user_event *find_user_event(struct user_event_group *group,
					  char *name, u32 *outkey)
{
	struct user_event *user;
	u32 key = user_event_key(name);

	*outkey = key;

	hash_for_each_possible(group->register_table, user, node, key)
		if (!strcmp(EVENT_NAME(user), name)) {
			refcount_inc(&user->refcnt);
			return user;
		}

	return NULL;
}

static int user_event_validate(struct user_event *user, void *data, int len)
{
	struct list_head *head = &user->validators;
	struct user_event_validator *validator;
	void *pos, *end = data + len;
	u32 loc, offset, size;

	list_for_each_entry(validator, head, link) {
		pos = data + validator->offset;

		/* Already done min_size check, no bounds check here */
		loc = *(u32 *)pos;
		offset = loc & 0xffff;
		size = loc >> 16;

		if (likely(validator->flags & VALIDATOR_REL))
			pos += offset + sizeof(loc);
		else
			pos = data + offset;

		pos += size;

		if (unlikely(pos > end))
			return -EFAULT;

		if (likely(validator->flags & VALIDATOR_ENSURE_NULL))
			if (unlikely(*(char *)(pos - 1) != '\0'))
				return -EFAULT;
	}

	return 0;
}

/*
 * Writes the user supplied payload out to a trace file.
 */
static void user_event_ftrace(struct user_event *user, struct iov_iter *i,
			      void *tpdata, bool *faulted)
{
	struct trace_event_file *file;
	struct trace_entry *entry;
	struct trace_event_buffer event_buffer;
	size_t size = sizeof(*entry) + i->count;

	file = (struct trace_event_file *)tpdata;

	if (!file ||
	    !(file->flags & EVENT_FILE_FL_ENABLED) ||
	    trace_trigger_soft_disabled(file))
		return;

	/* Allocates and fills trace_entry, + 1 of this is data payload */
	entry = trace_event_buffer_reserve(&event_buffer, file, size);

	if (unlikely(!entry))
		return;

	if (unlikely(!copy_nofault(entry + 1, i->count, i)))
		goto discard;

	if (!list_empty(&user->validators) &&
	    unlikely(user_event_validate(user, entry, size)))
		goto discard;

	trace_event_buffer_commit(&event_buffer);

	return;
discard:
	*faulted = true;
	__trace_event_discard_commit(event_buffer.buffer,
				     event_buffer.event);
}

#ifdef CONFIG_PERF_EVENTS
/*
 * Writes the user supplied payload out to perf ring buffer.
 */
static void user_event_perf(struct user_event *user, struct iov_iter *i,
			    void *tpdata, bool *faulted)
{
	struct hlist_head *perf_head;

	perf_head = this_cpu_ptr(user->call.perf_events);

	if (perf_head && !hlist_empty(perf_head)) {
		struct trace_entry *perf_entry;
		struct pt_regs *regs;
		size_t size = sizeof(*perf_entry) + i->count;
		int context;

		perf_entry = perf_trace_buf_alloc(ALIGN(size, 8),
						  &regs, &context);

		if (unlikely(!perf_entry))
			return;

		perf_fetch_caller_regs(regs);

		if (unlikely(!copy_nofault(perf_entry + 1, i->count, i)))
			goto discard;

		if (!list_empty(&user->validators) &&
		    unlikely(user_event_validate(user, perf_entry, size)))
			goto discard;

		perf_trace_buf_submit(perf_entry, size, context,
				      user->call.event.type, 1, regs,
				      perf_head, NULL);

		return;
discard:
		*faulted = true;
		perf_swevent_put_recursion_context(context);
	}
}
#endif

/*
 * Update the register page that is shared between user processes.
 */
static void update_reg_page_for(struct user_event *user)
{
	struct tracepoint *tp = &user->tracepoint;
	char status = 0;

	if (atomic_read(&tp->key.enabled) > 0) {
		struct tracepoint_func *probe_func_ptr;
		user_event_func_t probe_func;

		rcu_read_lock_sched();

		probe_func_ptr = rcu_dereference_sched(tp->funcs);

		if (probe_func_ptr) {
			do {
				probe_func = probe_func_ptr->func;

				if (probe_func == user_event_ftrace)
					status |= EVENT_STATUS_FTRACE;
#ifdef CONFIG_PERF_EVENTS
				else if (probe_func == user_event_perf)
					status |= EVENT_STATUS_PERF;
#endif
				else
					status |= EVENT_STATUS_OTHER;
			} while ((++probe_func_ptr)->func);
		}

		rcu_read_unlock_sched();
	}

	if (status)
		user_event_register_set(user);
	else
		user_event_register_clear(user);

	user->status = status;
}

/*
 * Register callback for our events from tracing sub-systems.
 */
static int user_event_reg(struct trace_event_call *call,
			  enum trace_reg type,
			  void *data)
{
	struct user_event *user = (struct user_event *)call->data;
	int ret = 0;

	if (!user)
		return -ENOENT;

	switch (type) {
	case TRACE_REG_REGISTER:
		ret = tracepoint_probe_register(call->tp,
						call->class->probe,
						data);
		if (!ret)
			goto inc;
		break;

	case TRACE_REG_UNREGISTER:
		tracepoint_probe_unregister(call->tp,
					    call->class->probe,
					    data);
		goto dec;

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		ret = tracepoint_probe_register(call->tp,
						call->class->perf_probe,
						data);
		if (!ret)
			goto inc;
		break;

	case TRACE_REG_PERF_UNREGISTER:
		tracepoint_probe_unregister(call->tp,
					    call->class->perf_probe,
					    data);
		goto dec;

	case TRACE_REG_PERF_OPEN:
	case TRACE_REG_PERF_CLOSE:
	case TRACE_REG_PERF_ADD:
	case TRACE_REG_PERF_DEL:
		break;
#endif
	}

	return ret;
inc:
	refcount_inc(&user->refcnt);
	update_reg_page_for(user);
	return 0;
dec:
	update_reg_page_for(user);
	refcount_dec(&user->refcnt);
	return 0;
}

static int user_event_create(const char *raw_command)
{
	struct user_event_group *group;
	struct user_event *user;
	char *name;
	int ret;

	if (!str_has_prefix(raw_command, USER_EVENTS_PREFIX))
		return -ECANCELED;

	raw_command += USER_EVENTS_PREFIX_LEN;
	raw_command = skip_spaces(raw_command);

	name = kstrdup(raw_command, GFP_KERNEL);

	if (!name)
		return -ENOMEM;

	group = current_user_event_group();

	if (!group) {
		kfree(name);
		return -ENOENT;
	}

	mutex_lock(&group->reg_mutex);

	ret = user_event_parse_cmd(group, name, &user);

	if (!ret)
		refcount_dec(&user->refcnt);

	mutex_unlock(&group->reg_mutex);

	if (ret)
		kfree(name);

	return ret;
}

static int user_event_show(struct seq_file *m, struct dyn_event *ev)
{
	struct user_event *user = container_of(ev, struct user_event, devent);
	struct ftrace_event_field *field, *next;
	struct list_head *head;
	int depth = 0;

	seq_printf(m, "%s%s", USER_EVENTS_PREFIX, EVENT_NAME(user));

	head = trace_get_fields(&user->call);

	list_for_each_entry_safe_reverse(field, next, head, link) {
		if (depth == 0)
			seq_puts(m, " ");
		else
			seq_puts(m, "; ");

		seq_printf(m, "%s %s", field->type, field->name);

		if (str_has_prefix(field->type, "struct "))
			seq_printf(m, " %d", field->size);

		depth++;
	}

	seq_puts(m, "\n");

	return 0;
}

static bool user_event_is_busy(struct dyn_event *ev)
{
	struct user_event *user = container_of(ev, struct user_event, devent);

	return !user_event_last_ref(user);
}

static int user_event_free(struct dyn_event *ev)
{
	struct user_event *user = container_of(ev, struct user_event, devent);

	if (!user_event_last_ref(user))
		return -EBUSY;

	return destroy_user_event(user);
}

static bool user_field_match(struct ftrace_event_field *field, int argc,
			     const char **argv, int *iout)
{
	char *field_name = NULL, *dyn_field_name = NULL;
	bool colon = false, match = false;
	int dyn_len, len;

	if (*iout >= argc)
		return false;

	dyn_len = user_dyn_field_set_string(argc, argv, iout, dyn_field_name,
					    0, &colon);

	len = user_field_set_string(field, field_name, 0, colon);

	if (dyn_len != len)
		return false;

	dyn_field_name = kmalloc(dyn_len, GFP_KERNEL);
	field_name = kmalloc(len, GFP_KERNEL);

	if (!dyn_field_name || !field_name)
		goto out;

	user_dyn_field_set_string(argc, argv, iout, dyn_field_name,
				  dyn_len, &colon);

	user_field_set_string(field, field_name, len, colon);

	match = strcmp(dyn_field_name, field_name) == 0;
out:
	kfree(dyn_field_name);
	kfree(field_name);

	return match;
}

static bool user_fields_match(struct user_event *user, int argc,
			      const char **argv)
{
	struct ftrace_event_field *field, *next;
	struct list_head *head = &user->fields;
	int i = 0;

	list_for_each_entry_safe_reverse(field, next, head, link)
		if (!user_field_match(field, argc, argv, &i))
			return false;

	if (i != argc)
		return false;

	return true;
}

static bool user_event_match(const char *system, const char *event,
			     int argc, const char **argv, struct dyn_event *ev)
{
	struct user_event *user = container_of(ev, struct user_event, devent);
	bool match;

	match = strcmp(EVENT_NAME(user), event) == 0 &&
		(!system || strcmp(system, USER_EVENTS_SYSTEM) == 0);

	if (match && argc > 0)
		match = user_fields_match(user, argc, argv);

	return match;
}

static struct dyn_event_operations user_event_dops = {
	.create = user_event_create,
	.show = user_event_show,
	.is_busy = user_event_is_busy,
	.free = user_event_free,
	.match = user_event_match,
};

static int user_event_trace_register(struct user_event *user)
{
	int ret;

	ret = register_trace_event(&user->call.event);

	if (!ret)
		return -ENODEV;

	ret = user_event_set_call_visible(user, true);

	if (ret)
		unregister_trace_event(&user->call.event);

	return ret;
}

/*
 * Parses the event name, arguments and flags then registers if successful.
 * The name buffer lifetime is owned by this method for success cases only.
 * Upon success the returned user_event has its ref count increased by 1.
 */
static int user_event_parse(struct user_event_group *group, char *name,
			    char *args, char *flags,
			    struct user_event **newuser)
{
	int ret;
	int index;
	u32 key;
	struct user_event *user;

	/* Prevent dyn_event from racing */
	mutex_lock(&event_mutex);
	user = find_user_event(group, name, &key);
	mutex_unlock(&event_mutex);

	if (user) {
		*newuser = user;
		/*
		 * Name is allocated by caller, free it since it already exists.
		 * Caller only worries about failure cases for freeing.
		 */
		kfree(name);
		return 0;
	}

	index = find_first_zero_bit(group->page_bitmap, MAX_EVENTS);

	if (index == MAX_EVENTS)
		return -EMFILE;

	user = kzalloc(sizeof(*user), GFP_KERNEL);

	if (!user)
		return -ENOMEM;

	INIT_LIST_HEAD(&user->class.fields);
	INIT_LIST_HEAD(&user->fields);
	INIT_LIST_HEAD(&user->validators);

	user->group = group;
	user->tracepoint.name = name;

	ret = user_event_parse_fields(user, args);

	if (ret)
		goto put_user;

	ret = user_event_create_print_fmt(user);

	if (ret)
		goto put_user;

	user->call.data = user;
	user->call.class = &user->class;
	user->call.name = name;
	user->call.flags = TRACE_EVENT_FL_TRACEPOINT;
	user->call.tp = &user->tracepoint;
	user->call.event.funcs = &user_event_funcs;
	user->class.system = group->system_name;

	user->class.fields_array = user_event_fields_array;
	user->class.get_fields = user_event_get_fields;
	user->class.reg = user_event_reg;
	user->class.probe = user_event_ftrace;
#ifdef CONFIG_PERF_EVENTS
	user->class.perf_probe = user_event_perf;
#endif

	mutex_lock(&event_mutex);

	ret = user_event_trace_register(user);

	if (ret)
		goto put_user_lock;

	user->index = index;

	/* Ensure we track self ref and caller ref (2) */
	refcount_set(&user->refcnt, 2);

	dyn_event_init(&user->devent, &user_event_dops);
	dyn_event_add(&user->devent, &user->call);
	set_bit(user->index, group->page_bitmap);
	hash_add(group->register_table, &user->node, key);

	mutex_unlock(&event_mutex);

	*newuser = user;
	return 0;
put_user_lock:
	mutex_unlock(&event_mutex);
put_user:
	user_event_destroy_fields(user);
	user_event_destroy_validators(user);
	kfree(user);
	return ret;
}

/*
 * Deletes a previously created event if it is no longer being used.
 */
static int delete_user_event(struct user_event_group *group, char *name)
{
	u32 key;
	struct user_event *user = find_user_event(group, name, &key);

	if (!user)
		return -ENOENT;

	refcount_dec(&user->refcnt);

	if (!user_event_last_ref(user))
		return -EBUSY;

	return destroy_user_event(user);
}

/*
 * Validates the user payload and writes via iterator.
 */
static ssize_t user_events_write_core(struct file *file, struct iov_iter *i)
{
	struct user_event_file_info *info = file->private_data;
	struct user_event_refs *refs;
	struct user_event *user = NULL;
	struct tracepoint *tp;
	ssize_t ret = i->count;
	int idx;

	if (unlikely(copy_from_iter(&idx, sizeof(idx), i) != sizeof(idx)))
		return -EFAULT;

	rcu_read_lock_sched();

	refs = rcu_dereference_sched(info->refs);

	/*
	 * The refs->events array is protected by RCU, and new items may be
	 * added. But the user retrieved from indexing into the events array
	 * shall be immutable while the file is opened.
	 */
	if (likely(refs && idx < refs->count))
		user = refs->events[idx];

	rcu_read_unlock_sched();

	if (unlikely(user == NULL))
		return -ENOENT;

	if (unlikely(i->count < user->min_size))
		return -EINVAL;

	tp = &user->tracepoint;

	/*
	 * It's possible key.enabled disables after this check, however
	 * we don't mind if a few events are included in this condition.
	 */
	if (likely(atomic_read(&tp->key.enabled) > 0)) {
		struct tracepoint_func *probe_func_ptr;
		user_event_func_t probe_func;
		struct iov_iter copy;
		void *tpdata;
		bool faulted;

		if (unlikely(fault_in_iov_iter_readable(i, i->count)))
			return -EFAULT;

		faulted = false;

		rcu_read_lock_sched();

		probe_func_ptr = rcu_dereference_sched(tp->funcs);

		if (probe_func_ptr) {
			do {
				copy = *i;
				probe_func = probe_func_ptr->func;
				tpdata = probe_func_ptr->data;
				probe_func(user, &copy, tpdata, &faulted);
			} while ((++probe_func_ptr)->func);
		}

		rcu_read_unlock_sched();

		if (unlikely(faulted))
			return -EFAULT;
	}

	return ret;
}

static int user_events_open(struct inode *node, struct file *file)
{
	struct user_event_group *group;
	struct user_event_file_info *info;

	group = current_user_event_group();

	if (!group)
		return -ENOENT;

	info = kzalloc(sizeof(*info), GFP_KERNEL);

	if (!info)
		return -ENOMEM;

	info->group = group;

	file->private_data = info;

	return 0;
}

static ssize_t user_events_write(struct file *file, const char __user *ubuf,
				 size_t count, loff_t *ppos)
{
	struct iovec iov;
	struct iov_iter i;

	if (unlikely(*ppos != 0))
		return -EFAULT;

	if (unlikely(import_single_range(WRITE, (char __user *)ubuf,
					 count, &iov, &i)))
		return -EFAULT;

	return user_events_write_core(file, &i);
}

static ssize_t user_events_write_iter(struct kiocb *kp, struct iov_iter *i)
{
	return user_events_write_core(kp->ki_filp, i);
}

static int user_events_ref_add(struct user_event_file_info *info,
			       struct user_event *user)
{
	struct user_event_group *group = info->group;
	struct user_event_refs *refs, *new_refs;
	int i, size, count = 0;

	refs = rcu_dereference_protected(info->refs,
					 lockdep_is_held(&group->reg_mutex));

	if (refs) {
		count = refs->count;

		for (i = 0; i < count; ++i)
			if (refs->events[i] == user)
				return i;
	}

	size = struct_size(refs, events, count + 1);

	new_refs = kzalloc(size, GFP_KERNEL);

	if (!new_refs)
		return -ENOMEM;

	new_refs->count = count + 1;

	for (i = 0; i < count; ++i)
		new_refs->events[i] = refs->events[i];

	new_refs->events[i] = user;

	refcount_inc(&user->refcnt);

	rcu_assign_pointer(info->refs, new_refs);

	if (refs)
		kfree_rcu(refs, rcu);

	return i;
}

static long user_reg_get(struct user_reg __user *ureg, struct user_reg *kreg)
{
	u32 size;
	long ret;

	ret = get_user(size, &ureg->size);

	if (ret)
		return ret;

	if (size > PAGE_SIZE)
		return -E2BIG;

	if (size < offsetofend(struct user_reg, write_index))
		return -EINVAL;

	ret = copy_struct_from_user(kreg, sizeof(*kreg), ureg, size);

	if (ret)
		return ret;

	kreg->size = size;

	return 0;
}

/*
 * Registers a user_event on behalf of a user process.
 */
static long user_events_ioctl_reg(struct user_event_file_info *info,
				  unsigned long uarg)
{
	struct user_reg __user *ureg = (struct user_reg __user *)uarg;
	struct user_reg reg;
	struct user_event *user;
	char *name;
	long ret;

	ret = user_reg_get(ureg, &reg);

	if (ret)
		return ret;

	name = strndup_user((const char __user *)(uintptr_t)reg.name_args,
			    MAX_EVENT_DESC);

	if (IS_ERR(name)) {
		ret = PTR_ERR(name);
		return ret;
	}

	ret = user_event_parse_cmd(info->group, name, &user);

	if (ret) {
		kfree(name);
		return ret;
	}

	ret = user_events_ref_add(info, user);

	/* No longer need parse ref, ref_add either worked or not */
	refcount_dec(&user->refcnt);

	/* Positive number is index and valid */
	if (ret < 0)
		return ret;

	put_user((u32)ret, &ureg->write_index);
	put_user(user->index, &ureg->status_bit);

	return 0;
}

/*
 * Deletes a user_event on behalf of a user process.
 */
static long user_events_ioctl_del(struct user_event_file_info *info,
				  unsigned long uarg)
{
	void __user *ubuf = (void __user *)uarg;
	char *name;
	long ret;

	name = strndup_user(ubuf, MAX_EVENT_DESC);

	if (IS_ERR(name))
		return PTR_ERR(name);

	/* event_mutex prevents dyn_event from racing */
	mutex_lock(&event_mutex);
	ret = delete_user_event(info->group, name);
	mutex_unlock(&event_mutex);

	kfree(name);

	return ret;
}

/*
 * Handles the ioctl from user mode to register or alter operations.
 */
static long user_events_ioctl(struct file *file, unsigned int cmd,
			      unsigned long uarg)
{
	struct user_event_file_info *info = file->private_data;
	struct user_event_group *group = info->group;
	long ret = -ENOTTY;

	switch (cmd) {
	case DIAG_IOCSREG:
		mutex_lock(&group->reg_mutex);
		ret = user_events_ioctl_reg(info, uarg);
		mutex_unlock(&group->reg_mutex);
		break;

	case DIAG_IOCSDEL:
		mutex_lock(&group->reg_mutex);
		ret = user_events_ioctl_del(info, uarg);
		mutex_unlock(&group->reg_mutex);
		break;
	}

	return ret;
}

/*
 * Handles the final close of the file from user mode.
 */
static int user_events_release(struct inode *node, struct file *file)
{
	struct user_event_file_info *info = file->private_data;
	struct user_event_group *group;
	struct user_event_refs *refs;
	struct user_event *user;
	int i;

	if (!info)
		return -EINVAL;

	group = info->group;

	/*
	 * Ensure refs cannot change under any situation by taking the
	 * register mutex during the final freeing of the references.
	 */
	mutex_lock(&group->reg_mutex);

	refs = info->refs;

	if (!refs)
		goto out;

	/*
	 * The lifetime of refs has reached an end, it's tied to this file.
	 * The underlying user_events are ref counted, and cannot be freed.
	 * After this decrement, the user_events may be freed elsewhere.
	 */
	for (i = 0; i < refs->count; ++i) {
		user = refs->events[i];

		if (user)
			refcount_dec(&user->refcnt);
	}
out:
	file->private_data = NULL;

	mutex_unlock(&group->reg_mutex);

	kfree(refs);
	kfree(info);

	return 0;
}

static const struct file_operations user_data_fops = {
	.open = user_events_open,
	.write = user_events_write,
	.write_iter = user_events_write_iter,
	.unlocked_ioctl	= user_events_ioctl,
	.release = user_events_release,
};

static struct user_event_group *user_status_group(struct file *file)
{
	struct seq_file *m = file->private_data;

	if (!m)
		return NULL;

	return m->private;
}

/*
 * Maps the shared page into the user process for checking if event is enabled.
 */
static int user_status_mmap(struct file *file, struct vm_area_struct *vma)
{
	char *pages;
	struct user_event_group *group = user_status_group(file);
	unsigned long size = vma->vm_end - vma->vm_start;

	if (size != MAX_BYTES)
		return -EINVAL;

	if (!group)
		return -EINVAL;

	pages = group->register_page_data;

	return remap_pfn_range(vma, vma->vm_start,
			       virt_to_phys(pages) >> PAGE_SHIFT,
			       size, vm_get_page_prot(VM_READ));
}

static void *user_seq_start(struct seq_file *m, loff_t *pos)
{
	if (*pos)
		return NULL;

	return (void *)1;
}

static void *user_seq_next(struct seq_file *m, void *p, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void user_seq_stop(struct seq_file *m, void *p)
{
}

static int user_seq_show(struct seq_file *m, void *p)
{
	struct user_event_group *group = m->private;
	struct user_event *user;
	char status;
	int i, active = 0, busy = 0, flags;

	if (!group)
		return -EINVAL;

	mutex_lock(&group->reg_mutex);

	hash_for_each(group->register_table, i, user, node) {
		status = user->status;
		flags = user->flags;

		seq_printf(m, "%d:%s", user->index, EVENT_NAME(user));

		if (flags != 0 || status != 0)
			seq_puts(m, " #");

		if (status != 0) {
			seq_puts(m, " Used by");
			if (status & EVENT_STATUS_FTRACE)
				seq_puts(m, " ftrace");
			if (status & EVENT_STATUS_PERF)
				seq_puts(m, " perf");
			if (status & EVENT_STATUS_OTHER)
				seq_puts(m, " other");
			busy++;
		}

		seq_puts(m, "\n");
		active++;
	}

	mutex_unlock(&group->reg_mutex);

	seq_puts(m, "\n");
	seq_printf(m, "Active: %d\n", active);
	seq_printf(m, "Busy: %d\n", busy);
	seq_printf(m, "Max: %ld\n", MAX_EVENTS);

	return 0;
}

static const struct seq_operations user_seq_ops = {
	.start = user_seq_start,
	.next  = user_seq_next,
	.stop  = user_seq_stop,
	.show  = user_seq_show,
};

static int user_status_open(struct inode *node, struct file *file)
{
	struct user_event_group *group;
	int ret;

	group = current_user_event_group();

	if (!group)
		return -ENOENT;

	ret = seq_open(file, &user_seq_ops);

	if (!ret) {
		/* Chain group to seq_file */
		struct seq_file *m = file->private_data;

		m->private = group;
	}

	return ret;
}

static const struct file_operations user_status_fops = {
	.open = user_status_open,
	.mmap = user_status_mmap,
	.read = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

/*
 * Creates a set of tracefs files to allow user mode interactions.
 */
static int create_user_tracefs(void)
{
	struct dentry *edata, *emmap;

	edata = tracefs_create_file("user_events_data", TRACE_MODE_WRITE,
				    NULL, NULL, &user_data_fops);

	if (!edata) {
		pr_warn("Could not create tracefs 'user_events_data' entry\n");
		goto err;
	}

	/* mmap with MAP_SHARED requires writable fd */
	emmap = tracefs_create_file("user_events_status", TRACE_MODE_WRITE,
				    NULL, NULL, &user_status_fops);

	if (!emmap) {
		tracefs_remove(edata);
		pr_warn("Could not create tracefs 'user_events_mmap' entry\n");
		goto err;
	}

	return 0;
err:
	return -ENODEV;
}

static int __init trace_events_user_init(void)
{
	int ret;

	init_group = user_event_group_create(&init_user_ns);

	if (!init_group)
		return -ENOMEM;

	ret = create_user_tracefs();

	if (ret) {
		pr_warn("user_events could not register with tracefs\n");
		user_event_group_destroy(init_group);
		init_group = NULL;
		return ret;
	}

	if (dyn_event_register(&user_event_dops))
		pr_warn("user_events could not register with dyn_events\n");

	return 0;
}

fs_initcall(trace_events_user_init);
