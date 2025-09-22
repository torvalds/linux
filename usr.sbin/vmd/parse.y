/*	$OpenBSD: parse.y,v 1.72 2025/06/03 20:13:42 bluhm Exp $	*/

/*
 * Copyright (c) 2007-2016 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2004 Ryan McBride <mcbride@openbsd.org>
 * Copyright (c) 2002, 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2001 Daniel Hartmeier.  All rights reserved.
 * Copyright (c) 2001 Theo de Raadt.  All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

%{
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <dev/vmm/vmm.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <agentx.h>
#include <stdio.h>
#include <limits.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <netdb.h>
#include <util.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>

#include "vmd.h"

TAILQ_HEAD(files, file)		 files = TAILQ_HEAD_INITIALIZER(files);
static struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	size_t			 ungetpos;
	size_t			 ungetsize;
	u_char			*ungetbuf;
	int			 eof_reached;
	int			 lineno;
	int			 errors;
} *file, *topfile;
struct file	*pushfile(const char *, int);
int		 popfile(void);
int		 yyparse(void);
int		 yylex(void);
int		 yyerror(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)))
    __attribute__((__nonnull__ (1)));
int		 kw_cmp(const void *, const void *);
int		 lookup(char *);
int		 igetc(void);
int		 lgetc(int);
void		 lungetc(int);
int		 findeol(void);

TAILQ_HEAD(symhead, sym)	 symhead = TAILQ_HEAD_INITIALIZER(symhead);
struct sym {
	TAILQ_ENTRY(sym)	 entry;
	int			 used;
	int			 persist;
	char			*nam;
	char			*val;
};
int		 symset(const char *, const char *, int);
char		*symget(const char *);

ssize_t		 parse_size(char *, int64_t);
int		 parse_disk(char *, int);
unsigned int	 parse_format(const char *);

static struct vmop_create_params vmc;
static struct vm_create_params	*vcp;
static struct vmd_switch	*vsw;
static char			*kernel = NULL;
static char			 vsw_type[IF_NAMESIZE];
static int			 vmc_disable;
static size_t			 vmc_nnics;
static int			 errors;
extern struct vmd		*env;
extern const char		*vmd_descsw[];

typedef struct {
	union {
		uint8_t		 lladdr[ETHER_ADDR_LEN];
		int64_t		 number;
		char		*string;
		struct {
			uid_t	 uid;
			int64_t	 gid;
		}		 owner;
	} v;
	int lineno;
} YYSTYPE;

%}


%token	INCLUDE ERROR
%token	ADD AGENTX ALLOW BOOT CDROM CONTEXT DEVICE DISABLE DISK DOWN ENABLE
%token	FORMAT GROUP
%token	INET6 INSTANCE INTERFACE LLADDR LOCAL LOCKED MEMORY NET NIFS OWNER
%token	PATH PREFIX RDOMAIN SIZE SOCKET SWITCH UP VM VMID STAGGERED START
%token  PARALLEL DELAY SEV SEVES
%token	<v.number>	NUMBER
%token	<v.string>	STRING
%type	<v.lladdr>	lladdr
%type	<v.number>	bootdevice
%type	<v.number>	disable
%type	<v.number>	image_format
%type	<v.number>	local
%type	<v.number>	locked
%type	<v.number>	updown
%type	<v.owner>	owner_id
%type	<v.string>	optstring
%type	<v.string>	string
%type	<v.string>	vm_instance
%type	<v.number>	sev;
%type	<v.number>	seves;

%%

grammar		: /* empty */
		| grammar include '\n'
		| grammar '\n'
		| grammar varset '\n'
		| grammar main '\n'
		| grammar switch '\n'
		| grammar vm '\n'
		| grammar error '\n'		{ file->errors++; }
		;

include		: INCLUDE string		{
			struct file	*nfile;

			if ((nfile = pushfile($2, 0)) == NULL) {
				yyerror("failed to include file %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			file = nfile;
			lungetc('\n');
		}
		;

varset		: STRING '=' STRING		{
			char *s = $1;
			while (*s++) {
				if (isspace((unsigned char)*s)) {
					yyerror("macro name cannot contain "
					    "whitespace");
					free($1);
					free($3);
					YYERROR;
				}
			}
			if (symset($1, $3, 0) == -1)
				fatalx("cannot store variable");
			free($1);
			free($3);
		}
		;

main		: LOCAL INET6 {
			env->vmd_cfg.cfg_flags |= VMD_CFG_INET6;
		}
		| LOCAL INET6 PREFIX STRING {
			const char	*err;

			if (parse_prefix6($4, &env->vmd_cfg.cfg_localprefix,
			    &err)) {
				yyerror("invalid local inet6 prefix: %s", err);
				YYERROR;
			} else {
				env->vmd_cfg.cfg_flags |= VMD_CFG_INET6;
				env->vmd_cfg.cfg_flags &= ~VMD_CFG_AUTOINET6;
			}
			free($4);
		}
		| LOCAL PREFIX STRING {
			const char	*err;

			if (parse_prefix4($3, &env->vmd_cfg.cfg_localprefix,
			    &err)) {
				yyerror("invalid local prefix: %s", err);
				YYERROR;
			}
			free($3);
		}
		| SOCKET OWNER owner_id {
			env->vmd_ps.ps_csock.cs_uid = $3.uid;
			env->vmd_ps.ps_csock.cs_gid = $3.gid == -1 ? 0 : $3.gid;
		}
		| AGENTX {
			env->vmd_cfg.cfg_agentx.ax_enabled = 1;
		} agentxopts {
			if (env->vmd_cfg.cfg_agentx.ax_path[0] == '\0')
				if (strlcpy(env->vmd_cfg.cfg_agentx.ax_path,
				    AGENTX_MASTER_PATH,
				    sizeof(env->vmd_cfg.cfg_agentx.ax_path)) >=
				    sizeof(env->vmd_cfg.cfg_agentx.ax_path)) {
					yyerror("invalid agentx path");
					YYERROR;
				}
		}
		| STAGGERED START PARALLEL NUMBER DELAY NUMBER {
			env->vmd_cfg.cfg_flags |= VMD_CFG_STAGGERED_START;
			env->vmd_cfg.delay.tv_sec = $6;
			env->vmd_cfg.parallelism = $4;
		}
		;

switch		: SWITCH string			{
			if ((vsw = calloc(1, sizeof(*vsw))) == NULL)
				fatal("could not allocate switch");

			vsw->sw_id = env->vmd_nswitches + 1;
			vsw->sw_name = $2;
			vsw->sw_flags = VMIFF_UP;

			vmc_disable = 0;
		} '{' optnl switch_opts_l '}'	{
			if (strnlen(vsw->sw_ifname,
			    sizeof(vsw->sw_ifname)) == 0) {
				yyerror("switch \"%s\" "
				    "is missing interface name",
				    vsw->sw_name);
				YYERROR;
			}

			if (vmc_disable) {
				log_debug("%s:%d: switch \"%s\""
				    " skipped (disabled)",
				    file->name, yylval.lineno, vsw->sw_name);
			} else if (!env->vmd_noaction) {
				TAILQ_INSERT_TAIL(env->vmd_switches,
				    vsw, sw_entry);
				env->vmd_nswitches++;
				log_debug("%s:%d: switch \"%s\" registered",
				    file->name, yylval.lineno, vsw->sw_name);
			}
		}
		;

switch_opts_l	: switch_opts_l switch_opts nl
		| switch_opts optnl
		;

switch_opts	: disable			{
			vmc_disable = $1;
		}
		| GROUP string			{
			if (priv_validgroup($2) == -1) {
				yyerror("invalid group name: %s", $2);
				free($2);
				YYERROR;
			}
			vsw->sw_group = $2;
		}
		| INTERFACE string		{
			if (priv_getiftype($2, vsw_type, NULL) == -1 ||
			    priv_findname(vsw_type, vmd_descsw) == -1) {
				yyerror("invalid switch interface: %s", $2);
				free($2);
				YYERROR;
			}

			if (strlcpy(vsw->sw_ifname, $2,
			    sizeof(vsw->sw_ifname)) >= sizeof(vsw->sw_ifname)) {
				yyerror("switch interface too long: %s", $2);
				free($2);
				YYERROR;
			}
			free($2);
		}
		| LOCKED LLADDR			{
			vsw->sw_flags |= VMIFF_LOCKED;
		}
		| RDOMAIN NUMBER		{
			if ($2 < 0 || $2 > RT_TABLEID_MAX) {
				yyerror("invalid rdomain: %lld", $2);
				YYERROR;
			}
			vsw->sw_flags |= VMIFF_RDOMAIN;
			vsw->sw_rdomain = $2;
		}
		| updown			{
			if ($1)
				vsw->sw_flags |= VMIFF_UP;
			else
				vsw->sw_flags &= ~VMIFF_UP;
		}
		;

vm		: VM string vm_instance		{
			unsigned int	 i;
			char		*name;

			memset(&vmc, 0, sizeof(vmc));
			vmc.vmc_kernel = -1;

			vcp = &vmc.vmc_params;
			vmc_disable = 0;
			vmc_nnics = 0;

			if ($3 != NULL) {
				/* This is an instance of a pre-configured VM */
				if (strlcpy(vmc.vmc_instance, $2,
				    sizeof(vmc.vmc_instance)) >=
				    sizeof(vmc.vmc_instance)) {
					yyerror("vm %s name too long", $2);
					free($2);
					free($3);
					YYERROR;
				}

				free($2);
				name = $3;
				vmc.vmc_flags |= VMOP_CREATE_INSTANCE;
			} else
				name = $2;

			for (i = 0; i < VM_MAX_NICS_PER_VM; i++) {
				/* Set the interface to UP by default */
				vmc.vmc_ifflags[i] |= IFF_UP;
			}

			if (strlcpy(vcp->vcp_name, name,
			    sizeof(vcp->vcp_name)) >= sizeof(vcp->vcp_name)) {
				yyerror("vm name too long");
				free($2);
				free($3);
				YYERROR;
			}

			/* set default user/group permissions */
			vmc.vmc_owner.uid = 0;
			vmc.vmc_owner.gid = -1;
		} '{' optnl vm_opts_l '}'	{
			struct vmd_vm	*vm;
			int		 ret;

			/* configured interfaces vs. number of interfaces */
			if (vmc_nnics > vmc.vmc_nnics)
				vmc.vmc_nnics = vmc_nnics;

			if (!env->vmd_noaction) {
				ret = vm_register(&env->vmd_ps, &vmc,
				    &vm, 0, 0);
				if (ret == -1 && errno == EALREADY) {
					log_debug("%s:%d: vm \"%s\""
					    " skipped (%s)",
					    file->name, yylval.lineno,
					    vcp->vcp_name,
					    (vm->vm_state & VM_STATE_RUNNING) ?
					    "running" : "already exists");
				} else if (ret == -1) {
					yyerror("vm \"%s\" failed: %s",
					    vcp->vcp_name, strerror(errno));
					YYERROR;
				} else {
					if (vmc_disable)
						vm->vm_state |= VM_STATE_DISABLED;
					else
						vm->vm_state |= VM_STATE_WAITING;
					log_debug("%s:%d: vm \"%s\" "
					    "registered (%s)",
					    file->name, yylval.lineno,
					    vcp->vcp_name,
					    vmc_disable ?
					    "disabled" : "enabled");
				}
				vm->vm_kernel_path = kernel;
				vm->vm_kernel = -1;
				vm->vm_from_config = 1;
			}
			kernel = NULL;
		}
		;

vm_instance	: /* empty */			{ $$ = NULL; }
		| INSTANCE string		{ $$ = $2; }
		;

vm_opts_l	: vm_opts_l vm_opts nl
		| vm_opts optnl
		;

vm_opts		: disable			{
			vmc_disable = $1;
		}
		| sev				{
			vcp->vcp_sev = 1;
		}
		| seves				{
			vcp->vcp_sev = vcp->vcp_seves = 1;
		}
		| DISK string image_format	{
			if (parse_disk($2, $3) != 0) {
				yyerror("failed to parse disks: %s", $2);
				free($2);
				YYERROR;
			}
			free($2);
			vmc.vmc_flags |= VMOP_CREATE_DISK;
		}
		| local INTERFACE optstring iface_opts_o {
			unsigned int	i;
			char		type[IF_NAMESIZE];

			i = vmc_nnics;
			if (++vmc_nnics > VM_MAX_NICS_PER_VM) {
				yyerror("too many interfaces: %zu", vmc_nnics);
				free($3);
				YYERROR;
			}

			if ($1)
				vmc.vmc_ifflags[i] |= VMIFF_LOCAL;
			if ($3 != NULL) {
				if (strcmp($3, "tap") != 0 &&
				    (priv_getiftype($3, type, NULL) == -1 ||
				    strcmp(type, "tap") != 0)) {
					yyerror("invalid interface: %s", $3);
					free($3);
					YYERROR;
				}

				if (strlcpy(vmc.vmc_ifnames[i], $3,
				    sizeof(vmc.vmc_ifnames[i])) >=
				    sizeof(vmc.vmc_ifnames[i])) {
					yyerror("interface name too long: %s",
					    $3);
					free($3);
					YYERROR;
				}
			}
			free($3);
			vmc.vmc_flags |= VMOP_CREATE_NETWORK;
		}
		| BOOT string			{
			char	 path[PATH_MAX];

			if (kernel != NULL) {
				yyerror("kernel specified more than once");
				free($2);
				YYERROR;

			}
			if (realpath($2, path) == NULL) {
				yyerror("kernel path not found: %s",
				    strerror(errno));
				free($2);
				YYERROR;
			}
			free($2);
			kernel = malloc(sizeof(path));
			if (kernel == NULL)
				yyerror("malloc");
			memcpy(kernel, &path, sizeof(path));
			vmc.vmc_flags |= VMOP_CREATE_KERNEL;
		}
		| BOOT DEVICE bootdevice	{
			vmc.vmc_bootdevice = $3;
		}
		| CDROM string			{
			if (vmc.vmc_cdrom[0] != '\0') {
				yyerror("cdrom specified more than once");
				free($2);
				YYERROR;

			}
			if (strlcpy(vmc.vmc_cdrom, $2,
			    sizeof(vmc.vmc_cdrom)) >=
			    sizeof(vmc.vmc_cdrom)) {
				yyerror("cdrom name too long");
				free($2);
				YYERROR;
			}
			free($2);
			vmc.vmc_flags |= VMOP_CREATE_CDROM;
		}
		| NIFS NUMBER			{
			if (vmc.vmc_nnics != 0) {
				yyerror("interfaces specified more than once");
				YYERROR;
			}
			if ($2 < 0 || $2 > VM_MAX_NICS_PER_VM) {
				yyerror("too many interfaces: %lld", $2);
				YYERROR;
			}
			vmc.vmc_nnics = (size_t)$2;
			vmc.vmc_flags |= VMOP_CREATE_NETWORK;
		}
		| MEMORY NUMBER			{
			ssize_t	 res;
			if (vcp->vcp_memranges[0].vmr_size != 0) {
				yyerror("memory specified more than once");
				YYERROR;
			}
			if ((res = parse_size(NULL, $2)) == -1) {
				yyerror("failed to parse size: %lld", $2);
				YYERROR;
			}
			vcp->vcp_memranges[0].vmr_size = (size_t)res;
			vmc.vmc_flags |= VMOP_CREATE_MEMORY;
		}
		| MEMORY STRING			{
			ssize_t	 res;
			if (vcp->vcp_memranges[0].vmr_size != 0) {
				yyerror("argument specified more than once");
				free($2);
				YYERROR;
			}
			if ((res = parse_size($2, 0)) == -1) {
				yyerror("failed to parse size: %s", $2);
				free($2);
				YYERROR;
			}
			vcp->vcp_memranges[0].vmr_size = (size_t)res;
			vmc.vmc_flags |= VMOP_CREATE_MEMORY;
		}
		| OWNER owner_id		{
			vmc.vmc_owner.uid = $2.uid;
			vmc.vmc_owner.gid = $2.gid;
		}
		| instance
		;

instance	: ALLOW INSTANCE '{' optnl instance_l '}'
		| ALLOW INSTANCE instance_flags
		;

instance_l	: instance_flags optcommanl instance_l
		| instance_flags optnl
		;

instance_flags	: BOOT		{ vmc.vmc_insflags |= VMOP_CREATE_KERNEL; }
		| MEMORY	{ vmc.vmc_insflags |= VMOP_CREATE_MEMORY; }
		| INTERFACE	{ vmc.vmc_insflags |= VMOP_CREATE_NETWORK; }
		| DISK		{ vmc.vmc_insflags |= VMOP_CREATE_DISK; }
		| CDROM		{ vmc.vmc_insflags |= VMOP_CREATE_CDROM; }
		| INSTANCE	{ vmc.vmc_insflags |= VMOP_CREATE_INSTANCE; }
		| OWNER owner_id {
			vmc.vmc_insowner.uid = $2.uid;
			vmc.vmc_insowner.gid = $2.gid;
		}
		;

owner_id	: NUMBER		{
			$$.uid = $1;
			$$.gid = -1;
		}
		| STRING		{
			char		*user, *group;
			struct passwd	*pw;
			struct group	*gr;

			$$.uid = 0;
			$$.gid = -1;

			user = $1;
			if ((group = strchr(user, ':')) != NULL) {
				if (group == user)
					user = NULL;
				*group++ = '\0';
			}

			if (user != NULL && *user) {
				if ((pw = getpwnam(user)) == NULL) {
					yyerror("failed to get user: %s",
					    user);
					free($1);
					YYERROR;
				}
				$$.uid = pw->pw_uid;
			}

			if (group != NULL && *group) {
				if ((gr = getgrnam(group)) == NULL) {
					yyerror("failed to get group: %s",
					    group);
					free($1);
					YYERROR;
				}
				$$.gid = gr->gr_gid;
			}

			free($1);
		}
		;

agentxopt	: CONTEXT STRING {
			if (strlcpy(env->vmd_cfg.cfg_agentx.ax_context, $2,
			    sizeof(env->vmd_cfg.cfg_agentx.ax_context)) >=
			    sizeof(env->vmd_cfg.cfg_agentx.ax_context)) {
				yyerror("agentx context too large");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| PATH STRING {
			if (strlcpy(env->vmd_cfg.cfg_agentx.ax_path, $2,
			    sizeof(env->vmd_cfg.cfg_agentx.ax_path)) >=
			    sizeof(env->vmd_cfg.cfg_agentx.ax_path)) {
				yyerror("agentx path too large");
				free($2);
				YYERROR;
			}
			free($2);
			if (env->vmd_cfg.cfg_agentx.ax_path[0] != '/') {
				yyerror("agentx path is not absolute");
				YYERROR;
			}
		}

agentxopts	: /* none */
		| agentxopts agentxopt
		;

image_format	: /* none 	*/	{
			$$ = 0;
		}
	     	| FORMAT string		{
			if (($$ = parse_format($2)) == 0) {
				yyerror("unrecognized disk format %s", $2);
				free($2);
				YYERROR;
			}
		}
		;

iface_opts_o	: '{' optnl iface_opts_l '}'
		| iface_opts_c
		| /* empty */
		;

iface_opts_l	: iface_opts_l iface_opts optnl
		| iface_opts optnl
		;

iface_opts_c	: iface_opts_c iface_opts optcomma
		| iface_opts
		;

iface_opts	: SWITCH string			{
			unsigned int	i = vmc_nnics;

			/* No need to check if the switch exists */
			if (strlcpy(vmc.vmc_ifswitch[i], $2,
			    sizeof(vmc.vmc_ifswitch[i])) >=
			    sizeof(vmc.vmc_ifswitch[i])) {
				yyerror("switch name too long: %s", $2);
				free($2);
				YYERROR;
			}
			free($2);
		}
		| GROUP string			{
			unsigned int	i = vmc_nnics;

			if (priv_validgroup($2) == -1) {
				yyerror("invalid group name: %s", $2);
				free($2);
				YYERROR;
			}

			/* No need to check if the group exists */
			(void)strlcpy(vmc.vmc_ifgroup[i], $2,
			    sizeof(vmc.vmc_ifgroup[i]));
			free($2);
		}
		| locked LLADDR lladdr		{
			if ($1)
				vmc.vmc_ifflags[vmc_nnics] |= VMIFF_LOCKED;
			memcpy(vmc.vmc_macs[vmc_nnics], $3, ETHER_ADDR_LEN);
		}
		| RDOMAIN NUMBER		{
			if ($2 < 0 || $2 > RT_TABLEID_MAX) {
				yyerror("invalid rdomain: %lld", $2);
				YYERROR;
			}
			vmc.vmc_ifflags[vmc_nnics] |= VMIFF_RDOMAIN;
			vmc.vmc_ifrdomain[vmc_nnics] = $2;
		}
		| updown			{
			if ($1)
				vmc.vmc_ifflags[vmc_nnics] |= VMIFF_UP;
			else
				vmc.vmc_ifflags[vmc_nnics] &= ~VMIFF_UP;
		}
		;

optstring	: STRING			{ $$ = $1; }
		| /* empty */			{ $$ = NULL; }
		;

string		: STRING string			{
			if (asprintf(&$$, "%s%s", $1, $2) == -1)
				fatal("asprintf string");
			free($1);
			free($2);
		}
		| STRING
		;

lladdr		: STRING			{
			struct ether_addr *ea;

			if ((ea = ether_aton($1)) == NULL) {
				yyerror("invalid address: %s\n", $1);
				free($1);
				YYERROR;
			}
			free($1);

			memcpy($$, ea, ETHER_ADDR_LEN);
		}
		| /* empty */ {
			memset($$, 0, ETHER_ADDR_LEN);
		}
		;

local		: /* empty */			{ $$ = 0; }
		| LOCAL				{ $$ = 1; }
		;

locked		: /* empty */			{ $$ = 0; }
		| LOCKED			{ $$ = 1; }
		;

updown		: UP				{ $$ = 1; }
		| DOWN				{ $$ = 0; }
		;

disable		: ENABLE			{ $$ = 0; }
		| DISABLE			{ $$ = 1; }
		;

sev		: SEV				{ $$ = 1; }
		;

seves		: SEVES				{ $$ = 1; }
		;

bootdevice	: CDROM				{ $$ = VMBOOTDEV_CDROM; }
		| DISK				{ $$ = VMBOOTDEV_DISK; }
		| NET				{ $$ = VMBOOTDEV_NET; }
		;

optcomma	: ','
		|
		;

optnl		: '\n' optnl
		|
		;

optcommanl	: ',' optnl
		| nl
		;

nl		: '\n' optnl
		;

%%

struct keywords {
	const char	*k_name;
	int		 k_val;
};

int
yyerror(const char *fmt, ...)
{
	va_list		 ap;
	char		*msg;

	file->errors++;
	va_start(ap, fmt);
	if (vasprintf(&msg, fmt, ap) == -1)
		fatal("yyerror vasprintf");
	va_end(ap);
	log_warnx("%s:%d: %s", file->name, yylval.lineno, msg);
	free(msg);
	return (0);
}

int
kw_cmp(const void *k, const void *e)
{
	return (strcmp(k, ((const struct keywords *)e)->k_name));
}

int
lookup(char *s)
{
	/* this has to be sorted always */
	static const struct keywords keywords[] = {
		{ "add",		ADD },
		{ "agentx",		AGENTX },
		{ "allow",		ALLOW },
		{ "boot",		BOOT },
		{ "cdrom",		CDROM },
		{ "context",		CONTEXT},
		{ "delay",		DELAY },
		{ "device",		DEVICE },
		{ "disable",		DISABLE },
		{ "disk",		DISK },
		{ "down",		DOWN },
		{ "enable",		ENABLE },
		{ "format",		FORMAT },
		{ "group",		GROUP },
		{ "id",			VMID },
		{ "include",		INCLUDE },
		{ "inet6",		INET6 },
		{ "instance",		INSTANCE },
		{ "interface",		INTERFACE },
		{ "interfaces",		NIFS },
		{ "lladdr",		LLADDR },
		{ "local",		LOCAL },
		{ "locked",		LOCKED },
		{ "memory",		MEMORY },
		{ "net",		NET },
		{ "owner",		OWNER },
		{ "parallel",		PARALLEL },
		{ "path",		PATH },
		{ "prefix",		PREFIX },
		{ "rdomain",		RDOMAIN },
		{ "sev",		SEV },
		{ "seves",		SEVES },
		{ "size",		SIZE },
		{ "socket",		SOCKET },
		{ "staggered",		STAGGERED },
		{ "start",		START  },
		{ "switch",		SWITCH },
		{ "up",			UP },
		{ "vm",			VM }
	};
	const struct keywords	*p;

	p = bsearch(s, keywords, sizeof(keywords)/sizeof(keywords[0]),
	    sizeof(keywords[0]), kw_cmp);

	if (p)
		return (p->k_val);
	else
		return (STRING);
}

#define START_EXPAND	1
#define DONE_EXPAND	2

static int	expanding;

int
igetc(void)
{
	int	c;

	while (1) {
		if (file->ungetpos > 0)
			c = file->ungetbuf[--file->ungetpos];
		else
			c = getc(file->stream);

		if (c == START_EXPAND)
			expanding = 1;
		else if (c == DONE_EXPAND)
			expanding = 0;
		else
			break;
	}
	return (c);
}

int
lgetc(int quotec)
{
	int		c, next;

	if (quotec) {
		if ((c = igetc()) == EOF) {
			yyerror("reached end of file while parsing "
			    "quoted string");
			if (file == topfile || popfile() == EOF)
				return (EOF);
			return (quotec);
		}
		return (c);
	}

	while ((c = igetc()) == '\\') {
		next = igetc();
		if (next != '\n') {
			c = next;
			break;
		}
		yylval.lineno = file->lineno;
		file->lineno++;
	}
	if (c == '\t' || c == ' ') {
		/* Compress blanks to a single space. */
		do {
			c = getc(file->stream);
		} while (c == '\t' || c == ' ');
		ungetc(c, file->stream);
		c = ' ';
	}

	if (c == EOF) {
		/*
		 * Fake EOL when hit EOF for the first time. This gets line
		 * count right if last line in included file is syntactically
		 * invalid and has no newline.
		 */
		if (file->eof_reached == 0) {
			file->eof_reached = 1;
			return ('\n');
		}
		while (c == EOF) {
			if (file == topfile || popfile() == EOF)
				return (EOF);
			c = igetc();
		}
	}
	return (c);
}

void
lungetc(int c)
{
	if (c == EOF)
		return;

	if (file->ungetpos >= file->ungetsize) {
		void *p = reallocarray(file->ungetbuf, file->ungetsize, 2);
		if (p == NULL)
			err(1, "%s", __func__);
		file->ungetbuf = p;
		file->ungetsize *= 2;
	}
	file->ungetbuf[file->ungetpos++] = c;
}

int
findeol(void)
{
	int	c;

	/* skip to either EOF or the first real EOL */
	while (1) {
		c = lgetc(0);
		if (c == '\n') {
			file->lineno++;
			break;
		}
		if (c == EOF)
			break;
	}
	return (ERROR);
}

int
yylex(void)
{
	char	 buf[8096];
	char	*p, *val;
	int	 quotec, next, c;
	int	 token;

top:
	p = buf;
	while ((c = lgetc(0)) == ' ' || c == '\t')
		; /* nothing */

	yylval.lineno = file->lineno;
	if (c == '#')
		while ((c = lgetc(0)) != '\n' && c != EOF)
			; /* nothing */
	if (c == '$' && !expanding) {
		while (1) {
			if ((c = lgetc(0)) == EOF)
				return (0);

			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			if (isalnum(c) || c == '_') {
				*p++ = c;
				continue;
			}
			*p = '\0';
			lungetc(c);
			break;
		}
		val = symget(buf);
		if (val == NULL) {
			yyerror("macro '%s' not defined", buf);
			return (findeol());
		}
		p = val + strlen(val) - 1;
		lungetc(DONE_EXPAND);
		while (p >= val) {
			lungetc((unsigned char)*p);
			p--;
		}
		lungetc(START_EXPAND);
		goto top;
	}

	switch (c) {
	case '\'':
	case '"':
		quotec = c;
		while (1) {
			if ((c = lgetc(quotec)) == EOF)
				return (0);
			if (c == '\n') {
				file->lineno++;
				continue;
			} else if (c == '\\') {
				if ((next = lgetc(quotec)) == EOF)
					return (0);
				if (next == quotec || next == ' ' ||
				    next == '\t')
					c = next;
				else if (next == '\n') {
					file->lineno++;
					continue;
				} else
					lungetc(next);
			} else if (c == quotec) {
				*p = '\0';
				break;
			} else if (c == '\0') {
				yyerror("syntax error");
				return (findeol());
			}
			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			*p++ = c;
		}
		yylval.v.string = strdup(buf);
		if (yylval.v.string == NULL)
			fatal("yylex: strdup");
		return (STRING);
	}

#define allowed_to_end_number(x) \
	(isspace(x) || x == ')' || x ==',' || x == '/' || x == '}' || x == '=')

	if (c == '-' || isdigit(c)) {
		do {
			*p++ = c;
			if ((size_t)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && isdigit(c));
		lungetc(c);
		if (p == buf + 1 && buf[0] == '-')
			goto nodigits;
		if (c == EOF || allowed_to_end_number(c)) {
			const char *errstr = NULL;

			*p = '\0';
			yylval.v.number = strtonum(buf, LLONG_MIN,
			    LLONG_MAX, &errstr);
			if (errstr) {
				yyerror("\"%s\" invalid number: %s",
				    buf, errstr);
				return (findeol());
			}
			return (NUMBER);
		} else {
nodigits:
			while (p > buf + 1)
				lungetc((unsigned char)*--p);
			c = (unsigned char)*--p;
			if (c == '-')
				return (c);
		}
	}

#define allowed_in_string(x) \
	(isalnum(x) || (ispunct(x) && x != '(' && x != ')' && \
	x != '{' && x != '}' && \
	x != '!' && x != '=' && x != '#' && \
	x != ','))

	if (isalnum(c) || c == ':' || c == '_' || c == '/') {
		do {
			*p++ = c;
			if ((size_t)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && (allowed_in_string(c)));
		lungetc(c);
		*p = '\0';
		if ((token = lookup(buf)) == STRING)
			if ((yylval.v.string = strdup(buf)) == NULL)
				fatal("yylex: strdup");
		return (token);
	}
	if (c == '\n') {
		yylval.lineno = file->lineno;
		file->lineno++;
	}
	if (c == EOF)
		return (0);
	return (c);
}

struct file *
pushfile(const char *name, int secret)
{
	struct file	*nfile;

	if ((nfile = calloc(1, sizeof(struct file))) == NULL) {
		log_warn("%s", __func__);
		return (NULL);
	}
	if ((nfile->name = strdup(name)) == NULL) {
		log_warn("%s", __func__);
		free(nfile);
		return (NULL);
	}
	if ((nfile->stream = fopen(nfile->name, "r")) == NULL) {
		free(nfile->name);
		free(nfile);
		return (NULL);
	}
	nfile->lineno = TAILQ_EMPTY(&files) ? 1 : 0;
	nfile->ungetsize = 16;
	nfile->ungetbuf = malloc(nfile->ungetsize);
	if (nfile->ungetbuf == NULL) {
		log_warn("%s", __func__);
		fclose(nfile->stream);
		free(nfile->name);
		free(nfile);
		return (NULL);
	}
	TAILQ_INSERT_TAIL(&files, nfile, entry);
	return (nfile);
}

int
popfile(void)
{
	struct file	*prev;

	if ((prev = TAILQ_PREV(file, files, entry)) != NULL)
		prev->errors += file->errors;

	TAILQ_REMOVE(&files, file, entry);
	fclose(file->stream);
	free(file->name);
	free(file->ungetbuf);
	free(file);
	file = prev;
	return (file ? 0 : EOF);
}

int
parse_config(const char *filename)
{
	extern const char	 default_conffile[];
	struct sym		*sym, *next;

	if ((file = pushfile(filename, 0)) == NULL) {
		/* no default config file is fine */
		if (errno == ENOENT && filename == default_conffile) {
			log_debug("%s: missing", filename);
			return (0);
		}
		log_warn("failed to open %s", filename);
		if (errno == ENOENT)
			return (0);
		return (-1);
	}
	topfile = file;
	setservent(1);

	/* Set the default switch type */
	(void)strlcpy(vsw_type, VMD_SWITCH_TYPE, sizeof(vsw_type));

	env->vmd_cfg.cfg_agentx.ax_enabled = 0;
	env->vmd_cfg.cfg_agentx.ax_context[0] = '\0';
	env->vmd_cfg.cfg_agentx.ax_path[0] = '\0';

	yyparse();
	errors = file->errors;
	popfile();

	endservent();

	/* Free macros and check which have not been used. */
	TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
		if (!sym->used)
			fprintf(stderr, "warning: macro '%s' not "
			    "used\n", sym->nam);
		if (!sym->persist) {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}

	if (errors)
		return (-1);

	return (0);
}

int
symset(const char *nam, const char *val, int persist)
{
	struct sym	*sym;

	TAILQ_FOREACH(sym, &symhead, entry) {
		if (strcmp(nam, sym->nam) == 0)
			break;
	}

	if (sym != NULL) {
		if (sym->persist == 1)
			return (0);
		else {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}
	if ((sym = calloc(1, sizeof(*sym))) == NULL)
		return (-1);

	sym->nam = strdup(nam);
	if (sym->nam == NULL) {
		free(sym);
		return (-1);
	}
	sym->val = strdup(val);
	if (sym->val == NULL) {
		free(sym->nam);
		free(sym);
		return (-1);
	}
	sym->used = 0;
	sym->persist = persist;
	TAILQ_INSERT_TAIL(&symhead, sym, entry);
	return (0);
}

int
cmdline_symset(char *s)
{
	char	*sym, *val;
	int	ret;

	if ((val = strrchr(s, '=')) == NULL)
		return (-1);
	sym = strndup(s, val - s);
	if (sym == NULL)
		fatal("%s: strndup", __func__);
	ret = symset(sym, val + 1, 1);
	free(sym);

	return (ret);
}

char *
symget(const char *nam)
{
	struct sym	*sym;

	TAILQ_FOREACH(sym, &symhead, entry) {
		if (strcmp(nam, sym->nam) == 0) {
			sym->used = 1;
			return (sym->val);
		}
	}
	return (NULL);
}

ssize_t
parse_size(char *word, int64_t val)
{
	char		 result[FMT_SCALED_STRSIZE];
	ssize_t		 size;
	long long	 res;

	if (word != NULL) {
		if (scan_scaled(word, &res) != 0) {
			log_warn("invalid memory size: %s", word);
			return (-1);
		}
		val = (int64_t)res;
	}

	if (val < (1024 * 1024)) {
		log_warnx("memory size must be at least 1MB");
		return (-1);
	}

	if (val > VMM_MAX_VM_MEM_SIZE) {
		if (fmt_scaled(VMM_MAX_VM_MEM_SIZE, result) == 0)
			log_warnx("memory size too large (limit is %s)",
			    result);
		else
			log_warnx("memory size too large");
		return (-1);
	}

	/* Round down to the megabyte. */
	size = (val / (1024 * 1024)) * (1024 * 1024);

	if (size != val) {
		if (fmt_scaled(size, result) == 0)
			log_debug("memory size rounded to %s", result);
		else
			log_debug("memory size rounded to %zd bytes", size);
	}

	return ((ssize_t)size);
}

int
parse_disk(char *word, int type)
{
	char	 buf[BUFSIZ], path[PATH_MAX];
	int	 fd;
	ssize_t	 len;

	if (vmc.vmc_ndisks >= VM_MAX_DISKS_PER_VM) {
		log_warnx("too many disks");
		return (-1);
	}

	if (realpath(word, path) == NULL) {
		log_warn("disk %s", word);
		return (-1);
	}

	if (!type) {
		/* Use raw as the default format */
		type = VMDF_RAW;

		/* Try to derive the format from the file signature */
		if ((fd = open(path, O_RDONLY)) != -1) {
			len = read(fd, buf, sizeof(buf));
			close(fd);
			if (len >= (ssize_t)strlen(VM_MAGIC_QCOW) &&
			    strncmp(buf, VM_MAGIC_QCOW,
			    strlen(VM_MAGIC_QCOW)) == 0) {
				/* The qcow version will be checked later */
				type = VMDF_QCOW2;
			}
		}
	}

	if (strlcpy(vmc.vmc_disks[vmc.vmc_ndisks], path,
	    sizeof(vmc.vmc_disks[vmc.vmc_ndisks])) >=
	    sizeof(vmc.vmc_disks[vmc.vmc_ndisks])) {
		log_warnx("disk path too long");
		return (-1);
	}
	vmc.vmc_disktypes[vmc.vmc_ndisks] = type;

	vmc.vmc_ndisks++;

	return (0);
}

unsigned int
parse_format(const char *word)
{
	if (strcasecmp(word, "raw") == 0)
		return (VMDF_RAW);
	else if (strcasecmp(word, "qcow2") == 0)
		return (VMDF_QCOW2);
	return (0);
}

/*
 * Parse an ipv4 address and prefix for local interfaces and validate
 * constraints for vmd networking.
 */
int
parse_prefix4(const char *str, struct local_prefix *out, const char **errstr)
{
	struct addrinfo		 hints, *res = NULL;
	struct sockaddr_storage	 ss;
	struct in_addr		 addr;
	int			 mask = 16;
	char			*p, *ps;

	if ((ps = strdup(str)) == NULL)
		fatal("%s: strdup", __func__);

	if ((p = strrchr(ps, '/')) != NULL) {
		mask = strtonum(p + 1, 1, 16, errstr);
		if (errstr != NULL && *errstr) {
			free(ps);
			return (1);
		}
		p[0] = '\0';
	}

	/* Attempt to construct an address from the user input. */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_NUMERICHOST;

	if (getaddrinfo(ps, NULL, &hints, &res) == 0) {
		memset(&ss, 0, sizeof(ss));
		memcpy(&ss, res->ai_addr, res->ai_addrlen);
		addr.s_addr = ss2sin(&ss)->sin_addr.s_addr;
		freeaddrinfo(res);
	} else { /* try 10/8 parsing */
		memset(&addr, 0, sizeof(addr));
		if (inet_net_pton(AF_INET, ps, &addr, sizeof(addr)) == -1) {
			if (errstr)
				*errstr = "invalid format";
			free(ps);
			return (1);
		}
	}
	free(ps);

	/*
	 * Validate the prefix by comparing it with the mask. Since we
	 * constrain the mask length to 16 above, this also validates
	 * we reserve the last 16 bits for use by vmd to assign vm id
	 * and interface id.
	 */
	if ((addr.s_addr & prefixlen2mask(mask)) != addr.s_addr) {
		if (errstr)
			*errstr = "bad mask";
		return (1);
	}

	/* Copy out the local prefix. */
	out->lp_in.s_addr = addr.s_addr;
	out->lp_mask.s_addr = prefixlen2mask(mask);
	return (0);
}

/*
 * Parse an ipv6 address and prefix for local interfaces and validate
 * constraints for vmd networking.
 */
int
parse_prefix6(const char *str, struct local_prefix *out, const char **errstr)
{
	struct addrinfo		 hints, *res = NULL;
	struct sockaddr_storage	 ss;
	struct in6_addr		 addr6, mask6;
	size_t			 i;
	int			 mask = 64, err;
	char			*p, *ps;

	if ((ps = strdup(str)) == NULL)
		fatal("%s: strdup", __func__);

	if ((p = strrchr(ps, '/')) != NULL) {
		mask = strtonum(p + 1, 0, 64, errstr);
		if (errstr != NULL && *errstr) {
			free(ps);
			return (1);
		}
		p[0] = '\0';
	}

	/* Attempt to construct an address from the user input. */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_NUMERICHOST;

	if ((err = getaddrinfo(ps, NULL, &hints, &res)) != 0) {
		if (errstr)
			*errstr = gai_strerror(err);
		free(ps);
		return (1);
	}
	free(ps);

	memset(&ss, 0, sizeof(ss));
	memcpy(&ss, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);

	memcpy(&addr6, (void*)&ss2sin6(&ss)->sin6_addr, sizeof(addr6));
	prefixlen2mask6(mask, &mask6);

	/*
	 * Validate the prefix by comparing it with the mask. Since we
	 * constrain the mask length to 64 above, this also validates
	 * that we're reserving bits for the encoding of the ipv4
	 * address, the vm id, and interface id. */
	for (i = 0; i < 16; i++) {
		if ((addr6.s6_addr[i] & mask6.s6_addr[i]) != addr6.s6_addr[i]) {
			if (errstr)
				*errstr = "bad mask";
			return (1);
		}
	}

	/* Copy out the local prefix. */
	memcpy(&out->lp_in6, &addr6, sizeof(out->lp_in6));
	memcpy(&out->lp_mask6, &mask6, sizeof(out->lp_mask6));
	return (0);
}
