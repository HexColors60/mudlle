/*
 * Copyright (c) 1993-2012 David Gay and Gustav H�llberg
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

#include <string.h>

#include "calloc.h"
#include "code.h"
#include "env.h"
#include "global.h"
#include "ins.h"
#include "tree.h"

struct locals_list
{
  struct locals_list *next;
  uint16_t index;
  struct vlist *locals;
  bool statics;
};

struct env_stack
{
  struct fncode *fn;
  struct env_stack *next, *prev;
  struct locals_list *locals;
  uint16_t size, max_size;      /* Current & max length of locals */
  uint16_t loop_depth;          /* Counts loop layers */
  struct variable_list *closure;
};

static struct env_stack *env_stack;

static struct variable_list *new_varlist(struct alloc_block *heap,
                                         enum variable_class vclass,
                                         ulong offset,
                                         struct variable_list *next)
{
  struct variable_list *newp = allocate(heap, sizeof *newp);
  *newp = (struct variable_list){
    .next   = next,
    .vclass = vclass,
    .offset = offset
  };
  return newp;
}

static struct locals_list *new_locals_list(struct alloc_block *heap,
                                           struct vlist *vars, uint16_t idx,
                                           struct locals_list *next,
                                           bool statics)
{
  struct locals_list *newp = allocate(heap, sizeof *newp);

  newp->next = next;
  newp->index = idx;
  newp->locals = vars;
  newp->statics = statics;

  return newp;
}

void env_reset(void)
{
  env_stack = NULL;
}

void env_push(struct vlist *locals, struct fncode *fn)
{
  struct env_stack *newp = allocate(fnmemory(fn), sizeof *newp);

  uint16_t nlocals = vlist_length(locals);
  *newp = (struct env_stack){
    .fn       = fn,
    .next     = env_stack,
    .size     = nlocals,
    .max_size = nlocals,
    .locals   = (locals
                 ? new_locals_list(fnmemory(fn), locals, 0, NULL, false)
                 : NULL)
  };
  if (env_stack) env_stack->prev = newp;
  env_stack = newp;
}

struct variable_list *env_pop(uint16_t *nb_locals)
{
  struct variable_list *closure = env_stack->closure;

  *nb_locals = env_stack->max_size;
  env_stack = env_stack->next;
  if (env_stack) env_stack->prev = NULL;
  return closure;
}

bool env_block_push(struct vlist *locals, bool statics)
{
  /* Add locals */
  env_stack->locals = new_locals_list(fnmemory(env_stack->fn), locals,
				      env_stack->size, env_stack->locals,
                                      statics);

  /* Update size info, clears vars if necessary */
  unsigned nsize = env_stack->size + vlist_length(locals);
  if (nsize > MAX_LOCAL_VARS)
    return false;

  uint16_t last_set = nsize;
  if (env_stack->max_size < nsize)
    {
      if (env_stack->loop_depth == 0)
        last_set = env_stack->max_size;
      env_stack->max_size = nsize;
    }
  for (uint16_t i = env_stack->size; i < last_set; i++)
    ins1(op_clear_local, i, env_stack->fn);
  env_stack->size = nsize;

  return true;
}

void env_block_pop(void)
{
  /* Cannot share variables as some of them may escape */
  /* Think about this (most variables can be shared, and the
     variable cells always can be) */
  /*env_stack->size -= vlist_length(env_stack->locals->locals);*/
  env_stack->locals = env_stack->locals->next;
}

void env_start_loop(void)
{
  ++env_stack->loop_depth;
}

void env_end_loop(void)
{
  --env_stack->loop_depth;
}

static enum variable_class env_close(struct env_stack *env, ulong pos,
                                ulong *offset)
/* Effects: Adds local variable pos of environment env to all closures
     below it in the env_stack.
   Returns: vclass_local if env is the last environment on the stack,
     vclass_closure otherwise.
     *offset is the offset in the closure or local variables at which
     the local variable <env,pos> can be found by the function whose
     environement is env_stack.
*/
{
  struct env_stack *subenv;
  enum variable_class vclass = vclass_local;

  /* Add <env,pos> to all environments below env */
  for (subenv = env->prev; subenv; subenv = subenv->prev)
    {
      struct variable_list **closure;
      ulong coffset;
      int found = false;

      /* Is <class,pos> already in closure ? */
      for (coffset = 0, closure = &subenv->closure; *closure;
	   coffset++, closure = &(*closure)->next)
	if (vclass == (*closure)->vclass && pos == (*closure)->offset)
	  {
            /* Yes ! */
	    found = true;
	    break;
	  }
      if (!found)
	/* Add variable to closure, at end */
	*closure = new_varlist(fnmemory(subenv->fn), vclass, pos, NULL);

      /* Copy reference to this closure position into <class,pos> */
      /* This is how the variable will be named in the next closure */
      vclass = vclass_closure;
      pos = coffset;
    }
  *offset = pos;
  return vclass;
}

enum variable_class env_lookup(const char *name, ulong *offset,
                               bool do_read, bool do_write,
                               bool *is_static)
{
  *is_static = false;
  if (strncasecmp(name, GLOBAL_ENV_PREFIX, strlen(GLOBAL_ENV_PREFIX)) == 0)
    name += strlen(GLOBAL_ENV_PREFIX);
  else
    for (struct env_stack *env = env_stack; env; env = env->next)
      {
	/* Look for variable in environment env */
	for (struct locals_list *scope = env->locals;
             scope;
             scope = scope->next)
          {
            ulong pos = scope->index;
            for (struct vlist *vars = scope->locals;
                 vars;
                 pos++, vars = vars->next)
              if (strcasecmp(name, vars->var) == 0)
                {
                  if (do_read)
                    vars->was_read = true;
                  if (do_write)
                    vars->was_written = true;
                  *is_static = scope->statics;
                  return env_close(env, pos, offset);
                }
          }
      }

  /* Not found, is global */
  *offset = global_lookup(name);
  return vclass_global;
}
