/*
 * Copyright (c) 1993-2004 David Gay and Gustav H�llberg
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

#ifndef RUNTIME_ERROR_H
#define RUNTIME_ERROR_H

#include <setjmp.h>

#define SIGNAL_ERROR 1

typedef enum { 
  error_bad_function,
  error_stack_underflow,
  error_bad_type,
  error_divide_by_zero,
  error_bad_index,
  error_bad_value,
  error_variable_read_only,
  error_loop,
  error_recurse,
  error_wrong_parameters,
  error_security_violation,
  error_value_read_only,
  error_user_interrupt,
  error_no_match,
  error_compile,
  last_runtime_error
} runtime_errors;

extern const char *mudlle_errors[last_runtime_error];

void error_init(void);

void early_runtime_error(runtime_errors error) NORETURN;
/* Effects: Runtime error 'error' has occured. Dump the call_stack to
     mudout & throw back to the exception handler with SIGNAL_ERROR
     and the error code in exception_value.
     Call this function instead of runtime_error if the arguments of the
     function at the top of call_stack are still on the stack.
   Note: Never returns
*/

void runtime_error(runtime_errors error) NORETURN;
/* Effects: Runtime error 'error' has occured in a primitive operation. 
     Dump the call_stack (plus the primitive operation call) to
     mudout & throw back to the exception handler with SIGNAL_ERROR
     and the error code in exception_value.
   Note: Never returns
*/

struct vector *get_mudlle_call_trace(void);

#endif

