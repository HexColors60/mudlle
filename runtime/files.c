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

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <glob.h>
#include <string.h>
#include <alloca.h>

#include "runtime/runtime.h"
#include "print.h"
#include "utils.h"
#include "mparser.h"
#include "interpret.h"
#include "runtime/files.h"
#include "call.h"

OPERATION(load, "s -> . Loads file s", 1, (struct string *name), 0)
{
  char *fname;

  TYPEIS(name, type_string);
  LOCALSTR(fname, name);
  return makebool(load_file(name->str, fname, 1, TRUE));
}

UNSAFEOP(mkdir, "s n1 -> n2. Make directory s (mode n1)",
	 2, (struct string *name, value mode),
	 OP_LEAF | OP_NOALLOC)
{
  TYPEIS(name, type_string);
  ISINT(mode);

  return makeint(mkdir(name->str, intval(mode)));
}

UNSAFEOP(directory_files, "s -> l. List all files of directory s (returns "
	 "false if problems)",
	 1, (struct string *dir),
	 OP_LEAF)
{
  DIR *d;

  TYPEIS(dir, type_string);

  if ((d = opendir(dir->str)))
    {
      struct dirent *entry;
      struct list *files = NULL;
      struct gcpro gcpro1;

      GCPRO1(files);
      while ((entry = readdir(d)))
	{
	  struct string *fname = alloc_string(entry->d_name);

	  files = alloc_list(fname, files);
	}
      UNGCPRO();
      closedir(d);

      return files;
    }
  return makebool(FALSE);	  
}

UNSAFEOP(glob_files, "s n -> l. Returns a list of all files matched by the "
	 "glob pattern s, using flags in n (GLOB_xxx). Returns FALSE "
	 "on error", 2, (struct string *pat, value n), OP_LEAF)
{
  glob_t files;
  struct list *l = NULL;
  struct gcpro gcpro1;
  char **s;
  long flags = intval(n);
  TYPEIS(pat, type_string);

#ifdef _GNU_GLOB_
  if (flags & ~(GLOB_TILDE | GLOB_BRACE | GLOB_MARK | GLOB_NOCHECK |
		GLOB_NOESCAPE | GLOB_PERIOD | GLOB_NOMAGIC | GLOB_ONLYDIR))
#else
  if (flags & ~( GLOB_MARK | GLOB_NOCHECK | GLOB_NOESCAPE ))
#endif
    runtime_error(error_bad_value);

  if (glob(pat->str, flags, NULL, &files))
    {
      globfree(&files);
      return makebool(FALSE);
    }

  GCPRO1(l);
  for (s = files.gl_pathv; *s; ++s)
    {
      struct string *f = alloc_string(*s);
      struct gcpro gcpro2;
      GCPRO(gcpro2, f);
      l = alloc_list(f, l);
      UNGCPRO1(gcpro2);
    }
  UNGCPRO();
  globfree(&files);
  return l;
}

static value build_file_stat(struct stat *sb)
{
  struct vector *info = alloc_vector(13);
  
  info->data[0] = makeint((int)sb->st_dev);
  info->data[1] = makeint(sb->st_ino);
  info->data[2] = makeint(sb->st_mode);
  info->data[3] = makeint(sb->st_nlink);
  info->data[4] = makeint(sb->st_uid);
  info->data[5] = makeint(sb->st_gid);
  info->data[6] = makeint((int)sb->st_rdev);
  info->data[7] = makeint(sb->st_size);
  info->data[8] = makeint(sb->st_atime);
  info->data[9] = makeint(sb->st_mtime);
  info->data[10] = makeint(sb->st_ctime);
  info->data[11] = makeint(sb->st_blksize);
  info->data[12] = makeint(sb->st_blocks);
  
  return info;
}

UNSAFEOP(file_stat, "s -> v. Returns status of file s (returns false for "
	 "failure). See the FS_xxx constants and file_lstat().",
	 1, (struct string *fname),
	 OP_LEAF)
{
  struct stat sb;

  TYPEIS(fname, type_string);
  if (!stat(fname->str, &sb))
    return build_file_stat(&sb);
  else
    return makebool(FALSE);
}

UNSAFEOP(file_lstat, "s -> v. Returns status of file s (not following links) "
	 ". Returns FALSE for failure. See the FS_xxx constants and file_stat()",
	 1, (struct string *fname),
	 OP_LEAF)
{
  struct stat sb;

  TYPEIS(fname, type_string);
  if (!lstat(fname->str, &sb))
    return build_file_stat(&sb);
  else
    return makebool(FALSE);
}

UNSAFEOP(readlink, "s1 -> s2. Returns the contents of symlink s, or FALSE "
	 "for failure", 1, (struct string *lname), OP_LEAF)
{
  struct stat sb;
  struct string *res;
  struct gcpro gcpro1;

  TYPEIS(lname, type_string);
  if (lstat(lname->str, &sb) ||
      !S_ISLNK(sb.st_mode))
    return makebool(FALSE);
  
  GCPRO1(lname);
  res = (struct string *)allocate_string(type_string, sb.st_size + 1);
  UNGCPRO();

  if (readlink(lname->str, res->str, sb.st_size) < 0)
    return makebool(FALSE);
  res->str[sb.st_size] = '\0';

  return res;
}

UNSAFEOP(file_regularp, "s -> b. Returns TRUE if s is a regular file (null "
	 "for failure)",
	 1, (struct string *fname),
	 OP_LEAF | OP_NOALLOC)
{
  struct stat sb;

  TYPEIS(fname, type_string);
  if(!stat(fname->str, &sb))
    return makebool(S_ISREG(sb.st_mode));
  else
    return NULL;
}

UNSAFEOP(remove, "s -> b. Removes file s, returns TRUE if success",
	  1, (struct string *fname),
	  OP_LEAF)
{
  TYPEIS(fname, type_string);

  return makebool(unlink(fname->str) == 0);
}

UNSAFEOP(rename, "s1 s2 -> n. Renames file s1 to s2. Returns the Unix error "
	 "number or 0 for success",
	  2, (struct string *oldname, struct string *newname),
	  OP_LEAF)
{
  TYPEIS(oldname, type_string);
  TYPEIS(newname, type_string);

  return makeint(rename(oldname->str, newname->str) ? errno : 0);
}

UNSAFEOP(file_read, "s1 -> s2. Reads file s1 and returns its contents (or "
	 "the Unix errno value)",
	  1, (struct string *name),
	  OP_LEAF)
{
  int fd;

  TYPEIS(name, type_string);

  if ((fd = open(name->str, O_RDONLY)) >= 0)
    {
      off_t size = lseek(fd, 0, SEEK_END);

      if (size >= 0)
	{
	  struct string *s = (struct string *)allocate_string(type_string, 
							      size + 1);

	  if (lseek(fd, 0, SEEK_SET) == 0 && read(fd, s->str, size) == size)
	    {
	      s->str[size] = '\0';
	      close(fd);
	      return s;
	    }
	}
      close(fd);
    }
  return makeint(errno);
}

UNSAFEOP(file_write, "s1 s2 -> n. Writes s2 to file s1. Creates s1 if it "
	 "doesn't exist. Returns the Unix return code (0 for success)",
	 2, (struct string *file, struct string *data),
	 OP_LEAF)
{
  int fd;
  TYPEIS(file, type_string);
  TYPEIS(data, type_string);

  fd = open(file->str, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd != -1)
    {
      int res, len;
      len = string_len(data);
      res = write(fd, data->str, string_len(data));
      close(fd);
      if (res == len)
	return makeint(0);
    }      

  return makeint(errno);
}

UNSAFEOP(file_append, "s1 s2 -> n. Appends string s2 to file s1. Creates s1 "
	 "if nonexistent. Returns the Unix error number for failure, 0 for "
	 "success.",
	 2, (struct string *file, struct string *val), 
	 OP_LEAF | OP_NOESCAPE)
{
  int fd;
  unsigned long size;

  TYPEIS(file, type_string);
  TYPEIS(val, type_string);

  fd = open(file->str, O_WRONLY | O_CREAT | O_APPEND, 0666);
  if (fd != -1)
    {
      if (fchmod(fd, 0666) == 0) /* ### What's the point of this? - finnag */
	{
	  int len;
	  size = string_len(val);
	  len = write(fd, val->str, size);
	  close(fd);
	  if (len == size)
	    return makeint(0);
	}
      else
	close(fd);
    }
  return makeint(errno);
}

#define DEF(s) system_define(#s, makeint(s))

void files_init(void)
{
  DEFINE("load", load);
  DEFINE("file_read", file_read);
  DEFINE("file_write", file_write);
  DEFINE("file_append", file_append);
  DEFINE("mkdir", mkdir);
  DEFINE("directory_files", directory_files);
  DEFINE("file_stat", file_stat);
  DEFINE("readlink", readlink);
  DEFINE("file_lstat", file_lstat);
  system_define("FS_DEV", makeint(0));
  system_define("FS_INO", makeint(1));
  system_define("FS_MODE", makeint(2));
  system_define("FS_NLINK", makeint(3));
  system_define("FS_UID", makeint(4));
  system_define("FS_GID", makeint(5));
  system_define("FS_RDEV", makeint(6));
  system_define("FS_SIZE", makeint(7));
  system_define("FS_ATIME", makeint(8));
  system_define("FS_MTIME", makeint(9));
  system_define("FS_CTIME", makeint(10));
  system_define("FS_BLKSIZE", makeint(11));
  system_define("FS_BLOCKS", makeint(12));
  DEF(S_IFMT);
  DEF(S_IFSOCK);
  DEF(S_IFLNK);
  DEF(S_IFBLK);
  DEF(S_IFREG);
  DEF(S_IFDIR);
  DEF(S_IFCHR);
  DEF(S_IFIFO);
  DEF(S_ISUID);
  DEF(S_ISGID);
  DEF(S_ISVTX);
  DEF(S_IRWXU);
  DEF(S_IRUSR);
  DEF(S_IWUSR);
  DEF(S_IXUSR);
  DEF(S_IRWXG);
  DEF(S_IRGRP);
  DEF(S_IWGRP);
  DEF(S_IXGRP);
  DEF(S_IRWXO);
  DEF(S_IROTH);
  DEF(S_IWOTH);
  DEF(S_IXOTH);

  DEFINE("file_regular?", file_regularp);
  DEFINE("remove", remove);
  DEFINE("rename", rename);

  DEFINE("glob_files", glob_files);
  DEF(GLOB_MARK);
  DEF(GLOB_NOCHECK);
  DEF(GLOB_NOESCAPE);
#ifdef _GNU_GLOB_
  DEF(GLOB_TILDE);
  DEF(GLOB_BRACE);
  DEF(GLOB_PERIOD);
  DEF(GLOB_NOMAGIC);
  DEF(GLOB_ONLYDIR);
#endif
}