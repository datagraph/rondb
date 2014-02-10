/*****************************************************************************

Copyright (c) 1995, 2014, Oracle and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************/

/******************************************************************//**
@file mach/mach0data.cc
Utilities for converting data from the database file
to the machine format.

Created 11/28/1995 Heikki Tuuri
***********************************************************************/

#include "mach0data.h"

#ifdef UNIV_NONINL
#include "mach0data.ic"
#endif

/** Read a 32-bit integer in a compressed form.
@param[in,out]	ptr	pointer to memory where to read;
advanced by the number of bytes consumed, or set NULL if out of space
@param[in]	end_ptr	end of the buffer
@return unsigned value */

ib_uint32_t
mach_parse_compressed(
	const byte**	ptr,
	const byte*	end_ptr)
{
	ib_uint32_t	val;

	if (*ptr >= end_ptr) {
		*ptr = NULL;
		return(0);
	}

	val = mach_read_from_1(*ptr);

	if (val < 0x80) {
		++*ptr;
		return(val);
	} else if (val < 0xC0) {
		if (end_ptr >= *ptr + 2) {
			val = mach_read_from_2(*ptr) & 0x7FFF;
			*ptr += 2;
			return(val);
		}
	} else if (val < 0xE0) {
		if (end_ptr >= *ptr + 3) {
			val = mach_read_from_3(*ptr) & 0x3FFFFF;
			*ptr += 3;
			return(val);
		}
	} else if (val < 0xF0) {
		if (end_ptr >= *ptr + 4) {
			val = mach_read_from_4(*ptr) & 0x1FFFFFFF;
			*ptr += 4;
			return(val);
		}
	} else {
		ut_ad(val == 0xF0);

		if (end_ptr >= *ptr + 5) {
			val = mach_read_from_4(*ptr + 1);
			ut_ad(val > 0x1FFFFFFF);
			*ptr += 5;
			return(val);
		}
	}

	*ptr = NULL;
	return(0);
}
