/*	$OpenBSD: phantstruct.h,v 1.5 2016/01/06 14:28:09 mestre Exp $	*/
/*	$NetBSD: phantstruct.h,v 1.2 1995/03/24 04:00:11 cgd Exp $	*/

/*
 * phantstruct.h - structure definitions for Phantasia
 */

#include <limits.h>
#include <stdbool.h>

struct	player	    	/* player statistics */
    {
    double	p_experience;	/* experience */
    double	p_level;    	/* level */
    double	p_strength;	/* strength */
    double	p_sword;	/* sword */
    double	p_might;	/* effect strength */
    double	p_energy;	/* energy */
    double	p_maxenergy;	/* maximum energy */
    double	p_shield;	/* shield */
    double	p_quickness;	/* quickness */
    double	p_quksilver;	/* quicksilver */
    double	p_speed;	/* effective quickness */
    double	p_magiclvl;	/* magic level */
    double	p_mana;		/* mana */
    double	p_brains;	/* brains */
    double	p_poison;	/* poison */
    double	p_gold;		/* gold */
    double	p_gems;		/* gems */
    double	p_sin;		/* sin */
    double	p_x;	    	/* x coord */
    double	p_y;	    	/* y coord */
    double	p_1scratch,
		p_2scratch;	/* variables used for decree, player battle */

    struct
	{
	short	ring_type;	/* type of ring */
	short	ring_duration;	/* duration of ring */
	bool	ring_inuse;	/* ring in use flag */
	}	p_ring;	    	/* ring stuff */

    long	p_age;		/* age of player */

    int		p_degenerated;	/* age/3000 last degenerated */

    short	p_type;		/* character type */
    short	p_specialtype;	/* special character type */
    short	p_lives;	/* multiple lives for council, valar */
    short	p_crowns;	/* crowns */
    short	p_charms;	/* charms */
    short	p_amulets;	/* amulets */
    short	p_holywater;   	/* holy water */
    short	p_lastused;	/* day of year last used */
    short	p_status;	/* playing, cloaked, etc. */
    short	p_tampered;	/* decree'd, etc. flag */
    short	p_istat;	/* used for inter-terminal battle */

    bool	p_palantir;	/* palantir */
    bool	p_blessing;	/* blessing */
    bool	p_virgin;	/* virgin */
    bool	p_blindness;	/* blindness */

    char	p_name[SZ_NAME];	/* name */
    char	p_password[SZ_PASSWORD];/* password */
    char	p_login[LOGIN_NAME_MAX];/* login */
    };

struct	monster	    	/* monster stats */
    {
    double	m_strength;	/* strength */
    double	m_brains;	/* brains */
    double	m_speed;	/* speed */
    double	m_energy;	/* energy */
    double	m_experience;	/* experience */
    double	m_flock;    	/* % chance of flocking */

    double	m_o_strength;	/* original strength */
    double	m_o_speed;	/* original speed */
    double	m_maxspeed;	/* maximum speed */
    double	m_o_energy;	/* original energy */
    double	m_melee;	/* melee damage */
    double	m_skirmish;	/* skirmish damage */

    int		m_treasuretype;	/* treasure type */
    int		m_type;	    	/* special type */

    char	m_name[26];	/* name */
    };

struct	energyvoid     	/* energy void */
    {
    double	ev_x;		/* x coordinate */
    double	ev_y;		/* y coordinate */
    bool	ev_active;	/* active or not */
    };

struct	scoreboard			/* scoreboard entry */
    {
    double	sb_level;		/* level of player */
    char	sb_type[4];		/* character type of player */
    char	sb_name[SZ_NAME];	/* name of player */
    char	sb_login[LOGIN_NAME_MAX];/* login of player */
    };

struct	charstats			/* character type statistics */
    {
    double	c_maxbrains;		/* max brains per level */
    double	c_maxmana;		/* max mana per level */
    double	c_weakness;		/* how strongly poison affects player */
    double	c_goldtote;		/* how much gold char can carry */
    int		c_ringduration;		/* bad ring duration */
    struct
	{
	double	base;		/* base for roll */
	double	interval;	/* interval for roll */
	double	increase;	/* increment per level */
	} c_quickness,		/* quickness */
	  c_strength,		/* strength */
	  c_mana,		/* mana */
	  c_energy,		/* energy level */
	  c_brains,		/* brains */
	  c_magiclvl;		/* magic level */
    };

struct menuitem				/* menu item for purchase */
    {
    char	*item;		/* menu item name */
    double	cost;		/* cost of item */
    };
