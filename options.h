/*
 * Copyright (c) 1993-1999 David Gay and Gustav H�llberg
 * All rights reserved.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose, without fee, and without written agreement is hereby granted,
 * provided that the above copyright notice and the following two paragraphs
 * appear in all copies of this software.
 * 
 * IN NO EVENT SHALL DAVID GAY OR GUSTAV HALLBERG BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF DAVID GAY OR
 * GUSTAV HALLBERG HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * DAVID GAY AND GUSTAV HALLBERG SPECIFICALLY DISCLAIM ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN
 * "AS IS" BASIS, AND DAVID GAY AND GUSTAV HALLBERG HAVE NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#ifndef OPTIONS_H
#define OPTIONS_H

/* Mudlle configuration */
/* This files contains only #define's, it is used for both C and assembly code */

#if defined(__GNUC__) || defined(AMIGA)
#define INLINE
#else
#define INLINE
#endif

#ifdef sparc
#ifdef __SVR4
#define HAVE_MEMMOVE
#define stricmp strcasecmp
#endif
#endif

#ifdef hpux
#define HAVE_MEMMOVE
#define stricmp strcasecmp
#endif

#ifdef __sgi
#define stricmp strcasecmp
#define HAVE_MEMMOVE
#endif

#ifdef i386
#define stricmp strcasecmp
#define HAVE_MEMMOVE
#define GCQDEBUG
#define GCDEBUG_CHECK
#endif

#ifdef AMIGA
#define HAVE_MEMMOVE
#endif

#ifndef linux
#  define PATH_MAX 1024
#endif

/* GC configuration, basic parameters */
/* More parameters are found in alloc.h (and some logic in alloc.c). */
#define INITIAL_BLOCKSIZE (128*1024)
#define DEF_SAVE_SIZE (64*1024)

#define GLOBAL_SIZE 512
#define DEFAULT_SECLEVEL 0
#define INTERRUPT
#define PRINT_CODE

#ifdef HAVE_MEMMOVE
#  undef HAVE_MEMMOVE
#  define HAVE_MEMMOVE 1
#else
#  define HAVE_MEMMOVE 0
#endif


/* Define NORETURN as a qualifier to indicate that a function never returns.
   With gcc, this is `volatile'. The empty definition is ok too. */
#ifdef __GNUC__
#define NORETURN __attribute__ ((noreturn))
#else
#define NORETURN
#endif


#ifdef sparc
#define USE_CCONTEXT
#ifdef __SVR4
#define __EXTENSIONS__
#define HAVE_ULONG
#include <sys/types.h>
/* This is now defined in config.h from configure
define HAVE_ALLOCA_H
*/
#define nosigsetjmp setjmp
#define nosiglongjmp longjmp
#else
#define HAVE_ALLOCA_H
#define nosigsetjmp _setjmp
#define nosiglongjmp _longjmp
#define HAVE_TM_ZONE
#endif
#endif

#ifdef hpux
#define NOCOMPILER
#define nosigsetjmp setjmp
#define nosiglongjmp longjmp
#endif

#ifdef __sgi
#include <sys/bsd_types.h>
#define HAVE_ULONG
#define NOCOMPILER
#define HAVE_ALLOCA_H
#define nosigsetjmp setjmp
#define nosiglongjmp longjmp
#endif

#ifdef linux
#ifndef __ASSEMBLER__
#include <sys/types.h>
#endif
#define HAVE_ULONG
/* This now defined in config.h from configure
#define HAVE_ALLOCA_H
*/
#define nosigsetjmp _setjmp
#define nosiglongjmp _longjmp
#if defined(i386) || defined(sparc)
#define USE_CCONTEXT
#else
#define NOCOMPILER
#endif
#endif

#ifdef AMIGA
#define HAVE_ALLOCA_H
#define HAVE_TM_ZONE
#define nosigsetjmp setjmp
#define nosiglongjmp longjmp
#endif

/* Execution limits */

#define MAX_CALLS 100000		/* Max # of calls executed / interpret */

#define MAX_FAST_CALLS 1000000	/* Max # of faster calls (machine code) */

#endif
