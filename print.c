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

#include <string.h>
#include <stddef.h>
#include <string.h>
#include <setjmp.h>
#include <stdlib.h>

#include "mudlle.h"
#include "print.h"
#include "types.h"
#include "table.h"
#include "objenv.h"
#include "code.h"

#define MAX_PRINT_COUNT 400
#define FLOATSTRLEN 20

static int prt_count;
jmp_buf print_complex;

static unsigned char writable_chars[256 / 8];
#define set_writable(c, ok) \
  do { if (ok) writable_chars[(c) >> 3] |= 1 << ((c) & 7); \
       else writable_chars[(c) >> 3] &= ~(1 << ((c) & 7)); } while(0)
#define writable(c) (writable_chars[(c) >> 3] & (1 << ((c) & 7)))

static void _print_value(struct oport *f, prt_level level, value v, int toplev);

static void write_string(struct oport *p, prt_level level, struct string *print)
{
  ulong l = string_len(print);

  if (level == prt_display)
    pswrite(p, print, 0, l);
  else
    {
      struct gcpro gcpro1;
      unsigned char *str = (unsigned char *)alloca(l + 1);
      unsigned char *endstr;

      memcpy((char *)str, print->str, l + 1);
      GCPRO1(p);
      /* The NULL byte at the end doesn't count */
      endstr = str + l;

      pputc('"', p);
      while (str < endstr)
	{
	  unsigned char *pos = str;

	  while (pos < endstr && writable(*pos)) pos++;
	  opwrite(p, (char *)str, pos - str);
	  if (pos < endstr)	/* We stopped for a \ */
	    {
	      pputc('\\', p);
	      switch (*pos)
		{
		case '\\': case '"': pputc(*pos, p); break;
		case '\n': pputc('n', p); break;
		case '\r': pputc('r', p); break;
		case '\t': pputc('t', p); break;
		case '\f': pputc('f', p); break;
		default: pprintf(p, "%03o", (unsigned)*pos); break;
		}
	      str = pos + 1;
	    }
	  else str = pos;
	}
      pputc('"', p);
      UNGCPRO();
    }
}

static int write_instruction(struct oport *f, instruction *i, ulong ofs)
{
  ubyte byte1, byte2;
  ubyte op;
  word word1;

  instruction *old_i = i;
  const char *brname[] = { "", "(loop)", "(nz)", "(z)" };
  const char *builtin_names[] = { 
    "eq", "neq", "gt", "lt", "le", "ge", "ref", "set",
    "add", "sub", "bitand", "bitor", "not" };

#define insubyte() (*i++)
#define insbyte() ((byte)insubyte())
#define insuword() (byte1 = *i++, byte2 = *i++, (byte1 << 8) + byte2)
#define insword() ((word)insuword())

  op = insubyte();

  pprintf(f, "%5d: ", ofs);
  if (op >= op_recall && op <= op_closure_var + closure_var)
    {
      const char *opname[] = { "recall", "assign", "closure var" };
      const char *classname[] = { "local", "closure", "global" };

      if ((op - op_recall) %3 == global_var)
	pprintf(f, "%s[%s] %lu\n", opname[(op - op_recall) / 3],
		classname[(op - op_recall) % 3], insuword());
      else
	pprintf(f, "%s[%s] %lu\n", opname[(op - op_recall) / 3],
		classname[(op - op_recall) % 3], insubyte());
    }
  else if (op >= op_builtin_eq && op <= op_builtin_not)
    pprintf(f, "builtin_%s\n", builtin_names[op - op_builtin_eq]);
  else if (op >= op_typecheck && op < op_typecheck + last_synthetic_type)
    pprintf(f, "typecheck %d\n", op - op_typecheck);
  else switch (op)
    {
    case op_define: pprintf(f, "define\n"); break;
    case op_return: pprintf(f, "return\n"); break;
    case op_constant1: pprintf(f, "constant %u\n", insubyte()); break;
    case op_constant2: pprintf(f, "constant %u\n", insuword()); break;
    case op_integer1: pprintf(f, "integer1 %d\n", insbyte()); break;
    case op_integer2: pprintf(f, "integer2 %d\n", insword()); break;
    case op_closure: pprintf(f, "closure %u\n", insubyte()); break;
    case op_closure_code1: pprintf(f, "closure code %u\n", insubyte()); break;
    case op_closure_code2: pprintf(f, "closure code %u\n", insuword()); break;
    case op_execute: pprintf(f, "execute %u\n", insubyte()); break;
    case op_execute_primitive: pprintf(f, "execute_primitive %u\n", insubyte()); break;
    case op_execute_secure: pprintf(f, "execute_secure %u\n", insubyte()); break;
    case op_execute_varargs: pprintf(f, "execute_varargs %u\n", insubyte()); break;
    case op_execute_global1: pprintf(f, "execute[global %u] 1\n", insuword()); break;
    case op_execute_global2: pprintf(f, "execute[global %u] 2\n", insuword()); break;
    case op_execute_primitive1: pprintf(f, "execute[primitive %u] 1\n", insuword()); break;
    case op_execute_primitive2: pprintf(f, "execute[primitive %u] 2\n", insuword()); break;
    case op_argcheck: pprintf(f, "argcheck %u\n", insubyte()); break;
    case op_varargs: pprintf(f, "varargs\n"); break;
    case op_discard: pprintf(f, "discard\n"); break;
    case op_pop_n: pprintf(f, "pop %u\n", insubyte()); break;
    case op_exit_n: pprintf(f, "exit %u\n", insubyte()); break;
    case op_branch1: case op_branch_z1: case op_branch_nz1: case op_loop1:
      byte1 = insbyte();
      pprintf(f, "branch%s %d (to %lu)\n", brname[(op - op_branch1) / 2], byte1,
	      ofs + i - old_i + byte1);
      break;
    case op_branch2: case op_branch_z2: case op_branch_nz2: case op_loop2:
      word1 = insword();
      pprintf(f, "wbranch%s %d (to %lu)\n", brname[(op - op_branch1) / 2], word1,
	      ofs + i - old_i + word1);
      break;
    case op_clear_local:
      pprintf(f, "clear[local] %lu\n", insubyte());
      break;
    default: pprintf(f, "Opcode %d\n", op); break;
    }
  return i - old_i;
}

static void write_code(struct oport *f, struct code *c)
{
  instruction *ins;
  ulong nbins, i;
  struct gcpro gcpro1, gcpro2;

  GCPRO2(f, c);
  ins = (instruction *)((char *)c + offsetof(struct code, constants[c->nb_constants]));
  nbins = (instruction *)((char *)c + c->o.size) - ins;
  pprintf(f, "Code %lu bytes:\n", nbins);
  i = 0;
  while (i < nbins)
    {
      ins = (instruction *)((char *)c + offsetof(struct code, constants[c->nb_constants]));
      i += write_instruction(f, ins + i, i);
    }

  pprintf(f, "\n%u locals, %u stack, seclevel %u, %u constants:\n",
	  c->nb_locals, c->stkdepth, c->seclevel, c->nb_constants);
  for (i = 0; i < c->nb_constants; i++)
    {
      pprintf(f, "%lu: ", i);
      _print_value(f, prt_examine, c->constants[i], 0);
      pprintf(f, "\n");
    }
  UNGCPRO();
}

static void write_closure(struct oport *f, struct closure *c)
{
  ulong nbvar = (c->o.size - offsetof(struct closure, variables)) / sizeof(value), i;
  struct gcpro gcpro1, gcpro2;

  GCPRO2(f, c);
  pprintf(f, "Closure, code is\n");
  _print_value(f, prt_examine, c->code, 0);
  pprintf(f, "\nand %lu variables are\n", nbvar);

  for (i = 0; i < nbvar; i++) 
    {
      pprintf(f, "%lu: ", i);
      _print_value(f, prt_examine, c->variables[i], 0);
      pprintf(f, "\n");
    }
  UNGCPRO();
}

static void write_vector(struct oport *f, prt_level level, struct vector *v, 
			 int toplev)
{
  ulong len = vector_len(v), i;
  struct gcpro gcpro1, gcpro2;

  GCPRO2(f, v);
  if (level != prt_display && toplev) pprintf(f, "'");
  pprintf(f, "[");
  for (i = 0; i < len; i++)
    {
      pputc(' ', f);
      _print_value(f, level, v->data[i], 0);
    }
  pprintf(f, " ]");
  UNGCPRO();
}

static void write_list(struct oport *f, prt_level level, struct list *v,
		       int toplev)
{
  struct gcpro gcpro1, gcpro2;

  GCPRO2(f, v);
  if (level != prt_display && toplev) 
    pputc('\'', f);
  pputc('(', f);
  do {
    _print_value(f, level, v->car, 0);
    if (!TYPE(v->cdr, type_pair)) break;
    pputc(' ', f);
    v = v->cdr;
  } while (1);
  
  if (v->cdr)
    {
      pputs(" . ", f);
      _print_value(f, level, v->cdr, 0);
    }
  pprintf(f, ")");
  UNGCPRO();
}

static struct oport *write_table_oport;
static prt_level write_table_level;

static void write_table_entry(struct symbol *s)
{
  pputc(' ', write_table_oport);
  write_string(write_table_oport, write_table_level, s->name);
  pputc('=', write_table_oport);
  _print_value(write_table_oport, write_table_level, s->data, 0);
}

static void write_table(struct oport *f, prt_level level, struct table *t,
			int toplev)
{
  struct gcpro gcpro1, gcpro2;

  if (level < prt_examine && table_entries(t) > 10)
    {
      pputs("{table}", f);
      return;
    }

  GCPRO2(f, t);
  if (level != prt_display && toplev) 
    pputc('\'', f);
  pputc('{', f);
  write_table_oport = f;
  write_table_level = level;
  table_foreach(t, write_table_entry);
  pputs(" }", f);
  UNGCPRO();
}

static void write_character(struct oport *f, prt_level level, struct character *c)
{
}

static void write_object(struct oport *f, prt_level level, struct object *c)
{
}

static void write_integer(struct oport *f, long v)
{
  char buf[INTSTRLEN];

  pputs(int2str(buf, 10, (ulong)v, TRUE), f);
}

static void write_float(struct oport *f, struct mudlle_float *v)
{
  char buf[FLOATSTRLEN];

  snprintf(buf, FLOATSTRLEN, "%#g", v->d);
  pputs(buf, f);
}

static void write_bigint(struct oport *f, prt_level level, struct bigint *bi)
{
#ifdef USE_GMP
  char *buf;
  int size;

  check_bigint(bi);
  size = mpz_sizeinbase(bi->mpz, 10);
  buf = alloca(size + 2);
  mpz_get_str(buf, 10, bi->mpz);
  if (level != prt_display)
    pputs("#b", f);
  pputs(buf, f);
#else
  pputs("#bigint-unsupported", f);
#endif
}

static void _print_value(struct oport *f, prt_level level, value v, int toplev)
{
  const char *mtypename[last_type] = {
    "code", "closure", "variable", "internal", "primitive", "varargs", "secure",
    "integer", "string", "vector", "list", "symbol", "table", "private",
    "object", "character", "gone", "output-port", "mcode", "float", "bigint" };
  const char visible_in[][last_type] = {
    /* Display */ { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1 },
    /* Print */   { 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 1, 1 },
    /* Examine */ { 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 1, 1 } };
  struct gcpro gcpro1, gcpro2;

  if (prt_count++ > MAX_PRINT_COUNT) longjmp(print_complex, 0);

  if (integerp(v)) write_integer(f, intval(v));
  else if (!v) pprintf(f, "null");
  else
    {
      struct obj *obj = v;

      assert(obj->type < last_type);
      if (!visible_in[level][obj->type])
	pprintf(f, "{%s}", mtypename[obj->type]);
      else
	switch (obj->type)
	  {
	  default: assert(0);
	  case type_string: write_string(f, level, v); break;
	  case type_symbol:
	    {
	      struct symbol *sym = v;

	      GCPRO2(f, sym);
	      pprintf(f, "<");
	      write_string(f, level, sym->name);
	      pprintf(f, ",");
	      _print_value(f, level, sym->data, 0);
	      pprintf(f, ">");
	      UNGCPRO();
	      break;
	    }
	  case type_variable:
	    {
	      struct variable *var = v;

	      GCPRO2(f, var);
	      pprintf(f, "variable = "); _print_value(f, level, var->vvalue, 0);
	      UNGCPRO();
	      break;
	    }
	  case type_code: write_code(f, v); break;
	  case type_closure: write_closure(f, v); break;
	  case type_table: write_table(f, level, v, toplev); break;
	  case type_pair: write_list(f, level, v, toplev); break;
	  case type_vector: write_vector(f, level, v, toplev); break;
	  case type_character: write_character(f, level, v); break;
	  case type_object: write_object(f, level, v); break;
	  case type_float: write_float(f, v); break;
	  case type_bigint: write_bigint(f, level, v); break;
	  }
    }
}

void output_value(struct oport *f, prt_level level, value v)
{
  if (!f) return;
  /* Optimise common cases (avoid complexity check overhead) */
  if (integerp(v)) write_integer(f, intval(v));
  else if (!v) pputs_cst("null", f);
  else if (((struct obj *)v)->type == type_string) write_string(f, level, v);
  else
    {
      struct gcpro *old_gcpro = gcpro;

      if (setjmp(print_complex)) /* exit when problems */
	{
	  gcpro = old_gcpro;
	  pputs_cst("<complex>", f);
	}
      else
	{
	  struct gcpro gcpro1, gcpro2, gcpro3;
	  struct oport *p;

	  GCPRO2(f, v);
	  p = make_string_outputport();
	  GCPRO(gcpro3, p);
	  prt_count = 0;
	  _print_value(p, level, v, 1);
	  port_append(f, p);
	  opclose(p);
	  UNGCPRO();
	}
    }
}

void print_init(void)
{
  unsigned int c;

  for (c = 32; c < 127; c++) set_writable(c, TRUE);
  set_writable('"', FALSE);
  set_writable('\\', FALSE);
  for (c = 160; c < 256; c++) set_writable(c, TRUE); 
}