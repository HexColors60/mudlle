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

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include "mudlle.h"
#include "types.h"
#include "alloc.h"
#include "ports.h"
#include "utils.h"
#include "utils.charset.h"

/* The various types of input & output ports */


#define STRING_BLOCK_SIZE 512	/* Size of each block */

struct string_oport_block /* A structure in which to accumulate output */
{
  struct obj o;
  struct string_oport_block *next;
  struct string *data;
};
/* sizeof(struct string_oport_block) should equal BLOCK_SIZE (see calloc.c)
   exactly, otherwise some memory will be wasted. */

struct string_oport /* Output to a string */
{
  struct oport p;
  struct string_oport_block *first, *current;
  value pos;
};

struct file_oport /* Output to a FILE * */
{
  struct oport p;
  struct gtemp *file;
};


static struct string_oport_block *free_blocks;

/* Creation & code for the various types of ports */
/* ---------------------------------------------- */

static struct string_oport_block *new_string_block(void)
{
  struct string_oport_block *newp;

  if (free_blocks) 
    {
      newp = free_blocks;
      GCCHECK(newp);
      free_blocks = free_blocks->next;
    }
  else
    {
      struct gcpro gcpro1;
      struct string *s;

      s = (struct string *)allocate_string(type_internal, STRING_BLOCK_SIZE);
      GCPRO1(s);
      newp = (struct string_oport_block *)allocate_record(type_internal, 2);
      UNGCPRO();
      newp->data = s;
      GCCHECK(newp);
    }

  newp->next = NULL;
  
  return newp;
}

static void output_string_close(struct oport *_p)
{
  struct string_oport *p = (struct string_oport *)_p;
  
  p->p.methods = NULL;

  {
    struct string_oport_block *b;

    for (b = p->first; b; b = b->next)
      GCCHECK(b);
  }

  /* Free data (add blocks to free block list) */
  p->current->next = free_blocks;
  free_blocks = p->first;
  p->first = p->current = NULL;
}

static void string_flush(struct oport *_p)
{
}
     
static void string_putc(struct oport *_p, char c)
{
  struct string_oport *p = (struct string_oport *)_p;
  struct string_oport_block *current = p->current;
  long pos = intval(p->pos);
  
  if (pos == STRING_BLOCK_SIZE)
    {
      struct string_oport_block *blk;
      struct gcpro gcpro1, gcpro2;

      GCPRO2(p, current);
      blk = new_string_block();
      UNGCPRO();
      p->current = current->next = blk;
      current = p->current;
      p->pos = makeint(pos = 0);
    }
  current->data->str[pos++] = c;
  p->pos = makeint(pos);
}

static void string_write(struct oport *_p, const char *data, int nchars)
{
  struct string_oport *p = (struct string_oport *)_p;
  struct string_oport_block *current = p->current;
  int fit;
  long pos = intval(p->pos);
  struct gcpro gcpro1, gcpro2;

  GCPRO2(p, current);
  while ((fit = STRING_BLOCK_SIZE - pos) < nchars)
    {
      struct string_oport_block *blk = new_string_block();

      memcpy(current->data->str + pos, data, fit);
      p->current = current->next = blk;
      current = p->current;
      data += fit;
      nchars -= fit;
      pos = 0;
    }
  UNGCPRO();
  memcpy(current->data->str + pos, data, nchars);
  p->pos = makeint(pos + nchars);
}

static void string_swrite(struct oport *_p, struct string *s, int from, int nchars)
{
  struct string_oport *p = (struct string_oport *)_p;
  struct string_oport_block *current = p->current;
  int fit;
  long pos = intval(p->pos);
  struct gcpro gcpro1, gcpro2, gcpro3;

  GCPRO2(p, current);
  GCPRO(gcpro3, s);
  while ((fit = STRING_BLOCK_SIZE - pos) < nchars)
    {
      struct string_oport_block *blk = new_string_block();

      memcpy(current->data->str + pos, s->str + from, fit);
      p->current = current->next = blk;
      current = p->current;
      from += fit;
      nchars -= fit;
      pos = 0;
    }
  UNGCPRO();
  memcpy(current->data->str + pos, s->str + from, nchars);
  p->pos = makeint(pos + nchars);
}

static struct oport_methods string_port_methods = {
  output_string_close,
  string_putc,
  string_write,
  string_swrite,
  string_flush
};

value make_string_outputport(void)
{
  struct string_oport *p = (struct string_oport *) allocate_record(type_outputport, 4);
  struct gcpro gcpro1;
  struct gtemp *m;
  struct string_oport_block *blk;

  GCPRO1(p);
  m = allocate_temp(type_internal, &string_port_methods);
  p->p.methods = m;
  blk = new_string_block();
  p->first = p->current = blk;
  p->pos = makeint(0);
  UNGCPRO();

  return p;
}

static void string_7bit_putc(struct oport *_p, char c)
{
  string_putc(_p, TO_7PRINT(c));
}

static void string_7bit_write(struct oport *p, const char *data, int nchars)
{
  char buf[STRING_BLOCK_SIZE];
  struct gcpro gcpro1;

  GCPRO1(p);
  while (nchars)
    {
      int i, fit = sizeof buf < nchars ? sizeof buf : nchars;

      for (i = 0; i < fit; ++i)
	buf[i] = TO_7PRINT(*data++);
      string_write(p, buf, fit);
      nchars -= fit;
    }
  UNGCPRO();
}

static void string_7bit_swrite(struct oport *p, struct string *s, int from, int nchars)
{
  char buf[STRING_BLOCK_SIZE];
  struct gcpro gcpro1, gcpro2;

  GCPRO2(p, s);
  while (nchars)
    {
      int i, fit = sizeof buf < nchars ? sizeof buf : nchars;

      for (i = 0; i < fit; ++i)
	buf[i] = TO_7PRINT(s->str[from + i]);
      string_write(p, buf, fit);
      nchars -= fit;
      from += fit;
    }
  UNGCPRO();
}

static struct oport_methods string_7bit_port_methods = {
  output_string_close,
  string_7bit_putc,
  string_7bit_write,
  string_7bit_swrite,
  string_flush
};

value make_string_7bit_outputport(void)
{
  struct string_oport *p = (struct string_oport *) allocate_record(type_outputport, 4);
  struct gcpro gcpro1;
  struct gtemp *m;
  struct string_oport_block *blk;

  GCPRO1(p);
  m = allocate_temp(type_internal, &string_7bit_port_methods);
  p->p.methods = m;
  blk = new_string_block();
  p->first = p->current = blk;
  p->pos = makeint(0);
  UNGCPRO();

  return p;
}

static void output_file_close(struct oport *_p)
{
  struct file_oport *p = (struct file_oport *)_p;
  FILE *f = p->file->external;

  fclose(f);
  p->file->external = NULL;
}

static void file_flush(struct oport *_p)
{
  struct file_oport *p = (struct file_oport *)_p;
  FILE *f = p->file->external;

  fflush(f);
}
     
static void file_putc(struct oport *_p, char c)
{
  struct file_oport *p = (struct file_oport *)_p;
  FILE *f = p->file->external;

  if (f) putc(c, f);
}

static void file_write(struct oport *_p, const char *data, int nchars)
{
  struct file_oport *p = (struct file_oport *)_p;
  FILE *f = p->file->external;

  if (f) fwrite(data, nchars, 1, f);
}

static void file_swrite(struct oport *_p, struct string *s, int from, int nchars)
{
  struct file_oport *p = (struct file_oport *)_p;
  FILE *f = p->file->external;

  if (f) fwrite(s->str + from, nchars, 1, f);
}

static struct oport_methods file_port_methods = {
  output_file_close,
  file_putc,
  file_write,
  file_swrite,
  file_flush
};

value make_file_outputport(FILE *f)
{
  struct file_oport *p = (struct file_oport *) allocate_record(type_outputport, 2);
  struct gcpro gcpro1;
  struct gtemp *m;

  GCPRO1(p);
  m = allocate_temp(type_internal, &file_port_methods);
  p->p.methods = m;
  m = allocate_temp(type_internal, f);
  p->file = m;
  UNGCPRO();

  return p;
}

int port_empty(struct oport *_p)
/* Return: true if the port is empty
   Requires: p be a string-type output port
*/
{
  struct string_oport *p = (struct string_oport *)_p;
  struct string_oport_block *current = p->first;

  return !current->next && intval(p->pos) == 0;
}

static ulong port_length(struct string_oport *p)
{
  struct string_oport_block *current = p->first;
  ulong size;

  size = 0;
  while (current->next)
    {
      size += STRING_BLOCK_SIZE;
      current = current->next;
    }
  return size + intval(p->pos);
}

static void port_copy(char *s, struct string_oport *p)
{
  struct string_oport_block *current = p->first;
  long pos = intval(p->pos);

  while (current->next)
    {
      memcpy(s, current->data->str, STRING_BLOCK_SIZE);
      s += STRING_BLOCK_SIZE;
      current = current->next;
    }
  memcpy(s, current->data->str, pos);
  s[pos] = '\0';
}

struct string *port_string(struct oport *_p)
{
  struct string_oport *p = (struct string_oport *)_p;
  struct gcpro gcpro1;
  struct string *result;

  GCPRO1(p);
  result = (struct string *)allocate_string(type_string, port_length(p) + 1);
  UNGCPRO();

  port_copy(result->str, p);

  return result;
}

char *port_cstring(struct oport *_p)
{
  struct string_oport *p = (struct string_oport *)_p;
  char *s;

  s = xmalloc(port_length(p) + 1);
  port_copy(s, p);

  return s;
}

void port_append(struct oport *p1, struct oport *_p2)
/* Effects: The characters of port p2 are appended to the end of port p1.
   Modifies: p1
   Requires: p2 be a string-type output port
*/
{
  struct string_oport *p2 = (struct string_oport *)_p2;
  struct string_oport_block *current = p2->first;
  long pos = intval(p2->pos);
  struct gcpro gcpro1, gcpro2;

  GCPRO2(p1, current);
  while (current->next)
    {
      pswrite(p1, current->data, 0, STRING_BLOCK_SIZE);
      current = current->next;
    }
  pswrite(p1, current->data, 0, pos);
  UNGCPRO();
}

/* C I/O routines for use with the ports */
/* ------------------------------------- */

void pputs(const char *s, struct oport *p)
{
  opwrite(p, s, strlen(s));
}

static char basechars[17] = "0123456789abcdef";

char *int2str(char *str, int base, ulong n, int is_signed)
/* Requires: base be 2, 8, 10 or 16. str be at least INTSTRLEN characters long.
   Effects: Prints the ASCII representation of n in base base to the
     string str.
     If is_signed is TRUE, n is actually a long
   Returns: A pointer to the start of the result.
*/
{
  char *pos;
  int minus;

  /* ints are 32 bits, the longest number will thus be
     32 digits (in binary) + 1(sign) characters long */
  pos = str + INTSTRLEN - 1;
  *--pos = '\0';

  if (is_signed && (long)n < 0)
    {
      minus = TRUE;
      if ((long)n <= -16)
	{
	  /* this is to take care of LONG_MIN */
	  *--pos = basechars[abs((long)n % base)];
	  (long)n /= base;
	}
      n = -(long)n;
    }
  else minus = FALSE;

  do {
    *--pos = basechars[n % base];
    n /= base;
  } while (n > 0);
  if (minus) *--pos = '-';

  return pos;
}

char *int2str_wide(char *str, ulong n, int is_signed)
/* Requires: str be at least INTSTRLEN characters long.
   Effects: Prints the ASCII representation of n in base 10 with
     1000-separation by commas
     If is_signed is TRUE, n is actually a long
   Returns: A pointer to the start of the result.
*/
{
  char *pos;
  int minus, i;

  pos = str + INTSTRLEN - 1;
  *--pos = '\0';

  i = 3;

  if (is_signed && (long)n < 0)
    {
      minus = TRUE;
      if (n <= -16)
	{
	  /* this is to take care of LONG_MIN */
	  *--pos = basechars[abs(n % 10)];
	  n /= 10;
	  --i;
	}
      n = -(long)n;
    }
  else minus = FALSE;

  do {
    if (!i)
      {
	*--pos = ',';
	i = 3;
      }
    *--pos = basechars[n % 10];
    n /= 10;
    --i;
  } while (n > 0);
  if (minus) *--pos = '-';

  return pos;
}

void vpprintf(struct oport *p, const char *fmt, va_list args)
{
  const char *percent, *add = NULL;
  char buf[INTSTRLEN], padchar;
  int longfmt, padright, fsize, fprec, addlen, cap, widefmt;
  struct gcpro gcpro1;

  if (!p || !p->methods) return;
  GCPRO1(p);
  while ((percent = strchr(fmt, '%')))
    {
      opwrite(p, fmt, percent - fmt);
      fmt = percent + 1;
      longfmt = FALSE;
      fsize = 0;
      fprec = -1;
      padright = FALSE;
      cap = FALSE;
      widefmt = FALSE;
      if (*fmt == '-')
	{
	  padright = TRUE;
	  fmt++;
	}

      if (*fmt == '0')
	padchar = '0';
      else
	padchar = ' ';

      if (*fmt == '\'')
	{
	  widefmt = TRUE;
	  fmt++;
	}

      while (isdigit(*fmt))
	{
	  fsize = fsize * 10 + *fmt - '0';
	  fmt++;
	}

      if (*fmt == '.')
	{
	  fprec = 0;
	  while (isdigit(*++fmt))
	    fprec = fprec * 10 + *fmt - '0';
	}

      if (*fmt == 'l')
	{
	  longfmt = TRUE;
	  fmt++;
	}

      switch (*fmt)
	{
	case '%':
	  add = "%";
	  break;
	case 'd': case 'i':
	  if (longfmt)
	    if (widefmt)
	      add = int2str_wide(buf, va_arg(args, long), TRUE);
	    else
	      add = int2str(buf, 10, va_arg(args, long), TRUE);
	  else
	    if (widefmt)
	      add = int2str_wide(buf, va_arg(args, int), TRUE);
	    else
	      add = int2str(buf, 10, va_arg(args, int), TRUE);
	  break;
	case 'u':
	  if (longfmt)
	    if (widefmt)
	      add = int2str_wide(buf, va_arg(args, long), FALSE);
	    else
	      add = int2str(buf, 10, va_arg(args, long), FALSE);
	  else
	    if (widefmt)
	      add = int2str_wide(buf, va_arg(args, int), FALSE);
	    else
	      add = int2str(buf, 10, va_arg(args, int), FALSE);
	  break;
	case 'x':
	  if (longfmt)
	    add = int2str(buf, 16, va_arg(args, unsigned long), FALSE);
	  else
	    add = int2str(buf, 16, va_arg(args, unsigned int), FALSE);
	  break;
	case 'o':
	  if (longfmt)
	    add = int2str(buf, 8, va_arg(args, unsigned long), FALSE);
	  else
	    add = int2str(buf, 8, va_arg(args, unsigned int), FALSE);
	  break;
	case 'S':
	  cap = TRUE;
	case 's':
	  add = va_arg(args, const char *);
	  if (!add) add = "(null)";
	  if (fprec > 0 &&
	      strlen(add) > fprec)
	    {
	      strncpy(buf, add, fprec);
	      buf[fprec] = 0;
	      add = buf;
	    }
	  break;
	case 'c':
	  add = buf;
	  buf[0] = va_arg(args, int); buf[1] = '\0';
	  break;
	case 'f':
	  if (fprec >= 0)
	    sprintf(buf, "%.*f", fprec, va_arg(args, double));
	  else
	    sprintf(buf, "%f", va_arg(args, double));
	  add = buf;
	  break;
	default: assert(0);
	}
      fmt++;

      addlen = strlen(add);
      if (fsize > 0 && !padright)
	{
	  int i = fsize - addlen;

	  while (--i >= 0) pputc(padchar, p);
	}
      if (cap && addlen > 0)
	{
	  pputc(toupper(add[0]), p);
	  opwrite(p, add + 1, addlen - 1);
	}
      else
	opwrite(p, add, addlen);
      if (fsize > 0 && padright)
	{
	  int i = fsize - addlen;

	  while (--i >= 0) pputc(' ', p);
	}
    }
  pputs(fmt, p);
  UNGCPRO();
}

void pprintf(struct oport *p, const char *fmt, ...)
{
  va_list args;
  
  va_start(args, fmt);
  vpprintf(p, fmt, args);
  va_end(args);  
}

void ports_init(void)
{
  staticpro((value *)&free_blocks);
}