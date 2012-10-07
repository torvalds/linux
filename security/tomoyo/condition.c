/*
 * security/tomoyo/condition.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include "common.h"
#include <linux/slab.h>

/* List of "struct tomoyo_condition". */
LIST_HEAD(tomoyo_condition_list);

/**
 * tomoyo_argv - Check argv[] in "struct linux_binbrm".
 *
 * @index:   Index number of @arg_ptr.
 * @arg_ptr: Contents of argv[@index].
 * @argc:    Length of @argv.
 * @argv:    Pointer to "struct tomoyo_argv".
 * @checked: Set to true if @argv[@index] was found.
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_argv(const unsigned int index, const char *arg_ptr,
			const int argc, const struct tomoyo_argv *argv,
			u8 *checked)
{
	int i;
	struct tomoyo_path_info arg;
	arg.name = arg_ptr;
	for (i = 0; i < argc; argv++, checked++, i++) {
		bool result;
		if (index != argv->index)
			continue;
		*checked = 1;
		tomoyo_fill_path_info(&arg);
		result = tomoyo_path_matches_pattern(&arg, argv->value);
		if (argv->is_not)
			result = !result;
		if (!result)
			return false;
	}
	return true;
}

/**
 * tomoyo_envp - Check envp[] in "struct linux_binbrm".
 *
 * @env_name:  The name of environment variable.
 * @env_value: The value of environment variable.
 * @envc:      Length of @envp.
 * @envp:      Pointer to "struct tomoyo_envp".
 * @checked:   Set to true if @envp[@env_name] was found.
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_envp(const char *env_name, const char *env_value,
			const int envc, const struct tomoyo_envp *envp,
			u8 *checked)
{
	int i;
	struct tomoyo_path_info name;
	struct tomoyo_path_info value;
	name.name = env_name;
	tomoyo_fill_path_info(&name);
	value.name = env_value;
	tomoyo_fill_path_info(&value);
	for (i = 0; i < envc; envp++, checked++, i++) {
		bool result;
		if (!tomoyo_path_matches_pattern(&name, envp->name))
			continue;
		*checked = 1;
		if (envp->value) {
			result = tomoyo_path_matches_pattern(&value,
							     envp->value);
			if (envp->is_not)
				result = !result;
		} else {
			result = true;
			if (!envp->is_not)
				result = !result;
		}
		if (!result)
			return false;
	}
	return true;
}

/**
 * tomoyo_scan_bprm - Scan "struct linux_binprm".
 *
 * @ee:   Pointer to "struct tomoyo_execve".
 * @argc: Length of @argc.
 * @argv: Pointer to "struct tomoyo_argv".
 * @envc: Length of @envp.
 * @envp: Poiner to "struct tomoyo_envp".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_scan_bprm(struct tomoyo_execve *ee,
			     const u16 argc, const struct tomoyo_argv *argv,
			     const u16 envc, const struct tomoyo_envp *envp)
{
	struct linux_binprm *bprm = ee->bprm;
	struct tomoyo_page_dump *dump = &ee->dump;
	char *arg_ptr = ee->tmp;
	int arg_len = 0;
	unsigned long pos = bprm->p;
	int offset = pos % PAGE_SIZE;
	int argv_count = bprm->argc;
	int envp_count = bprm->envc;
	bool result = true;
	u8 local_checked[32];
	u8 *checked;
	if (argc + envc <= sizeof(local_checked)) {
		checked = local_checked;
		memset(local_checked, 0, sizeof(local_checked));
	} else {
		checked = kzalloc(argc + envc, GFP_NOFS);
		if (!checked)
			return false;
	}
	while (argv_count || envp_count) {
		if (!tomoyo_dump_page(bprm, pos, dump)) {
			result = false;
			goto out;
		}
		pos += PAGE_SIZE - offset;
		while (offset < PAGE_SIZE) {
			/* Read. */
			const char *kaddr = dump->data;
			const unsigned char c = kaddr[offset++];
			if (c && arg_len < TOMOYO_EXEC_TMPSIZE - 10) {
				if (c == '\\') {
					arg_ptr[arg_len++] = '\\';
					arg_ptr[arg_len++] = '\\';
				} else if (c > ' ' && c < 127) {
					arg_ptr[arg_len++] = c;
				} else {
					arg_ptr[arg_len++] = '\\';
					arg_ptr[arg_len++] = (c >> 6) + '0';
					arg_ptr[arg_len++] =
						((c >> 3) & 7) + '0';
					arg_ptr[arg_len++] = (c & 7) + '0';
				}
			} else {
				arg_ptr[arg_len] = '\0';
			}
			if (c)
				continue;
			/* Check. */
			if (argv_count) {
				if (!tomoyo_argv(bprm->argc - argv_count,
						 arg_ptr, argc, argv,
						 checked)) {
					result = false;
					break;
				}
				argv_count--;
			} else if (envp_count) {
				char *cp = strchr(arg_ptr, '=');
				if (cp) {
					*cp = '\0';
					if (!tomoyo_envp(arg_ptr, cp + 1,
							 envc, envp,
							 checked + argc)) {
						result = false;
						break;
					}
				}
				envp_count--;
			} else {
				break;
			}
			arg_len = 0;
		}
		offset = 0;
		if (!result)
			break;
	}
out:
	if (result) {
		int i;
		/* Check not-yet-checked entries. */
		for (i = 0; i < argc; i++) {
			if (checked[i])
				continue;
			/*
			 * Return true only if all unchecked indexes in
			 * bprm->argv[] are not matched.
			 */
			if (argv[i].is_not)
				continue;
			result = false;
			break;
		}
		for (i = 0; i < envc; envp++, i++) {
			if (checked[argc + i])
				continue;
			/*
			 * Return true only if all unchecked environ variables
			 * in bprm->envp[] are either undefined or not matched.
			 */
			if ((!envp->value && !envp->is_not) ||
			    (envp->value && envp->is_not))
				continue;
			result = false;
			break;
		}
	}
	if (checked != local_checked)
		kfree(checked);
	return result;
}

/**
 * tomoyo_scan_exec_realpath - Check "exec.realpath" parameter of "struct tomoyo_condition".
 *
 * @file:  Pointer to "struct file".
 * @ptr:   Pointer to "struct tomoyo_name_union".
 * @match: True if "exec.realpath=", false if "exec.realpath!=".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_scan_exec_realpath(struct file *file,
				      const struct tomoyo_name_union *ptr,
				      const bool match)
{
	bool result;
	struct tomoyo_path_info exe;
	if (!file)
		return false;
	exe.name = tomoyo_realpath_from_path(&file->f_path);
	if (!exe.name)
		return false;
	tomoyo_fill_path_info(&exe);
	result = tomoyo_compare_name_union(&exe, ptr);
	kfree(exe.name);
	return result == match;
}

/**
 * tomoyo_get_dqword - tomoyo_get_name() for a quoted string.
 *
 * @start: String to save.
 *
 * Returns pointer to "struct tomoyo_path_info" on success, NULL otherwise.
 */
static const struct tomoyo_path_info *tomoyo_get_dqword(char *start)
{
	char *cp = start + strlen(start) - 1;
	if (cp == start || *start++ != '"' || *cp != '"')
		return NULL;
	*cp = '\0';
	if (*start && !tomoyo_correct_word(start))
		return NULL;
	return tomoyo_get_name(start);
}

/**
 * tomoyo_parse_name_union_quoted - Parse a quoted word.
 *
 * @param: Pointer to "struct tomoyo_acl_param".
 * @ptr:   Pointer to "struct tomoyo_name_union".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_parse_name_union_quoted(struct tomoyo_acl_param *param,
					   struct tomoyo_name_union *ptr)
{
	char *filename = param->data;
	if (*filename == '@')
		return tomoyo_parse_name_union(param, ptr);
	ptr->filename = tomoyo_get_dqword(filename);
	return ptr->filename != NULL;
}

/**
 * tomoyo_parse_argv - Parse an argv[] condition part.
 *
 * @left:  Lefthand value.
 * @right: Righthand value.
 * @argv:  Pointer to "struct tomoyo_argv".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_parse_argv(char *left, char *right,
			      struct tomoyo_argv *argv)
{
	if (tomoyo_parse_ulong(&argv->index, &left) !=
	    TOMOYO_VALUE_TYPE_DECIMAL || *left++ != ']' || *left)
		return false;
	argv->value = tomoyo_get_dqword(right);
	return argv->value != NULL;
}

/**
 * tomoyo_parse_envp - Parse an envp[] condition part.
 *
 * @left:  Lefthand value.
 * @right: Righthand value.
 * @envp:  Pointer to "struct tomoyo_envp".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_parse_envp(char *left, char *right,
			      struct tomoyo_envp *envp)
{
	const struct tomoyo_path_info *name;
	const struct tomoyo_path_info *value;
	char *cp = left + strlen(left) - 1;
	if (*cp-- != ']' || *cp != '"')
		goto out;
	*cp = '\0';
	if (!tomoyo_correct_word(left))
		goto out;
	name = tomoyo_get_name(left);
	if (!name)
		goto out;
	if (!strcmp(right, "NULL")) {
		value = NULL;
	} else {
		value = tomoyo_get_dqword(right);
		if (!value) {
			tomoyo_put_name(name);
			goto out;
		}
	}
	envp->name = name;
	envp->value = value;
	return true;
out:
	return false;
}

/**
 * tomoyo_same_condition - Check for duplicated "struct tomoyo_condition" entry.
 *
 * @a: Pointer to "struct tomoyo_condition".
 * @b: Pointer to "struct tomoyo_condition".
 *
 * Returns true if @a == @b, false otherwise.
 */
static inline bool tomoyo_same_condition(const struct tomoyo_condition *a,
					 const struct tomoyo_condition *b)
{
	return a->size == b->size && a->condc == b->condc &&
		a->numbers_count == b->numbers_count &&
		a->names_count == b->names_count &&
		a->argc == b->argc && a->envc == b->envc &&
		a->grant_log == b->grant_log && a->transit == b->transit &&
		!memcmp(a + 1, b + 1, a->size - sizeof(*a));
}

/**
 * tomoyo_condition_type - Get condition type.
 *
 * @word: Keyword string.
 *
 * Returns one of values in "enum tomoyo_conditions_index" on success,
 * TOMOYO_MAX_CONDITION_KEYWORD otherwise.
 */
static u8 tomoyo_condition_type(const char *word)
{
	u8 i;
	for (i = 0; i < TOMOYO_MAX_CONDITION_KEYWORD; i++) {
		if (!strcmp(word, tomoyo_condition_keyword[i]))
			break;
	}
	return i;
}

/* Define this to enable debug mode. */
/* #define DEBUG_CONDITION */

#ifdef DEBUG_CONDITION
#define dprintk printk
#else
#define dprintk(...) do { } while (0)
#endif

/**
 * tomoyo_commit_condition - Commit "struct tomoyo_condition".
 *
 * @entry: Pointer to "struct tomoyo_condition".
 *
 * Returns pointer to "struct tomoyo_condition" on success, NULL otherwise.
 *
 * This function merges duplicated entries. This function returns NULL if
 * @entry is not duplicated but memory quota for policy has exceeded.
 */
static struct tomoyo_condition *tomoyo_commit_condition
(struct tomoyo_condition *entry)
{
	struct tomoyo_condition *ptr;
	bool found = false;
	if (mutex_lock_interruptible(&tomoyo_policy_lock)) {
		dprintk(KERN_WARNING "%u: %s failed\n", __LINE__, __func__);
		ptr = NULL;
		found = true;
		goto out;
	}
	list_for_each_entry(ptr, &tomoyo_condition_list, head.list) {
		if (!tomoyo_same_condition(ptr, entry) ||
		    atomic_read(&ptr->head.users) == TOMOYO_GC_IN_PROGRESS)
			continue;
		/* Same entry found. Share this entry. */
		atomic_inc(&ptr->head.users);
		found = true;
		break;
	}
	if (!found) {
		if (tomoyo_memory_ok(entry)) {
			atomic_set(&entry->head.users, 1);
			list_add(&entry->head.list, &tomoyo_condition_list);
		} else {
			found = true;
			ptr = NULL;
		}
	}
	mutex_unlock(&tomoyo_policy_lock);
out:
	if (found) {
		tomoyo_del_condition(&entry->head.list);
		kfree(entry);
		entry = ptr;
	}
	return entry;
}

/**
 * tomoyo_get_transit_preference - Parse domain transition preference for execve().
 *
 * @param: Pointer to "struct tomoyo_acl_param".
 * @e:     Pointer to "struct tomoyo_condition".
 *
 * Returns the condition string part.
 */
static char *tomoyo_get_transit_preference(struct tomoyo_acl_param *param,
					   struct tomoyo_condition *e)
{
	char * const pos = param->data;
	bool flag;
	if (*pos == '<') {
		e->transit = tomoyo_get_domainname(param);
		goto done;
	}
	{
		char *cp = strchr(pos, ' ');
		if (cp)
			*cp = '\0';
		flag = tomoyo_correct_path(pos) || !strcmp(pos, "keep") ||
			!strcmp(pos, "initialize") || !strcmp(pos, "reset") ||
			!strcmp(pos, "child") || !strcmp(pos, "parent");
		if (cp)
			*cp = ' ';
	}
	if (!flag)
		return pos;
	e->transit = tomoyo_get_name(tomoyo_read_token(param));
done:
	if (e->transit)
		return param->data;
	/*
	 * Return a bad read-only condition string that will let
	 * tomoyo_get_condition() return NULL.
	 */
	return "/";
}

/**
 * tomoyo_get_condition - Parse condition part.
 *
 * @param: Pointer to "struct tomoyo_acl_param".
 *
 * Returns pointer to "struct tomoyo_condition" on success, NULL otherwise.
 */
struct tomoyo_condition *tomoyo_get_condition(struct tomoyo_acl_param *param)
{
	struct tomoyo_condition *entry = NULL;
	struct tomoyo_condition_element *condp = NULL;
	struct tomoyo_number_union *numbers_p = NULL;
	struct tomoyo_name_union *names_p = NULL;
	struct tomoyo_argv *argv = NULL;
	struct tomoyo_envp *envp = NULL;
	struct tomoyo_condition e = { };
	char * const start_of_string =
		tomoyo_get_transit_preference(param, &e);
	char * const end_of_string = start_of_string + strlen(start_of_string);
	char *pos;
rerun:
	pos = start_of_string;
	while (1) {
		u8 left = -1;
		u8 right = -1;
		char *left_word = pos;
		char *cp;
		char *right_word;
		bool is_not;
		if (!*left_word)
			break;
		/*
		 * Since left-hand condition does not allow use of "path_group"
		 * or "number_group" and environment variable's names do not
		 * accept '=', it is guaranteed that the original line consists
		 * of one or more repetition of $left$operator$right blocks
		 * where "$left is free from '=' and ' '" and "$operator is
		 * either '=' or '!='" and "$right is free from ' '".
		 * Therefore, we can reconstruct the original line at the end
		 * of dry run even if we overwrite $operator with '\0'.
		 */
		cp = strchr(pos, ' ');
		if (cp) {
			*cp = '\0'; /* Will restore later. */
			pos = cp + 1;
		} else {
			pos = "";
		}
		right_word = strchr(left_word, '=');
		if (!right_word || right_word == left_word)
			goto out;
		is_not = *(right_word - 1) == '!';
		if (is_not)
			*(right_word++ - 1) = '\0'; /* Will restore later. */
		else if (*(right_word + 1) != '=')
			*right_word++ = '\0'; /* Will restore later. */
		else
			goto out;
		dprintk(KERN_WARNING "%u: <%s>%s=<%s>\n", __LINE__, left_word,
			is_not ? "!" : "", right_word);
		if (!strcmp(left_word, "grant_log")) {
			if (entry) {
				if (is_not ||
				    entry->grant_log != TOMOYO_GRANTLOG_AUTO)
					goto out;
				else if (!strcmp(right_word, "yes"))
					entry->grant_log = TOMOYO_GRANTLOG_YES;
				else if (!strcmp(right_word, "no"))
					entry->grant_log = TOMOYO_GRANTLOG_NO;
				else
					goto out;
			}
			continue;
		}
		if (!strncmp(left_word, "exec.argv[", 10)) {
			if (!argv) {
				e.argc++;
				e.condc++;
			} else {
				e.argc--;
				e.condc--;
				left = TOMOYO_ARGV_ENTRY;
				argv->is_not = is_not;
				if (!tomoyo_parse_argv(left_word + 10,
						       right_word, argv++))
					goto out;
			}
			goto store_value;
		}
		if (!strncmp(left_word, "exec.envp[\"", 11)) {
			if (!envp) {
				e.envc++;
				e.condc++;
			} else {
				e.envc--;
				e.condc--;
				left = TOMOYO_ENVP_ENTRY;
				envp->is_not = is_not;
				if (!tomoyo_parse_envp(left_word + 11,
						       right_word, envp++))
					goto out;
			}
			goto store_value;
		}
		left = tomoyo_condition_type(left_word);
		dprintk(KERN_WARNING "%u: <%s> left=%u\n", __LINE__, left_word,
			left);
		if (left == TOMOYO_MAX_CONDITION_KEYWORD) {
			if (!numbers_p) {
				e.numbers_count++;
			} else {
				e.numbers_count--;
				left = TOMOYO_NUMBER_UNION;
				param->data = left_word;
				if (*left_word == '@' ||
				    !tomoyo_parse_number_union(param,
							       numbers_p++))
					goto out;
			}
		}
		if (!condp)
			e.condc++;
		else
			e.condc--;
		if (left == TOMOYO_EXEC_REALPATH ||
		    left == TOMOYO_SYMLINK_TARGET) {
			if (!names_p) {
				e.names_count++;
			} else {
				e.names_count--;
				right = TOMOYO_NAME_UNION;
				param->data = right_word;
				if (!tomoyo_parse_name_union_quoted(param,
								    names_p++))
					goto out;
			}
			goto store_value;
		}
		right = tomoyo_condition_type(right_word);
		if (right == TOMOYO_MAX_CONDITION_KEYWORD) {
			if (!numbers_p) {
				e.numbers_count++;
			} else {
				e.numbers_count--;
				right = TOMOYO_NUMBER_UNION;
				param->data = right_word;
				if (!tomoyo_parse_number_union(param,
							       numbers_p++))
					goto out;
			}
		}
store_value:
		if (!condp) {
			dprintk(KERN_WARNING "%u: dry_run left=%u right=%u "
				"match=%u\n", __LINE__, left, right, !is_not);
			continue;
		}
		condp->left = left;
		condp->right = right;
		condp->equals = !is_not;
		dprintk(KERN_WARNING "%u: left=%u right=%u match=%u\n",
			__LINE__, condp->left, condp->right,
			condp->equals);
		condp++;
	}
	dprintk(KERN_INFO "%u: cond=%u numbers=%u names=%u ac=%u ec=%u\n",
		__LINE__, e.condc, e.numbers_count, e.names_count, e.argc,
		e.envc);
	if (entry) {
		BUG_ON(e.names_count | e.numbers_count | e.argc | e.envc |
		       e.condc);
		return tomoyo_commit_condition(entry);
	}
	e.size = sizeof(*entry)
		+ e.condc * sizeof(struct tomoyo_condition_element)
		+ e.numbers_count * sizeof(struct tomoyo_number_union)
		+ e.names_count * sizeof(struct tomoyo_name_union)
		+ e.argc * sizeof(struct tomoyo_argv)
		+ e.envc * sizeof(struct tomoyo_envp);
	entry = kzalloc(e.size, GFP_NOFS);
	if (!entry)
		goto out2;
	*entry = e;
	e.transit = NULL;
	condp = (struct tomoyo_condition_element *) (entry + 1);
	numbers_p = (struct tomoyo_number_union *) (condp + e.condc);
	names_p = (struct tomoyo_name_union *) (numbers_p + e.numbers_count);
	argv = (struct tomoyo_argv *) (names_p + e.names_count);
	envp = (struct tomoyo_envp *) (argv + e.argc);
	{
		bool flag = false;
		for (pos = start_of_string; pos < end_of_string; pos++) {
			if (*pos)
				continue;
			if (flag) /* Restore " ". */
				*pos = ' ';
			else if (*(pos + 1) == '=') /* Restore "!=". */
				*pos = '!';
			else /* Restore "=". */
				*pos = '=';
			flag = !flag;
		}
	}
	goto rerun;
out:
	dprintk(KERN_WARNING "%u: %s failed\n", __LINE__, __func__);
	if (entry) {
		tomoyo_del_condition(&entry->head.list);
		kfree(entry);
	}
out2:
	tomoyo_put_name(e.transit);
	return NULL;
}

/**
 * tomoyo_get_attributes - Revalidate "struct inode".
 *
 * @obj: Pointer to "struct tomoyo_obj_info".
 *
 * Returns nothing.
 */
void tomoyo_get_attributes(struct tomoyo_obj_info *obj)
{
	u8 i;
	struct dentry *dentry = NULL;

	for (i = 0; i < TOMOYO_MAX_PATH_STAT; i++) {
		struct inode *inode;
		switch (i) {
		case TOMOYO_PATH1:
			dentry = obj->path1.dentry;
			if (!dentry)
				continue;
			break;
		case TOMOYO_PATH2:
			dentry = obj->path2.dentry;
			if (!dentry)
				continue;
			break;
		default:
			if (!dentry)
				continue;
			dentry = dget_parent(dentry);
			break;
		}
		inode = dentry->d_inode;
		if (inode) {
			struct tomoyo_mini_stat *stat = &obj->stat[i];
			stat->uid  = inode->i_uid;
			stat->gid  = inode->i_gid;
			stat->ino  = inode->i_ino;
			stat->mode = inode->i_mode;
			stat->dev  = inode->i_sb->s_dev;
			stat->rdev = inode->i_rdev;
			obj->stat_valid[i] = true;
		}
		if (i & 1) /* i == TOMOYO_PATH1_PARENT ||
			      i == TOMOYO_PATH2_PARENT */
			dput(dentry);
	}
}

/**
 * tomoyo_condition - Check condition part.
 *
 * @r:    Pointer to "struct tomoyo_request_info".
 * @cond: Pointer to "struct tomoyo_condition". Maybe NULL.
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
bool tomoyo_condition(struct tomoyo_request_info *r,
		      const struct tomoyo_condition *cond)
{
	u32 i;
	unsigned long min_v[2] = { 0, 0 };
	unsigned long max_v[2] = { 0, 0 };
	const struct tomoyo_condition_element *condp;
	const struct tomoyo_number_union *numbers_p;
	const struct tomoyo_name_union *names_p;
	const struct tomoyo_argv *argv;
	const struct tomoyo_envp *envp;
	struct tomoyo_obj_info *obj;
	u16 condc;
	u16 argc;
	u16 envc;
	struct linux_binprm *bprm = NULL;
	if (!cond)
		return true;
	condc = cond->condc;
	argc = cond->argc;
	envc = cond->envc;
	obj = r->obj;
	if (r->ee)
		bprm = r->ee->bprm;
	if (!bprm && (argc || envc))
		return false;
	condp = (struct tomoyo_condition_element *) (cond + 1);
	numbers_p = (const struct tomoyo_number_union *) (condp + condc);
	names_p = (const struct tomoyo_name_union *)
		(numbers_p + cond->numbers_count);
	argv = (const struct tomoyo_argv *) (names_p + cond->names_count);
	envp = (const struct tomoyo_envp *) (argv + argc);
	for (i = 0; i < condc; i++) {
		const bool match = condp->equals;
		const u8 left = condp->left;
		const u8 right = condp->right;
		bool is_bitop[2] = { false, false };
		u8 j;
		condp++;
		/* Check argv[] and envp[] later. */
		if (left == TOMOYO_ARGV_ENTRY || left == TOMOYO_ENVP_ENTRY)
			continue;
		/* Check string expressions. */
		if (right == TOMOYO_NAME_UNION) {
			const struct tomoyo_name_union *ptr = names_p++;
			switch (left) {
				struct tomoyo_path_info *symlink;
				struct tomoyo_execve *ee;
				struct file *file;
			case TOMOYO_SYMLINK_TARGET:
				symlink = obj ? obj->symlink_target : NULL;
				if (!symlink ||
				    !tomoyo_compare_name_union(symlink, ptr)
				    == match)
					goto out;
				break;
			case TOMOYO_EXEC_REALPATH:
				ee = r->ee;
				file = ee ? ee->bprm->file : NULL;
				if (!tomoyo_scan_exec_realpath(file, ptr,
							       match))
					goto out;
				break;
			}
			continue;
		}
		/* Check numeric or bit-op expressions. */
		for (j = 0; j < 2; j++) {
			const u8 index = j ? right : left;
			unsigned long value = 0;
			switch (index) {
			case TOMOYO_TASK_UID:
				value = from_kuid(&init_user_ns, current_uid());
				break;
			case TOMOYO_TASK_EUID:
				value = from_kuid(&init_user_ns, current_euid());
				break;
			case TOMOYO_TASK_SUID:
				value = from_kuid(&init_user_ns, current_suid());
				break;
			case TOMOYO_TASK_FSUID:
				value = from_kuid(&init_user_ns, current_fsuid());
				break;
			case TOMOYO_TASK_GID:
				value = from_kgid(&init_user_ns, current_gid());
				break;
			case TOMOYO_TASK_EGID:
				value = from_kgid(&init_user_ns, current_egid());
				break;
			case TOMOYO_TASK_SGID:
				value = from_kgid(&init_user_ns, current_sgid());
				break;
			case TOMOYO_TASK_FSGID:
				value = from_kgid(&init_user_ns, current_fsgid());
				break;
			case TOMOYO_TASK_PID:
				value = tomoyo_sys_getpid();
				break;
			case TOMOYO_TASK_PPID:
				value = tomoyo_sys_getppid();
				break;
			case TOMOYO_TYPE_IS_SOCKET:
				value = S_IFSOCK;
				break;
			case TOMOYO_TYPE_IS_SYMLINK:
				value = S_IFLNK;
				break;
			case TOMOYO_TYPE_IS_FILE:
				value = S_IFREG;
				break;
			case TOMOYO_TYPE_IS_BLOCK_DEV:
				value = S_IFBLK;
				break;
			case TOMOYO_TYPE_IS_DIRECTORY:
				value = S_IFDIR;
				break;
			case TOMOYO_TYPE_IS_CHAR_DEV:
				value = S_IFCHR;
				break;
			case TOMOYO_TYPE_IS_FIFO:
				value = S_IFIFO;
				break;
			case TOMOYO_MODE_SETUID:
				value = S_ISUID;
				break;
			case TOMOYO_MODE_SETGID:
				value = S_ISGID;
				break;
			case TOMOYO_MODE_STICKY:
				value = S_ISVTX;
				break;
			case TOMOYO_MODE_OWNER_READ:
				value = S_IRUSR;
				break;
			case TOMOYO_MODE_OWNER_WRITE:
				value = S_IWUSR;
				break;
			case TOMOYO_MODE_OWNER_EXECUTE:
				value = S_IXUSR;
				break;
			case TOMOYO_MODE_GROUP_READ:
				value = S_IRGRP;
				break;
			case TOMOYO_MODE_GROUP_WRITE:
				value = S_IWGRP;
				break;
			case TOMOYO_MODE_GROUP_EXECUTE:
				value = S_IXGRP;
				break;
			case TOMOYO_MODE_OTHERS_READ:
				value = S_IROTH;
				break;
			case TOMOYO_MODE_OTHERS_WRITE:
				value = S_IWOTH;
				break;
			case TOMOYO_MODE_OTHERS_EXECUTE:
				value = S_IXOTH;
				break;
			case TOMOYO_EXEC_ARGC:
				if (!bprm)
					goto out;
				value = bprm->argc;
				break;
			case TOMOYO_EXEC_ENVC:
				if (!bprm)
					goto out;
				value = bprm->envc;
				break;
			case TOMOYO_NUMBER_UNION:
				/* Fetch values later. */
				break;
			default:
				if (!obj)
					goto out;
				if (!obj->validate_done) {
					tomoyo_get_attributes(obj);
					obj->validate_done = true;
				}
				{
					u8 stat_index;
					struct tomoyo_mini_stat *stat;
					switch (index) {
					case TOMOYO_PATH1_UID:
					case TOMOYO_PATH1_GID:
					case TOMOYO_PATH1_INO:
					case TOMOYO_PATH1_MAJOR:
					case TOMOYO_PATH1_MINOR:
					case TOMOYO_PATH1_TYPE:
					case TOMOYO_PATH1_DEV_MAJOR:
					case TOMOYO_PATH1_DEV_MINOR:
					case TOMOYO_PATH1_PERM:
						stat_index = TOMOYO_PATH1;
						break;
					case TOMOYO_PATH2_UID:
					case TOMOYO_PATH2_GID:
					case TOMOYO_PATH2_INO:
					case TOMOYO_PATH2_MAJOR:
					case TOMOYO_PATH2_MINOR:
					case TOMOYO_PATH2_TYPE:
					case TOMOYO_PATH2_DEV_MAJOR:
					case TOMOYO_PATH2_DEV_MINOR:
					case TOMOYO_PATH2_PERM:
						stat_index = TOMOYO_PATH2;
						break;
					case TOMOYO_PATH1_PARENT_UID:
					case TOMOYO_PATH1_PARENT_GID:
					case TOMOYO_PATH1_PARENT_INO:
					case TOMOYO_PATH1_PARENT_PERM:
						stat_index =
							TOMOYO_PATH1_PARENT;
						break;
					case TOMOYO_PATH2_PARENT_UID:
					case TOMOYO_PATH2_PARENT_GID:
					case TOMOYO_PATH2_PARENT_INO:
					case TOMOYO_PATH2_PARENT_PERM:
						stat_index =
							TOMOYO_PATH2_PARENT;
						break;
					default:
						goto out;
					}
					if (!obj->stat_valid[stat_index])
						goto out;
					stat = &obj->stat[stat_index];
					switch (index) {
					case TOMOYO_PATH1_UID:
					case TOMOYO_PATH2_UID:
					case TOMOYO_PATH1_PARENT_UID:
					case TOMOYO_PATH2_PARENT_UID:
						value = from_kuid(&init_user_ns, stat->uid);
						break;
					case TOMOYO_PATH1_GID:
					case TOMOYO_PATH2_GID:
					case TOMOYO_PATH1_PARENT_GID:
					case TOMOYO_PATH2_PARENT_GID:
						value = from_kgid(&init_user_ns, stat->gid);
						break;
					case TOMOYO_PATH1_INO:
					case TOMOYO_PATH2_INO:
					case TOMOYO_PATH1_PARENT_INO:
					case TOMOYO_PATH2_PARENT_INO:
						value = stat->ino;
						break;
					case TOMOYO_PATH1_MAJOR:
					case TOMOYO_PATH2_MAJOR:
						value = MAJOR(stat->dev);
						break;
					case TOMOYO_PATH1_MINOR:
					case TOMOYO_PATH2_MINOR:
						value = MINOR(stat->dev);
						break;
					case TOMOYO_PATH1_TYPE:
					case TOMOYO_PATH2_TYPE:
						value = stat->mode & S_IFMT;
						break;
					case TOMOYO_PATH1_DEV_MAJOR:
					case TOMOYO_PATH2_DEV_MAJOR:
						value = MAJOR(stat->rdev);
						break;
					case TOMOYO_PATH1_DEV_MINOR:
					case TOMOYO_PATH2_DEV_MINOR:
						value = MINOR(stat->rdev);
						break;
					case TOMOYO_PATH1_PERM:
					case TOMOYO_PATH2_PERM:
					case TOMOYO_PATH1_PARENT_PERM:
					case TOMOYO_PATH2_PARENT_PERM:
						value = stat->mode & S_IALLUGO;
						break;
					}
				}
				break;
			}
			max_v[j] = value;
			min_v[j] = value;
			switch (index) {
			case TOMOYO_MODE_SETUID:
			case TOMOYO_MODE_SETGID:
			case TOMOYO_MODE_STICKY:
			case TOMOYO_MODE_OWNER_READ:
			case TOMOYO_MODE_OWNER_WRITE:
			case TOMOYO_MODE_OWNER_EXECUTE:
			case TOMOYO_MODE_GROUP_READ:
			case TOMOYO_MODE_GROUP_WRITE:
			case TOMOYO_MODE_GROUP_EXECUTE:
			case TOMOYO_MODE_OTHERS_READ:
			case TOMOYO_MODE_OTHERS_WRITE:
			case TOMOYO_MODE_OTHERS_EXECUTE:
				is_bitop[j] = true;
			}
		}
		if (left == TOMOYO_NUMBER_UNION) {
			/* Fetch values now. */
			const struct tomoyo_number_union *ptr = numbers_p++;
			min_v[0] = ptr->values[0];
			max_v[0] = ptr->values[1];
		}
		if (right == TOMOYO_NUMBER_UNION) {
			/* Fetch values now. */
			const struct tomoyo_number_union *ptr = numbers_p++;
			if (ptr->group) {
				if (tomoyo_number_matches_group(min_v[0],
								max_v[0],
								ptr->group)
				    == match)
					continue;
			} else {
				if ((min_v[0] <= ptr->values[1] &&
				     max_v[0] >= ptr->values[0]) == match)
					continue;
			}
			goto out;
		}
		/*
		 * Bit operation is valid only when counterpart value
		 * represents permission.
		 */
		if (is_bitop[0] && is_bitop[1]) {
			goto out;
		} else if (is_bitop[0]) {
			switch (right) {
			case TOMOYO_PATH1_PERM:
			case TOMOYO_PATH1_PARENT_PERM:
			case TOMOYO_PATH2_PERM:
			case TOMOYO_PATH2_PARENT_PERM:
				if (!(max_v[0] & max_v[1]) == !match)
					continue;
			}
			goto out;
		} else if (is_bitop[1]) {
			switch (left) {
			case TOMOYO_PATH1_PERM:
			case TOMOYO_PATH1_PARENT_PERM:
			case TOMOYO_PATH2_PERM:
			case TOMOYO_PATH2_PARENT_PERM:
				if (!(max_v[0] & max_v[1]) == !match)
					continue;
			}
			goto out;
		}
		/* Normal value range comparison. */
		if ((min_v[0] <= max_v[1] && max_v[0] >= min_v[1]) == match)
			continue;
out:
		return false;
	}
	/* Check argv[] and envp[] now. */
	if (r->ee && (argc || envc))
		return tomoyo_scan_bprm(r->ee, argc, argv, envc, envp);
	return true;
}
