/*****************************************************************************

Copyright (c) 1995, 2016, Oracle and/or its affiliates. All Rights Reserved.

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

/**************************************************//**
@file include/os0thread.h
The interface to the operating system
process and thread control primitives

Created 9/8/1995 Heikki Tuuri
*******************************************************/

#ifndef os0thread_h
#define os0thread_h

#include "univ.i"
#include "os/thread.h"

/* Maximum number of threads which can be created in the program;
this is also the size of the wait slot array for MySQL threads which
can wait inside InnoDB */

#define	OS_THREAD_MAX_N		srv_max_n_threads

/* Possible fixed priorities for threads */
#define OS_THREAD_PRIORITY_NONE		100
#define OS_THREAD_PRIORITY_BACKGROUND	1
#define OS_THREAD_PRIORITY_NORMAL	2
#define OS_THREAD_PRIORITY_ABOVE_NORMAL	3

#ifdef _WIN32
extern "C"  {
typedef LPTHREAD_START_ROUTINE	os_thread_func_t;
}

/** Macro for specifying a Windows thread start function. */
#define DECLARE_THREAD(func)	WINAPI func

/** Required to get around a build error on Windows. Even though our functions
are defined/declared as WINAPI f(LPVOID a); the compiler complains that they
are defined as: os_thread_ret_t (__cdecl*)(void*). Because our functions
don't access the arguments and don't return any value, we should be safe. */
#define os_thread_create(f,a,i)	\
	os_thread_create_func(reinterpret_cast<os_thread_func_t>(f), a, i)

#else

extern "C"  { typedef void*	(*os_thread_func_t)(void*); }

/** Macro for specifying a POSIX thread start function. */
#define DECLARE_THREAD(func)	func
#define os_thread_create(f,a,i)	os_thread_create_func(f, a, i)

#endif /* _WIN32 */

/* Define a function pointer type to use in a typecast */
typedef void* (*os_posix_f_t) (void*);

#ifdef HAVE_PSI_INTERFACE
/* Define for performance schema registration key */
typedef unsigned int    mysql_pfs_key_t;
#endif /* HAVE_PSI_INTERFACE */

/** Number of threads active. */
extern	ulint	os_thread_count;
/****************************************************************//**
Creates a new thread of execution. The execution starts from
the function given.
NOTE: We count the number of threads in os_thread_exit(). A created
thread should always use that to exit so thatthe thread count will be
decremented.
We do not return an error code because if there is one, we crash here. */
void
os_thread_create_func(
/*==================*/
	os_thread_func_t	func,		/*!< in: pointer to function
						from which to start */
	void*			arg,		/*!< in: argument to start
						function */
	os_thread_id_t*		thread_id);	/*!< out: id of the created
						thread, or NULL */

/** Exits the current thread. */
void
os_thread_exit()
	UNIV_COLD __attribute__((noreturn));

/*****************************************************************//**
Advises the os to give up remainder of the thread's time slice. */
void
os_thread_yield(void);
/*=================*/
/*****************************************************************//**
The thread sleeps at least the time given in microseconds. */
void
os_thread_sleep(
/*============*/
	ulint	tm);	/*!< in: time in microseconds */

/**
Initializes OS thread management data structures. */
void
os_thread_init();
/*============*/

/**
Frees OS thread management data structures. */
void
os_thread_free();
/*============*/

/*****************************************************************//**
Check if there are threads active.
@return true if the thread count > 0. */
bool
os_thread_active();
/*==============*/

#ifndef UNIV_NONINL
#include "os0thread.ic"
#endif

#endif
