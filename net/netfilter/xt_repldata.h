/*
 * Today's hack: quantum tunneling in structs
 *
 * 'entries' and 'term' are never anywhere referenced by word in code. In fact,
 * they serve as the hanging-off data accessed through repl.data[].
 */

#define xt_alloc_initial_table(type, typ2) ({ \
	unsigned int hook_mask = info->valid_hooks; \
	unsigned int nhooks = hweight32(hook_mask); \
	unsigned int bytes = 0, hooknum = 0, i = 0; \
	struct { \
		struct type##_replace repl; \
		struct type##_standard entries[nhooks]; \
		struct type##_error term; \
	} *tbl = kzalloc(sizeof(*tbl), GFP_KERNEL); \
	if (tbl == NULL) \
		return NULL; \
	strncpy(tbl->repl.name, info->name, sizeof(tbl->repl.name)); \
	tbl->term = (struct type##_error)typ2##_ERROR_INIT;  \
	tbl->repl.valid_hooks = hook_mask; \
	tbl->repl.num_entries = nhooks + 1; \
	tbl->repl.size = nhooks * sizeof(struct type##_standard) + \
	                 sizeof(struct type##_error); \
	for (; hook_mask != 0; hook_mask >>= 1, ++hooknum) { \
		if (!(hook_mask & 1)) \
			continue; \
		tbl->repl.hook_entry[hooknum] = bytes; \
		tbl->repl.underflow[hooknum]  = bytes; \
		tbl->entries[i++] = (struct type##_standard) \
			typ2##_STANDARD_INIT(NF_ACCEPT); \
		bytes += sizeof(struct type##_standard); \
	} \
	tbl; \
})
