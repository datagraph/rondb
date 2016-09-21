/* Copyright (c) 2000, 2016, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @file mysys/my_symlink2.cc
  Advanced symlink handling.
  This is used in MyISAM to let users symlinks tables to different disk.
  The main idea with these functions is to automaticly create, delete and
  rename files and symlinks like they would be one unit.
*/

#include "my_config.h"

#include <errno.h>
#include <m_string.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "my_dbug.h"
#include "my_inttypes.h"
#include "my_io.h"
#include "my_sys.h"
#include "my_thread_local.h"
#include "mysys_err.h"

File my_create_with_symlink(const char *linkname, const char *filename,
			    int createflags, int access_flags, myf MyFlags)
{
#ifdef _WIN32
  if (linkname)
    filename= linkname;
  if (!(MyFlags & MY_DELETE_OLD))
  {
    if (!access(filename,F_OK))
    {
      char errbuf[MYSYS_STRERROR_SIZE];
      errno= EEXIST;
      set_my_errno(EEXIST);
      my_error(EE_CANTCREATEFILE, MYF(0), filename,
               EEXIST, my_strerror(errbuf, sizeof(errbuf), EEXIST));
      return -1;
    }
  }
  return my_create(filename, createflags, access_flags, MyFlags);
#else
  File file;
  int tmp_errno;
  /* Test if we should create a link */
  int create_link;
  char abs_linkname[FN_REFLEN];
  DBUG_ENTER("my_create_with_symlink");
  DBUG_PRINT("enter", ("linkname: %s  filename: %s",
                       linkname ? linkname : "(null)",
                       filename ? filename : "(null)"));

  if (!my_enable_symlinks)
  {
    DBUG_PRINT("info", ("Symlinks disabled"));
    /* Create only the file, not the link and file */
    create_link= 0;
    if (linkname)
      filename= linkname;
  }
  else
  {
    if (linkname)
      my_realpath(abs_linkname, linkname, MYF(0));
    create_link= (linkname && strcmp(abs_linkname,filename));
  }

  if (!(MyFlags & MY_DELETE_OLD))
  {
    if (!access(filename,F_OK))
    {
      char errbuf[MYSYS_STRERROR_SIZE];
      errno= EEXIST;
      set_my_errno(EEXIST);
      my_error(EE_CANTCREATEFILE, MYF(0), filename,
               EEXIST, my_strerror(errbuf, sizeof(errbuf), EEXIST));
      DBUG_RETURN(-1);
    }
    if (create_link && !access(linkname,F_OK))
    {
      char errbuf[MYSYS_STRERROR_SIZE];
      errno= EEXIST;
      set_my_errno(EEXIST);
      my_error(EE_CANTCREATEFILE, MYF(0), linkname,
               EEXIST, my_strerror(errbuf, sizeof(errbuf), EEXIST));
      DBUG_RETURN(-1);
    }
  }

  if ((file=my_create(filename, createflags, access_flags, MyFlags)) >= 0)
  {
    if (create_link)
    {
      /* Delete old link/file */
      if (MyFlags & MY_DELETE_OLD)
	my_delete(linkname, MYF(0));
      /* Create link */
      if (my_symlink(filename, linkname, MyFlags))
      {
	/* Fail, remove everything we have done */
	tmp_errno=my_errno();
	my_close(file,MYF(0));
	my_delete(filename, MYF(0));
	file= -1;
	set_my_errno(tmp_errno);
      }
    }
  }
  DBUG_RETURN(file);
#endif // !_WIN32
}

/*
  If the file was a symlink, delete both symlink and the file which the
  symlink pointed to.
*/

int my_delete_with_symlink(const char *name, myf MyFlags)
{
#ifdef _WIN32
  return my_delete(name, MyFlags);
#else
  char link_name[FN_REFLEN];
  int was_symlink= (my_enable_symlinks &&
		    !my_readlink(link_name, name, MYF(0)));
  int result;
  DBUG_ENTER("my_delete_with_symlink");

  if (!(result=my_delete(name, MyFlags)))
  {
    if (was_symlink)
      result=my_delete(link_name, MyFlags);
  }
  DBUG_RETURN(result);
#endif // _WIN32
}

/*
  If the file is a normal file, just rename it.
  If the file is a symlink:
   - Create a new file with the name 'to' that points at
     symlink_dir/basename(to)
   - Rename the symlinked file to symlink_dir/basename(to)
   - Delete 'from'
   If something goes wrong, restore everything.
*/

int my_rename_with_symlink(const char *from, const char *to, myf MyFlags)
{
#ifdef _WIN32
  return my_rename(from, to, MyFlags);
#else
  char link_name[FN_REFLEN], tmp_name[FN_REFLEN];
  int was_symlink= (my_enable_symlinks &&
		    !my_readlink(link_name, from, MYF(0)));
  int result=0;
  int name_is_different;
  DBUG_ENTER("my_rename_with_symlink");

  if (!was_symlink)
    DBUG_RETURN(my_rename(from, to, MyFlags));

  /* Change filename that symlink pointed to */
  my_stpcpy(tmp_name, to);
  fn_same(tmp_name,link_name,1);		/* Copy dir */
  name_is_different= strcmp(link_name, tmp_name);
  if (name_is_different && !access(tmp_name, F_OK))
  {
    set_my_errno(EEXIST);
    if (MyFlags & MY_WME)
    {
      char errbuf[MYSYS_STRERROR_SIZE];
      my_error(EE_CANTCREATEFILE, MYF(0), tmp_name,
               EEXIST, my_strerror(errbuf, sizeof(errbuf), EEXIST));
    }
    DBUG_RETURN(1);
  }

  /* Create new symlink */
  if (my_symlink(tmp_name, to, MyFlags))
    DBUG_RETURN(1);

  /*
    Rename symlinked file if the base name didn't change.
    This can happen if you use this function where 'from' and 'to' has
    the same basename and different directories.
   */

  if (name_is_different && my_rename(link_name, tmp_name, MyFlags))
  {
    int save_errno=my_errno();
    my_delete(to, MyFlags);			/* Remove created symlink */
    set_my_errno(save_errno);
    DBUG_RETURN(1);
  }

  /* Remove original symlink */
  if (my_delete(from, MyFlags))
  {
    int save_errno=my_errno();
    /* Remove created link */
    my_delete(to, MyFlags);
    /* Rename file back */
    if (strcmp(link_name, tmp_name))
      (void) my_rename(tmp_name, link_name, MyFlags);
    set_my_errno(save_errno);
    result= 1;
  }
  DBUG_RETURN(result);
#endif /* !_WIN32 */
}
