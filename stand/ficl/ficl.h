/*******************************************************************
** f i c l . h
** Forth Inspired Command Language
** Author: John Sadler (john_sadler@alum.mit.edu)
** Created: 19 July 1997
** Dedicated to RHS, in loving memory
** $Id: ficl.h,v 1.18 2001/12/05 07:21:34 jsadler Exp $
*******************************************************************/
/*
** Copyright (c) 1997-2001 John Sadler (john_sadler@alum.mit.edu)
** All rights reserved.
**
** Get the latest Ficl release at http://ficl.sourceforge.net
**
** I am interested in hearing from anyone who uses ficl. If you have
** a problem, a success story, a defect, an enhancement request, or
** if you would like to contribute to the ficl release, please
** contact me by email at the address above.
**
** L I C E N S E  and  D I S C L A I M E R
** 
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
*/

/* $FreeBSD$ */

#if !defined (__FICL_H__)
#define __FICL_H__
/*
** Ficl (Forth-inspired command language) is an ANS Forth
** interpreter written in C. Unlike traditional Forths, this
** interpreter is designed to be embedded into other systems
** as a command/macro/development prototype language. 
**
** Where Forths usually view themselves as the center of the system
** and expect the rest of the system to be coded in Forth, Ficl
** acts as a component of the system. It is easy to export 
** code written in C or ASM to Ficl in the style of TCL, or to invoke
** Ficl code from a compiled module. This allows you to do incremental
** development in a way that combines the best features of threaded 
** languages (rapid development, quick code/test/debug cycle,
** reasonably fast) with the best features of C (everyone knows it,
** easier to support large blocks of code, efficient, type checking).
**
** Ficl provides facilities for interoperating
** with programs written in C: C functions can be exported to Ficl,
** and Ficl commands can be executed via a C calling interface. The
** interpreter is re-entrant, so it can be used in multiple instances
** in a multitasking system. Unlike Forth, Ficl's outer interpreter
** expects a text block as input, and returns to the caller after each
** text block, so the "data pump" is somewhere in external code. This
** is more like TCL than Forth, which usually expcets to be at the center
** of the system, requesting input at its convenience. Each Ficl virtual 
** machine can be bound to a different I/O channel, and is independent
** of all others in in the same address space except that all virtual
** machines share a common dictionary (a sort or open symbol table that
** defines all of the elements of the language).
**
** Code is written in ANSI C for portability. 
**
** Summary of Ficl features and constraints:
** - Standard: Implements the ANSI Forth CORE word set and part 
**   of the CORE EXT word-set, SEARCH and SEARCH EXT, TOOLS and
**   TOOLS EXT, LOCAL and LOCAL ext and various extras.
** - Extensible: you can export code written in Forth, C, 
**   or asm in a straightforward way. Ficl provides open
**   facilities for extending the language in an application
**   specific way. You can even add new control structures!
** - Ficl and C can interact in two ways: Ficl can encapsulate
**   C code, or C code can invoke Ficl code.
** - Thread-safe, re-entrant: The shared system dictionary 
**   uses a locking mechanism that you can either supply
**   or stub out to provide exclusive access. Each Ficl
**   virtual machine has an otherwise complete state, and
**   each can be bound to a separate I/O channel (or none at all).
** - Simple encapsulation into existing systems: a basic implementation
**   requires three function calls (see the example program in testmain.c).
** - ROMable: Ficl is designed to work in RAM-based and ROM code / RAM data
**   environments. It does require somewhat more memory than a pure
**   ROM implementation because it builds its system dictionary in 
**   RAM at startup time.
** - Written an ANSI C to be as simple as I can make it to understand,
**   support, debug, and port. Compiles without complaint at /Az /W4 
**   (require ANSI C, max warnings) under Microsoft VC++ 5.
** - Does full 32 bit math (but you need to implement
**   two mixed precision math primitives (see sysdep.c))
** - Indirect threaded interpreter is not the fastest kind of
**   Forth there is (see pForth 68K for a really fast subroutine
**   threaded interpreter), but it's the cleanest match to a
**   pure C implementation.
**
** P O R T I N G   F i c l
**
** To install Ficl on your target system, you need an ANSI C compiler
** and its runtime library. Inspect the system dependent macros and
** functions in sysdep.h and sysdep.c and edit them to suit your
** system. For example, INT16 is a short on some compilers and an
** int on others. Check the default CELL alignment controlled by
** FICL_ALIGN. If necessary, add new definitions of ficlMalloc, ficlFree,
** ficlLockDictionary, and ficlTextOut to work with your operating system.
** Finally, use testmain.c as a guide to installing the Ficl system and 
** one or more virtual machines into your code. You do not need to include
** testmain.c in your build.
**
** T o   D o   L i s t
**
** 1. Unimplemented system dependent CORE word: key
** 2. Ficl uses the PAD in some CORE words - this violates the standard,
**    but it's cleaner for a multithreaded system. I'll have to make a
**    second pad for reference by the word PAD to fix this.
**
** F o r   M o r e   I n f o r m a t i o n
**
** Web home of ficl
**   http://ficl.sourceforge.net
** Check this website for Forth literature (including the ANSI standard)
**   http://www.taygeta.com/forthlit.html
** and here for software and more links
**   http://www.taygeta.com/forth.html
**
** Obvious Performance enhancement opportunities
** Compile speed
** - work on interpret speed
** - turn off locals (FICL_WANT_LOCALS)
** Interpret speed 
** - Change inner interpreter (and everything else)
**   so that a definition is a list of pointers to functions
**   and inline data rather than pointers to words. This gets
**   rid of vm->runningWord and a level of indirection in the
**   inner loop. I'll look at it for ficl 3.0
** - Make the main hash table a bigger prime (HASHSIZE)
** - FORGET about twiddling the hash function - my experience is
**   that that is a waste of time.
** - Eliminate the need to pass the pVM parameter on the stack
**   by dedicating a register to it. Most words need access to the
**   vm, but the parameter passing overhead can be reduced. One way
**   requires that the host OS have a task switch callout. Create
**   a global variable for the running VM and refer to it in words
**   that need VM access. Alternative: use thread local storage. 
**   For single threaded implementations, you can just use a global.
**   The first two solutions create portability problems, so I
**   haven't considered doing them. Another possibility is to
**   declare the pVm parameter to be "register", and hope the compiler
**   pays attention.
**
*/

/*
** Revision History:
** 
** 15 Apr 1999 (sadler) Merged FreeBSD changes for exception wordset and
** counted strings in ficlExec. 
** 12 Jan 1999 (sobral) Corrected EVALUATE behavior. Now TIB has an
** "end" field, and all words respect this. ficlExec is passed a "size"
** of TIB, as well as vmPushTib. This size is used to calculate the "end"
** of the string, ie, base+size. If the size is not known, pass -1.
**
** 10 Jan 1999 (sobral) EXCEPTION word set has been added, and existing
** words has been modified to conform to EXCEPTION EXT word set. 
**
** 27 Aug 1998 (sadler) testing and corrections for LOCALS, LOCALS EXT,
**  SEARCH / SEARCH EXT, TOOLS / TOOLS EXT. 
**  Added .X to display in hex, PARSE and PARSE-WORD to supplement WORD,
**  EMPTY to clear stack.
**
** 29 jun 1998 (sadler) added variable sized hash table support
**  and ANS Forth optional SEARCH & SEARCH EXT word set.
** 26 May 1998 (sadler) 
**  FICL_PROMPT macro
** 14 April 1998 (sadler) V1.04
**  Ficlwin: Windows version, Skip Carter's Linux port
** 5 March 1998 (sadler) V1.03
**  Bug fixes -- passes John Ryan's ANS test suite "core.fr"
**
** 24 February 1998 (sadler) V1.02
** -Fixed bugs in <# # #>
** -Changed FICL_WORD so that storage for the name characters
**  can be allocated from the dictionary as needed rather than 
**  reserving 32 bytes in each word whether needed or not - 
**  this saved 50% of the dictionary storage requirement.
** -Added words in testmain for Win32 functions system,chdir,cwd,
**  also added a word that loads and evaluates a file.
**
** December 1997 (sadler)
** -Added VM_RESTART exception handling in ficlExec -- this lets words
**  that require additional text to succeed (like :, create, variable...)
**  recover gracefully from an empty input buffer rather than emitting
**  an error message. Definitions can span multiple input blocks with
**  no restrictions.
** -Changed #include order so that <assert.h> is included in sysdep.h,
**  and sysdep is included in all other files. This lets you define
**  NDEBUG in sysdep.h to disable assertions if you want to.
** -Make PC specific system dependent code conditional on _M_IX86
**  defined so that ports can coexist in sysdep.h/sysdep.c
*/

#ifdef __cplusplus
extern "C" {
#endif

#include "sysdep.h"
#include <limits.h> /* UCHAR_MAX */

/*
** Forward declarations... read on.
*/
struct ficl_word;
typedef struct ficl_word FICL_WORD;
struct vm;
typedef struct vm FICL_VM;
struct ficl_dict;
typedef struct ficl_dict FICL_DICT;
struct ficl_system;
typedef struct ficl_system FICL_SYSTEM;
struct ficl_system_info;
typedef struct ficl_system_info FICL_SYSTEM_INFO;

/* 
** the Good Stuff starts here...
*/
#define FICL_VER        "3.03"
#define FICL_VER_MAJOR  3
#define FICL_VER_MINOR  3
#if !defined (FICL_PROMPT)
#define FICL_PROMPT "ok> "
#endif

/*
** ANS Forth requires false to be zero, and true to be the ones
** complement of false... that unifies logical and bitwise operations
** nicely.
*/
#define FICL_TRUE  ((unsigned long)~(0L))
#define FICL_FALSE (0)
#define FICL_BOOL(x) ((x) ? FICL_TRUE : FICL_FALSE)


/*
** A CELL is the main storage type. It must be large enough
** to contain a pointer or a scalar. In order to accommodate 
** 32 bit and 64 bit processors, use abstract types for int, 
** unsigned, and float.
*/
typedef union _cell
{
    FICL_INT i;
    FICL_UNS u;
#if (FICL_WANT_FLOAT)
    FICL_FLOAT f;
#endif
    void *p;
    void (*fn)(void);
} CELL;

/*
** LVALUEtoCELL does a little pointer trickery to cast any CELL sized
** lvalue (informal definition: an expression whose result has an
** address) to CELL. Remember that constants and casts are NOT
** themselves lvalues!
*/
#define LVALUEtoCELL(v) (*(CELL *)&v)

/*
** PTRtoCELL is a cast through void * intended to satisfy the
** most outrageously pedantic compiler... (I won't mention 
** its name)
*/
#define PTRtoCELL (CELL *)(void *)
#define PTRtoSTRING (FICL_STRING *)(void *)

/*
** Strings in FICL are stored in Pascal style - with a count
** preceding the text. We'll also NULL-terminate them so that 
** they work with the usual C lib string functions. (Belt &
** suspenders? You decide.)
** STRINGINFO hides the implementation with a couple of
** macros for use in internal routines.
*/

typedef unsigned char FICL_COUNT;
#define FICL_STRING_MAX UCHAR_MAX
typedef struct _ficl_string
{
    FICL_COUNT count;
    char text[1];
} FICL_STRING;

typedef struct 
{
    FICL_UNS count;
    char *cp;
} STRINGINFO;

#define SI_COUNT(si) (si.count)
#define SI_PTR(si)   (si.cp)
#define SI_SETLEN(si, len) (si.count = (FICL_UNS)(len))
#define SI_SETPTR(si, ptr) (si.cp = (char *)(ptr))
/* 
** Init a STRINGINFO from a pointer to NULL-terminated string
*/
#define SI_PSZ(si, psz) \
            {si.cp = psz; si.count = (FICL_COUNT)strlen(psz);}
/* 
** Init a STRINGINFO from a pointer to FICL_STRING
*/
#define SI_PFS(si, pfs) \
            {si.cp = pfs->text; si.count = pfs->count;}

/*
** Ficl uses this little structure to hold the address of 
** the block of text it's working on and an index to the next
** unconsumed character in the string. Traditionally, this is
** done by a Text Input Buffer, so I've called this struct TIB.
**
** Since this structure also holds the size of the input buffer,
** and since evaluate requires that, let's put the size here.
** The size is stored as an end-pointer because that is what the
** null-terminated string aware functions find most easy to deal
** with.
** Notice, though, that nobody really uses this except evaluate,
** so it might just be moved to FICL_VM instead. (sobral)
*/
typedef struct
{
    FICL_INT index;
    char *end;
    char *cp;
} TIB;


/*
** Stacks get heavy use in Ficl and Forth...
** Each virtual machine implements two of them:
** one holds parameters (data), and the other holds return
** addresses and control flow information for the virtual
** machine. (Note: C's automatic stack is implicitly used,
** but not modeled because it doesn't need to be...)
** Here's an abstract type for a stack
*/
typedef struct _ficlStack
{
    FICL_UNS nCells;    /* size of the stack */
    CELL *pFrame;       /* link reg for stack frame */
    CELL *sp;           /* stack pointer */
    CELL base[1];       /* Top of stack */
} FICL_STACK;

/*
** Stack methods... many map closely to required Forth words.
*/
FICL_STACK *stackCreate   (unsigned nCells);
void        stackDelete   (FICL_STACK *pStack);
int         stackDepth    (FICL_STACK *pStack);
void        stackDrop     (FICL_STACK *pStack, int n);
CELL        stackFetch    (FICL_STACK *pStack, int n);
CELL        stackGetTop   (FICL_STACK *pStack);
void        stackLink     (FICL_STACK *pStack, int nCells);
void        stackPick     (FICL_STACK *pStack, int n);
CELL        stackPop      (FICL_STACK *pStack);
void       *stackPopPtr   (FICL_STACK *pStack);
FICL_UNS    stackPopUNS   (FICL_STACK *pStack);
FICL_INT    stackPopINT   (FICL_STACK *pStack);
void        stackPush     (FICL_STACK *pStack, CELL c);
void        stackPushPtr  (FICL_STACK *pStack, void *ptr);
void        stackPushUNS  (FICL_STACK *pStack, FICL_UNS u);
void        stackPushINT  (FICL_STACK *pStack, FICL_INT i);
void        stackReset    (FICL_STACK *pStack);
void        stackRoll     (FICL_STACK *pStack, int n);
void        stackSetTop   (FICL_STACK *pStack, CELL c);
void        stackStore    (FICL_STACK *pStack, int n, CELL c);
void        stackUnlink   (FICL_STACK *pStack);

#if (FICL_WANT_FLOAT)
float       stackPopFloat (FICL_STACK *pStack);
void        stackPushFloat(FICL_STACK *pStack, FICL_FLOAT f);
#endif

/*
** Shortcuts (Guy Carver)
*/
#define PUSHPTR(p)   stackPushPtr(pVM->pStack,p)
#define PUSHUNS(u)   stackPushUNS(pVM->pStack,u)
#define PUSHINT(i)   stackPushINT(pVM->pStack,i)
#define PUSHFLOAT(f) stackPushFloat(pVM->fStack,f)
#define PUSH(c)      stackPush(pVM->pStack,c)
#define POPPTR()     stackPopPtr(pVM->pStack)
#define POPUNS()     stackPopUNS(pVM->pStack)
#define POPINT()     stackPopINT(pVM->pStack)
#define POPFLOAT()   stackPopFloat(pVM->fStack)
#define POP()        stackPop(pVM->pStack)
#define GETTOP()     stackGetTop(pVM->pStack)
#define SETTOP(c)    stackSetTop(pVM->pStack,LVALUEtoCELL(c))
#define GETTOPF()    stackGetTop(pVM->fStack)
#define SETTOPF(c)   stackSetTop(pVM->fStack,LVALUEtoCELL(c))
#define STORE(n,c)   stackStore(pVM->pStack,n,LVALUEtoCELL(c))
#define DEPTH()      stackDepth(pVM->pStack)
#define DROP(n)      stackDrop(pVM->pStack,n)
#define DROPF(n)     stackDrop(pVM->fStack,n)
#define FETCH(n)     stackFetch(pVM->pStack,n)
#define PICK(n)      stackPick(pVM->pStack,n)
#define PICKF(n)     stackPick(pVM->fStack,n)
#define ROLL(n)      stackRoll(pVM->pStack,n)
#define ROLLF(n)     stackRoll(pVM->fStack,n)

/* 
** The virtual machine (VM) contains the state for one interpreter.
** Defined operations include:
** Create & initialize
** Delete
** Execute a block of text
** Parse a word out of the input stream
** Call return, and branch 
** Text output
** Throw an exception
*/

typedef FICL_WORD ** IPTYPE; /* the VM's instruction pointer */

/*
** Each VM has a placeholder for an output function -
** this makes it possible to have each VM do I/O
** through a different device. If you specify no
** OUTFUNC, it defaults to ficlTextOut.
*/
typedef void (*OUTFUNC)(FICL_VM *pVM, char *text, int fNewline);

/*
** Each VM operates in one of two non-error states: interpreting
** or compiling. When interpreting, words are simply executed.
** When compiling, most words in the input stream have their
** addresses inserted into the word under construction. Some words
** (known as IMMEDIATE) are executed in the compile state, too.
*/
/* values of STATE */
#define INTERPRET 0
#define COMPILE   1

/*
** The pad is a small scratch area for text manipulation. ANS Forth
** requires it to hold at least 84 characters.
*/
#if !defined nPAD
#define nPAD 256
#endif

/* 
** ANS Forth requires that a word's name contain {1..31} characters.
*/
#if !defined nFICLNAME
#define nFICLNAME       31
#endif

/*
** OK - now we can really define the VM...
*/
struct vm
{
    FICL_SYSTEM    *pSys;       /* Which system this VM belongs to  */
    FICL_VM        *link;       /* Ficl keeps a VM list for simple teardown */
    jmp_buf        *pState;     /* crude exception mechanism...     */
    OUTFUNC         textOut;    /* Output callback - see sysdep.c   */
    void *          pExtend;    /* vm extension pointer for app use - initialized from FICL_SYSTEM */
    short           fRestart;   /* Set TRUE to restart runningWord  */
    IPTYPE          ip;         /* instruction pointer              */
    FICL_WORD      *runningWord;/* address of currently running word (often just *(ip-1) ) */
    FICL_UNS        state;      /* compiling or interpreting        */
    FICL_UNS        base;       /* number conversion base           */
    FICL_STACK     *pStack;     /* param stack                      */
    FICL_STACK     *rStack;     /* return stack                     */
#if FICL_WANT_FLOAT
    FICL_STACK     *fStack;     /* float stack (optional)           */
#endif
    CELL            sourceID;   /* -1 if EVALUATE, 0 if normal input */
    TIB             tib;        /* address of incoming text string  */
#if FICL_WANT_USER
    CELL            user[FICL_USER_CELLS];
#endif
    char            pad[nPAD];  /* the scratch area (see above)     */
};

/*
** A FICL_CODE points to a function that gets called to help execute
** a word in the dictionary. It always gets passed a pointer to the
** running virtual machine, and from there it can get the address
** of the parameter area of the word it's supposed to operate on.
** For precompiled words, the code is all there is. For user defined
** words, the code assumes that the word's parameter area is a list
** of pointers to the code fields of other words to execute, and
** may also contain inline data. The first parameter is always
** a pointer to a code field.
*/
typedef void (*FICL_CODE)(FICL_VM *pVm);

#if 0
#define VM_ASSERT(pVM) assert((*(pVM->ip - 1)) == pVM->runningWord)
#else
#define VM_ASSERT(pVM) 
#endif

/* 
** Ficl models memory as a contiguous space divided into
** words in a linked list called the dictionary.
** A FICL_WORD starts each entry in the list.
** Version 1.02: space for the name characters is allotted from
** the dictionary ahead of the word struct, rather than using
** a fixed size array for each name.
*/
struct ficl_word
{
    struct ficl_word *link;     /* Previous word in the dictionary      */
    UNS16 hash;
    UNS8 flags;                 /* Immediate, Smudge, Compile-only      */
    FICL_COUNT nName;           /* Number of chars in word name         */
    char *name;                 /* First nFICLNAME chars of word name   */
    FICL_CODE code;             /* Native code to execute the word      */
    CELL param[1];              /* First data cell of the word          */
};

/*
** Worst-case size of a word header: nFICLNAME chars in name
*/
#define CELLS_PER_WORD  \
    ( (sizeof (FICL_WORD) + nFICLNAME + sizeof (CELL)) \
                          / (sizeof (CELL)) )

int wordIsImmediate(FICL_WORD *pFW);
int wordIsCompileOnly(FICL_WORD *pFW);

/* flag values for word header */
#define FW_IMMEDIATE    1   /* execute me even if compiling */
#define FW_COMPILE      2   /* error if executed when not compiling */
#define FW_SMUDGE       4   /* definition in progress - hide me */
#define FW_ISOBJECT     8   /* word is an object or object member variable */

#define FW_COMPIMMED    (FW_IMMEDIATE | FW_COMPILE)
#define FW_DEFAULT      0


/*
** Exit codes for vmThrow
*/
#define VM_INNEREXIT -256   /* tell ficlExecXT to exit inner loop */
#define VM_OUTOFTEXT -257   /* hungry - normal exit */
#define VM_RESTART   -258   /* word needs more text to succeed - re-run it */
#define VM_USEREXIT  -259   /* user wants to quit */
#define VM_ERREXIT   -260   /* interp found an error */
#define VM_BREAK     -261   /* debugger breakpoint */
#define VM_ABORT       -1   /* like errexit -- abort */
#define VM_ABORTQ      -2   /* like errexit -- abort" */
#define VM_QUIT       -56   /* like errexit, but leave pStack & base alone */


void        vmBranchRelative(FICL_VM *pVM, int offset);
FICL_VM *   vmCreate       (FICL_VM *pVM, unsigned nPStack, unsigned nRStack);
void        vmDelete       (FICL_VM *pVM);
void        vmExecute      (FICL_VM *pVM, FICL_WORD *pWord);
FICL_DICT  *vmGetDict      (FICL_VM *pVM);
char *      vmGetString    (FICL_VM *pVM, FICL_STRING *spDest, char delimiter);
STRINGINFO  vmGetWord      (FICL_VM *pVM);
STRINGINFO  vmGetWord0     (FICL_VM *pVM);
int         vmGetWordToPad (FICL_VM *pVM);
STRINGINFO  vmParseString  (FICL_VM *pVM, char delimiter);
STRINGINFO  vmParseStringEx(FICL_VM *pVM, char delimiter, char fSkipLeading);
CELL        vmPop          (FICL_VM *pVM);
void        vmPush         (FICL_VM *pVM, CELL c);
void        vmPopIP        (FICL_VM *pVM);
void        vmPushIP       (FICL_VM *pVM, IPTYPE newIP);
void        vmQuit         (FICL_VM *pVM);
void        vmReset        (FICL_VM *pVM);
void        vmSetTextOut   (FICL_VM *pVM, OUTFUNC textOut);
void        vmTextOut      (FICL_VM *pVM, char *text, int fNewline);
void        vmTextOut      (FICL_VM *pVM, char *text, int fNewline);
void        vmThrow        (FICL_VM *pVM, int except);
void        vmThrowErr     (FICL_VM *pVM, char *fmt, ...);

#define vmGetRunningWord(pVM) ((pVM)->runningWord)


/*
** The inner interpreter - coded as a macro (see note for 
** INLINE_INNER_LOOP in sysdep.h for complaints about VC++ 5
*/
#define M_VM_STEP(pVM) \
        FICL_WORD *tempFW = *(pVM)->ip++; \
        (pVM)->runningWord = tempFW; \
        tempFW->code(pVM); 

#define M_INNER_LOOP(pVM) \
    for (;;)  { M_VM_STEP(pVM) }


#if INLINE_INNER_LOOP != 0
#define     vmInnerLoop(pVM) M_INNER_LOOP(pVM)
#else
void        vmInnerLoop(FICL_VM *pVM);
#endif

/*
** vmCheckStack needs a vm pointer because it might have to say
** something if it finds a problem. Parms popCells and pushCells
** correspond to the number of parameters on the left and right of 
** a word's stack effect comment.
*/
void        vmCheckStack(FICL_VM *pVM, int popCells, int pushCells);
#if FICL_WANT_FLOAT
void        vmCheckFStack(FICL_VM *pVM, int popCells, int pushCells);
#endif

/*
** TIB access routines...
** ANS forth seems to require the input buffer to be represented 
** as a pointer to the start of the buffer, and an index to the
** next character to read.
** PushTib points the VM to a new input string and optionally
**  returns a copy of the current state
** PopTib restores the TIB state given a saved TIB from PushTib
** GetInBuf returns a pointer to the next unused char of the TIB
*/
void        vmPushTib  (FICL_VM *pVM, char *text, FICL_INT nChars, TIB *pSaveTib);
void        vmPopTib   (FICL_VM *pVM, TIB *pTib);
#define     vmGetInBuf(pVM)      ((pVM)->tib.cp + (pVM)->tib.index)
#define     vmGetInBufLen(pVM)   ((pVM)->tib.end - (pVM)->tib.cp)
#define     vmGetInBufEnd(pVM)   ((pVM)->tib.end)
#define     vmGetTibIndex(pVM)    (pVM)->tib.index
#define     vmSetTibIndex(pVM, i) (pVM)->tib.index = i
#define     vmUpdateTib(pVM, str) (pVM)->tib.index = (str) - (pVM)->tib.cp

/*
** Generally useful string manipulators omitted by ANSI C...
** ltoa complements strtol
*/
#if defined(_WIN32) && !FICL_MAIN
/* #SHEESH
** Why do Microsoft Meatballs insist on contaminating
** my namespace with their string functions???
*/
#pragma warning(disable: 4273)
#endif

int        isPowerOfTwo(FICL_UNS u);

char       *ltoa( FICL_INT value, char *string, int radix );
char       *ultoa(FICL_UNS value, char *string, int radix );
char        digit_to_char(int value);
char       *strrev( char *string );
char       *skipSpace(char *cp, char *end);
char       *caseFold(char *cp);
int         strincmp(char *cp1, char *cp2, FICL_UNS count);

#if defined(_WIN32) && !FICL_MAIN
#pragma warning(default: 4273)
#endif

/*
** Ficl hash table - variable size.
** assert(size > 0)
** If size is 1, the table degenerates into a linked list.
** A WORDLIST (see the search order word set in DPANS) is
** just a pointer to a FICL_HASH in this implementation.
*/
#if !defined HASHSIZE /* Default size of hash table. For most uniform */
#define HASHSIZE 241  /*   performance, use a prime number!   */
#endif

typedef struct ficl_hash 
{
    struct ficl_hash *link;  /* link to parent class wordlist for OO */
    char      *name;         /* optional pointer to \0 terminated wordlist name */
    unsigned   size;         /* number of buckets in the hash */
    FICL_WORD *table[1];
} FICL_HASH;

void        hashForget    (FICL_HASH *pHash, void *where);
UNS16       hashHashCode  (STRINGINFO si);
void        hashInsertWord(FICL_HASH *pHash, FICL_WORD *pFW);
FICL_WORD  *hashLookup    (FICL_HASH *pHash, STRINGINFO si, UNS16 hashCode);
void        hashReset     (FICL_HASH *pHash);

/*
** A Dictionary is a linked list of FICL_WORDs. It is also Ficl's
** memory model. Description of fields:
**
** here -- points to the next free byte in the dictionary. This
**      pointer is forced to be CELL-aligned before a definition is added.
**      Do not assume any specific alignment otherwise - Use dictAlign().
**
** smudge -- pointer to word currently being defined (or last defined word)
**      If the definition completes successfully, the word will be
**      linked into the hash table. If unsuccessful, dictUnsmudge
**      uses this pointer to restore the previous state of the dictionary.
**      Smudge prevents unintentional recursion as a side-effect: the
**      dictionary search algo examines only completed definitions, so a 
**      word cannot invoke itself by name. See the ficl word "recurse".
**      NOTE: smudge always points to the last word defined. IMMEDIATE
**      makes use of this fact. Smudge is initially NULL.
**
** pForthWords -- pointer to the default wordlist (FICL_HASH).
**      This is the initial compilation list, and contains all
**      ficl's precompiled words.
**
** pCompile -- compilation wordlist - initially equal to pForthWords
** pSearch  -- array of pointers to wordlists. Managed as a stack.
**      Highest index is the first list in the search order.
** nLists   -- number of lists in pSearch. nLists-1 is the highest 
**      filled slot in pSearch, and points to the first wordlist
**      in the search order
** size -- number of cells in the dictionary (total)
** dict -- start of data area. Must be at the end of the struct.
*/
struct ficl_dict
{
    CELL *here;
    FICL_WORD *smudge;
    FICL_HASH *pForthWords;
    FICL_HASH *pCompile;
    FICL_HASH *pSearch[FICL_DEFAULT_VOCS];
    int        nLists;
    unsigned   size;    /* Number of cells in dict (total)*/
    CELL       *dict;   /* Base of dictionary memory      */
};

void       *alignPtr(void *ptr);
void        dictAbortDefinition(FICL_DICT *pDict);
void        dictAlign      (FICL_DICT *pDict);
int         dictAllot      (FICL_DICT *pDict, int n);
int         dictAllotCells (FICL_DICT *pDict, int nCells);
void        dictAppendCell (FICL_DICT *pDict, CELL c);
void        dictAppendChar (FICL_DICT *pDict, char c);
FICL_WORD  *dictAppendWord (FICL_DICT *pDict, 
                           char *name, 
                           FICL_CODE pCode, 
                           UNS8 flags);
FICL_WORD  *dictAppendWord2(FICL_DICT *pDict, 
                           STRINGINFO si, 
                           FICL_CODE pCode, 
                           UNS8 flags);
void        dictAppendUNS  (FICL_DICT *pDict, FICL_UNS u);
int         dictCellsAvail (FICL_DICT *pDict);
int         dictCellsUsed  (FICL_DICT *pDict);
void        dictCheck      (FICL_DICT *pDict, FICL_VM *pVM, int n);
void        dictCheckThreshold(FICL_DICT* dp);
FICL_DICT  *dictCreate(unsigned nCELLS);
FICL_DICT  *dictCreateHashed(unsigned nCells, unsigned nHash);
FICL_HASH  *dictCreateWordlist(FICL_DICT *dp, int nBuckets);
void        dictDelete     (FICL_DICT *pDict);
void        dictEmpty      (FICL_DICT *pDict, unsigned nHash);
#if FICL_WANT_FLOAT
void        dictHashSummary(FICL_VM *pVM);
#endif
int         dictIncludes   (FICL_DICT *pDict, void *p);
FICL_WORD  *dictLookup     (FICL_DICT *pDict, STRINGINFO si);
#if FICL_WANT_LOCALS
FICL_WORD  *ficlLookupLoc  (FICL_SYSTEM *pSys, STRINGINFO si);
#endif
void        dictResetSearchOrder(FICL_DICT *pDict);
void        dictSetFlags   (FICL_DICT *pDict, UNS8 set, UNS8 clr);
void        dictSetImmediate(FICL_DICT *pDict);
void        dictUnsmudge   (FICL_DICT *pDict);
CELL       *dictWhere      (FICL_DICT *pDict);


/* 
** P A R S E   S T E P
** (New for 2.05)
** See words.c: interpWord
** By default, ficl goes through two attempts to parse each token from its input
** stream: it first attempts to match it with a word in the dictionary, and
** if that fails, it attempts to convert it into a number. This mechanism is now
** extensible by additional steps. This allows extensions like floating point and 
** double number support to be factored cleanly.
**
** Each parse step is a function that receives the next input token as a STRINGINFO.
** If the parse step matches the token, it must apply semantics to the token appropriate
** to the present value of VM.state (compiling or interpreting), and return FICL_TRUE.
** Otherwise it returns FICL_FALSE. See words.c: isNumber for an example
**
** Note: for the sake of efficiency, it's a good idea both to limit the number
** of parse steps and to code each parse step so that it rejects tokens that
** do not match as quickly as possible.
*/

typedef int (*FICL_PARSE_STEP)(FICL_VM *pVM, STRINGINFO si);

/*
** Appends a parse step function to the end of the parse list (see 
** FICL_PARSE_STEP notes in ficl.h for details). Returns 0 if successful,
** nonzero if there's no more room in the list. Each parse step is a word in 
** the dictionary. Precompiled parse steps can use (PARSE-STEP) as their 
** CFA - see parenParseStep in words.c.
*/
int  ficlAddParseStep(FICL_SYSTEM *pSys, FICL_WORD *pFW); /* ficl.c */
void ficlAddPrecompiledParseStep(FICL_SYSTEM *pSys, char *name, FICL_PARSE_STEP pStep);
void ficlListParseSteps(FICL_VM *pVM);

/*
** FICL_BREAKPOINT record.
** origXT - if NULL, this breakpoint is unused. Otherwise it stores the xt 
** that the breakpoint overwrote. This is restored to the dictionary when the
** BP executes or gets cleared
** address - the location of the breakpoint (address of the instruction that
**           has been replaced with the breakpoint trap
** origXT  - The original contents of the location with the breakpoint
** Note: address is NULL when this breakpoint is empty
*/
typedef struct FICL_BREAKPOINT
{
    void      *address;
    FICL_WORD *origXT;
} FICL_BREAKPOINT;


/*
** F I C L _ S Y S T E M
** The top level data structure of the system - ficl_system ties a list of
** virtual machines with their corresponding dictionaries. Ficl 3.0 will
** support multiple Ficl systems, allowing multiple concurrent sessions 
** to separate dictionaries with some constraints. 
** The present model allows multiple sessions to one dictionary provided
** you implement ficlLockDictionary() as specified in sysdep.h
** Note: the pExtend pointer is there to provide context for applications. It is copied
** to each VM's pExtend field as that VM is created.
*/
struct ficl_system 
{
    FICL_SYSTEM *link;
    void *pExtend;      /* Initializes VM's pExtend pointer (for application use) */
    FICL_VM *vmList;
    FICL_DICT *dp;
    FICL_DICT *envp;
#ifdef FICL_WANT_LOCALS
    FICL_DICT *localp;
#endif
    FICL_WORD *pInterp[3];
    FICL_WORD *parseList[FICL_MAX_PARSE_STEPS];
	OUTFUNC    textOut;

	FICL_WORD *pBranchParen;
	FICL_WORD *pDoParen;
	FICL_WORD *pDoesParen;
	FICL_WORD *pExitInner;
	FICL_WORD *pExitParen;
	FICL_WORD *pBranch0;
	FICL_WORD *pInterpret;
	FICL_WORD *pLitParen;
	FICL_WORD *pTwoLitParen;
	FICL_WORD *pLoopParen;
	FICL_WORD *pPLoopParen;
	FICL_WORD *pQDoParen;
	FICL_WORD *pSemiParen;
	FICL_WORD *pOfParen;
	FICL_WORD *pStore;
	FICL_WORD *pDrop;
	FICL_WORD *pCStringLit;
	FICL_WORD *pStringLit;

#if FICL_WANT_LOCALS
	FICL_WORD *pGetLocalParen;
	FICL_WORD *pGet2LocalParen;
	FICL_WORD *pGetLocal0;
	FICL_WORD *pGetLocal1;
	FICL_WORD *pToLocalParen;
	FICL_WORD *pTo2LocalParen;
	FICL_WORD *pToLocal0;
	FICL_WORD *pToLocal1;
	FICL_WORD *pLinkParen;
	FICL_WORD *pUnLinkParen;
	FICL_INT   nLocals;
	CELL *pMarkLocals;
#endif

	FICL_BREAKPOINT bpStep;
};

struct ficl_system_info
{
	int size;           /* structure size tag for versioning */
	int nDictCells;     /* Size of system's Dictionary */
	OUTFUNC textOut;    /* default textOut function */
	void *pExtend;      /* Initializes VM's pExtend pointer - for application use */
    int nEnvCells;      /* Size of Environment dictionary */
};


#define ficlInitInfo(x) { memset((x), 0, sizeof(FICL_SYSTEM_INFO)); \
         (x)->size = sizeof(FICL_SYSTEM_INFO); }

/*
** External interface to FICL...
*/
/* 
** f i c l I n i t S y s t e m
** Binds a global dictionary to the interpreter system and initializes
** the dict to contain the ANSI CORE wordset. 
** You can specify the address and size of the allocated area.
** Using ficlInitSystemEx you can also specify the text output function.
** After that, ficl manages it.
** First step is to set up the static pointers to the area.
** Then write the "precompiled" portion of the dictionary in.
** The dictionary needs to be at least large enough to hold the
** precompiled part. Try 1K cells minimum. Use "words" to find
** out how much of the dictionary is used at any time.
*/
FICL_SYSTEM *ficlInitSystemEx(FICL_SYSTEM_INFO *fsi);

/* Deprecated call */
FICL_SYSTEM *ficlInitSystem(int nDictCells);

/*
** f i c l T e r m S y s t e m
** Deletes the system dictionary and all virtual machines that
** were created with ficlNewVM (see below). Call this function to
** reclaim all memory used by the dictionary and VMs.
*/
void       ficlTermSystem(FICL_SYSTEM *pSys);

/*
** f i c l E v a l u a t e
** Evaluates a block of input text in the context of the
** specified interpreter. Also sets SOURCE-ID properly.
**
** PLEASE USE THIS FUNCTION when throwing a hard-coded
** string to the FICL interpreter.
*/
int        ficlEvaluate(FICL_VM *pVM, char *pText);

/*
** f i c l E x e c
** Evaluates a block of input text in the context of the
** specified interpreter. Emits any requested output to the
** interpreter's output function. If the input string is NULL
** terminated, you can pass -1 as nChars rather than count it.
** Execution returns when the text block has been executed,
** or an error occurs.
** Returns one of the VM_XXXX codes defined in ficl.h:
** VM_OUTOFTEXT is the normal exit condition
** VM_ERREXIT means that the interp encountered a syntax error
**      and the vm has been reset to recover (some or all
**      of the text block got ignored
** VM_USEREXIT means that the user executed the "bye" command
**      to shut down the interpreter. This would be a good
**      time to delete the vm, etc -- or you can ignore this
**      signal.
** VM_ABORT and VM_ABORTQ are generated by 'abort' and 'abort"'
**      commands.
** Preconditions: successful execution of ficlInitSystem,
**      Successful creation and init of the VM by ficlNewVM (or equiv)
**
** If you call ficlExec() or one of its brothers, you MUST
** ensure pVM->sourceID was set to a sensible value.
** ficlExec() explicitly DOES NOT manage SOURCE-ID for you.
*/
int        ficlExec (FICL_VM *pVM, char *pText);
int        ficlExecC(FICL_VM *pVM, char *pText, FICL_INT nChars);
int        ficlExecXT(FICL_VM *pVM, FICL_WORD *pWord);

/*
** ficlExecFD(FICL_VM *pVM, int fd);
 * Evaluates text from file passed in via fd.
 * Execution returns when all of file has been executed or an
 * error occurs.
 */
int        ficlExecFD(FICL_VM *pVM, int fd);

/*
** Create a new VM from the heap, and link it into the system VM list.
** Initializes the VM and binds default sized stacks to it. Returns the
** address of the VM, or NULL if an error occurs.
** Precondition: successful execution of ficlInitSystem
*/
FICL_VM   *ficlNewVM(FICL_SYSTEM *pSys);

/*
** Force deletion of a VM. You do not need to do this 
** unless you're creating and discarding a lot of VMs.
** For systems that use a constant pool of VMs for the life
** of the system, ficltermSystem takes care of VM cleanup
** automatically.
*/
void ficlFreeVM(FICL_VM *pVM);


/*
** Set the stack sizes (return and parameter) to be used for all
** subsequently created VMs. Returns actual stack size to be used.
*/
int ficlSetStackSize(int nStackCells);

/*
** Returns the address of the most recently defined word in the system
** dictionary with the given name, or NULL if no match.
** Precondition: successful execution of ficlInitSystem
*/
FICL_WORD *ficlLookup(FICL_SYSTEM *pSys, char *name);

/*
** f i c l G e t D i c t
** Utility function - returns the address of the system dictionary.
** Precondition: successful execution of ficlInitSystem
*/
FICL_DICT *ficlGetDict(FICL_SYSTEM *pSys);
FICL_DICT *ficlGetEnv (FICL_SYSTEM *pSys);
void       ficlSetEnv (FICL_SYSTEM *pSys, char *name, FICL_UNS value);
void       ficlSetEnvD(FICL_SYSTEM *pSys, char *name, FICL_UNS hi, FICL_UNS lo);
#if FICL_WANT_LOCALS
FICL_DICT *ficlGetLoc (FICL_SYSTEM *pSys);
#endif
/* 
** f i c l B u i l d
** Builds a word into the system default dictionary in a thread-safe way.
** Preconditions: system must be initialized, and there must
** be enough space for the new word's header! Operation is
** controlled by ficlLockDictionary, so any initialization
** required by your version of the function (if you "overrode"
** it) must be complete at this point.
** Parameters:
** name  -- the name of the word to be built
** code  -- code to execute when the word is invoked - must take a single param
**          pointer to a FICL_VM
** flags -- 0 or more of FW_IMMEDIATE, FW_COMPILE, use bitwise OR! 
**          Most words can use FW_DEFAULT.
** nAllot - number of extra cells to allocate in the parameter area (usually zero)
*/
int        ficlBuild(FICL_SYSTEM *pSys, char *name, FICL_CODE code, char flags);

/* 
** f i c l C o m p i l e C o r e
** Builds the ANS CORE wordset into the dictionary - called by
** ficlInitSystem - no need to waste dict space by doing it again.
*/
void       ficlCompileCore(FICL_SYSTEM *pSys);
void       ficlCompilePrefix(FICL_SYSTEM *pSys);
void       ficlCompileSearch(FICL_SYSTEM *pSys);
void       ficlCompileSoftCore(FICL_SYSTEM *pSys);
void       ficlCompileTools(FICL_SYSTEM *pSys);
void       ficlCompileFile(FICL_SYSTEM *pSys);
#if FICL_WANT_FLOAT
void       ficlCompileFloat(FICL_SYSTEM *pSys);
int        ficlParseFloatNumber( FICL_VM *pVM, STRINGINFO si ); /* float.c */
#endif
#if FICL_PLATFORM_EXTEND
void       ficlCompilePlatform(FICL_SYSTEM *pSys);
#endif
int        ficlParsePrefix(FICL_VM *pVM, STRINGINFO si);

/*
** from words.c...
*/
void       constantParen(FICL_VM *pVM);
void       twoConstParen(FICL_VM *pVM);
int        ficlParseNumber(FICL_VM *pVM, STRINGINFO si);
void       ficlTick(FICL_VM *pVM);
void       parseStepParen(FICL_VM *pVM);

/*
** From tools.c
*/
int        isAFiclWord(FICL_DICT *pd, FICL_WORD *pFW);

/* 
** The following supports SEE and the debugger.
*/
typedef enum  
{
    BRANCH,
    COLON, 
    CONSTANT, 
    CREATE,
    DO,
    DOES, 
    IF,
    LITERAL,
    LOOP,
    OF,
    PLOOP,
    PRIMITIVE,
    QDO,
    STRINGLIT,
    CSTRINGLIT,
#if FICL_WANT_USER
    USER, 
#endif
    VARIABLE, 
} WORDKIND;

WORDKIND   ficlWordClassify(FICL_WORD *pFW);

/*
** Dictionary on-demand resizing
*/
extern CELL dictThreshold;
extern CELL dictIncrease;

/*
** Various FreeBSD goodies
*/

#if defined(__i386__) && !defined(TESTMAIN)
extern void ficlOutb(FICL_VM *pVM);
extern void ficlInb(FICL_VM *pVM);
#endif

extern void ficlSetenv(FICL_VM *pVM);
extern void ficlSetenvq(FICL_VM *pVM);
extern void ficlGetenv(FICL_VM *pVM);
extern void ficlUnsetenv(FICL_VM *pVM);
extern void ficlCopyin(FICL_VM *pVM);
extern void ficlCopyout(FICL_VM *pVM);
extern void ficlFindfile(FICL_VM *pVM);
extern void ficlCcall(FICL_VM *pVM);
#if !defined(TESTMAIN)
extern void ficlPnpdevices(FICL_VM *pVM);
extern void ficlPnphandlers(FICL_VM *pVM);
#endif

/*
** Used with File-Access wordset.
*/
#define FICL_FAM_READ	1
#define FICL_FAM_WRITE	2
#define FICL_FAM_APPEND	4
#define FICL_FAM_BINARY	8

#define FICL_FAM_OPEN_MODE(fam)	((fam) & (FICL_FAM_READ | FICL_FAM_WRITE | FICL_FAM_APPEND))


#if (FICL_WANT_FILE)
typedef struct ficlFILE
{
	FILE *f;
	char filename[256];
} ficlFILE;
#endif

#include <sys/linker_set.h>

typedef void ficlCompileFcn(FICL_SYSTEM *);
#define FICL_COMPILE_SET(func)	\
	DATA_SET(Xficl_compile_set, func)
SET_DECLARE(Xficl_compile_set, ficlCompileFcn);

#ifdef LOADER_VERIEXEC
#include <verify_file.h>
#endif

#ifdef __cplusplus
}
#endif

#endif /* __FICL_H__ */
