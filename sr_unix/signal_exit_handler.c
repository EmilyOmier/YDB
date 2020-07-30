/****************************************************************
 *								*
 * Copyright (c) 2020 YottaDB LLC and/or its subsidiaries.	*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_stdlib.h"
#include "gtm_signal.h"

#include "error.h"
#include "gtmimagename.h"
#include "signal_exit_handler.h"
#include "generic_signal_handler.h"
#include "invocation_mode.h"

GBLREF	boolean_t		created_core;
GBLREF	void			(*call_on_signal)();
GBLREF	int4			exi_condition;
GBLREF	struct sigaction	orig_sig_action[];
#ifdef DEBUG
GBLREF	volatile int4		exit_state;
GBLREF	boolean_t		exit_handler_active;
#endif

/* This is exit handler code that is invoked by "generic_signal_handler.c" and "deferred_exit_handler.c"
 * as part of handling a signal that requires the process to terminate.
 *
 * is_defered_exit = FALSE implies caller is "generic_signal_handler.c"
 * is_defered_exit = TRUE  implies caller is "deferred_exit_handler.c"
 */
void signal_exit_handler(char *exit_handler_name, int sig, siginfo_t *info, void *context, boolean_t is_deferred_exit)
{
	void			(*signal_routine)();
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	/* If any special routines are registered to be driven on a signal, drive them now */
	if (0 != exi_condition)
	{
		/* If this process has a runtime environment set up and we had created a core file, call the routine to
		 * also generate the zshow dump. Might be of help to the user to know the M-environment at core time.
		 * Bypass this code if we didn't create a core since that means it is not a YDB issue that forced the
		 * process' demise (and since this is an uncaring signal that is causing us , we could be in any number of
		 * situations that would cause a ZSHOW dump to explode). Better for user to use jobexam to cause a dump prior
		 * to terminating the process in a deferrable fashion.
		 */
		if (created_core && IS_MCODE_RUNNING && (NULL != (signal_routine = RFPTR(create_fatal_error_zshow_dmp_fptr))))
		{	/* note assignment of signal_routine above */
			SFPTR(create_fatal_error_zshow_dmp_fptr, NULL);
			(*signal_routine)(exi_condition);
		}
		/* Some mupip functions define an entry point to drive on signals. Make sure to do this AFTER we create the
		 * dump file above as it may detach things (like the recvpool) we need to create the above dump.
		 */
		if (NULL != (signal_routine = call_on_signal))	/* Note assignment */
		{
			call_on_signal = NULL;		/* So we don't recursively call ourselves */
			(*signal_routine)();
		}
	}
	/* Our last main task before we exit (which drives the exit handler) is if the main program of this process
	 * is not M (meaning simple*API or EasyAPI or various language using call-ins) and if a handler for this signal
	 * existed when YDB was intialized, we need to drive that handler now. The problem is that some languages (Golang
	 * specifically), may rethrow this signal which causes an assert failure. To mitigate this problem, we invoke the
	 * exit handler logic NOW before we give the main program's handler control and if we get the same signal again
	 * after the exit handler cleanup has run, we just pass it straight to the main's handler and do not process it
	 * again. (Also done in deferred_exit_handler()).
	 */
	DRIVE_EXIT_HANDLER_IF_EXISTS;
	if (!USING_ALTERNATE_SIGHANDLING)
	{
		DRIVE_NON_YDB_SIGNAL_HANDLER_IF_ANY("deferred_exit_handler", sig, info, context, TRUE);
	} else
	{	/* The main() entry point of this process is not YottaDB but is something else that is using alternate signal
		 * handling (currently only Go is supported). For Go, we cannot EXIT here. We need to drive a panic() to
		 * unwind everything now that YottaDB released state information has been cleaned up (by the exit handler).
		 * For Go, drive a callback routine to do the panic() for us given the signal that caused this state.
		 */
		DRIVE_ALTSIG_CALLBACK(sig);
		return;
	}
	assert(is_deferred_exit || (EXIT_IMMED <= exit_state) || !exit_handler_active);
	EXIT(-exi_condition);
	assert(FALSE);	/* previous EXIT call should terminate the process */
	return;
}
