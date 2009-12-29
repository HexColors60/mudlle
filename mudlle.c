/*
 * Copyright (c) 1993-2006 David Gay and Gustav H�llberg
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
#include "mparser.h"
#include "lexer.h"
#include "compile.h"
#include "alloc.h"
#include "global.h"
#include "utils.h"
#include "module.h"
#include "mcompile.h"
#include "call.h"
#include "interpret.h"
#include "runtime/runtime.h"
#include "context.h"

#ifdef USE_READLINE
#  include <readline/history.h>
#  include <readline/readline.h>

#  define HISTORY_FILE ".mudlle-history"
#endif

extern FILE *yyin;
int debug_level = 0;

int load_file(const char *name, const char *nicename, int seclev, int reload)
{
  FILE *f;
  value result;
  int ok;

  if (!(f = fopen(name, "r")))
    runtime_error(error_bad_value);

  read_from_file(f, nicename);
  ok = interpret(&result, seclev, reload);
  fclose(f);

  return ok;
}

static void execute(char *line)
{
  value result;

  read_from_string(line, NULL);
  if (interpret(&result, 1, true))
    {
      printf("Result: ");
      mprint(mudout, prt_print, result);
      printf("\n");
    }
}

#ifdef USE_READLINE
static void history_exit(void)
{
  write_history(HISTORY_FILE);
}
#endif


int main(int argc, char **argv)
{
  struct session_context context;
  struct oport *out;

#ifdef USE_READLINE
  using_history();
  read_history(HISTORY_FILE);
  rl_bind_key('\t', rl_insert);
#endif

  garbage_init();
  global_init();
  print_init();
  stack_init();
  module_init();
  runtime_init();
  parser_init();
  compile_init();
  mcompile_init();
  interpret_init();
  error_init();
  ports_init();
  context_init();

  define_string_vector("argv", (const char *const *)argv, argc);
  
#ifdef USE_READLINE
  if (atexit(history_exit))
    perror("atexit(history_exit)");
#endif

  out = make_file_oport(stdout);
  session_start(&context, 0, 0, out, out);
  for (;;)
    {
#ifdef USE_READLINE
      char *line = readline((char *)"mudlle> ");

      if (!line)
	break;
      if (*line)
        {
	  add_history(line);
	  execute(line);
        }
      free(line);
#else
      char line[512];

      fputs("mudlle> ", stdout);
      fflush(stdout);
      if (!fgets(line, sizeof line, stdin))
	break;
      if (*line)
        execute(line);
#endif
    }
  session_end();

  return 0;
}

