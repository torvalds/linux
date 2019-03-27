/*-
 * spkr.c -- device driver for console speaker
 *
 * v1.4 by Eric S. Raymond (esr@snark.thyrsus.com) Aug 1993
 * modified for FreeBSD by Andrew A. Chernov <ache@astral.msk.su>
 * modified for PC98 by Kakefuda
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/ctype.h>
#include <sys/malloc.h>
#include <machine/clock.h>
#include <dev/speaker/speaker.h>

static	d_open_t	spkropen;
static	d_close_t	spkrclose;
static	d_write_t	spkrwrite;
static	d_ioctl_t	spkrioctl;

static struct cdevsw spkr_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	spkropen,
	.d_close =	spkrclose,
	.d_write =	spkrwrite,
	.d_ioctl =	spkrioctl,
	.d_name =	"spkr",
};

static MALLOC_DEFINE(M_SPKR, "spkr", "Speaker buffer");

/*
 **************** MACHINE DEPENDENT PART STARTS HERE *************************
 * This section defines a function tone() which causes a tone of given
 * frequency and duration from the ISA console speaker.
 * Another function endtone() is defined to force sound off, and there is
 * also a rest() entry point to do pauses.
 *
 * Audible sound is generated using the Programmable Interval Timer (PIT) and
 * Programmable Peripheral Interface (PPI) attached to the ISA speaker. The
 * PPI controls whether sound is passed through at all; the PIT's channel 2 is
 * used to generate clicks (a square wave) of whatever frequency is desired.
 */

#define SPKRPRI PSOCK
static char endtone, endrest;

static void tone(unsigned int thz, unsigned int centisecs);
static void rest(int centisecs);
static void playinit(void);
static void playtone(int pitch, int value, int sustain);
static void playstring(char *cp, size_t slen);

/* 
 * Emit tone of frequency thz for given number of centisecs 
 */
static void
tone(unsigned int thz, unsigned int centisecs)
{
	int sps, timo;

	if (thz <= 0)
		return;

#ifdef DEBUG
	(void) printf("tone: thz=%d centisecs=%d\n", thz, centisecs);
#endif /* DEBUG */

	/* set timer to generate clicks at given frequency in Hertz */
	sps = splclock();

	if (timer_spkr_acquire()) {
		/* enter list of waiting procs ??? */
		splx(sps);
		return;
	}
	splx(sps);
	disable_intr();
	timer_spkr_setfreq(thz);
	enable_intr();

	/*
	 * Set timeout to endtone function, then give up the timeslice.
	 * This is so other processes can execute while the tone is being
	 * emitted.
	 */
	timo = centisecs * hz / 100;
	if (timo > 0)
		tsleep(&endtone, SPKRPRI | PCATCH, "spkrtn", timo);
	sps = splclock();
	timer_spkr_release();
	splx(sps);
}

/*
 * Rest for given number of centisecs 
 */
static void
rest(int centisecs)
{
	int timo;

	/*
	 * Set timeout to endrest function, then give up the timeslice.
	 * This is so other processes can execute while the rest is being
	 * waited out.
	 */
#ifdef DEBUG
	(void) printf("rest: %d\n", centisecs);
#endif /* DEBUG */
	timo = centisecs * hz / 100;
	if (timo > 0)
		tsleep(&endrest, SPKRPRI | PCATCH, "spkrrs", timo);
}

/*
 **************** PLAY STRING INTERPRETER BEGINS HERE **********************
 * Play string interpretation is modelled on IBM BASIC 2.0's PLAY statement;
 * M[LNS] are missing; the ~ synonym and the _ slur mark and the octave-
 * tracking facility are added.
 * Requires tone(), rest(), and endtone(). String play is not interruptible
 * except possibly at physical block boundaries.
 */

#ifndef  __bool_true_false_are_defined
typedef int	bool;
#endif
#define TRUE	1
#define FALSE	0

#define dtoi(c)		((c) - '0')

static int octave;	/* currently selected octave */
static int whole;	/* whole-note time at current tempo, in ticks */
static int value;	/* whole divisor for note time, quarter note = 1 */
static int fill;	/* controls spacing of notes */
static bool octtrack;	/* octave-tracking on? */
static bool octprefix;	/* override current octave-tracking state? */

/*
 * Magic number avoidance...
 */
#define SECS_PER_MIN	60	/* seconds per minute */
#define WHOLE_NOTE	4	/* quarter notes per whole note */
#define MIN_VALUE	64	/* the most we can divide a note by */
#define DFLT_VALUE	4	/* default value (quarter-note) */
#define FILLTIME	8	/* for articulation, break note in parts */
#define STACCATO	6	/* 6/8 = 3/4 of note is filled */
#define NORMAL		7	/* 7/8ths of note interval is filled */
#define LEGATO		8	/* all of note interval is filled */
#define DFLT_OCTAVE	4	/* default octave */
#define MIN_TEMPO	32	/* minimum tempo */
#define DFLT_TEMPO	120	/* default tempo */
#define MAX_TEMPO	255	/* max tempo */
#define NUM_MULT	3	/* numerator of dot multiplier */
#define DENOM_MULT	2	/* denominator of dot multiplier */

/* letter to half-tone:  A   B  C  D  E  F  G */
static int notetab[8] = {9, 11, 0, 2, 4, 5, 7};

/*
 * This is the American Standard A440 Equal-Tempered scale with frequencies
 * rounded to nearest integer. Thank Goddess for the good ol' CRC Handbook...
 * our octave 0 is standard octave 2.
 */
#define OCTAVE_NOTES	12	/* semitones per octave */
static int pitchtab[] =
{
/*        C     C#    D     D#    E     F     F#    G     G#    A     A#    B*/
/* 0 */   65,   69,   73,   78,   82,   87,   93,   98,  103,  110,  117,  123,
/* 1 */  131,  139,  147,  156,  165,  175,  185,  196,  208,  220,  233,  247,
/* 2 */  262,  277,  294,  311,  330,  349,  370,  392,  415,  440,  466,  494,
/* 3 */  523,  554,  587,  622,  659,  698,  740,  784,  831,  880,  932,  988,
/* 4 */ 1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1975,
/* 5 */ 2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951,
/* 6 */ 4186, 4435, 4698, 4978, 5274, 5588, 5920, 6272, 6644, 7040, 7459, 7902,
};

static void
playinit()
{
    octave = DFLT_OCTAVE;
    whole = (100 * SECS_PER_MIN * WHOLE_NOTE) / DFLT_TEMPO;
    fill = NORMAL;
    value = DFLT_VALUE;
    octtrack = FALSE;
    octprefix = TRUE;	/* act as though there was an initial O(n) */
}

/* 
 * Play tone of proper duration for current rhythm signature 
 */
static void
playtone(int pitch, int value, int sustain)
{
	int sound, silence, snum = 1, sdenom = 1;

	/* this weirdness avoids floating-point arithmetic */
	for (; sustain; sustain--) {
		/* See the BUGS section in the man page for discussion */
		snum *= NUM_MULT;
		sdenom *= DENOM_MULT;
	}

	if (value == 0 || sdenom == 0)
		return;

	if (pitch == -1)
		rest(whole * snum / (value * sdenom));
	else {
		sound = (whole * snum) / (value * sdenom)
			- (whole * (FILLTIME - fill)) / (value * FILLTIME);
		silence = whole * (FILLTIME-fill) * snum / (FILLTIME * value * sdenom);

#ifdef DEBUG
		(void) printf("playtone: pitch %d for %d ticks, rest for %d ticks\n",
			pitch, sound, silence);
#endif /* DEBUG */

		tone(pitchtab[pitch], sound);
		if (fill != LEGATO)
			rest(silence);
	}
}

/*
 * Interpret and play an item from a notation string 
 */
static void
playstring(char *cp, size_t slen)
{
	int pitch, oldfill, lastpitch = OCTAVE_NOTES * DFLT_OCTAVE;

#define GETNUM(cp, v)	for(v=0; isdigit(cp[1]) && slen > 0; ) \
				{v = v * 10 + (*++cp - '0'); slen--;}
	for (; slen--; cp++) {
		int sustain, timeval, tempo;
		char c = toupper(*cp);

#ifdef DEBUG
		(void) printf("playstring: %c (%x)\n", c, c);
#endif /* DEBUG */

		switch (c) {
		case 'A':  
		case 'B': 
		case 'C': 
		case 'D': 
		case 'E': 
		case 'F': 
		case 'G':
			/* compute pitch */
			pitch = notetab[c - 'A'] + octave * OCTAVE_NOTES;

			/* this may be followed by an accidental sign */
			if (cp[1] == '#' || cp[1] == '+') {
				++pitch;
				++cp;
				slen--;
			} else if (cp[1] == '-') {
				--pitch;
				++cp;
				slen--;
			}

			/*
			 * If octave-tracking mode is on, and there has been no octave-
			 * setting prefix, find the version of the current letter note
			 * closest to the last regardless of octave.
			 */
			if (octtrack && !octprefix) {
				if (abs(pitch-lastpitch) > abs(pitch+OCTAVE_NOTES - 
					lastpitch)) {
					++octave;
					pitch += OCTAVE_NOTES;
				}

				if (abs(pitch-lastpitch) > abs((pitch-OCTAVE_NOTES) - 
					lastpitch)) {
					--octave;
					pitch -= OCTAVE_NOTES;
				}
			}
			octprefix = FALSE;
			lastpitch = pitch;

			/* ...which may in turn be followed by an override time value */
			GETNUM(cp, timeval);
			if (timeval <= 0 || timeval > MIN_VALUE)
				timeval = value;

			/* ...and/or sustain dots */
			for (sustain = 0; cp[1] == '.'; cp++) { 
				slen--;
				sustain++;
			}

			/* ...and/or a slur mark */
			oldfill = fill;
			if (cp[1] == '_') {
				fill = LEGATO;
				++cp;
				slen--;
			}

			/* time to emit the actual tone */
			playtone(pitch, timeval, sustain);

			fill = oldfill;
			break;
		case 'O':
			if (cp[1] == 'N' || cp[1] == 'n') {
				octprefix = octtrack = FALSE;
				++cp;
				slen--;
			} else if (cp[1] == 'L' || cp[1] == 'l') {
				octtrack = TRUE;
				++cp;
				slen--;
			} else {
				GETNUM(cp, octave);
				if (octave >= nitems(pitchtab) / OCTAVE_NOTES)
					octave = DFLT_OCTAVE;
				octprefix = TRUE;
			}
			break;
		case '>':
			if (octave < nitems(pitchtab) / OCTAVE_NOTES - 1)
				octave++;
			octprefix = TRUE;
			break;
		case '<':
			if (octave > 0)
				octave--;
			octprefix = TRUE;
			break;
		case 'N':
			GETNUM(cp, pitch);
			for (sustain = 0; cp[1] == '.'; cp++) {
				slen--;
				sustain++;
			}
			oldfill = fill;
			if (cp[1] == '_') {
				fill = LEGATO;
				++cp;
				slen--;
			}
			playtone(pitch - 1, value, sustain);
			fill = oldfill;
			break;
		case 'L':
			GETNUM(cp, value);
			if (value <= 0 || value > MIN_VALUE)
				value = DFLT_VALUE;
			break;
		case 'P':
		case '~':
			/* this may be followed by an override time value */
			GETNUM(cp, timeval);
			if (timeval <= 0 || timeval > MIN_VALUE)
				timeval = value;
			for (sustain = 0; cp[1] == '.'; cp++) {
				slen--;
				sustain++;
			}
			playtone(-1, timeval, sustain);
			break;
		case 'T':
			GETNUM(cp, tempo);
			if (tempo < MIN_TEMPO || tempo > MAX_TEMPO)
				tempo = DFLT_TEMPO;
			whole = (100 * SECS_PER_MIN * WHOLE_NOTE) / tempo;
			break;
		case 'M':
			if (cp[1] == 'N' || cp[1] == 'n') {
				fill = NORMAL;
				++cp;
				slen--;
			} else if (cp[1] == 'L' || cp[1] == 'l') {
				fill = LEGATO;
				++cp;
				slen--;
			} else if (cp[1] == 'S' || cp[1] == 's') {
				fill = STACCATO;
				++cp;
				slen--;
			}
			break;
		}
	}
}

/*
 * ****************** UNIX DRIVER HOOKS BEGIN HERE **************************
 * This section implements driver hooks to run playstring() and the tone(),
 * endtone(), and rest() functions defined above.
 */

static int spkr_active = FALSE; /* exclusion flag */
static char *spkr_inbuf;  /* incoming buf */

static int
spkropen(dev, flags, fmt, td)
	struct cdev *dev;
	int flags;
	int fmt;
	struct thread	*td;
{
#ifdef DEBUG
	(void) printf("spkropen: entering with dev = %s\n", devtoname(dev));
#endif /* DEBUG */

	if (spkr_active)
		return(EBUSY);
	else {
#ifdef DEBUG
		(void) printf("spkropen: about to perform play initialization\n");
#endif /* DEBUG */
		playinit();
		spkr_inbuf = malloc(DEV_BSIZE, M_SPKR, M_WAITOK);
		spkr_active = TRUE;
		return(0);
    	}
}

static int
spkrwrite(dev, uio, ioflag)
	struct cdev *dev;
	struct uio *uio;
	int ioflag;
{
#ifdef DEBUG
	printf("spkrwrite: entering with dev = %s, count = %zd\n",
		devtoname(dev), uio->uio_resid);
#endif /* DEBUG */

	if (uio->uio_resid > (DEV_BSIZE - 1))     /* prevent system crashes */
		return(E2BIG);	
	else {
		unsigned n;
		char *cp;
		int error;

		n = uio->uio_resid;
		cp = spkr_inbuf;
		error = uiomove(cp, n, uio);
		if (!error) {
			cp[n] = '\0';
			playstring(cp, n);
		}
	return(error);
	}
}

static int
spkrclose(dev, flags, fmt, td)
	struct cdev *dev;
	int flags;
	int fmt;
	struct thread *td;
{
#ifdef DEBUG
	(void) printf("spkrclose: entering with dev = %s\n", devtoname(dev));
#endif /* DEBUG */

	wakeup(&endtone);
	wakeup(&endrest);
	free(spkr_inbuf, M_SPKR);
	spkr_active = FALSE;
	return(0);
}

static int
spkrioctl(dev, cmd, cmdarg, flags, td)
	struct cdev *dev;
	unsigned long cmd;
	caddr_t cmdarg;
	int flags;
	struct thread *td;
{
#ifdef DEBUG
	(void) printf("spkrioctl: entering with dev = %s, cmd = %lx\n",
    		devtoname(dev), cmd);
#endif /* DEBUG */

	if (cmd == SPKRTONE) {
		tone_t	*tp = (tone_t *)cmdarg;

		if (tp->frequency == 0)
			rest(tp->duration);
		else
			tone(tp->frequency, tp->duration);
		return 0;
	} else if (cmd == SPKRTUNE) {
		tone_t  *tp = (tone_t *)(*(caddr_t *)cmdarg);
		tone_t ttp;
		int error;

		for (; ; tp++) {
			error = copyin(tp, &ttp, sizeof(tone_t));
			if (error)
				return(error);

			if (ttp.duration == 0)
				break;

			if (ttp.frequency == 0)
				rest(ttp.duration);
			else
				tone(ttp.frequency, ttp.duration);
		}
		return(0);
	}
	return(EINVAL);
}

static struct cdev *speaker_dev;

/*
 * Module handling
 */
static int
speaker_modevent(module_t mod, int type, void *data)
{
	int error = 0;

	switch(type) {
	case MOD_LOAD: 
		speaker_dev = make_dev(&spkr_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0600, "speaker");
		break;
	case MOD_SHUTDOWN:
	case MOD_UNLOAD:
		destroy_dev(speaker_dev);
		break;
	default:
		error = EOPNOTSUPP;
	}
	return (error);
}

DEV_MODULE(speaker, speaker_modevent, NULL);
