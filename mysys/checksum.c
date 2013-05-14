/* Copyright (c) 2000-2003, 2007 MySQL AB


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



#include <my_global.h>
#include <my_sys.h>
#include <zlib.h>

/*
  Calculate a long checksum for a memoryblock.

  SYNOPSIS
    my_checksum()
      crc       start value for crc
      pos       pointer to memory block
      length    length of the block
*/

ha_checksum my_checksum(ha_checksum crc, const uchar *pos, size_t length)
{
#ifdef NOT_USED
  const uchar *end=pos+length;
  for ( ; pos != end ; pos++)
    crc=((crc << 8) + *((uchar*) pos)) + (crc >> (8*sizeof(ha_checksum)-8));
  return crc;
#else
  return (ha_checksum)crc32((uint)crc, pos, (uint)length);
#endif
}

