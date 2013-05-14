/* Copyright (c) 2000-2002, 2004, 2006, 2007, 2013 MySQL AB


   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA */


#include "mysys_priv.h"
#include "mysys_err.h"
#include <errno.h>


/*
  Read a chunk of bytes from a file with retry's if needed

  The parameters are:
    File descriptor
    Buffer to hold at least Count bytes
    Bytes to read
    Flags on what to do on error

    Return:
      -1 on error
      0  if flag has bits MY_NABP or MY_FNABP set
      N  number of bytes read.
*/

size_t my_read(File Filedes, uchar *Buffer, size_t Count, myf MyFlags)
{
  size_t readbytes, save_count;
  DBUG_ENTER("my_read");
  DBUG_PRINT("my",("Fd: %d  Buffer: 0x%lx  Count: %lu  MyFlags: %d",
                   Filedes, (long) Buffer, (ulong) Count, MyFlags));
  save_count= Count;

  for (;;)
  {
    errno= 0;					/* Linux doesn't reset this */
    if (DBUG_EVALUATE_IF("simulate_file_read_error", 1, 0) ||
        (readbytes= read(Filedes, Buffer, (uint) Count)) != Count)
    {
      DBUG_EXECUTE_IF ("simulate_file_read_error",
                       {
                         errno= ENOSPC;
                         readbytes= (size_t) -1;
                         DBUG_SET("-d,simulate_file_read_error");
                         DBUG_SET("-d,simulate_my_b_fill_error");
                       });


      my_errno= errno ? errno : -1;
      DBUG_PRINT("warning",("Read only %d bytes off %lu from %d, errno: %d",
                            (int) readbytes, (ulong) Count, Filedes,
                            my_errno));
#ifdef THREAD
      if ((readbytes == 0 || (int) readbytes == -1) && errno == EINTR)
      {  
        DBUG_PRINT("debug", ("my_read() was interrupted and returned %ld",
                             (long) readbytes));
        continue;                              /* Interrupted */
      }
#endif
      if (MyFlags & (MY_WME | MY_FAE | MY_FNABP))
      {
        if (readbytes == (size_t) -1)
          my_error(EE_READ, MYF(ME_BELL+ME_WAITTANG),
                   my_filename(Filedes),my_errno);
        else if (MyFlags & (MY_NABP | MY_FNABP))
          my_error(EE_EOFERR, MYF(ME_BELL+ME_WAITTANG),
                   my_filename(Filedes),my_errno);
      }
      if (readbytes == (size_t) -1 ||
          ((MyFlags & (MY_FNABP | MY_NABP)) && !(MyFlags & MY_FULL_IO)))
        DBUG_RETURN(MY_FILE_ERROR);	/* Return with error */
      if (readbytes != (size_t) -1 && (MyFlags & MY_FULL_IO))
      {
        Buffer+= readbytes;
        Count-= readbytes;
        continue;
      }
    }

    if (MyFlags & (MY_NABP | MY_FNABP))
      readbytes= 0;			/* Ok on read */
    else if (MyFlags & MY_FULL_IO)
      readbytes= save_count;
    break;
  }
  DBUG_RETURN(readbytes);
} /* my_read */
