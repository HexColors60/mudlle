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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <time.h>

#include "runtime/runtime.h"
#include "runtime/basic.h"
#include "interpret.h"
#include "mudio.h"
#include "tree.h"
#include "global.h"
#include "alloc.h"
#include "vector.h"
#include "stringops.h"
#include "symbol.h"
#include "call.h"
#include "table.h"

TYPEDOP(codep, "x -> b. TRUE if x is a function", 1, (value v),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE, "x.n")
{
  return makebool(TYPE(v, type_closure) || TYPE(v, type_primitive) ||
		  TYPE(v, type_secure) || TYPE(v, type_varargs));
}

TYPEDOP(apply, "fn v -> x. Excutes fn with arguments v, returns its result",
	2, (value f, struct vector *args),
	0, "fv.x")
{
  TYPEIS(args, type_vector);
  callable(f, vector_len(args));
  return call(f, args);
}

OPERATION(error, "n -> . Causes error n", 1, (value errno),
	  OP_NOESCAPE)
{
  ISINT(errno);
  runtime_error((runtime_errors)intval(errno));
  NOTREACHED;
}

static value result;

static void docall0(void *x)
{
  result = call0(x);
}

OPERATION(catch_error, "fn b -> x. Executes fn() and returns its result. If an error occurs,\n\
returns the error number. If b is true, error messages are suppressed",
	  2, (value f, value suppress),
	  0)
{
  callable(f, 0);

  if (mcatch(docall0, f, !istrue(suppress))) return result; /* No error */

  if (exception_signal == SIGNAL_ERROR &&
      exception_value != makeint(error_loop) &&
      exception_value != makeint(error_recurse)) return exception_value;
  mthrow(exception_signal, exception_value);
  NOTREACHED;
}

OPERATION(handle_error, "fn1 fn2 -> x. Executes fn1(). If an error occurs, calls fn2(errno). Returns result of fn1 or fn2",
	  2, (value f, value handler),
	  0)
{
  struct gcpro gcpro1;
  int ok;

  callable(f, 0);
  callable(handler, 1);

  GCPRO1(handler);
  ok = mcatch(docall0, f, FALSE);
  UNGCPRO();

  if (ok) return result; /* No error */
  if (exception_signal == SIGNAL_ERROR &&
      exception_value != makeint(error_loop) &&
      exception_value != makeint(error_recurse))
    return call1(handler, exception_value);

  mthrow(exception_signal, exception_value);
  NOTREACHED;
}

UNSAFEOP(session, "fn -> . Calls fn() in it's own session",
	 1, (struct closure *fn),
	 0)
{
  struct session_context newc;
  value aresult;

  callable(fn, 0);
  session_start(&newc, minlevel, muduser, mudout, muderr);
  aresult = mcatch_call0(fn);
  session_end();

  return aresult;
}

static typing tref = { "vn.x", "sn.n", "ts.x", "os.x", "ns.x", NULL };

FULLOP(ref, "x1 x2 -> x3. Generic interface to lookup operations: x1[x2] -> x3",
       2, (value x1, value x2),
       0, OP_LEAF | OP_NOALLOC | OP_NOESCAPE, tref)
{
  if (!pointerp(x1)) runtime_error(error_bad_type);
  switch (((struct obj *)x1)->type)
    {
    case type_vector:
      return code_vector_ref(x1, x2);
    case type_string:
      return code_string_ref(x1, x2);
    case type_table:
      return code_table_ref(x1, x2);
    default: runtime_error(error_bad_type);
    }
  NOTREACHED;
}

static typing tset = { "vnx.3", "snn.n", "tsx.3", "osx.3", "nsx.3", NULL };

FULLOP(set, "x1 x2 x3 -> . Generic interface to set operations: x1[x2] = x3",
       3, (value x1, value x2, value x3),
       0, OP_LEAF | OP_NOESCAPE, tset)
{
  if (!pointerp(x1)) runtime_error(error_bad_type);
  switch (((struct obj *)x1)->type)
    {
    case type_vector:
      return code_vector_set(x1, x2, x3);
    case type_string:
      return code_string_set(x1, x2, x3);
    case type_table:
      return code_table_set(x1, x2, x3);
    default: runtime_error(error_bad_type);
    }
  NOTREACHED;
}

/* "Object" manipulation:
   load, save, size
   protect, test status, etc
*/

#define OBJ_MAGIC 0x871f54ab

UNSAFEOP(obj_save, "s x -> . Writes mudlle value x to file s",
	 2, (struct string *file, value x), 
	 OP_LEAF | OP_NOESCAPE)
{
  int fd, ok;
  struct gcpro gcpro1;
  void *data;
  unsigned long size, magic, nsize;
  char tmp_file[PATH_MAX];

  TYPEIS(file, type_string);
  GCPRO1(file);
  data = gc_save(x, &size);
  UNGCPRO();

  snprintf(tmp_file, sizeof tmp_file, "%s.%d.%ld", 
	   file->str, getpid(), time(NULL));

  fd = creat(tmp_file, 0666);
  if (fd < 0) runtime_error(error_bad_value);

  magic = htonl(OBJ_MAGIC);
  nsize = htonl(size);
  ok = write(fd, &magic, sizeof magic) == sizeof magic &&
    write(fd, &nsize, sizeof nsize) == sizeof nsize &&
      write(fd, data, size) == size;
  close(fd);
  
  if (!ok || rename(tmp_file, file->str)) 
    {
      unlink(tmp_file);
      runtime_error(error_bad_value);
    }

  undefined();
}

static value _obj_load(value (*fgc_load)(void *_load, unsigned long size),
		       struct string *file)
{
  int fd;
  unsigned long magic, size;
  void *data;

  TYPEIS(file, type_string);
  fd = open(file->str, O_RDONLY);
  if (fd < 0) runtime_error(error_bad_value);
  
  if (read(fd, &magic, sizeof magic) == sizeof magic &&
      ntohl(magic) == OBJ_MAGIC &&
      read(fd, &size, sizeof size) == sizeof size)
    {
      size = ntohl(size);
      data = alloca(size);
      if (read(fd, data, size) == size)
	{
	  close(fd);
	  return fgc_load(data, size);
	}
    }
  close(fd);
  runtime_error(error_bad_value);
  NOTREACHED;
}

UNSAFEOP(obj_load, "s -> x. Loads a value from a mudlle save file",
	 1, (struct string *file),
	 OP_LEAF | OP_NOESCAPE)
{
  return _obj_load(gc_load, file);
}

#ifndef GCDEBUG
UNSAFEOP(obj_load_debug, "s -> x. Loads a value from a GCDEBUG mudlle save file",
	 1, (struct string *file),
	 OP_LEAF | OP_NOESCAPE)
{
  return _obj_load(gc_load_debug, file);
}
#endif

OPERATION(obj_sizep, "x -> (n1 . n2) Returns object's size n1 (in bytes) (of which n2 mutable bytes)",
	  1, (value x),
	  OP_LEAF | OP_NOESCAPE)
{
  unsigned long size, mutble;

  size = gc_size(x, &mutble);
  return alloc_list(makeint(size), makeint(mutble));
}

TYPEDOP(immutablep, "x -> b. Returns true if x is an immutable value",
	1, (value x),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE, "x.n")
{
  return makebool(immutablep(x));
}

TYPEDOP(readonlyp, "x -> b. Returns true if x is a read-only value",
	1, (value x),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE, "x.n")
{
  return makebool(readonlyp(x));
}

TYPEDOP(protect, "x -> x. Makes value x readonly",
	1, (struct obj *x),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE, "x.1")
{
  if (!pointerp(x))
    return x;

  if (x->type == type_table)
    protect_table((struct table *)x);
  else
    x->flags |= OBJ_READONLY;

  return x;
}

UNSAFEOP(detect_immutability, " -> . Detects the immutable values",
	 0, (void),
	 OP_LEAF | OP_NOESCAPE)
{
  detect_immutability();
  undefined();
}

TYPEDOP(typeof, "x -> n. Return type of x",
	1, (value x),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE, "x.n")
{
  return makeint(TYPEOF(x));
}

UNSAFEOP(unlimited_execution, " -> . Disables execution-time limits", 0, (void),
	 OP_NOESCAPE)
{
  unlimited_execution();
  undefined();
}

void basic_init(void)
{
  DEFINE("function?", codep);
  DEFINE("error", error);
  DEFINE("catch_error", catch_error);
  DEFINE("handle_error", handle_error);
  define_string_vector("error_messages", mudlle_errors, last_runtime_error);

  DEFINE("session", session);
  DEFINE("apply", apply);

  DEFINE("typeof", typeof);
  DEFINE("immutable?", immutablep);
  DEFINE("readonly?", readonlyp);
  DEFINE("protect", protect);
  DEFINE("detect_immutability", detect_immutability);

  DEFINE("size_data", obj_sizep);
  DEFINE("save_data", obj_save);
  DEFINE("load_data", obj_load);

#ifndef GCDEBUG
  DEFINE("load_data_debug", obj_load_debug);
#endif

  DEFINE("ref", ref);
  DEFINE("set!", set);

  DEFINE("unlimited_execution", unlimited_execution);
}