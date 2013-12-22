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

#include "../mudlle.h"

#include <ctype.h>
#include <errno.h>
#include <fnmatch.h>
#include <iconv.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include "../call.h"
#include "../charset.h"
#include "../print.h"
#include "runtime.h"
#include "stringops.h"


#ifdef USE_PCRE
#  ifdef HAVE_PCRE_PCRE_H
#    include <pcre/pcre.h>
#  elif HAVE_PCRE_H
#    include <pcre.h>
#  else
#    error "Do not know where to find pcre.h"
#  endif
#endif

#ifdef HAVE_CRYPT_H
#  include <crypt.h>
#elif defined HAVE_CRYPT
#  include <unistd.h>
#endif

/* these flags unfortunately overlap with some of PCRE's normal flags */
#define PCRE_7BIT    ((MAX_TAGGED_INT >> 1) + 1) /* highest bit */
#define PCRE_INDICES (PCRE_7BIT >> 1)            /* second highest bit */
#define PCRE_BOOLEAN (PCRE_7BIT >> 2)            /* third highest bit */

bool is_regexp(value _re)
{
  struct grecord *re = _re;
  return (TYPE(re, type_private)
          && re->data[0] == makeint(PRIVATE_REGEXP));
}

TYPEDOP(stringp, "string?", "`x -> `b. TRUE if `x is a string", 1, (value v),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_STR_READONLY, "x.n")
{
  return makebool(TYPE(v, type_string));
}

TYPEDOP(make_string, 0, "`n -> `s. Create a string of length `n, where 0 <= `n"
        " <= `MAX_STRING_SIZE.",
        1, (value msize), OP_LEAF | OP_NOESCAPE, "n.s")
{
  int size = GETINT(msize);
  if (size < 0 || size > MAX_STRING_SIZE)
    runtime_error(error_bad_value);
  struct string *newp = alloc_empty_string(size);
  /* alloc_empty_string() doesn't zero its data,
   * and we don't want to pass sensitive info to
   * someone on accident, so let's zero away... */
  memset(newp->str, 0, size);
  return newp;
}

TYPEDOP(string_length, 0, "`s -> `n. Return length of string",
        1, (struct string *str),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_STR_READONLY, "s.n")
{
  TYPEIS(str, type_string);
  return makeint(string_len(str));
}

TYPEDOP(string_downcase, 0, "`s0 -> `s1. Returns a copy of `s0 with all"
        " characters lower case",
	1, (struct string *s),
	OP_LEAF | OP_NOESCAPE | OP_STR_READONLY, "s.s")
{
  TYPEIS(s, type_string);
  GCPRO1(s);
  long l = string_len(s);
  struct string *newp = alloc_empty_string(l);
  UNGCPRO();

  char *s1 = s->str, *s2 = newp->str;
  while (l--)
    s2[l] = TO_8LOWER(s1[l]);

  return newp;
}

TYPEDOP(string_upcase, 0, "`s0 -> `s1. Returns a copy of `s0 with all"
        " characters upper case",
	1, (struct string *s),
	OP_LEAF | OP_NOESCAPE | OP_STR_READONLY, "s.s")
{
  TYPEIS(s, type_string);
  GCPRO1(s);
  long l = string_len(s);
  struct string *newp = alloc_empty_string(l);
  UNGCPRO();

  char *s1 = s->str, *s2 = newp->str;
  while (l--)
    s2[l] = TO_8UPPER(s1[l]);

  return newp;
}


TYPEDOP(string_fill, "string_fill!", "`s `n -> `s. Set all characters of `s to"
        " character `n",
	2, (struct string *str, value c),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE, "sn.1")
{
  TYPEIS(str, type_string);
  memset(str->str, GETINT(c), string_len(str));
  return str;
}

TYPEDOP(string_7bit, 0, "`s0 -> `s1. Returns a copy of `s0 with all characters"
        " converted to printable 7 bit form",
	1, (struct string *s),
	OP_LEAF | OP_NOESCAPE | OP_STR_READONLY, "s.s")
{
  TYPEIS(s, type_string);
  GCPRO1(s);
  long l = string_len(s);
  struct string *newp = alloc_empty_string(l);
  UNGCPRO();

  char *s1 = s->str, *s2 = newp->str;
  while (l--)
    s2[l] = TO_7PRINT(s1[l]);

  return newp;
}

TYPEDOP(ascii_to_html, 0, "`s0 -> `s0|`s1. Escapes HTML special characters."
        " May return either `s0, if `s0 doesn't contain any special chars,"
        " or a new string.",
	1, (struct string *s),
	OP_LEAF | OP_NOESCAPE | OP_STR_READONLY, "s.s")
{
  TYPEIS(s, type_string);
  long l = string_len(s);
  long want = l;
  const char *s1 = s->str;
  for (int i = 0; i < l; ++i)
    switch (s1[i])
      {
	case '&': /* "&amp;"  */ want += 4; break;
	case '>': /* "&gt;"   */
	case '<': /* "&lt;"   */ want += 3; break;
	case '"': /* "&quot;" */ want += 5; break;
      }
  if (want == l)
    return s;

  GCPRO1(s);
  struct string *newp = alloc_empty_string(want);
  UNGCPRO();

  s1 = s->str;
  char *s2 = newp->str;
  for (int i = 0; i < l; ++i)
    switch (s1[i])
      {
	case '&': memcpy(s2, "&amp;", 5);  s2 += 5; break;
	case '>': memcpy(s2, "&gt;", 4);   s2 += 4; break;
	case '<': memcpy(s2, "&lt;", 4);   s2 += 4; break;
	case '"': memcpy(s2, "&quot;", 6); s2 += 6; break;
	default: *s2++ = s1[i]; break;
      }
  assert(s2 == newp->str + want);

  return newp;
}

#ifndef NBSP
#  define NBSP "\240"
#endif

TYPEDOP(string_from_utf8, 0, "`s0 `n -> `s1. Returns the UTF-8 string `s0"
        " converted to an ISO" NBSP "8859-1 string. `n controls how conversion"
        " errors are handled:\n"
        "   0  \tcharacters that cannot be represented in ISO" NBSP "8859-1"
        " and incorrect UTF-8 codes cause a runtime error\n"
        "   1  \tcharacters that cannot be represented are translitterated"
        " if possible; incorrect codes cause a runtime error\n"
        "   2  \tcharacters that cannot be represented are translitterated"
        " if possible; incorrect codes are skipped\n"
        "   3  \tcharacters that cannot be represented and incorrect"
        " codes are skipped",
        2, (struct string *s, value mmode),
	OP_LEAF | OP_NOESCAPE | OP_STR_READONLY, "sn.s")
{
  TYPEIS(s, type_string);

  int mode = GETINT(mmode);
  const char *toenc = NULL;
  switch (mode) {
  case 0: toenc = "ISO-8859-1"; break;
  case 1: case 2: toenc = "ISO-8859-1//TRANSLIT"; break;
  case 3: toenc = "ISO-8859-1//IGNORE"; break;
  }
  if (toenc == NULL)
    runtime_error(error_bad_value);

  char *localstr;
  LOCALSTR(localstr, s);

  iconv_t cd = iconv_open(toenc, "UTF-8");
  if (cd == (iconv_t)-1)
    runtime_error(error_bad_value);

  struct oport *op = NULL;
  GCPRO1(op);

  struct string *result;

  size_t inlen = string_len(s);
  for (;;) {
    char buf[4096], *ostr = buf;
    size_t olen = sizeof buf;
    size_t r = iconv(cd, &localstr, &inlen, &ostr, &olen);
    if (r == (size_t)-1 && errno != E2BIG && inlen > 0) {
      if (mode >= 2) {
        --inlen;
        ++localstr;
      } else {
        iconv_close(cd);
        runtime_error(error_bad_value);
      }
    }
    if (op == NULL) {
      if (inlen == 0) {
        /* common (?) case; everything converted in one go */
        result = alloc_string_length(buf, ostr - buf);
        break;
      }
      op = make_string_oport();
    }
    opwrite(op, buf, ostr - buf);
    if (inlen == 0) {
      result = port_string(op);
      break;
    }
  }
  UNGCPRO();
  iconv_close(cd);
  return result;
}

EXT_TYPEDOP(string_ref, 0, "`s `n1 -> `n2. Return the code (`n2) of the `n1'th"
            " character of `s",
	    2, (struct string *str, value c),
	    OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_STR_READONLY, "sn.n")
{
  TYPEIS(str, type_string);
  if (!integerp(c))
    primitive_runtime_error(error_bad_type, &op_string_ref, 2, str, c);
  long idx = intval(c);
  if (idx < 0)
    idx += string_len(str);
  if (idx < 0 || idx >= string_len(str))
    primitive_runtime_error(error_bad_index, &op_string_ref, 2, str, c);
  return makeint((unsigned char)str->str[idx]);
}

EXT_TYPEDOP(string_set, "string_set!", "`s `n1 `n2 -> `n2. Set the `n1'th"
            " character of `s to the character whose code is `n2",
	    3, (struct string *str, value i, value mc),
	    OP_LEAF | OP_NOALLOC | OP_NOESCAPE, "snn.n")
{
  TYPEIS(str, type_string);
  if (str->o.flags & OBJ_READONLY)
    primitive_runtime_error(error_value_read_only, &op_string_set, 3,
                            str, i, mc);
  if (!integerp(i) || !integerp(mc))
    primitive_runtime_error(error_bad_type, &op_string_set, 3,
                            str, i, mc);
  long idx = intval(i);
  long c = intval(mc);
  if (idx < 0)
    idx += string_len(str);
  if (idx < 0 || idx >= string_len(str))
    primitive_runtime_error(error_bad_index, &op_string_set, 3,
                            str, i, mc);

  return makeint((unsigned char)(str->str[idx] = c));
}

#define STRING_CMP(type, desc, canon)                                   \
static value string_n ## type ## cmp(struct string *s1,                 \
                                     struct string *s2, long lmax)      \
{                                                                       \
  long l1 = string_len(s1);                                             \
  if (l1 > lmax) l1 = lmax;                                             \
  long l2 = string_len(s2);                                             \
  if (l2 > lmax) l2 = lmax;                                             \
  const char *t1 = s1->str;                                             \
  const char *t2 = s2->str;                                             \
                                                                        \
  for (int i = 0; ; ++i, ++t1, ++t2)                                    \
    {                                                                   \
      if (i == l1) return makeint(i - l2);                              \
      if (i == l2) return makeint(1);                                   \
      int diff = canon(*t1) - canon(*t2);                               \
      if (diff) return makeint(diff);                                   \
    }                                                                   \
}                                                                       \
                                                                        \
TYPEDOP(string_n ## type ## cmp, 0, "`s1 `s2 `n0 -> `n1. Compare at"    \
        " most `n0 characters of two strings" desc "."                  \
        " Returns 0 if `s1 = `s2, < 0 if `s1 < `s2"                     \
        " and > 0 if `s1 > `s2",                                        \
	3, (struct string *s1, struct string *s2, value nmax),          \
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE                              \
        | OP_STR_READONLY | OP_CONST,                                   \
        "ssn.n")                                                        \
{                                                                       \
  if (!TYPE(s1, type_string) || !TYPE(s2, type_string)                  \
      || !integerp(nmax))                                               \
    primitive_runtime_error(error_bad_type,                             \
                            &op_string_n ## type ## cmp,                \
                            3, s1, s2, nmax);                           \
  return string_n ## type ## cmp(s1, s2, intval(nmax));                 \
}                                                                       \
                                                                        \
TYPEDOP(string_ ## type ## cmp, 0, "`s1 `s2 -> `n. Compare two"         \
        " strings " desc ". Returns 0 if `s1 = `s2, < 0 if `s1 < `s2"   \
        " and > 0 if `s1 > `s2",                                        \
	2, (struct string *s1, struct string *s2),                      \
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE                              \
        | OP_STR_READONLY | OP_CONST,                                   \
        "ss.n")                                                         \
{                                                                       \
  if (!TYPE(s1, type_string) || !TYPE(s2, type_string))                 \
    primitive_runtime_error(error_bad_type, &op_string_ ## type ## cmp, \
                            2, s1, s2);                                 \
  return string_n ## type ## cmp(s1, s2, LONG_MAX);                     \
}

STRING_CMP(, , (unsigned char))
STRING_CMP(8i, " ignoring case", TO_8LOWER)
STRING_CMP(i, " ignoring accentuation and case", TO_7LOWER)

TYPEDOP(string_search, 0,
        "`s1 `s2 -> `n. Searches in string `s1 for string `s2."
        " Returns -1 if not found, index of first matching character"
        " otherwise.",
	2, (struct string *s1, struct string *s2),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_STR_READONLY | OP_CONST,
        "ss.n")
{
  ulong l1, l2, i, j, i1;
  char *t1, *t2, lastc2;

  TYPEIS(s1, type_string);
  TYPEIS(s2, type_string);

  l1 = string_len(s1);
  l2 = string_len(s2);

  /* Immediate termination conditions */
  if (l2 == 0) return makeint(0);
  if (l2 > l1) return makeint(-1);

  t1 = s1->str;
  t2 = s2->str;
  lastc2 = t2[l2 - 1];

  i = l2 - 1; /* No point in starting earlier */
  for (;;)
    {
      /* Search for lastc2 in t1 starting at i */
      while (t1[i] != lastc2)
	if (++i == l1) return makeint(-1);

      /* Check if rest of string matches */
      j = l2 - 1;
      i1 = i;
      do
	if (j == 0) return makeint(i1); /* match found at i1 */
      while (t2[--j] == t1[--i1]);

      /* No match. If we wanted better efficiency, we could skip over
	 more than one character here (depending on where the next to
	 last 'lastc2' is in s2.
	 Probably not worth the bother for short strings */
      if (++i == l1) return makeint(-1); /* Might be end of s1 */
    }
}

TYPEDOP(string_isearch, 0,
        "`s1 `s2 -> `n. Searches in string `s1 for string `s2"
        " (case- and accentuation-insensitive)."
        " Returns -1 if not found, index of first matching character"
        " otherwise.",
	2, (struct string *s1, struct string *s2),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_STR_READONLY | OP_CONST,
        "ss.n")
{
  ulong l1, l2, i, j, i1;
  char *t1, *t2, lastc2;

  TYPEIS(s1, type_string);
  TYPEIS(s2, type_string);

  l1 = string_len(s1);
  l2 = string_len(s2);

  /* Immediate termination conditions */
  if (l2 == 0) return makeint(0);
  if (l2 > l1) return makeint(-1);

  t1 = s1->str;
  t2 = s2->str;
  lastc2 = TO_7LOWER(t2[l2 - 1]);

  i = l2 - 1; /* No point in starting earlier */
  for (;;)
    {
      /* Search for lastc2 in t1 starting at i */
      while (TO_7LOWER(t1[i]) != lastc2)
	if (++i == l1) return makeint(-1);

      /* Check if rest of string matches */
      j = l2 - 1;
      i1 = i;
      do
	{
	  if (j == 0) return makeint(i1); /* match found at i1 */
	  --j; --i1;
	}
      while (TO_7LOWER(t2[j]) == TO_7LOWER(t1[i1]));

      /* No match. If we wanted better efficiency, we could skip over
	 more than one character here (depending on where the next to
	 last 'lastc2' is in s2.
	 Probably not worth the bother for short strings */
      if (++i == l1) return makeint(-1); /* Might be end of s1 */
    }
}

TYPEDOP(substring, 0, "`s1 `n1 `n2 -> `s2. Extract substring of `s starting"
        " at `n1 of length `n2. The first character is numbered 0",
	3, (struct string *s, value start, value length),
	OP_LEAF | OP_NOESCAPE | OP_STR_READONLY, "snn.s")
{
  TYPEIS(s, type_string);

  long first = GETINT(start);
  if (first < 0)
    first += string_len(s);
  long size = GETINT(length);
  if (first < 0 || size < 0 || first + size > string_len(s))
    runtime_error(error_bad_index);

  GCPRO1(s);
  struct string *newp = alloc_empty_string(size);
  memcpy(newp->str, s->str + first, size);
  UNGCPRO();

  return newp;
}

bool string_equalp(struct string *a, struct string *b)
{
  assert(TYPE(a, type_string) && TYPE(b, type_string));
  long la = string_len(a);
  return la == string_len(b) && memcmp(a->str, b->str, la) == 0;
}

value string_append(struct string *s1, struct string *s2,
                    const struct primitive_ext *op)
{
  GCPRO2(s1, s2);

  long l1 = string_len(s1);
  long l2 = string_len(s2);

  long nl = l1 + l2;
  if (nl > MAX_STRING_SIZE)
    primitive_runtime_error(error_bad_value, op, 2, s1, s2);

  struct string *newp = alloc_empty_string(nl);
  memcpy(newp->str, s1->str, l1);
  memcpy(newp->str + l1, s2->str, l2);
  UNGCPRO();

  return newp;
}

EXT_TYPEDOP(string_append, 0, "`s1 `s2 -> `s. Concatenate `s1 and `s2."
            " The resulting string must not have more than `MAX_STRING_SIZE"
            " characters.",
	    2, (struct string *s1, struct string *s2),
	    OP_LEAF | OP_NOESCAPE | OP_STR_READONLY, "ss.s")
{
  TYPEIS(s1, type_string);
  TYPEIS(s2, type_string);
  return string_append(s1, s2, &op_string_append);
}

TYPEDOP(split_words, 0, "`s -> `l. Split string `s into a list of words."
        " Single- or double-quoted sequences of words are kept together.",
        1, (struct string *s),
        OP_LEAF | OP_NOESCAPE | OP_STR_READONLY, "s.l")
{
  struct list *l = NULL, *last = NULL;

  TYPEIS(s, type_string);
  long slen = string_len(s);

  GCPRO3(l, last, s);

  int idx = 0;

  for (;;) {
    while (idx < slen && s->str[idx] == ' ')
      ++idx;

    if (idx == slen)
      break;

    char *endp;
    int end;
    if ((s->str[idx] == '\'' || s->str[idx] == '"') /* quoted words */
        && (endp = memchr(s->str + idx + 1, s->str[idx], slen - idx - 1)))
        end = endp - s->str + 1;
    else
      {
        end = idx + 1;
        while (end < slen && s->str[end] != ' ')
          ++end;
      }

    int len = end - idx;
    struct string *wrd = alloc_empty_string(len);
    memcpy(wrd->str, s->str + idx, len);

    idx = end;

    if (!l)
      l = last = alloc_list(wrd, NULL);
    else
      {
	value v = alloc_list(wrd, NULL);
	last->cdr = v;
	last = v;
      }
  }

  UNGCPRO();

  return l;
}

TYPEDOP(atoi, 0, "`s -> `n|`s. Converts string into integer. Returns `s if"
        " conversion failed.",
	1, (struct string *s),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_STR_READONLY | OP_CONST,
        "s.S")
{
  if (!TYPE(s, type_string))
    primitive_runtime_error(error_bad_type, &op_atoi, 1, s);

  int n;
  if (!mudlle_strtoint(s->str, &n))
    return s;

  return makeint(n);
}

TYPEDOP(itoa, 0, "`n -> `s. Converts integer into string", 1, (value n),
        OP_LEAF | OP_NOESCAPE | OP_CONST, "n.s")
{
  if (!integerp(n))
    primitive_runtime_error(error_bad_type, &op_itoa, 1, n);

  char buf[16];
  sprintf(buf, "%ld", intval(n));
  return make_readonly(alloc_string(buf));
}

TYPEDOP(isalpha, "calpha?", "`n -> `b. TRUE if `n is a letter (allowed in"
        " keywords)",
	1, (value n),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_CONST, "n.n")
{
  return makebool(IS_8NAME(GETINT(n)));
}

TYPEDOP(isdigit, "cdigit?", "`n -> `b. TRUE if `n is a digit",
	1, (value n),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_CONST, "n.n")
{
  return makebool(isdigit((unsigned char)GETINT(n)));
}

TYPEDOP(isxdigit, "cxdigit?", "`n -> `b. TRUE if `n is a hexadecimal digit",
	1, (value n),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_CONST, "n.n")
{
  return makebool(isxdigit((unsigned char)GETINT(n)));
}

TYPEDOP(isprint, "cprint?", "`n -> `b. TRUE if `n is a printable character",
        1, (value n),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_CONST, "n.n")
{
  return makebool(IS_8PRINT(GETINT(n)));
}

TYPEDOP(isspace, "cspace?", "`n -> `b. TRUE if `n is a white space character."
        " N.b., returns FALSE for non-breaking space (`NBSP).",
        1, (value n),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_CONST, "n.n")
{
  /* not using IS_8SPACE() so any line-wrapping code that uses
     cspace?() to find potential line breaks keeps working */
  return makebool(isspace((unsigned char)GETINT(n)));
}

TYPEDOP(isupper, "cupper?", "`n -> `b. TRUE if `n is an upper case character",
        1, (value n),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_CONST, "n.n")
{
  unsigned char c = GETINT(n);
  return makebool(TO_8LOWER(c) != c);
}

TYPEDOP(islower, "clower?", "`n -> `b. TRUE if `n is an lower case character",
        1, (value n),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_CONST, "n.n")
{
  unsigned char c = GETINT(n);
  return makebool(TO_8UPPER(c) != c);
}

TYPEDOP(cupper, 0, "`n0 -> `n1. Return `n0's upper case variant", 1, (value n),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_CONST, "n.n")
{
  return makeint(TO_8UPPER(GETINT(n)));
}

TYPEDOP(clower, 0, "`n0 -> `n1. Return `n0's lower case variant", 1, (value n),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_CONST, "n.n")
{
  return makeint(TO_8LOWER(GETINT(n)));
}

TYPEDOP(cicmp, 0, "`n0 `n1 -> `n2. Compare characters `n0 and `n1 as"
        " `string_icmp() does. Returns -1, 0, or 1 if `n0 is less than,"
        " equal, or greater than `n1.",
        2, (value n0, value n1),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_CONST, "nn.n")
{
  return makeint(TO_7LOWER(GETINT(n0)) - TO_7LOWER(GETINT(n1)));
}

TYPEDOP(c7bit, 0, "`n0 -> `n1. Return `n0's 7 bit variant", 1, (value n),
	OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_CONST, "n.n")
{
  return makeint(TO_7PRINT(GETINT(n)));
}



const unsigned char *get_iso88591_pcre_table(void)
{
#ifdef USE_PCRE
  static bool been_here = false;
  static const unsigned char *table;

  if (been_here)
    return table;

  been_here = true;
  char *olocale = strdup(setlocale(LC_ALL, NULL));
  if (setlocale(LC_ALL, "en_US.iso88591") == NULL)
    return NULL;
  table = pcre_maketables();
  setlocale(LC_ALL, olocale);
  return table;
#else  /* ! USE_PCRE */
  return NULL;
#endif /* ! USE_PCRE */
}

TYPEDOP(is_regexp, "regexp?", "`x -> `b. Returns TRUE if `x is a"
        " regular expression, as created by `make_regexp()",
        1, (value re), OP_LEAF | OP_NOALLOC | OP_NOESCAPE, "x.n")
{
  return makebool(is_regexp(re));
}

TYPEDOP(fnmatch, 0, "`s0 `s1 `n -> `b. Returns true if the glob pattern `s0"
        " matches the string `s1 using flags in `n:\n"
        "  \t`FNM_NOESCAPE  \tdo not treat backslash (\\) as an escape"
        " character\n"
        "  \t`FNM_PATHNAME  \tperform a pathname match, where wildcards"
        " (* and ?) only match between slashes\n"
        "  \t`FNM_PERIOD    \tdo not let wildcards match leading periods",
        3, (struct string *pat, struct string *str, value mflags),
        OP_LEAF | OP_NOALLOC | OP_NOESCAPE | OP_STR_READONLY | OP_CONST,
        "ssn.n")
{
  TYPEIS(pat, type_string);
  TYPEIS(str, type_string);
  long flags = GETINT(mflags);
  if (flags & ~(FNM_NOESCAPE | FNM_PATHNAME | FNM_PERIOD))
    runtime_error(error_bad_value);

  /* zero bytes cannot match or be matched */
  if (string_len(pat) != strlen(pat->str)
      || string_len(str) != strlen(str->str))
    return makebool(false);

  return makebool(fnmatch(pat->str, str->str, flags) == 0);
}

#ifdef USE_PCRE

static struct string *regexp_str;

static void *regexp_malloc(size_t s)
{
  /*
   * This assert checks the assumption (which is more or less guaranteed
   * in the documentation of pcre) that pcre_malloc is called at most once
   * once per call to make_regexp.
   */
  assert(!regexp_str);
  regexp_str = (struct string *)allocate_string(type_string, s);
  return regexp_str->str;
}

static void regexp_free(void *p)
{
  /*
   * This assert checks the assumption (which is more or less guaranteed
   * in the documentation of pcre) that pcre_free is called at most once,
   * and only after pcre_malloc has been called.
   */
  assert(TYPE(regexp_str, type_string) && regexp_str->str == p);
  regexp_str = NULL;
}

TYPEDOP(make_regexp, 0, "`s `n -> `r. Create a matcher for the regular"
        " expression `s with flags `n.\n"
        "Returns cons(`errorstring, `erroroffset) on error.\n"
	"The following flags are supported:\n"
	"  \t`PCRE_7BIT       \tconvert pattern to its 7-bit equivalent\n"
	"  \t`PCRE_ANCHORED   \tforce pattern anchoring\n"
	"  \t`PCRE_CASELESS   \tdo caseless matching\n"
	"  \t`PCRE_DOLLAR_ENDONLY\n"
	"                  \t$ only matches end of string,"
	" and not newline\n"
	"  \t`PCRE_DOTALL     \t. matches anything, including newline\n"
	"  \t`PCRE_EXTENDED   \tignore whitespace and # comments\n"
	"  \t`PCRE_EXTRA      \tuse PCRE extra features"
	" (no idea what that means)\n"
	"  \t`PCRE_MULTILINE  \t^ and $ match at newlines\n"
	"  \t`PCRE_UNGREEDY   \tinvert greedyness of quantifiers",
	2, (struct string *pat, value mflags),
	OP_LEAF | OP_NOESCAPE | OP_STR_READONLY, "sn.[ok]")
{
  pcre *p;
  const char *errstr;
  int errofs;
  void *(*old_malloc)(size_t) = pcre_malloc;
  void (*old_free)(void *) = pcre_free;
  char *lpat;

  int flags = GETINT(mflags);
  TYPEIS(pat, type_string);

  LOCALSTR(lpat, pat);
  if (flags & PCRE_7BIT)
    {
      strto7print(lpat);
      flags &= ~PCRE_7BIT;
    }
  regexp_str = NULL;
  GCPRO1(regexp_str);

  const unsigned char *table = get_iso88591_pcre_table();
  pcre_malloc = regexp_malloc;
  pcre_free = regexp_free;
  p = pcre_compile(lpat, flags, &errstr, &errofs, table);
  pcre_malloc = old_malloc;
  pcre_free = old_free;

  value result;
  if (p == NULL)
    result = alloc_list(alloc_string(errstr), makeint(errofs));
  else
    {
      struct grecord *mregexp  = alloc_private(PRIVATE_REGEXP, 1);
      make_readonly(regexp_str);
      mregexp->data[1] = &regexp_str->o;
      result = make_readonly(mregexp);
    }

  UNGCPRO();
  return result;
}

static const pcre *get_regexp(value x)
{
  if (!is_regexp(x))
    runtime_error(error_bad_type);

  struct grecord *re = x;
  struct string *restr = (struct string *)re->data[1];
  assert(TYPE(restr, type_string));
  return (const pcre *)restr->str;
}

TYPEDOP(regexp_exec, 0,
        "`r `s `n0 `n1 -> `v. Tries to match the string `s, starting at"
	" character `n0 with regexp matcher `r and flags `n1.\n"
	"Returns a vector of submatches or false if no match.\n"
	"A submatch is either a string matched by the corresponding"
	" parenthesis group, or null if that group was not used.\n"
	"If `n1 & `PCRE_INDICES, submatches are instead represented as"
	" cons(`start, `length) or null.\n"
        "If `n1 & PCRE_BOOLEAN, the result is instead a boolean saying"
        " whether a match was found.\n"
        "`PCRE_INDICES and `PCRE_BOOLEAN must not both be set.\n"
	"The following flags are supported:\n"
	"  \t`PCRE_7BIT      \tconvert the haystack to its 7-bit equivalent"
	" before matching\n"
	"  \t`PCRE_ANCHORED  \tmatch only at the first position\n"
        "  \t`PCRE_BOOLEAN   \tsee above\n"
	"  \t`PCRE_INDICES   \tsee above\n"
	"  \t`PCRE_NOTBOL    \t`s is not the beginning of a line\n"
	"  \t`PCRE_NOTEMPTY  \tan empty string is not a valid match\n"
	"  \t`PCRE_NOTEOL    \t`s is not the end of a line",
        4, (struct string *mre, struct string *str, value msofs, value mflags),
	OP_LEAF | OP_NOESCAPE | OP_STR_READONLY, "osnn.[vn]")
{
  int flags = GETINT(mflags);
  const pcre *re = get_regexp(mre);
  TYPEIS(str, type_string);

  size_t slen = string_len(str);

  long sofs = GETINT(msofs);
  if (sofs < 0 || sofs > slen)
    runtime_error(error_bad_index);

  int nsub;
  /* this should probably be an assertion */
  if (pcre_fullinfo(re, NULL, PCRE_INFO_CAPTURECOUNT, &nsub) != 0)
    runtime_error(error_bad_value);
  int olen = (nsub + 1) * 3;
  int ovec[olen];

  bool indices = flags & PCRE_INDICES;
  bool boolean = flags & PCRE_BOOLEAN;
  flags &= ~(PCRE_INDICES | PCRE_BOOLEAN);
  if (indices && boolean)
    runtime_error(error_bad_value);

  {
    char *haystack = str->str;
    char *lstr = NULL;

    if (flags & PCRE_7BIT)
      {
        lstr = malloc(slen + 1);
        for (size_t idx = 0; idx <= slen; ++idx)
          lstr[idx] = TO_7PRINT(haystack[idx]);
        haystack = lstr;
        flags &= ~PCRE_7BIT;
      }

    /* n.b., requires reasonably new libpcre (older ones did not have
       the 'sofs' parameter) */
    int res = pcre_exec(re, NULL, haystack, slen, sofs, flags, ovec, olen);

    free(lstr);

    if (res == PCRE_ERROR_NOMATCH)
      return makebool(false);
    if (res < 0)
      runtime_error(error_bad_value);
    if (flags & PCRE_BOOLEAN)
      return makebool(true);
  }

  struct vector *v = alloc_vector(nsub + 1);
  GCPRO1(v);

  for (int i = 0; i <= nsub; ++i)
    {
      int st = ovec[i * 2];
      if (st >= 0)
	{
	  int ln = ovec[i * 2 + 1] - st;
          if (indices)
            SET_VECTOR(v, i, alloc_list(makeint(st), makeint(ln)));
	  else
            {
              struct string *tmp = alloc_empty_string(ln);
              memcpy(tmp->str, str->str + st, ln);
              v->data[i] = tmp;
            }
	}
    }

  UNGCPRO();

  return v;
}

#endif /* USE_PCRE */

#ifdef HAVE_CRYPT
TYPEDOP(crypt, 0, "`s1 `s2 -> `s3. Encrypt `s1 using `s2 as salt",
	2, (struct string *s, struct string *salt),
	OP_LEAF | OP_NOESCAPE | OP_STR_READONLY, "ss.s")
{
  char buffer[9];
  int i;
  char *p;

  TYPEIS(s, type_string);
  TYPEIS(salt, type_string);

  /* Use all the characters in the string, rather than the first 8. */
  memset(buffer, 0, sizeof buffer);

  for (p = s->str, i = 0; *p; ++p, i = (i + 1) & 7)
    buffer[i] += *p;

  for (i = 0; i < 8; ++i)
    if (!buffer[i])
      buffer[i] = ' ';

  return alloc_string(crypt(buffer, salt->str));
}
#endif	/* HAVE_CRYPT */

static void define_string(const char *name, const char *val)
{
  system_define(name, make_readonly(alloc_string(val)));
}

static void define_strings(void)
{
  const char host_type[] =
#ifdef i386
    "i386"
#elif defined __x86_64__
    "x86_64"
#else
#  error Fix me
#endif
    ;
  define_string("host_type", host_type);
}

void string_init(void)
{
  DEFINE(stringp);
  DEFINE(make_string);
  DEFINE(string_length);
  DEFINE(string_fill);
  DEFINE(string_ref);
  DEFINE(string_set);
  DEFINE(string_cmp);
  DEFINE(string_icmp);
  DEFINE(string_8icmp);
  DEFINE(string_ncmp);
  DEFINE(string_nicmp);
  DEFINE(string_n8icmp);
  DEFINE(string_search);
  DEFINE(string_isearch);
  DEFINE(substring);
  DEFINE(string_append);
  DEFINE(split_words);
  DEFINE(itoa);
  DEFINE(atoi);
  DEFINE(string_upcase);
  DEFINE(string_downcase);
  DEFINE(string_7bit);
  DEFINE(ascii_to_html);
  DEFINE(string_from_utf8);
  DEFINE(isalpha);
  DEFINE(isupper);
  DEFINE(islower);
  DEFINE(isdigit);
  DEFINE(isxdigit);
  DEFINE(isprint);
  DEFINE(isspace);
  DEFINE(cupper);
  DEFINE(clower);
  DEFINE(cicmp);
  DEFINE(c7bit);

  define_strings();

#define def(x) system_define(#x, makeint(x))
  DEFINE(fnmatch);
  def(FNM_NOESCAPE);
  def(FNM_PATHNAME);
  def(FNM_PERIOD);

  DEFINE(is_regexp);

#ifdef USE_PCRE

  DEFINE(make_regexp);
  DEFINE(regexp_exec);

  /* PCRE options */
  def(PCRE_7BIT);
  def(PCRE_ANCHORED);
  def(PCRE_BOOLEAN);
  def(PCRE_CASELESS);
  def(PCRE_DOLLAR_ENDONLY);
  def(PCRE_DOTALL);
  def(PCRE_EXTENDED);
  def(PCRE_EXTRA);
  def(PCRE_INDICES);
  def(PCRE_MULTILINE);
  def(PCRE_NOTBOL);
  def(PCRE_NOTEMPTY);
  def(PCRE_NOTEOL);
  def(PCRE_UNGREEDY);
#endif  /* USE_PCRE */

#ifdef HAVE_CRYPT
  DEFINE(crypt);
#endif

  system_define("MAX_STRING_SIZE", makeint(MAX_STRING_SIZE));
}
