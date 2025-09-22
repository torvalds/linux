/*	$OpenBSD: setup.c,v 1.20 2019/06/28 13:32:52 deraadt Exp $	*/
/*	$NetBSD: setup.c,v 1.4 1995/04/24 12:24:41 cgd Exp $	*/

/*
 * setup.c - set up all files for Phantasia
 */
#include <sys/stat.h>

#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "macros.h"
#include "pathnames.h"
#include "phantdefs.h"
#include "phantglobs.h"

__dead void Error(char *, char *);

/**/
/************************************************************************
/
/ FUNCTION NAME: main()
/
/ FUNCTION: setup files for Phantasia 3.3.2
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: exit(), stat(), Error(), open(), close(), fopen(), 
/	fgets(), floor(), umask(), strlcpy(),
/	unlink(), fwrite(), fclose(), sscanf(), printf(), strlen(), fprintf()
/
/ GLOBAL INPUTS: Curmonster, _iob[], Databuf[], *Monstfp, Enrgyvoid
/
/ GLOBAL OUTPUTS: Curmonster, Databuf[], *Monstfp, Enrgyvoid
/
/ DESCRIPTION: 
/
/	This program tries to verify the parameters specified in
/	the Makefile.
/
/	Create all necessary files.  Note that nothing needs to be
/	put in these files.
/	Also, the monster binary data base is created here.
/
*************************************************************************/

static char *files[] = {		/* all files to create */
	_PATH_MONST,
	_PATH_PEOPLE,
	_PATH_MESS,
	_PATH_LASTDEAD,
	_PATH_MOTD,
	_PATH_GOLD,
	_PATH_VOID,
	_PATH_SCORE,
	NULL,
};

char *monsterfile="monsters.asc";

int
main(int argc, char *argv[])
{
	char	**filename;	/* for pointing to file names */
	int	fd;		/* file descriptor */
	FILE	*fp;			/* for opening files */
	struct stat	fbuf;		/* for getting files statistics */
	int ch;
	char path[PATH_MAX], *prefix;

	while ((ch = getopt(argc, argv, "hm:")) != -1)
		switch(ch) {
		case 'm':
			monsterfile = optarg;
			break;
		case 'h':
		default:
			break;
		}
	argc -= optind;
	argv += optind;

    umask(0117);		/* only owner can read/write created files */

    prefix = getenv("DESTDIR");

    /* try to create data files */
    filename = &files[0];
    while (*filename != NULL)
	/* create each file */
	{
	snprintf(path, sizeof(path), "%s%s", prefix?prefix:"", *filename);
	if (stat(path, &fbuf) == 0)
	    /* file exists; remove it */
	    {
	    if (!strcmp(*filename, _PATH_PEOPLE))
		/* do not reset character file if it already exists */
		{
		++filename;
		continue;
		}

	    if (!strcmp(*filename, _PATH_SCORE))
		/* do not reset score file if it already exists */
		{
		++filename;
		continue;
		}

	    if (unlink(path) == -1)
		Error("Cannot unlink %s.\n", path);
	    }

	if ((fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0660)) == -1)
	    Error("Cannot create %s.\n", path);

	close(fd);			/* close newly created file */

	++filename;			/* process next file */
	}

    /* put holy grail info into energy void file */
    Enrgyvoid.ev_active = true;
    Enrgyvoid.ev_x = ROLL(-1.0e6, 2.0e6);
    Enrgyvoid.ev_y = ROLL(-1.0e6, 2.0e6);
    snprintf(path, sizeof(path), "%s%s", prefix?prefix:"", _PATH_VOID);
    if ((fp = fopen(path, "w")) == NULL)
	Error("Cannot update %s.\n", path);
    else
	{
	fwrite(&Enrgyvoid, SZ_VOIDSTRUCT, 1, fp);
	fclose(fp);
	}

    /* create binary monster data base */
    snprintf(path, sizeof(path), "%s%s", prefix?prefix:"", _PATH_MONST);
    if ((Monstfp = fopen(path, "w")) == NULL)
	Error("Cannot update %s.\n", path);
    else
	{
	if ((fp = fopen(monsterfile, "r")) == NULL)
	    {
	    fclose(Monstfp);
	    Error("cannot open %s to create monster database.\n", "monsters.asc");
	    }
	else
	    {
	    Curmonster.m_o_strength =
	    Curmonster.m_o_speed =
	    Curmonster.m_maxspeed =
	    Curmonster.m_o_energy =
	    Curmonster.m_melee =
	    Curmonster.m_skirmish = 0.0;

	    while (fgets(Databuf, SZ_DATABUF, fp) != NULL)
		/* read in text file, convert to binary */
		{
		sscanf(&Databuf[24], "%lf%lf%lf%lf%lf%d%d%lf",
		    &Curmonster.m_strength, &Curmonster.m_brains,
		    &Curmonster.m_speed, &Curmonster.m_energy,
		    &Curmonster.m_experience, &Curmonster.m_treasuretype,
		    &Curmonster.m_type, &Curmonster.m_flock);
		Databuf[24] = '\0';
		strlcpy(Curmonster.m_name, Databuf, sizeof Curmonster.m_name);
		fwrite(&Curmonster, SZ_MONSTERSTRUCT, 1, Monstfp);
		}
	    fclose(fp);
	    fclose(Monstfp);
	    }
	}

    return 0;
}
/**/
/************************************************************************
/
/ FUNCTION NAME: Error()
/
/ FUNCTION: print an error message, and exit
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	char *str - format string for printf()
/	char *file - file which caused error
/
/ RETURN VALUE: none
/
/ MODULES CALLED: exit(), perror(), fprintf()
/
/ GLOBAL INPUTS: _iob[]
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Print an error message, then exit.
/
*************************************************************************/

void
Error(char *str, char *file)
{
	fprintf(stderr, "Error: ");
	fprintf(stderr, str, file);
	perror(file);
	exit(1);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: drandom()
/
/ FUNCTION: return a random number
/
/ AUTHOR: E. A. Estes, 2/7/86
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: arc4random()
/
/ GLOBAL INPUTS: none
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION: 
/
*************************************************************************/

double
drandom(void)
{
	return((double) arc4random() / (UINT32_MAX + 1.0));
}
