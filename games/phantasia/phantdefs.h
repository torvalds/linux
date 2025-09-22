/*	$OpenBSD: phantdefs.h,v 1.5 2001/09/19 10:51:55 pjanzen Exp $	*/
/*	$NetBSD: phantdefs.h,v 1.2 1995/03/24 03:59:28 cgd Exp $	*/

/*
 * phantdefs.h - important constants for Phantasia
 */

/* ring constants */
#define R_NONE		0		/* no ring */
#define R_NAZREG	1		/* regular Nazgul ring (expires) */
#define R_DLREG		2		/* regular Dark Lord ring (does not expire) */
#define R_BAD		3		/* bad ring */
#define R_SPOILED 	4		/* ring which has gone bad */

/* status constants */
#define S_NOTUSED	0		/* record not in use */
#define S_OFF		1		/* not playing */
#define S_PLAYING	2		/* playing - nothing else */
#define S_CLOAKED	3		/* playing - cloaked */
#define S_INBATTLE	4		/* playing - in battle */
#define S_MONSTER	5		/* playing - fighting monster */
#define S_TRADING	6		/* playing - at a trading post */
#define S_HUNGUP	7		/* error occurred with character */

/* tampered constants */
#define T_OFF		0		/* nothing */
#define T_NRGVOID	1		/* hit an energy void */
#define T_GRAIL		2		/* landed on the holy grail */
#define T_TRANSPORT	3		/* transported by king */
#define T_BESTOW	4		/* gold bestowed by king */
#define T_CURSED	5		/* cursed by king */
#define T_MONSTER	6		/* monster lobbed by valar */
#define T_BLESSED	7		/* blessed by valar */
#define T_RELOCATE	8		/* moved by valar */
#define T_HEAL		9		/* healed by valar */
#define T_VAPORIZED	10		/* vaporized by wizard */
#define T_EXVALAR	11		/* no longer valar */

/* inter-terminal battle status constants */
#define I_OFF		0		/* nothing */
#define I_RAN		1		/* ran away */
#define I_STUCK		2		/* tried to run unsuccessfully */
#define I_BLEWIT	3		/* tried to luckout unsuccessfully */
#define I_KILLED	4		/* killed foe */

/* constants for altering coordinates */
#define A_SPECIFIC	0		/* coordinates specified */
#define A_FORCED	1		/* coordinates specified, ignore Beyond */
#define A_NEAR		2		/* coordinates not specified, move near */
#define A_FAR		3		/* coordinates not specified, move far */

/* constants for character types */
#define C_MAGIC		0		/* magic user */
#define C_FIGHTER	1		/* fighter */
#define C_ELF		2		/* elf */
#define C_DWARF		3		/* dwarf */
#define C_HALFLING	4		/* halfling */
#define C_EXPER		5		/* experimento */
#define C_SUPER		6		/* super being */

/* constants for special character types */
#define SC_NONE		0		/* not a special character */
#define SC_KING		1		/* king */
#define SC_COUNCIL	2		/* council of the wise */
#define SC_VALAR	3		/* valar */
#define SC_EXVALAR	4		/* ex-valar */

/* special monster constants */
#define SM_NONE		0		/* nothing special */
#define SM_UNICORN	1		/* unicorn */
#define SM_MODNAR	2		/* Modnar */
#define SM_MIMIC	3		/* mimic */
#define SM_DARKLORD	4		/* Dark Lord */
#define SM_LEANAN	5		/* Leanan-Sidhe */
#define SM_SARUMAN	6		/* Saruman */
#define SM_THAUMATURG	7		/* thaumaturgist */
#define SM_BALROG	8		/* balrog */
#define SM_VORTEX	9		/* vortex */
#define SM_NAZGUL	10		/* nazgul */
#define SM_TIAMAT	11		/* Tiamat */
#define SM_KOBOLD	12		/* kobold */
#define SM_SHELOB	13		/* Shelob */
#define SM_FAERIES	14		/* assorted faeries */
#define SM_LAMPREY	15		/* lamprey */
#define SM_SHRIEKER	16		/* shrieker */
#define SM_BONNACON	17		/* bonnacon */
#define SM_SMEAGOL	18		/* Smeagol */
#define SM_SUCCUBUS	19		/* succubus */
#define SM_CERBERUS	20		/* Cerberus */
#define SM_UNGOLIANT	21		/* Ungoliant */
#define SM_JABBERWOCK	22		/* jabberwock */
#define SM_MORGOTH	23		/* Morgoth */
#define SM_TROLL	24		/* troll */
#define SM_WRAITH	25		/* wraith */

/* constants for spells */
#define ML_ALLORNOTHING	0.0		/* magic level for 'all or nothing' */
#define MM_ALLORNOTHING	1.0		/* mana used for 'all or nothing' */
#define ML_MAGICBOLT	5.0		/* magic level for 'magic bolt' */
#define ML_FORCEFIELD	15.0		/* magic level for 'force field' */
#define MM_FORCEFIELD	30.0		/* mana used for 'force field' */
#define ML_XFORM	25.0		/* magic level for 'transform' */
#define MM_XFORM	50.0		/* mana used for 'transform' */
#define ML_INCRMIGHT	35.0		/* magic level for 'increase might' */
#define MM_INCRMIGHT	75.0		/* mana used for 'increase might' */
#define ML_INVISIBLE	45.0		/* magic level for 'invisibility' */
#define MM_INVISIBLE	90.0		/* mana used for 'invisibility' */
#define ML_XPORT	60.0		/* magic level for 'transport' */
#define MM_XPORT	125.0		/* mana used for 'transport' */
#define ML_PARALYZE	75.0		/* magic level for 'paralyze' */
#define MM_PARALYZE	150.0		/* mana used for 'paralyze' */
#define MM_SPECIFY	1000.0		/* mana used for 'specify' */
#define ML_CLOAK	20.0		/* magic level for 'cloak' */
#define MEL_CLOAK	7.0		/* experience level for 'cloak' */
#define MM_CLOAK	35.0		/* mana used for 'cloak' */
#define ML_TELEPORT	40.0		/* magic level for 'teleport' */
#define MEL_TELEPORT	12.0		/* experience level for 'teleport' */
#define MM_INTERVENE	1000.0		/* mana used to 'intervene' */

/* some miscellaneous constants */
#define SZ_DATABUF	100		/* size of input buffer */
#define SZ_PLAYERSTRUCT	sizeof(struct player) /* size of player structure */
#define SZ_VOIDSTRUCT	sizeof(struct energyvoid) /* size of energy void struct */
#define SZ_SCORESTRUCT	sizeof(struct scoreboard) /* size of score board entry */
#define SZ_MONSTERSTRUCT sizeof(struct monster) /* size of monster structure */
#define SZ_NAME		21		/* size of player name (incl. trailing nul) */
#define SZ_PASSWORD	9		/* size of password (incl. trailing nul) */

#define N_DAYSOLD	21		/* number of days old for purge */
#define N_AGE		500		/* age to degenerate ratio */
#define N_GEMVALUE	(1000.0)	/* number of gold pieces to gem ratio */
#define N_TAXAMOUNT	(7.0)		/* tax percent */

#define D_BEYOND	(1.1e6)		/* distance to beyond point of no return */
#define D_EXPER		(2000.0)	/* distance experimentos are allowed */

#define CH_MARKDELETE	'\001'		/* used to alter name of deleted players */
#define CH_KILL		'\030'		/* kill character (ctrl-X) */
#define CH_ERASE	'\010'		/* erase character (ctrl-H) */
#define CH_NEWLINE	'\n'		/* newline */
#define CH_RETURN	'\r'		/* carriage return */
#define CH_REDRAW	'\014'		/* redraw screen character (ctrl-L) */
