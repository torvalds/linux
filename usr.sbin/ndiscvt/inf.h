/*
 * $Id: inf.h,v 1.3 2003/11/30 21:58:16 winter Exp $
 *
 * $FreeBSD$
 */

#define W_MAX	32

struct section {
	const char *	name;

	TAILQ_ENTRY(section)	link;
};
TAILQ_HEAD(section_head, section);

struct assign {
	struct section	*section;

	const char *	key;
	const char *	vals[W_MAX];

	TAILQ_ENTRY(assign)	link;
};
TAILQ_HEAD(assign_head, assign);

struct reg {
	struct section *section;

	const char *	root;
	const char *	subkey;
	const char *	key;
	u_int		flags;
	const char *	value;

	TAILQ_ENTRY(reg)	link;
};
TAILQ_HEAD(reg_head, reg);

#define	FLG_ADDREG_TYPE_SZ		0x00000000
#define	FLG_ADDREG_BINVALUETYPE		0x00000001
#define	FLG_ADDREG_NOCLOBBER		0x00000002
#define	FLG_ADDREG_DELVAL		0x00000004
#define	FLG_ADDREG_APPEND		0x00000008
#define	FLG_ADDREG_KEYONLY		0x00000010
#define	FLG_ADDREG_OVERWRITEONLY	0x00000020
#define	FLG_ADDREG_64BITKEY		0x00001000
#define	FLG_ADDREG_KEYONLY_COMMON	0x00002000
#define	FLG_ADDREG_32BITKEY		0x00004000
#define	FLG_ADDREG_TYPE_MULTI_SZ	0x00010000
#define	FLG_ADDREG_TYPE_EXPAND_SZ	0x00020000
#define	FLG_ADDREG_TYPE_DWORD		0x00010001
#define	FLG_ADDREG_TYPE_NONE		0x00020001

extern void	section_add	(const char *);
extern void	assign_add	(const char *);
extern void	define_add	(const char *);
extern void	regkey_add	(const char *);

extern void	push_word	(const char *);
extern void	clear_words	(void);
extern int	inf_parse	(FILE *, FILE *);
