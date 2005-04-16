/* Solaris/SPARC constants and definitions -- 
 * (C) 1996 Miguel de Icaza
 *
 * This file is not meant to be included by user level applications
 * but the solaris syscall emulator
 */

#ifndef _SPARC_SVR4_H
#define _SPARC_SVR4_H

/* Signals as used by svr4 */
typedef struct {                /* signal set type */
	ulong sigbits[4];
} svr4_sigset_t;

/* Values for siginfo.code */
#define SVR4_SINOINFO 32767
/* Siginfo, sucker expects bunch of information on those parameters */
typedef union {
	char total_size [128];
	struct {
		int signo;
		int code;
		int error;
		union {
		} data; 
	} siginfo;
} svr4_siginfo_t;

/* Context definition */

/* Location of the user stored registers into a greg_t */
enum {
	SVR4_PSR, SVR4_PC, SVR4_NPC, SVR4_Y,
	SVR4_G1,  SVR4_G2, SVR4_G3,  SVR4_G4,
	SVR4_G5,  SVR4_G6, SVR4_G7,  SVR4_O0,
	SVR4_O1,  SVR4_O2, SVR4_O3,  SVR4_O4,
	SVR4_O5,  SVR4_O6, SVR4_O7
};

/* sizeof (regs) / sizeof (greg_t), defined in the ABI */
#define SVR4_NREGS  19
#define SVR4_MAXWIN 31

typedef struct {
	uint rwin_lo[8];
	uint rwin_in[8];
} svr4_rwindow_t;

typedef struct {
	int            count;
	int            __user *winptr [SVR4_MAXWIN]; /* pointer to the windows */
	svr4_rwindow_t win[SVR4_MAXWIN];      /* the windows */
} svr4_gwindows_t;

typedef int svr4_gregset_t[SVR4_NREGS];

typedef struct {
	double   fpu_regs[32];
	void     *fp_q;
	unsigned fp_fsr;
	u_char   fp_nqel;
	u_char   fp_nqsize;
	u_char   inuse;		/* if fpu is in use */
} svr4_fregset_t;

typedef struct {
	uint    id;		/* if this holds "xrs" string => ptr is valid */
	caddr_t ptr;
} svr4_xrs_t;

/* Machine dependent context */
typedef struct {
	svr4_gregset_t   greg;	/* registers 0..19 (see top) */
	svr4_gwindows_t  __user *gwin;	/* may point to register windows */
	svr4_fregset_t   freg;	/* floating point registers */
	svr4_xrs_t       xrs;	/* mhm? */
	long             pad[19];
} svr4_mcontext_t;

/* flags for stack_t.flags */
enum svr4_stack_flags {
	SVR4_SS_ONSTACK,
	SVR4_SS_DISABLE,
};

/* signal stack exection place, unsupported */
typedef struct svr4_stack_t {
        char __user *sp;
        int  size;
        int  flags;
} svr4_stack_t;

/* Context used by getcontext and setcontext */
typedef struct svr4_ucontext_t {
	u_long               flags; /* context flags, indicate what is loaded */
	struct svr4_ucontext *link;
	svr4_sigset_t        sigmask;
	svr4_stack_t         stack;
	svr4_mcontext_t      mcontext;
	long                 pad[23];
} svr4_ucontext_t;                          

/* windows hold the windows as they were at signal time,
 * ucontext->mcontext holds a pointer to them.
 * addresses for uc and si are passed as parameters to svr4 signal
 * handler
 */

/* This is the signal frame that is passed to the signal handler */
typedef struct {
	svr4_gwindows_t gw;	/* windows */
	svr4_ucontext_t uc;	/* machine context */
	svr4_siginfo_t  si;	/* siginfo */
} svr4_signal_frame_t;

#define SVR4_SF_ALIGNED (((sizeof (svr4_signal_frame_t) + 7) & (~7)))

#endif /* include control */
