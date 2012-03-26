/*
 * Kernel Debugger Architecture Independent Console I/O handler
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2006 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (c) 2009 Wind River Systems, Inc.  All Rights Reserved.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/nmi.h>
#include <linux/delay.h>
#include <linux/kgdb.h>
#include <linux/kdb.h>
#include <linux/kallsyms.h>
#include "kdb_private.h"

#define CMD_BUFLEN 256
char kdb_prompt_str[CMD_BUFLEN];

int kdb_trap_printk;

static int kgdb_transition_check(char *buffer)
{
	if (buffer[0] != '+' && buffer[0] != '$') {
		KDB_STATE_SET(KGDB_TRANS);
		kdb_printf("%s", buffer);
	} else {
		int slen = strlen(buffer);
		if (slen > 3 && buffer[slen - 3] == '#') {
			kdb_gdb_state_pass(buffer);
			strcpy(buffer, "kgdb");
			KDB_STATE_SET(DOING_KGDB);
			return 1;
		}
	}
	return 0;
}

static int kdb_read_get_key(char *buffer, size_t bufsize)
{
#define ESCAPE_UDELAY 1000
#define ESCAPE_DELAY (2*1000000/ESCAPE_UDELAY) /* 2 seconds worth of udelays */
	char escape_data[5];	/* longest vt100 escape sequence is 4 bytes */
	char *ped = escape_data;
	int escape_delay = 0;
	get_char_func *f, *f_escape = NULL;
	int key;

	for (f = &kdb_poll_funcs[0]; ; ++f) {
		if (*f == NULL) {
			/* Reset NMI watchdog once per poll loop */
			touch_nmi_watchdog();
			f = &kdb_poll_funcs[0];
		}
		if (escape_delay == 2) {
			*ped = '\0';
			ped = escape_data;
			--escape_delay;
		}
		if (escape_delay == 1) {
			key = *ped++;
			if (!*ped)
				--escape_delay;
			break;
		}
		key = (*f)();
		if (key == -1) {
			if (escape_delay) {
				udelay(ESCAPE_UDELAY);
				--escape_delay;
			}
			continue;
		}
		if (bufsize <= 2) {
			if (key == '\r')
				key = '\n';
			*buffer++ = key;
			*buffer = '\0';
			return -1;
		}
		if (escape_delay == 0 && key == '\e') {
			escape_delay = ESCAPE_DELAY;
			ped = escape_data;
			f_escape = f;
		}
		if (escape_delay) {
			*ped++ = key;
			if (f_escape != f) {
				escape_delay = 2;
				continue;
			}
			if (ped - escape_data == 1) {
				/* \e */
				continue;
			} else if (ped - escape_data == 2) {
				/* \e<something> */
				if (key != '[')
					escape_delay = 2;
				continue;
			} else if (ped - escape_data == 3) {
				/* \e[<something> */
				int mapkey = 0;
				switch (key) {
				case 'A': /* \e[A, up arrow */
					mapkey = 16;
					break;
				case 'B': /* \e[B, down arrow */
					mapkey = 14;
					break;
				case 'C': /* \e[C, right arrow */
					mapkey = 6;
					break;
				case 'D': /* \e[D, left arrow */
					mapkey = 2;
					break;
				case '1': /* dropthrough */
				case '3': /* dropthrough */
				/* \e[<1,3,4>], may be home, del, end */
				case '4':
					mapkey = -1;
					break;
				}
				if (mapkey != -1) {
					if (mapkey > 0) {
						escape_data[0] = mapkey;
						escape_data[1] = '\0';
					}
					escape_delay = 2;
				}
				continue;
			} else if (ped - escape_data == 4) {
				/* \e[<1,3,4><something> */
				int mapkey = 0;
				if (key == '~') {
					switch (escape_data[2]) {
					case '1': /* \e[1~, home */
						mapkey = 1;
						break;
					case '3': /* \e[3~, del */
						mapkey = 4;
						break;
					case '4': /* \e[4~, end */
						mapkey = 5;
						break;
					}
				}
				if (mapkey > 0) {
					escape_data[0] = mapkey;
					escape_data[1] = '\0';
				}
				escape_delay = 2;
				continue;
			}
		}
		break;	/* A key to process */
	}
	return key;
}

/*
 * kdb_read
 *
 *	This function reads a string of characters, terminated by
 *	a newline, or by reaching the end of the supplied buffer,
 *	from the current kernel debugger console device.
 * Parameters:
 *	buffer	- Address of character buffer to receive input characters.
 *	bufsize - size, in bytes, of the character buffer
 * Returns:
 *	Returns a pointer to the buffer containing the received
 *	character string.  This string will be terminated by a
 *	newline character.
 * Locking:
 *	No locks are required to be held upon entry to this
 *	function.  It is not reentrant - it relies on the fact
 *	that while kdb is running on only one "master debug" cpu.
 * Remarks:
 *
 * The buffer size must be >= 2.  A buffer size of 2 means that the caller only
 * wants a single key.
 *
 * An escape key could be the start of a vt100 control sequence such as \e[D
 * (left arrow) or it could be a character in its own right.  The standard
 * method for detecting the difference is to wait for 2 seconds to see if there
 * are any other characters.  kdb is complicated by the lack of a timer service
 * (interrupts are off), by multiple input sources and by the need to sometimes
 * return after just one key.  Escape sequence processing has to be done as
 * states in the polling loop.
 */

static char *kdb_read(char *buffer, size_t bufsize)
{
	char *cp = buffer;
	char *bufend = buffer+bufsize-2;	/* Reserve space for newline
						 * and null byte */
	char *lastchar;
	char *p_tmp;
	char tmp;
	static char tmpbuffer[CMD_BUFLEN];
	int len = strlen(buffer);
	int len_tmp;
	int tab = 0;
	int count;
	int i;
	int diag, dtab_count;
	int key;


	diag = kdbgetintenv("DTABCOUNT", &dtab_count);
	if (diag)
		dtab_count = 30;

	if (len > 0) {
		cp += len;
		if (*(buffer+len-1) == '\n')
			cp--;
	}

	lastchar = cp;
	*cp = '\0';
	kdb_printf("%s", buffer);
poll_again:
	key = kdb_read_get_key(buffer, bufsize);
	if (key == -1)
		return buffer;
	if (key != 9)
		tab = 0;
	switch (key) {
	case 8: /* backspace */
		if (cp > buffer) {
			if (cp < lastchar) {
				memcpy(tmpbuffer, cp, lastchar - cp);
				memcpy(cp-1, tmpbuffer, lastchar - cp);
			}
			*(--lastchar) = '\0';
			--cp;
			kdb_printf("\b%s \r", cp);
			tmp = *cp;
			*cp = '\0';
			kdb_printf(kdb_prompt_str);
			kdb_printf("%s", buffer);
			*cp = tmp;
		}
		break;
	case 13: /* enter */
		*lastchar++ = '\n';
		*lastchar++ = '\0';
		if (!KDB_STATE(KGDB_TRANS)) {
			KDB_STATE_SET(KGDB_TRANS);
			kdb_printf("%s", buffer);
		}
		kdb_printf("\n");
		return buffer;
	case 4: /* Del */
		if (cp < lastchar) {
			memcpy(tmpbuffer, cp+1, lastchar - cp - 1);
			memcpy(cp, tmpbuffer, lastchar - cp - 1);
			*(--lastchar) = '\0';
			kdb_printf("%s \r", cp);
			tmp = *cp;
			*cp = '\0';
			kdb_printf(kdb_prompt_str);
			kdb_printf("%s", buffer);
			*cp = tmp;
		}
		break;
	case 1: /* Home */
		if (cp > buffer) {
			kdb_printf("\r");
			kdb_printf(kdb_prompt_str);
			cp = buffer;
		}
		break;
	case 5: /* End */
		if (cp < lastchar) {
			kdb_printf("%s", cp);
			cp = lastchar;
		}
		break;
	case 2: /* Left */
		if (cp > buffer) {
			kdb_printf("\b");
			--cp;
		}
		break;
	case 14: /* Down */
		memset(tmpbuffer, ' ',
		       strlen(kdb_prompt_str) + (lastchar-buffer));
		*(tmpbuffer+strlen(kdb_prompt_str) +
		  (lastchar-buffer)) = '\0';
		kdb_printf("\r%s\r", tmpbuffer);
		*lastchar = (char)key;
		*(lastchar+1) = '\0';
		return lastchar;
	case 6: /* Right */
		if (cp < lastchar) {
			kdb_printf("%c", *cp);
			++cp;
		}
		break;
	case 16: /* Up */
		memset(tmpbuffer, ' ',
		       strlen(kdb_prompt_str) + (lastchar-buffer));
		*(tmpbuffer+strlen(kdb_prompt_str) +
		  (lastchar-buffer)) = '\0';
		kdb_printf("\r%s\r", tmpbuffer);
		*lastchar = (char)key;
		*(lastchar+1) = '\0';
		return lastchar;
	case 9: /* Tab */
		if (tab < 2)
			++tab;
		p_tmp = buffer;
		while (*p_tmp == ' ')
			p_tmp++;
		if (p_tmp > cp)
			break;
		memcpy(tmpbuffer, p_tmp, cp-p_tmp);
		*(tmpbuffer + (cp-p_tmp)) = '\0';
		p_tmp = strrchr(tmpbuffer, ' ');
		if (p_tmp)
			++p_tmp;
		else
			p_tmp = tmpbuffer;
		len = strlen(p_tmp);
		count = kallsyms_symbol_complete(p_tmp,
						 sizeof(tmpbuffer) -
						 (p_tmp - tmpbuffer));
		if (tab == 2 && count > 0) {
			kdb_printf("\n%d symbols are found.", count);
			if (count > dtab_count) {
				count = dtab_count;
				kdb_printf(" But only first %d symbols will"
					   " be printed.\nYou can change the"
					   " environment variable DTABCOUNT.",
					   count);
			}
			kdb_printf("\n");
			for (i = 0; i < count; i++) {
				if (kallsyms_symbol_next(p_tmp, i) < 0)
					break;
				kdb_printf("%s ", p_tmp);
				*(p_tmp + len) = '\0';
			}
			if (i >= dtab_count)
				kdb_printf("...");
			kdb_printf("\n");
			kdb_printf(kdb_prompt_str);
			kdb_printf("%s", buffer);
		} else if (tab != 2 && count > 0) {
			len_tmp = strlen(p_tmp);
			strncpy(p_tmp+len_tmp, cp, lastchar-cp+1);
			len_tmp = strlen(p_tmp);
			strncpy(cp, p_tmp+len, len_tmp-len + 1);
			len = len_tmp - len;
			kdb_printf("%s", cp);
			cp += len;
			lastchar += len;
		}
		kdb_nextline = 1; /* reset output line number */
		break;
	default:
		if (key >= 32 && lastchar < bufend) {
			if (cp < lastchar) {
				memcpy(tmpbuffer, cp, lastchar - cp);
				memcpy(cp+1, tmpbuffer, lastchar - cp);
				*++lastchar = '\0';
				*cp = key;
				kdb_printf("%s\r", cp);
				++cp;
				tmp = *cp;
				*cp = '\0';
				kdb_printf(kdb_prompt_str);
				kdb_printf("%s", buffer);
				*cp = tmp;
			} else {
				*++lastchar = '\0';
				*cp++ = key;
				/* The kgdb transition check will hide
				 * printed characters if we think that
				 * kgdb is connecting, until the check
				 * fails */
				if (!KDB_STATE(KGDB_TRANS)) {
					if (kgdb_transition_check(buffer))
						return buffer;
				} else {
					kdb_printf("%c", key);
				}
			}
			/* Special escape to kgdb */
			if (lastchar - buffer >= 5 &&
			    strcmp(lastchar - 5, "$?#3f") == 0) {
				kdb_gdb_state_pass(lastchar - 5);
				strcpy(buffer, "kgdb");
				KDB_STATE_SET(DOING_KGDB);
				return buffer;
			}
			if (lastchar - buffer >= 11 &&
			    strcmp(lastchar - 11, "$qSupported") == 0) {
				kdb_gdb_state_pass(lastchar - 11);
				strcpy(buffer, "kgdb");
				KDB_STATE_SET(DOING_KGDB);
				return buffer;
			}
		}
		break;
	}
	goto poll_again;
}

/*
 * kdb_getstr
 *
 *	Print the prompt string and read a command from the
 *	input device.
 *
 * Parameters:
 *	buffer	Address of buffer to receive command
 *	bufsize Size of buffer in bytes
 *	prompt	Pointer to string to use as prompt string
 * Returns:
 *	Pointer to command buffer.
 * Locking:
 *	None.
 * Remarks:
 *	For SMP kernels, the processor number will be
 *	substituted for %d, %x or %o in the prompt.
 */

char *kdb_getstr(char *buffer, size_t bufsize, char *prompt)
{
	if (prompt && kdb_prompt_str != prompt)
		strncpy(kdb_prompt_str, prompt, CMD_BUFLEN);
	kdb_printf(kdb_prompt_str);
	kdb_nextline = 1;	/* Prompt and input resets line number */
	return kdb_read(buffer, bufsize);
}

/*
 * kdb_input_flush
 *
 *	Get rid of any buffered console input.
 *
 * Parameters:
 *	none
 * Returns:
 *	nothing
 * Locking:
 *	none
 * Remarks:
 *	Call this function whenever you want to flush input.  If there is any
 *	outstanding input, it ignores all characters until there has been no
 *	data for approximately 1ms.
 */

static void kdb_input_flush(void)
{
	get_char_func *f;
	int res;
	int flush_delay = 1;
	while (flush_delay) {
		flush_delay--;
empty:
		touch_nmi_watchdog();
		for (f = &kdb_poll_funcs[0]; *f; ++f) {
			res = (*f)();
			if (res != -1) {
				flush_delay = 1;
				goto empty;
			}
		}
		if (flush_delay)
			mdelay(1);
	}
}

/*
 * kdb_printf
 *
 *	Print a string to the output device(s).
 *
 * Parameters:
 *	printf-like format and optional args.
 * Returns:
 *	0
 * Locking:
 *	None.
 * Remarks:
 *	use 'kdbcons->write()' to avoid polluting 'log_buf' with
 *	kdb output.
 *
 *  If the user is doing a cmd args | grep srch
 *  then kdb_grepping_flag is set.
 *  In that case we need to accumulate full lines (ending in \n) before
 *  searching for the pattern.
 */

static char kdb_buffer[256];	/* A bit too big to go on stack */
static char *next_avail = kdb_buffer;
static int  size_avail;
static int  suspend_grep;

/*
 * search arg1 to see if it contains arg2
 * (kdmain.c provides flags for ^pat and pat$)
 *
 * return 1 for found, 0 for not found
 */
static int kdb_search_string(char *searched, char *searchfor)
{
	char firstchar, *cp;
	int len1, len2;

	/* not counting the newline at the end of "searched" */
	len1 = strlen(searched)-1;
	len2 = strlen(searchfor);
	if (len1 < len2)
		return 0;
	if (kdb_grep_leading && kdb_grep_trailing && len1 != len2)
		return 0;
	if (kdb_grep_leading) {
		if (!strncmp(searched, searchfor, len2))
			return 1;
	} else if (kdb_grep_trailing) {
		if (!strncmp(searched+len1-len2, searchfor, len2))
			return 1;
	} else {
		firstchar = *searchfor;
		cp = searched;
		while ((cp = strchr(cp, firstchar))) {
			if (!strncmp(cp, searchfor, len2))
				return 1;
			cp++;
		}
	}
	return 0;
}

int vkdb_printf(const char *fmt, va_list ap)
{
	int diag;
	int linecount;
	int logging, saved_loglevel = 0;
	int saved_trap_printk;
	int got_printf_lock = 0;
	int retlen = 0;
	int fnd, len;
	char *cp, *cp2, *cphold = NULL, replaced_byte = ' ';
	char *moreprompt = "more> ";
	struct console *c = console_drivers;
	static DEFINE_SPINLOCK(kdb_printf_lock);
	unsigned long uninitialized_var(flags);

	preempt_disable();
	saved_trap_printk = kdb_trap_printk;
	kdb_trap_printk = 0;

	/* Serialize kdb_printf if multiple cpus try to write at once.
	 * But if any cpu goes recursive in kdb, just print the output,
	 * even if it is interleaved with any other text.
	 */
	if (!KDB_STATE(PRINTF_LOCK)) {
		KDB_STATE_SET(PRINTF_LOCK);
		spin_lock_irqsave(&kdb_printf_lock, flags);
		got_printf_lock = 1;
		atomic_inc(&kdb_event);
	} else {
		__acquire(kdb_printf_lock);
	}

	diag = kdbgetintenv("LINES", &linecount);
	if (diag || linecount <= 1)
		linecount = 24;

	diag = kdbgetintenv("LOGGING", &logging);
	if (diag)
		logging = 0;

	if (!kdb_grepping_flag || suspend_grep) {
		/* normally, every vsnprintf starts a new buffer */
		next_avail = kdb_buffer;
		size_avail = sizeof(kdb_buffer);
	}
	vsnprintf(next_avail, size_avail, fmt, ap);

	/*
	 * If kdb_parse() found that the command was cmd xxx | grep yyy
	 * then kdb_grepping_flag is set, and kdb_grep_string contains yyy
	 *
	 * Accumulate the print data up to a newline before searching it.
	 * (vsnprintf does null-terminate the string that it generates)
	 */

	/* skip the search if prints are temporarily unconditional */
	if (!suspend_grep && kdb_grepping_flag) {
		cp = strchr(kdb_buffer, '\n');
		if (!cp) {
			/*
			 * Special cases that don't end with newlines
			 * but should be written without one:
			 *   The "[nn]kdb> " prompt should
			 *   appear at the front of the buffer.
			 *
			 *   The "[nn]more " prompt should also be
			 *     (MOREPROMPT -> moreprompt)
			 *   written *   but we print that ourselves,
			 *   we set the suspend_grep flag to make
			 *   it unconditional.
			 *
			 */
			if (next_avail == kdb_buffer) {
				/*
				 * these should occur after a newline,
				 * so they will be at the front of the
				 * buffer
				 */
				cp2 = kdb_buffer;
				len = strlen(kdb_prompt_str);
				if (!strncmp(cp2, kdb_prompt_str, len)) {
					/*
					 * We're about to start a new
					 * command, so we can go back
					 * to normal mode.
					 */
					kdb_grepping_flag = 0;
					goto kdb_printit;
				}
			}
			/* no newline; don't search/write the buffer
			   until one is there */
			len = strlen(kdb_buffer);
			next_avail = kdb_buffer + len;
			size_avail = sizeof(kdb_buffer) - len;
			goto kdb_print_out;
		}

		/*
		 * The newline is present; print through it or discard
		 * it, depending on the results of the search.
		 */
		cp++;	 	     /* to byte after the newline */
		replaced_byte = *cp; /* remember what/where it was */
		cphold = cp;
		*cp = '\0';	     /* end the string for our search */

		/*
		 * We now have a newline at the end of the string
		 * Only continue with this output if it contains the
		 * search string.
		 */
		fnd = kdb_search_string(kdb_buffer, kdb_grep_string);
		if (!fnd) {
			/*
			 * At this point the complete line at the start
			 * of kdb_buffer can be discarded, as it does
			 * not contain what the user is looking for.
			 * Shift the buffer left.
			 */
			*cphold = replaced_byte;
			strcpy(kdb_buffer, cphold);
			len = strlen(kdb_buffer);
			next_avail = kdb_buffer + len;
			size_avail = sizeof(kdb_buffer) - len;
			goto kdb_print_out;
		}
		/*
		 * at this point the string is a full line and
		 * should be printed, up to the null.
		 */
	}
kdb_printit:

	/*
	 * Write to all consoles.
	 */
	retlen = strlen(kdb_buffer);
	if (!dbg_kdb_mode && kgdb_connected) {
		gdbstub_msg_write(kdb_buffer, retlen);
	} else {
		if (dbg_io_ops && !dbg_io_ops->is_console) {
			len = strlen(kdb_buffer);
			cp = kdb_buffer;
			while (len--) {
				dbg_io_ops->write_char(*cp);
				cp++;
			}
		}
		while (c) {
			c->write(c, kdb_buffer, retlen);
			touch_nmi_watchdog();
			c = c->next;
		}
	}
	if (logging) {
		saved_loglevel = console_loglevel;
		console_loglevel = 0;
		printk(KERN_INFO "%s", kdb_buffer);
	}

	if (KDB_STATE(PAGER) && strchr(kdb_buffer, '\n'))
		kdb_nextline++;

	/* check for having reached the LINES number of printed lines */
	if (kdb_nextline == linecount) {
		char buf1[16] = "";
#if defined(CONFIG_SMP)
		char buf2[32];
#endif

		/* Watch out for recursion here.  Any routine that calls
		 * kdb_printf will come back through here.  And kdb_read
		 * uses kdb_printf to echo on serial consoles ...
		 */
		kdb_nextline = 1;	/* In case of recursion */

		/*
		 * Pause until cr.
		 */
		moreprompt = kdbgetenv("MOREPROMPT");
		if (moreprompt == NULL)
			moreprompt = "more> ";

#if defined(CONFIG_SMP)
		if (strchr(moreprompt, '%')) {
			sprintf(buf2, moreprompt, get_cpu());
			put_cpu();
			moreprompt = buf2;
		}
#endif

		kdb_input_flush();
		c = console_drivers;

		if (!dbg_io_ops->is_console) {
			len = strlen(moreprompt);
			cp = moreprompt;
			while (len--) {
				dbg_io_ops->write_char(*cp);
				cp++;
			}
		}
		while (c) {
			c->write(c, moreprompt, strlen(moreprompt));
			touch_nmi_watchdog();
			c = c->next;
		}

		if (logging)
			printk("%s", moreprompt);

		kdb_read(buf1, 2); /* '2' indicates to return
				    * immediately after getting one key. */
		kdb_nextline = 1;	/* Really set output line 1 */

		/* empty and reset the buffer: */
		kdb_buffer[0] = '\0';
		next_avail = kdb_buffer;
		size_avail = sizeof(kdb_buffer);
		if ((buf1[0] == 'q') || (buf1[0] == 'Q')) {
			/* user hit q or Q */
			KDB_FLAG_SET(CMD_INTERRUPT); /* command interrupted */
			KDB_STATE_CLEAR(PAGER);
			/* end of command output; back to normal mode */
			kdb_grepping_flag = 0;
			kdb_printf("\n");
		} else if (buf1[0] == ' ') {
			kdb_printf("\n");
			suspend_grep = 1; /* for this recursion */
		} else if (buf1[0] == '\n') {
			kdb_nextline = linecount - 1;
			kdb_printf("\r");
			suspend_grep = 1; /* for this recursion */
		} else if (buf1[0] && buf1[0] != '\n') {
			/* user hit something other than enter */
			suspend_grep = 1; /* for this recursion */
			kdb_printf("\nOnly 'q' or 'Q' are processed at more "
				   "prompt, input ignored\n");
		} else if (kdb_grepping_flag) {
			/* user hit enter */
			suspend_grep = 1; /* for this recursion */
			kdb_printf("\n");
		}
		kdb_input_flush();
	}

	/*
	 * For grep searches, shift the printed string left.
	 *  replaced_byte contains the character that was overwritten with
	 *  the terminating null, and cphold points to the null.
	 * Then adjust the notion of available space in the buffer.
	 */
	if (kdb_grepping_flag && !suspend_grep) {
		*cphold = replaced_byte;
		strcpy(kdb_buffer, cphold);
		len = strlen(kdb_buffer);
		next_avail = kdb_buffer + len;
		size_avail = sizeof(kdb_buffer) - len;
	}

kdb_print_out:
	suspend_grep = 0; /* end of what may have been a recursive call */
	if (logging)
		console_loglevel = saved_loglevel;
	if (KDB_STATE(PRINTF_LOCK) && got_printf_lock) {
		got_printf_lock = 0;
		spin_unlock_irqrestore(&kdb_printf_lock, flags);
		KDB_STATE_CLEAR(PRINTF_LOCK);
		atomic_dec(&kdb_event);
	} else {
		__release(kdb_printf_lock);
	}
	kdb_trap_printk = saved_trap_printk;
	preempt_enable();
	return retlen;
}

int kdb_printf(const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vkdb_printf(fmt, ap);
	va_end(ap);

	return r;
}
EXPORT_SYMBOL_GPL(kdb_printf);
