/****************************************************************
 *								*
 * Copyright (c) 2001-2021 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2017-2023 YottaDB LLC and/or its subsidiaries. *
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include <stdarg.h>
#include "gtm_stdio.h"
#include <errno.h>
#ifdef GTM_PTHREAD
#  include "gtm_pthread.h"
#endif
#include "gtm_signal.h"
#include "gtm_stat.h"
#include "gtm_stdlib.h"
#include "gtm_string.h"
#include "gtm_limits.h"

#include <errno.h>

#include "cli.h"
#include "stringpool.h"
#include "stack_frame.h"
#include "mvalconv.h"
#include "libyottadb_int.h"
#include "lv_val.h"
#include "fgncal.h"
#include "gtmci.h"
#include "error.h"
#include "startup.h"
#include "op.h"
#include "gtm_startup.h"
#include "job_addr.h"
#include "invocation_mode.h"
#include "gtmimagename.h"
#include "gtm_exit_handler.h"
#include "code_address_type.h"
#include "push_lvval.h"
#include "gtmmsg.h"
#include "gtm_threadgbl_init.h"
#include "gtmio.h"
#include "gdsroot.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsfhead.h"
#include "compiler.h"
#include "have_crit.h"
#include "callg.h"
#include "min_max.h"
#include "alias.h"
#include "parm_pool.h"
#include "auto_zlink.h"
#include "filestruct.h"
#include "gdscc.h"
#include "gdskill.h"
#include "jnl.h"
#include "tp_frame.h"
#include "mv_stent.h"

GBLREF  stack_frame     	*frame_pointer;
GBLREF  unsigned char		*msp;
GBLREF  mv_stent         	*mv_chain;
GBLREF	int			mumps_status;
GBLREF 	void			(*restart)();
GBLREF 	boolean_t		ydb_init_complete;
GBLREF	rhdtyp			*ci_base_addr;
GBLREF  unsigned char		*fgncal_stack;
GBLREF  uint4			dollar_tlevel;
GBLREF	int			process_exiting;
#ifdef GTM_PTHREAD
GBLREF	boolean_t		gtm_jvm_process;
GBLREF	pthread_t		gtm_main_thread_id;
GBLREF	boolean_t		gtm_main_thread_id_set;
#endif
<<<<<<< HEAD
GBLREF	int			dollar_truth;
GBLREF	tp_frame		*tp_pointer;
GBLREF	boolean_t		noThreadAPI_active;
GBLREF	boolean_t		simpleThreadAPI_active;

=======
GBLREF	char			gtm_dist[GTM_PATH_MAX];
GBLREF	unsigned int		gtm_dist_len;
GBLREF boolean_t		gtm_dist_ok_to_use;
GTMTRIG_DBG_ONLY(GBLREF ch_ret_type (*ch_at_trigger_init)();)
>>>>>>> 451ab477 (GT.M V7.0-000)
LITREF  gtmImageName            gtmImageNames[];

void gtm_levl_ret_code(void);

error_def(ERR_ACTLSTTOOLONG);
error_def(ERR_CALLINAFTERXIT);
error_def(ERR_CIMAXLEVELS);
error_def(ERR_CINOENTRY);
error_def(ERR_CIRCALLNAME);
error_def(ERR_DISTPATHMAX);
error_def(ERR_FMLLSTMISSING);
error_def(ERR_YDBDISTUNDEF);
error_def(ERR_GTMSECSHRPERM);
error_def(ERR_INVYDBEXIT);
error_def(ERR_JOBLABOFF);
error_def(ERR_MAXACTARG);
error_def(ERR_MAXSTRLEN);
error_def(ERR_SYSCALL);

#define REVERT_AND_RETURN						\
{									\
	REVERT; /* gtmci_ch */						\
	return 0;							\
}

/* Unwind the M stack back to where the stack pointer (msp) was last saved */
#define FGNCAL_UNWIND							\
{									\
	if (msp < fgncal_stack)						\
		fgncal_unwind();					\
}

/* When passing arguments from Java, ensure that the expected types match the actual ones. If not,
 * use the arg_types array to pass back the information needed for a detailed error message.
 */
#define CHECK_FOR_TYPE_MISMATCH(INDEX, EXP_TYPE, ACT_TYPE)		\
{									\
	if (EXP_TYPE != ACT_TYPE)					\
	{								\
		arg_types[3] = ACT_TYPE;				\
		arg_types[2] = EXP_TYPE;				\
		arg_types[1] = INDEX;					\
		arg_types[0] = -1;					\
		REVERT_AND_RETURN;					\
	}								\
}

/* When passing arguments from Java, ensure that the either of the expected types matches the actual one.
 * If not, use the arg_types array to pass back the information needed for a detailed error message.
 */
#define CHECK_FOR_TYPES_MISMATCH(INDEX, EXP_TYPE1, EXP_TYPE2, ACT_TYPE)	\
{									\
	if ((EXP_TYPE1 != ACT_TYPE) && (EXP_TYPE2 != ACT_TYPE))		\
	{								\
		arg_types[4] = ACT_TYPE;				\
		arg_types[3] = EXP_TYPE1;				\
		arg_types[2] = EXP_TYPE2;				\
		arg_types[1] = INDEX;					\
		arg_types[0] = -1;					\
		REVERT_AND_RETURN;					\
	}								\
}

/* When returning a typed value, ensure that the declared type matches the expected one. If not,
 * use the arg_types array to pass back the information needed for a detailed error message.
 */
#define CHECK_FOR_RET_TYPE_MISMATCH(INDEX, EXP_TYPE, ACT_TYPE)	\
{								\
	if ((0 == INDEX) && (EXP_TYPE != ACT_TYPE))		\
	{							\
		arg_types[3] = ACT_TYPE;			\
		arg_types[2] = EXP_TYPE;			\
		arg_types[1] = 0;				\
		arg_types[0] = -1;				\
		REVERT_AND_RETURN;				\
	}							\
}

#define	INVOKE_YDB_NESTED_CALLIN_AND_RETURN_ON_ERROR							\
MBSTART {												\
	boolean_t		error_encountered;							\
													\
	ESTABLISH_NORET(gtmci_ch, error_encountered);							\
	if (error_encountered)										\
	{	/* "gtmci_ch" encountered an error and transferred control back here. Return. */	\
		REVERT;											\
		return mumps_status;									\
	}												\
	ydb_nested_callin();            /* Note - sets fgncal_stack */					\
	REVERT;												\
} MBEND

callin_entry_list *ci_find_rtn_entry(ci_tab_entry_t *ci_tab, const char *call_name)
{	/* Lookup in a hashtable for entry corresponding to routine name */
	hash_table_str	*ci_hashtab;
	ht_ent_str      *callin_entry;
	stringkey       symkey;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	symkey.str.addr = (char *)call_name;
	symkey.str.len = STRLEN(call_name);
	COMPUTE_HASH_STR(&symkey);
	ci_hashtab = ci_tab->hashtab;
	callin_entry = lookup_hashtab_str(ci_hashtab, &symkey);
	return (callin_entry ? callin_entry->value : NULL);
}

/* Java-specific version of call-in handler. */
int ydb_cij(const char *c_rtn_name, char **arg_blob, int count, int *arg_types, unsigned int *io_vars_mask,
	    unsigned int *has_ret_value)
{
	callin_entry_list	*entry;
	ci_tab_entry_t		*ci_tab;
	int4			*lnr_tab_ent;
	mstr			label, routine;
	int			has_return, i, len;
	rhdtyp          	*base_addr;
	char			*xfer_addr;
	uint4			inp_mask, out_mask, mask;
	mval			arg_mval, *arg_ptr;
	enum ydb_types		arg_type;
	ydb_string_t		*mstr_parm;
	parmblk_struct 		param_blk;
	int			status;
	intrpt_state_t		old_intrpt_state;
	char			**arg_blob_ptr;
	int			*java_arg_type;
	boolean_t		ci_ret_code_quit_needed = FALSE;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	if (!ydb_init_complete)
	{
		if ((status = ydb_init()) != 0)		/* Note - sets fgncal_stack */
			return status;
	} else if (!(frame_pointer->type & SFT_CI))
	{	/* "ydb_init" has already set up a call-in base frame (either for simpleAPI or for a call-in).
		 * But the current frame is not a call-in base frame. So set a new/nested call-in base frame up.
		 */
		INVOKE_YDB_NESTED_CALLIN_AND_RETURN_ON_ERROR;	/* invokes "ydb_nested_callin" */
	} else if (dollar_tlevel && (tp_pointer->fp == frame_pointer) && tp_pointer->ydb_tp_s_tstart)
	{	/* Current frame is already a call-in frame. If we are already in a TP transaction and simpleAPI/"ydb_tp_s"
		 * had started this TP and the current "frame_pointer" was the current even at the time of "ydb_tp_s"
		 * we need to create a nested call-in frame as part of this "ydb_ci" invocation (in order to detect a
		 * CALLINTCOMMIT or CALLINTROLLBACK situation since simpleAPI and call-ins both create the same SFT_CI
		 * type of call-in base frame).
		 */
		INVOKE_YDB_NESTED_CALLIN_AND_RETURN_ON_ERROR;	/* invokes "ydb_nested_callin" */
		/* Since this is a nested call-in created just for this "ydb_ci" invocation, we need to unwind this call-in
		 * frame/stack when returning (unlike a regular "ydb_ci" invocation where we keep the environment as is
		 * in the hope of future "ydb_ci" calls).
		 */
		ci_ret_code_quit_needed = TRUE;
	}
	GTM_PTHREAD_ONLY(assert(gtm_main_thread_id_set && pthread_equal(gtm_main_thread_id, pthread_self())));
	assert(NULL == TREF(temp_fgncal_stack));
	FGNCAL_UNWIND;		/* note - this is outside the establish since gtmci_ch calso calls fgncal_unwind() which,
				 * if this failed, would lead to a nested error which we'd like to avoid */
	ESTABLISH_RET(gtmci_ch, mumps_status);
<<<<<<< HEAD
	entry = ci_load_table_rtn_entry(c_rtn_name, &ci_tab);   /* load ci table, locate entry for return name */
	lref_parse((unsigned char*)entry->label_ref.addr, &routine, &label, &i);
	/* The 3rd argument is NULL because we will get lnr_adr via TABENT_PROXY. */
	if (!job_addr(&routine, &label, 0, (char **)&base_addr, &xfer_addr))
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_JOBLABOFF);
	/* Thejob_addr() call above has done a zlink of the routine we want to drive if needed and has give us
	 * the routine header and execution address we need. But it did not do an autorelink check so do that now
	 * before we put these values into a stackframe as this call may change what is needing to go there.
=======
	if (!c_rtn_name)
		RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_CIRCALLNAME);
	if (!TREF(ci_table))	/* Load the call-in table only once from env variable GTMCI. */
	{
		TREF(ci_table) = citab_parse(FALSE);
		if (!TREF(callin_hashtab))
		{
			TREF(callin_hashtab) = (hash_table_str *)malloc(SIZEOF(hash_table_str));
			(TREF(callin_hashtab))->base = NULL;
			/* Need to initialize hash table. */
			init_hashtab_str(TREF(callin_hashtab), CALLIN_HASHTAB_SIZE,
				HASHTAB_NO_COMPACT, HASHTAB_NO_SPARE_TABLE);
			assert((TREF(callin_hashtab))->base);
		}
		for (entry = TREF(ci_table); NULL != entry; entry = entry->next_entry)
		{	/* Loop over the list and populate the hash table. */
			symkey.str.addr = entry->call_name.addr;
			symkey.str.len = entry->call_name.len;
			COMPUTE_HASH_STR(&symkey);
			added = add_hashtab_str(TREF(callin_hashtab), &symkey, entry, &syment);
			assert(added);
			assert(syment->value == entry);
		}
	}
	if (!(entry = get_entry(c_rtn_name, FALSE)))	/* c_rtn_name not found in the table. */
		RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(4) ERR_CINOENTRY, 2, LEN_AND_STR(c_rtn_name));
	lref_parse((unsigned char*)entry->label_ref.addr, &routine, &label, &i);
	/* The 3rd argument is NULL because we will get lnr_adr via TABENT_PROXY. */
	if (!job_addr(&routine, &label, 0, (char **)&base_addr, NULL))
		RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_JOBLABOFF);
	memset(&param_blk, 0, SIZEOF(param_blk));
	param_blk.rtnaddr = (void *)(ARLINK_ONLY(0) NON_ARLINK_ONLY(base_addr));
	/* lnr_entry below is a pointer to the code offset for this label from the
	 * beginning of text base(on USHBIN platforms) or from the beginning of routine
	 * header (on NON_USHBIN platforms).
	 * On NON_USHBIN platforms -- 2nd argument to EXTCALL is this pointer
	 * On USHBIN -- 2nd argument to EXTCALL is the pointer to this pointer (&lnr_entry)
>>>>>>> 451ab477 (GT.M V7.0-000)
	 */
	explicit_relink_check(base_addr, TRUE);
	if (base_addr != (TABENT_PROXY).rtnhdr_adr)
	{	/* Routine was re-loaded - recompute execution address as well */
		base_addr = (TABENT_PROXY).rtnhdr_adr;
		lnr_tab_ent = find_line_addr(base_addr, &label, 0, NULL);
		xfer_addr = (char *)LINE_NUMBER_ADDR(base_addr, lnr_tab_ent);
	}
	/* Verify that if we are calling a routine we believe to have parms, that it actually does expect a parameter
	 * list. If not we need to raise an error.
	 */
	if ((0 < entry->argcnt) && !(TABENT_PROXY).has_parms)
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_FMLLSTMISSING, 2, (int)label.len, label.addr);
	/* Fill in the param_blk to be passed to push_parm_ci() to set up routine arguments (if any) */
	if (MAX_ACTUALS < entry->argcnt)
		RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_MAXACTARG);
	if (entry->argcnt < count)
		RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(4) ERR_ACTLSTTOOLONG, 2, (int)label.len, label.addr);
	param_blk.argcnt = count;
	has_return = (ydb_void != entry->return_type);
	MV_INIT(&arg_mval);
	if (has_return)
	{	/* Create mval slot for return value */
		param_blk.retaddr = (void *)push_lvval(&arg_mval);
		arg_blob_ptr = &arg_blob[0] + GTM64_ONLY(1) NON_GTM64_ONLY(2);
		java_arg_type = arg_types + 1;
	} else
	{
		param_blk.retaddr = 0;
		arg_blob_ptr = &arg_blob[0];
		java_arg_type = arg_types;
	}
	inp_mask = entry->input_mask;
	out_mask = entry->output_mask;
	*io_vars_mask = out_mask;
	if (*has_ret_value != has_return)
	{
		*has_ret_value = has_return;
		REVERT_AND_RETURN;
	}
	*has_ret_value = has_return;
	for (i = 0, mask = inp_mask; i < count; ++i, mask >>= 1, java_arg_type++, arg_blob_ptr += GTM64_ONLY(1) NON_GTM64_ONLY(2))
	{	/* Copy the arguments' values into mval containers. Since some arguments might be declared as output-only,
		 * we need to go over all of them unconditionally, but only do the copying for the ones that are used for
		 * the input direction (I or IO). The integer values passed to CHECK_FOR_TYPE_MISMATCH as a second argument
		 * indicate the types to expect according to the call-in table definition, and are in correspondence with the
		 * constants declared in GTMContainerType class in gtmji.jar: 0 for GTMBoolean, 1 for GTMInteger, and so on.
		 */
		arg_mval.mvtype = MV_XZERO;
		switch(entry->parms[i])
		{
			case ydb_jboolean:
				CHECK_FOR_TYPE_MISMATCH(i + 1, 0, *java_arg_type);
				if (MASK_BIT_ON(mask))
					i2mval(&arg_mval, *(int *)arg_blob_ptr);
				break;
			case ydb_jint:
				CHECK_FOR_TYPE_MISMATCH(i + 1, 1, *java_arg_type);
				if (MASK_BIT_ON(mask))
					i2mval(&arg_mval, *(int *)arg_blob_ptr);
				break;
			case ydb_jlong:
				CHECK_FOR_TYPE_MISMATCH(i + 1, 2, *java_arg_type);
				if (MASK_BIT_ON(mask))
				i82mval(&arg_mval, *(gtm_int64_t *)arg_blob_ptr);
				break;
			case ydb_jfloat:
				CHECK_FOR_TYPE_MISMATCH(i + 1, 3, *java_arg_type);
				if (MASK_BIT_ON(mask))
					float2mval(&arg_mval, *(float *)arg_blob_ptr);
				break;
			case ydb_jdouble:
				CHECK_FOR_TYPE_MISMATCH(i + 1, 4, *java_arg_type);
				if (MASK_BIT_ON(mask))
					double2mval(&arg_mval, *(double *)arg_blob_ptr);
				break;
			case ydb_jstring:
				CHECK_FOR_TYPES_MISMATCH(i + 1, 7, 5, *java_arg_type);
				if (MASK_BIT_ON(mask))
				{
					mstr_parm = *(ydb_string_t **)arg_blob_ptr;
					arg_mval.mvtype = MV_STR;
					if (MAX_STRLEN < (uint4)mstr_parm->length)
						RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_MAXSTRLEN);
					arg_mval.str.len = (mstr_len_t)mstr_parm->length;
					arg_mval.str.addr = mstr_parm->address;
					s2pool(&arg_mval.str);
				}
				break;
			case ydb_jbyte_array:
				CHECK_FOR_TYPES_MISMATCH(i + 1, 8, 6, *java_arg_type);
				if (MASK_BIT_ON(mask))
				{
					mstr_parm = *(ydb_string_t **)arg_blob_ptr;
					arg_mval.mvtype = MV_STR;
					if (MAX_STRLEN < (uint4)mstr_parm->length)
						RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_MAXSTRLEN);
					arg_mval.str.len = (mstr_len_t)mstr_parm->length;
					arg_mval.str.addr = mstr_parm->address;
					s2pool(&arg_mval.str);
				}
				break;
			case ydb_jbig_decimal:
				CHECK_FOR_TYPE_MISMATCH(i + 1, 9, *java_arg_type);
				if (MASK_BIT_ON(mask))
				{
					mstr_parm = *(ydb_string_t **)arg_blob_ptr;
					arg_mval.mvtype = MV_STR;
					if (MAX_STRLEN < (uint4)mstr_parm->length)
						RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_MAXSTRLEN);
					arg_mval.str.len = (mstr_len_t)mstr_parm->length;
					arg_mval.str.addr = mstr_parm->address;
					s2pool(&arg_mval.str);
				}
				break;
			default:			/* Indicate an invalid type. */
				arg_types[1] = i + 1;
				arg_types[0] = -2;
				REVERT_AND_RETURN;
		}
		param_blk.args[i] = push_lvval(&arg_mval);
	}
	/* Need to create the new stackframe this call will run in. Any mv_stents created on this frame's behalf by
	 * push_parm_ci() or op_bindparm() will pop when this frame pops. All those mv_stents created above as part
	 * of argument processing will stick around to be used in argument output processing once the call returns.
	 */
#	ifdef HAS_LITERAL_SECT
	new_stack_frame(base_addr, (unsigned char *)LINKAGE_ADR(base_addr), (unsigned char *)xfer_addr);
#	else
	/* Any platform that does not follow pv-based linkage model either
	 *	(1) uses the following calculation to determine the context pointer value, or
	 *	(2) doesn't need a context pointer
	 * Such platforms are not currently supported by YottaDB (Linux32 and Cygwin32 fall in this category)
	 */
	assertpro(FALSE);
#	endif
	param_blk.mask = out_mask;
	push_parm_ci(dollar_truth, &param_blk);		/* Set up the parameter block for op_bindparm() in callee */
	old_intrpt_state = intrpt_ok_state;
	intrpt_ok_state = INTRPT_OK_TO_INTERRUPT;	/* Reset interrupt state for the new M session. */
	assert(frame_pointer->old_frame_pointer->type & SFT_CI);
	REVERT;						/* Revert gtmci_ch. */
	/*				*/
	/* Drive the call_in routine	*/
	/*				*/
	TREF(zhalt_retval) = 0;		/* Reset global "TREF(zhalt_retval)" before dm_start() call.
					 * If a ZHALT is done inside that invocation, it would update this global.
					 */
	ESTABLISH_RET(stop_image_conditional_core, mumps_status);
	dm_start(); 	/* Kick off execution */
	REVERT;
	/*				*/
	/* Return value processing	*/
	/*				*/
	assert(!stringpool_unusable);
	intrpt_ok_state = old_intrpt_state;		/* Restore the old interrupt state. */
	/* Check if ZHALT with non-zero argument happened inside the "dm_start()" invocation.
	 * If so, forward that error to caller of "ydb_cij()".
	 */
	if (TREF(zhalt_retval))
	{
		assert(1 == mumps_status);	/* SUCCESS */
		if (ci_ret_code_quit_needed)
			ci_ret_code_quit();	/* Unwind the current invocation of call-in environment */
		return TREF(zhalt_retval);
	}
	if (1 != mumps_status)
	{	/* dm_start() initializes mumps_status to 1 before execution. If mumps_status is not 1,
		 * it is either the unhandled error code propaged by $ZT/$ET (from mdb_condition_handler)
		 * or zero on returning from ZGOTO 0 (ci_ret_code_quit).
		 */
		if (mumps_status)
		{	/* This is an error codepath. Do cleanup of frames (including the call-in base frame for some errors)
			 * and rethrow error if needed. Currently only the following error(s) need rethrow.
			 *	ERR_TPRETRY
			 * The above 3 errors indicate there is a parent M frame or ydb_tp_s frame at a lower
			 * callin depth that needs to see this error.
			 */
			FGNCAL_UNWIND_CLEANUP;	/* Unwind all frames up to the call-in base frame */
			assert(SFT_CI & frame_pointer->type);
			if (ERR_TPRETRY == mumps_status)
			{	/* These error(s) need to be rethrown. But before that unwind the call-in base frame */
				if (NULL != frame_pointer)
				{
					ci_ret_code_quit();	/* Unwind the current invocation of call-in environment */
					ci_ret_code_quit_needed = FALSE;
				}
				DRIVECH(mumps_status);
				assert(FALSE);
			}
		}
		if (ci_ret_code_quit_needed)
			ci_ret_code_quit();	/* Unwind the current invocation of call-in environment */
		return mumps_status;
	}
	ESTABLISH_RET(gtmci_ch, mumps_status);
	/* Convert mval args designated for output-only or input-output use to C types. */
	arg_blob_ptr = &arg_blob[0];
	for (i = 0; i <= count; ++i, arg_blob_ptr += GTM64_ONLY(1) NON_GTM64_ONLY(2))
	{
		if (0 == i)				/* Special case for return value. */
		{
			if (!has_return)
			{
				arg_blob_ptr -= GTM64_ONLY(1) NON_GTM64_ONLY(2);
				continue;
			}
			arg_ptr = &((lv_val *)(param_blk.retaddr))->v;
			op_exfunret(arg_ptr);		/* Validate return value specified and type */
			mask = 1;
			arg_type = entry->return_type;
		} else
		{
			arg_ptr = &param_blk.args[i - 1]->v;
			mask = out_mask;
			arg_type = entry->parms[i - 1];
			out_mask >>= 1;
		}
		/* Do not process parameters that are either input-only(I) or output(O/IO)
		 * parameters that are not modified by the M routine.
		 */
		if (MV_ON(mask, arg_ptr))
		{	/* Process all output (O/IO) and return parameters modified by the M routine */
			switch(arg_type)
			{
				case ydb_jboolean:
					CHECK_FOR_RET_TYPE_MISMATCH(i, 0, *arg_types);
					*(ydb_int_t *)arg_blob_ptr = mval2double(arg_ptr) ? 1 : 0;
					break;
                                case ydb_jint:
					CHECK_FOR_RET_TYPE_MISMATCH(i, 1, *arg_types);
					*(ydb_int_t *)arg_blob_ptr = mval2i(arg_ptr);
					break;
				case ydb_jlong:
					CHECK_FOR_RET_TYPE_MISMATCH(i, 2, *arg_types);
					*(gtm_int64_t *)arg_blob_ptr = mval2i8(arg_ptr);
					break;
				case ydb_jfloat:
					CHECK_FOR_RET_TYPE_MISMATCH(i, 3, *arg_types);
					*(ydb_float_t *)arg_blob_ptr = mval2double(arg_ptr);
					break;
				case ydb_jdouble:
					CHECK_FOR_RET_TYPE_MISMATCH(i, 4, *arg_types);
					*(ydb_double_t *)arg_blob_ptr = mval2double(arg_ptr);
					break;
				case ydb_jstring:
					CHECK_FOR_RET_TYPE_MISMATCH(i, 7, *arg_types);
					MV_FORCE_STR(arg_ptr);
					/* Since the ci_gateway.c code temporarily switches the character following the string's
					 * content in memory to '\0' (for generation of a proper UTF string), ensure that the
					 * whole string resides in the stringpool, and that we do have that one byte to play with.
					 */
					if (!IS_IN_STRINGPOOL(arg_ptr->str.addr, arg_ptr->str.len))
						s2pool(&arg_ptr->str);
					ENSURE_STP_FREE_SPACE(1);
					(*(ydb_string_t **)arg_blob_ptr)->address = arg_ptr->str.addr;
					(*(ydb_string_t **)arg_blob_ptr)->length = arg_ptr->str.len;
					break;
				case ydb_jbyte_array:
					CHECK_FOR_RET_TYPE_MISMATCH(i, 8, *arg_types);
					MV_FORCE_STR(arg_ptr);
					(*(ydb_string_t **)arg_blob_ptr)->address = arg_ptr->str.addr;
					(*(ydb_string_t **)arg_blob_ptr)->length = arg_ptr->str.len;
					break;
				case ydb_jbig_decimal:	/* We currently do not support output for big decimal. */
					break;
				default:
					assertpro((arg_type >= ydb_jboolean) && (arg_type <= ydb_jbig_decimal));
			}
		}
	}
	REVERT;
	assert(NULL == TREF(temp_fgncal_stack));
	FGNCAL_UNWIND;
	if (ci_ret_code_quit_needed)
		ci_ret_code_quit();	/* Unwind the current invocation of call-in environment */
	return 0;
}

/* Common work-routine for ydb_ci() and ydb_cip() to drive callin.
 * Returns
 *	= 0 on success (i.e. YDB_OK)
 *	> 0 on error   (i.e. positive error code)
 */
int ydb_ci_exec(const char *c_rtn_name, ci_name_descriptor *ci_info, va_list temp_var, boolean_t internal_use)
{
	ci_tab_entry_t		*ci_tab;
	callin_entry_list	*entry;
	va_list			var;
	int4			*lnr_tab_ent;
	mstr			label, routine;
	int			has_return, i;
	rhdtyp          	*base_addr;
	char			*xfer_addr;
	uint4			inp_mask, out_mask, mask;
	mval			arg_mval, *arg_ptr;
	enum ydb_types		arg_type;
	ydb_string_t		*mstr_parm;
	char			*ydb_char_ptr;
	parmblk_struct 		param_blk;
	int			status;
	intrpt_state_t		old_intrpt_state;
	boolean_t		ci_ret_code_quit_needed = FALSE;
	boolean_t		unwind_here = FALSE;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	VAR_COPY(var, temp_var);
	if (internal_use)
	{
		TREF(comm_filter_init) = TRUE;
		if (!(frame_pointer->type & SFT_CI))
			unwind_here = TRUE;
	}
	/* Do the "ydb_init" (if needed) first as it would set gtm_threadgbl etc. which is needed to use TREF later */
	if (!ydb_init_complete)
	{
		if ((status = ydb_init()) != 0)		/* Note - sets fgncal_stack */
		{
			va_end(var);
			return status;
		}
		/* Since we called "ydb_init" above, "gtm_threadgbl" would have been set to a non-null VALUE
		 * and so any call to SETUP_THREADGBL_ACCESS done by the function that called this macro
		 * needs to be redone to set "lcl_gtm_threadgbl" to point to this new "gtm_threadgbl".
		 */
		SETUP_THREADGBL_ACCESS;
	} else if (!(frame_pointer->type & SFT_CI))
	{	/* "ydb_init" has already set up a call-in base frame (either for simpleAPI or for a call-in).
		 * But the current frame is not a call-in base frame. So set a new/nested call-in base frame up.
		 */
		INVOKE_YDB_NESTED_CALLIN_AND_RETURN_ON_ERROR;	/* invokes "ydb_nested_callin" */
	} else if (dollar_tlevel && (tp_pointer->fp == frame_pointer) && tp_pointer->ydb_tp_s_tstart)
	{	/* Current frame is already a call-in frame. If we are already in a TP transaction and simpleAPI/"ydb_tp_s"
		 * had started this TP and the current "frame_pointer" was the current even at the time of "ydb_tp_s"
		 * we need to create a nested call-in frame as part of this "ydb_ci" invocation (in order to detect a
		 * CALLINTCOMMIT or CALLINTROLLBACK situation since simpleAPI and call-ins both create the same SFT_CI
		 * type of call-in base frame).
		 */
		INVOKE_YDB_NESTED_CALLIN_AND_RETURN_ON_ERROR;	/* invokes "ydb_nested_callin" */
		/* Since this is a nested call-in created just for this "ydb_ci" invocation, we need to unwind this call-in
		 * frame/stack when returning (unlike a regular "ydb_ci" invocation where we keep the environment as is
		 * in the hope of future "ydb_ci" calls).
		 */
		ci_ret_code_quit_needed = TRUE;
	}
	assert(NULL == TREF(temp_fgncal_stack));
	FGNCAL_UNWIND;		/* note - this is outside the establish since gtmci_ch calso calls fgncal_unwind() which,
				 * if this failed, would lead to a nested error which we'd like to avoid */
	ESTABLISH_RET(gtmci_ch, mumps_status);
	if (!c_rtn_name)
		RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_CIRCALLNAME);
	if (!internal_use)
	{	/* Check if the currently active call-in table is already loaded. If so, use it. */
		ci_tab = TREF(ci_table_curr);
		if (NULL == ci_tab)
		{	/* There is no currently active call-in table. Use the default call-in table if it has been loaded. */
			ci_tab = TREF(ci_table_default);
		}
		if (NULL == ci_tab)
		{	/* Neither the active nor the default call-in table is available. Load the default call-in table. */
			ci_tab = ci_tab_entry_open(INTERNAL_USE_FALSE, NULL);
			TREF(ci_table_curr) = ci_tab;
			TREF(ci_table_default) = ci_tab;
		}
<<<<<<< HEAD
=======
	}
	if (NULL == callin_handle)
	{
		if (!(entry = get_entry(c_rtn_name, internal_use)))	/* c_rtn_name not found in the table */
			RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(4) ERR_CINOENTRY, 2, LEN_AND_STR(c_rtn_name));
		if (populate_handle)
			callin_handle = entry;
>>>>>>> 451ab477 (GT.M V7.0-000)
	} else
	{	/* Call from the filter command*/
		ci_tab = TREF(ci_table_internal_filter);
		if (NULL == ci_tab)
		{
			ci_tab = ci_tab_entry_open(INTERNAL_USE_TRUE, NULL);
			TREF(ci_table_internal_filter) = ci_tab;
		}
	}
	entry = (NULL != ci_info) ? ci_info->handle : NULL;
	if (NULL == entry)
	{
		if (!(entry = ci_find_rtn_entry(ci_tab, c_rtn_name)))	/* c_rtn_name not found in the table */
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_CINOENTRY, 3, LEN_AND_STR(c_rtn_name), ci_tab->fname);
		if (NULL != ci_info)
			ci_info->handle = entry;
	}
	lref_parse((unsigned char*)entry->label_ref.addr, &routine, &label, &i);
<<<<<<< HEAD
	/* 3rd argument is 0 because we don't support a line offset from a label in call-ins */
	if (!job_addr(&routine, &label, 0, (char **)&base_addr, &xfer_addr))
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_JOBLABOFF);
	/* Thejob_addr() call above has done a zlink of the routine we want to drive if needed and has give us
	 * the routine header and execution address we need. But it did not do an autorelink check so do that now
	 * before we put these values into a stackframe as this call may change what is needing to go there.
=======
	/* 3rd argument is NULL because we will get lnr_adr via TABENT_PROXY */
	if (!job_addr(&routine, &label, 0, (char **)&base_addr, NULL))
		RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_JOBLABOFF);
	memset(&param_blk, 0, SIZEOF(param_blk));
	param_blk.rtnaddr = (void *)(ARLINK_ONLY(0) NON_ARLINK_ONLY(base_addr));
	/* lnr_entry below is a pointer to the code offset for this label from the
	 * beginning of text base(on USHBIN platforms) or from the beginning of routine
	 * header (on NON_USHBIN platforms).
	 * On NON_USHBIN platforms -- 2nd argument to EXTCALL is this pointer
	 * On USHBIN -- 2nd argument to EXTCALL is the pointer to this pointer (&lnr_entry)
	 *
	 * Assign the address for line number entry storage, so that the adjacent address holds has_parms value.
>>>>>>> 451ab477 (GT.M V7.0-000)
	 */
	explicit_relink_check(base_addr, TRUE);
	if (base_addr != (TABENT_PROXY).rtnhdr_adr)
	{	/* Routine was re-loaded - recompute execution address as well */
		base_addr = (TABENT_PROXY).rtnhdr_adr;
		lnr_tab_ent = find_line_addr(base_addr, &label, 0, NULL);
		xfer_addr = (char *)LINE_NUMBER_ADDR(base_addr, lnr_tab_ent);
	}
	/* Verify that if we are calling a routine we believe to have parms, that it actually does expect a parameter
	 * list. If not we need to raise an error.
	 */
	if ((0 < entry->argcnt) && !(TABENT_PROXY).has_parms)
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_FMLLSTMISSING, 2, (int)label.len, label.addr);
	/* Fill in the param_blk to be passed to push_parm_ci() to set up routine arguments (if any) */
	param_blk.argcnt = entry->argcnt;
	if (MAX_ACTUALS < param_blk.argcnt)
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_MAXACTARG);
	has_return = (ydb_void == entry->return_type) ? 0 : 1;
	MV_INIT(&arg_mval);
	if (has_return)
	{	/* Create mval slot for return value */
		param_blk.retaddr = (void *)push_lvval(&arg_mval);
		va_arg(var, void *);	/* advance va_arg */
	} else
		param_blk.retaddr = NULL;
	inp_mask = entry->input_mask;
	out_mask = entry->output_mask;
	for (i = 0, mask = ~inp_mask; i < entry->argcnt; ++i, mask >>= 1)
	{	/* Copy pass-by-value arguments - since only first MAX_ACTUALS could be O/IO,
		 * any additional params will be treated as Input-only (I).
		 * inp_mask is inversed to achieve this.
		 */
		arg_mval.mvtype = MV_XZERO;
		if (MASK_BIT_ON(mask))
		{ 	/* Output-only(O) params : advance va_arg pointer */
			switch(entry->parms[i])
			{
				case ydb_int:				/* Int sizes are the same so group them */
				case ydb_uint:
					va_arg(var, ydb_int_t);
					break;
				case ydb_long:				/* Long sizes are the same so group them */
				case ydb_ulong:
					va_arg(var, ydb_long_t);
					break;
#				ifdef GTM64
				case ydb_int64:				/* 64 bit int sizes are the same so group them */
				case ydb_uint64:
					va_arg(var, ydb_int64_t);
					break;
#				endif
				case ydb_int_star:			/* Address-of sizes are the same so group them */
				case ydb_uint_star:
				case ydb_long_star:
				case ydb_ulong_star:
#				ifdef GTM64
				case ydb_int64_star:
				case ydb_uint64_star:
#				endif
				case ydb_float_star:
				case ydb_double_star:
				case ydb_char_star:
				case ydb_string_star:
				case ydb_buffer_star:;
					/* Note: For Output-only (O) or RETURN parameters in "ydb_buffer_star" case,
					 * we do not issue ERR_PARAMINVALID error if "len_used" is greater than "len_alloc"
					 * like we do for I or IO parameters. This is because it is more user-friendly to
					 * ignore "len_used" and instead set it later based on the output/return value.
					 * At that time, we will check if "buf_addr" is NULL and the output/return "len_used"
					 * is non-zero. If so we will issue an ERR_PARAMINVALID error then.
					 */
					va_arg(var, void *);
					break;
				case ydb_float:
				case ydb_double:
					va_arg(var, ydb_double_t);
					break;
				default:
					va_end(var);
					assertpro(FALSE);
			}
		} else
		{ 	/* I/IO params: create mval for each native type param */
			switch(entry->parms[i])
			{
                                case ydb_int:
                                        i2mval(&arg_mval, va_arg(var, ydb_int_t));
                                        break;
                                case ydb_uint:
                                        i2usmval(&arg_mval, va_arg(var, ydb_uint_t));
                                        break;
				case ydb_long:
#					ifdef GTM64
					i82mval(&arg_mval, (gtm_int64_t)va_arg(var, ydb_long_t));
#					else
					i2mval(&arg_mval, (int)va_arg(var, ydb_long_t));
#					endif
					break;
				case ydb_ulong:
#					ifdef GTM64
					ui82mval(&arg_mval, (gtm_uint64_t)va_arg(var, ydb_ulong_t));
#					else
					i2usmval(&arg_mval, (int)va_arg(var, ydb_ulong_t));
#					endif
					break;
#			        ifdef GTM64
				case ydb_int64:
					i82mval(&arg_mval, (ydb_int64_t)va_arg(var, ydb_int64_t));
					break;
				case ydb_uint64:
					ui82mval(&arg_mval, (ydb_uint64_t)va_arg(var, ydb_uint64_t));
					break;
#				endif
                                case ydb_int_star:
                                        i2mval(&arg_mval, *va_arg(var, ydb_int_t *));
                                        break;
                                case ydb_uint_star:
                                        i2usmval(&arg_mval, *va_arg(var, ydb_uint_t *));
                                        break;
				case ydb_long_star:
#					ifdef GTM64
					i82mval(&arg_mval, (gtm_int64_t)*va_arg(var, ydb_long_t *));
#					else
					i2mval(&arg_mval, (int)*va_arg(var, ydb_long_t *));
#					endif
					break;
				case ydb_ulong_star:
#					ifdef GTM64
					ui82mval(&arg_mval, (gtm_uint64_t)*va_arg(var, ydb_ulong_t *));
#					else
					i2usmval(&arg_mval, (int)*va_arg(var, ydb_ulong_t *));
#					endif
					break;
#				ifdef GTM64
				case ydb_int64_star:
					i82mval(&arg_mval, (ydb_int64_t)*va_arg(var, ydb_int64_t *));
					break;
				case ydb_uint64_star:
					ui82mval(&arg_mval, (ydb_uint64_t)*va_arg(var, ydb_uint64_t *));
					break;
#				endif
				case ydb_float:
					float2mval(&arg_mval, (ydb_float_t)va_arg(var, ydb_double_t));
					break;
				case ydb_double:
					double2mval(&arg_mval, va_arg(var, ydb_double_t));
					break;
				case ydb_float_star:
					float2mval(&arg_mval, *va_arg(var, ydb_float_t *));
					break;
				case ydb_double_star:
					double2mval(&arg_mval, *va_arg(var, ydb_double_t *));
					break;
				case ydb_char_star:
					arg_mval.mvtype = MV_STR;
					arg_mval.str.addr = va_arg(var, ydb_char_t *);
					arg_mval.str.len = STRLEN(arg_mval.str.addr);
					if (MAX_STRLEN < arg_mval.str.len)
					{
						va_end(var);
						RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_MAXSTRLEN);
					}
					s2pool(&arg_mval.str);
					break;
				case ydb_string_star:
					mstr_parm = va_arg(var, ydb_string_t *);
					arg_mval.mvtype = MV_STR;
					if (MAX_STRLEN < (uint4)mstr_parm->length)
					{
						va_end(var);
						RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_MAXSTRLEN);
					}
					arg_mval.str.len = (mstr_len_t)mstr_parm->length;
					arg_mval.str.addr = mstr_parm->address;
					s2pool(&arg_mval.str);
					break;
				case ydb_buffer_star:;
					ydb_buffer_t	*buff_ptr;

					buff_ptr = va_arg(var, ydb_buffer_t *);
					arg_mval.mvtype = MV_STR;
					if (IS_INVALID_YDB_BUFF_T(buff_ptr))
					{
						char	buff1[64], buff2[64];

						SNPRINTF(buff1, SIZEOF(buff1), "Invalid ydb_buffer_t (parameter %d)", i);
						SNPRINTF(buff2, SIZEOF(buff2), "ydb_ci()/ydb_cip()/ydb_ci_t()/ydb_cip_t()");
						va_end(var);
						rts_error_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_PARAMINVALID, 4,
							      LEN_AND_STR(buff1), LEN_AND_STR(buff2));
					}
					if (MAX_STRLEN < (uint4)buff_ptr->len_used)
					{
						va_end(var);
						rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_MAXSTRLEN);
					}
					arg_mval.str.len = (mstr_len_t)buff_ptr->len_used;
					arg_mval.str.addr = buff_ptr->buf_addr;
					s2pool(&arg_mval.str);
					break;
				default:
					va_end(var);
					assertpro(FALSE); /* should have been caught by "citab_parse" */
			}
		}
		param_blk.args[i] = push_lvval(&arg_mval);
	}
	va_end(var);
	/* Need to create the new stackframe this call will run in. Any mv_stents created on this frame's behalf by
	 * push_parm_ci() or op_bindparm() will pop when this frame pops. All those mv_stents created above as part
	 * of argument processing will stick around to be used in argument output processing once the call returns.
	 */
#	ifdef HAS_LITERAL_SECT
	new_stack_frame(base_addr, (unsigned char *)LINKAGE_ADR(base_addr), (unsigned char *)xfer_addr);
#	else
	/* Any platform that does not follow pv-based linkage model either
	 *	(1) uses the following calculation to determine the context pointer value, or
	 *	(2) doesn't need a context pointer
	 * Such platforms are not currently supported by YottaDB (Linux32 and Cygwin32 fall in this category)
	 */
	assertpro(FALSE);
#	endif
	param_blk.mask = out_mask;
	push_parm_ci(dollar_truth, &param_blk);		/* Set up the parameter block for op_bindparm() in callee */
	old_intrpt_state = intrpt_ok_state;
	intrpt_ok_state = INTRPT_OK_TO_INTERRUPT; 	/* Reset interrupt state for the new M session */
	assert(frame_pointer->old_frame_pointer->type & SFT_CI);
	REVERT; /* gtmci_ch */
	/*				*/
	/* Drive the call_in routine	*/
	/*				*/
	TREF(zhalt_retval) = 0;		/* Reset global "TREF(zhalt_retval)" before dm_start() call.
					 * If a ZHALT is done inside that invocation, it would update this global.
					 */
	ESTABLISH_RET(stop_image_conditional_core, mumps_status);
	dm_start(); 	/* Kick off execution */
	REVERT;
	/*				*/
	/* Return value processing	*/
	/*				*/
	assert(!stringpool_unusable);
	intrpt_ok_state = old_intrpt_state; 		/* Restore the old interrupt state */
	/* Check if ZHALT with non-zero argument happened inside the "dm_start()" invocation.
	 * If so, forward that error to caller of "ydb_ci_exec()".
	 */
	if (TREF(zhalt_retval))
	{
		assert(1 == mumps_status);	/* SUCCESS */
		if (ci_ret_code_quit_needed)
			ci_ret_code_quit();	/* Unwind the current invocation of call-in environment */
		return TREF(zhalt_retval);
	}
	if (1 != mumps_status)
	{	/* dm_start() initializes mumps_status to 1 before execution. If mumps_status is not 1,
		 * it is either the unhandled error code propaged by $ZT/$ET (from mdb_condition_handler)
		 * or zero on returning from ZGOTO 0 (ci_ret_code_quit).
		 */
		if (internal_use)
			TREF(comm_filter_init) = FALSE;  /*exiting from filters*/
		if (mumps_status)
		{	/* This is an error codepath. Do cleanup of frames (including the call-in base frame)
			 * and rethrow error if needed. Currently only the following error(s) need rethrow.
			 *	ERR_TPRETRY
			 * The above 3 errors indicate there is a parent M frame or ydb_tp_s frame at a lower
			 * callin depth that needs to see this error.
			 */
			FGNCAL_UNWIND_CLEANUP;	/* Unwind all frames up to the call-in base frame */
			assert(SFT_CI & frame_pointer->type);
			if (ERR_TPRETRY == mumps_status)
			{	/* These error(s) need to be rethrown. But before that unwind the call-in base frame */
				if (NULL != frame_pointer)
				{
					ci_ret_code_quit();	/* Unwind the current invocation of call-in environment */
					ci_ret_code_quit_needed = FALSE;
				}
				DRIVECH(mumps_status);
				assert(FALSE);
			}
		}
		if (ci_ret_code_quit_needed)
			ci_ret_code_quit();	/* Unwind the current invocation of call-in environment */
		return mumps_status;
	}
	ESTABLISH_RET(gtmci_ch, mumps_status);
	/* Convert mval args passed by reference to C types */
	for (i = 0; i <= entry->argcnt; ++i)
	{
		if (0 == i) /* Special case for return value */
		{
			if (!has_return)
				continue;
			arg_ptr = &((lv_val *)(param_blk.retaddr))->v;
			op_exfunret(arg_ptr);		/* Validate return value specified and type */
			mask = 1;
			arg_type = entry->return_type;
		} else
		{
			arg_ptr = &param_blk.args[i - 1]->v;
			mask = out_mask;
			arg_type = entry->parms[i - 1];
			out_mask >>= 1;
		}
		/* Do not process parameters that are either input-only(I) or output(O/IO)
		 * parameters that are not modified by the M routine.
		 */
		if (MV_ON(mask, arg_ptr))
		{	/* Process all output (O/IO) or return parameters modified by the M routine */
			switch(arg_type)
			{
                                case ydb_int_star:
                                        *va_arg(temp_var, ydb_int_t *) = mval2i(arg_ptr);
					break;
                                case ydb_uint_star:
                                        *va_arg(temp_var, ydb_uint_t *) = mval2ui(arg_ptr);
					break;
				case ydb_long_star:
					*va_arg(temp_var, ydb_long_t *) =
						GTM64_ONLY(mval2i8(arg_ptr)) NON_GTM64_ONLY(mval2i(arg_ptr));
					break;
				case ydb_ulong_star:
					*va_arg(temp_var, ydb_ulong_t *) =
						GTM64_ONLY(mval2ui8(arg_ptr)) NON_GTM64_ONLY(mval2ui(arg_ptr));
					break;
#				ifdef GTM64
				case ydb_int64_star:
					*va_arg(temp_var, ydb_ulong_t *) = mval2ui8(arg_ptr);
					break;
				case ydb_uint64_star:
					*va_arg(temp_var, ydb_ulong_t *) = mval2ui8(arg_ptr);
					break;
#				endif
				case ydb_float_star:
					*va_arg(temp_var, ydb_float_t *) = mval2double(arg_ptr);
					break;
				case ydb_double_star:
					*va_arg(temp_var, ydb_double_t *) = mval2double(arg_ptr);
					break;
				case ydb_char_star:
					ydb_char_ptr = va_arg(temp_var, ydb_char_t *);
					MV_FORCE_STR(arg_ptr);
					memcpy(ydb_char_ptr, arg_ptr->str.addr, arg_ptr->str.len);
					ydb_char_ptr[arg_ptr->str.len] = 0; /* trailing null */
					break;
				case ydb_string_star:
					mstr_parm = va_arg(temp_var, ydb_string_t *);
					MV_FORCE_STR(arg_ptr);
					if (mstr_parm->length > arg_ptr->str.len)
						mstr_parm->length = arg_ptr->str.len;
					memcpy(mstr_parm->address, arg_ptr->str.addr, mstr_parm->length);
					break;
				case ydb_buffer_star:;
					ydb_buffer_t	*buff_ptr;

					buff_ptr = va_arg(temp_var, ydb_buffer_t *);
					MV_FORCE_STR(arg_ptr);
					if (arg_ptr->str.len > buff_ptr->len_alloc)
					{
						va_end(temp_var);
						rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_INVSTRLEN, 2,
									arg_ptr->str.len, buff_ptr->len_alloc);
					}
					/* Check for ERR_PARAMINVALID error only if output/return "len_used" is non-zero
					 * and "buf_addr" is NULL. See comment block about this error in an earlier part
					 * of this function for why we do this check now rather than before.
					 */
					buff_ptr->len_used = arg_ptr->str.len;
					if (IS_INVALID_YDB_BUFF_T(buff_ptr))
					{
						char	buff1[64], buff2[64];

						SNPRINTF(buff1, SIZEOF(buff1), "Invalid ydb_buffer_t (parameter %d)", i);
						SNPRINTF(buff2, SIZEOF(buff2), "ydb_ci()/ydb_cip()/ydb_ci_t()/ydb_cip_t()");
						va_end(temp_var);
						rts_error_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_PARAMINVALID, 4,
							      LEN_AND_STR(buff1), LEN_AND_STR(buff2));
					}
					if (arg_ptr->str.len)
						memcpy(buff_ptr->buf_addr, arg_ptr->str.addr, arg_ptr->str.len);
					buff_ptr->len_used = arg_ptr->str.len;
					assert(!IS_INVALID_YDB_BUFF_T(buff_ptr));
					break;
				default:
					va_end(temp_var);
					assertpro(FALSE);
			}
		} else
		{	/* This is not an output parameter (i.e. it is a I type parameter) */
			switch(arg_type)
			{
                                case ydb_int_star:		/* All of the address types are same size so can lump together */
                                case ydb_uint_star:
				case ydb_long_star:
				case ydb_ulong_star:
#				ifdef GTM64
				case ydb_int64_star:
				case ydb_uint64_star:
#				endif
				case ydb_float_star:
				case ydb_double_star:
				case ydb_char_star:
				case ydb_string_star:
				case ydb_buffer_star:
					va_arg(temp_var, void *);
					break;
                                case ydb_int:			/* The int sizes are the same so group them */
                                case ydb_uint:
                                        va_arg(temp_var, ydb_int_t);
					break;
 				case ydb_long:			/* Long sizes are the same so group them */
				case ydb_ulong:
					va_arg(temp_var, ydb_long_t);
					break;
#				ifdef GTM64
				case ydb_int64:				/* 64 bit int sizes are the same so group them */
				case ydb_uint64:
					va_arg(temp_var, ydb_int64_t);
					break;
#				endif
				case ydb_float:
				case ydb_double:
					va_arg(temp_var, ydb_double_t);
					break;
				default:
					va_end(temp_var);
					assertpro(FALSE);
			}
		}
	}
	va_end(temp_var);
	REVERT;
	assert(NULL == TREF(temp_fgncal_stack));
	FGNCAL_UNWIND;
	/* Added below, since ydb_ci/GTMCI only unwinds back to current CI frame,
	 * to unwind the two frames (current + base)
	 * in normal scenarios, gtm_exit is called
	 */
	if (internal_use)
		TREF(comm_filter_init) = FALSE;  /* Exiting from filters */
	if (ci_ret_code_quit_needed || unwind_here)
		ci_ret_code_quit();	/* Unwind the current invocation of call-in environment */
	return 0;
}

int gtm_ci_filter(const char *c_rtn_name, ...)
{
	va_list var;

	VAR_START(var, c_rtn_name);
	/* Note: "va_end(var)" done inside "ydb_ci_exec" */
	return ydb_ci_exec(c_rtn_name, NULL, var, TRUE);
}

#ifdef GTM_PTHREAD
/* Java flavor of ydb_init() */
int ydb_jinit()
{
	gtm_jvm_process = TRUE;
	return ydb_init();
}
#endif

/* The java plug-in has some very direct references to some of these routines that
 * cannot be changed by the pre-processor so for now, we have some stub routines
 * that take care of the translation. These routines are exported along with their
 * ydb_* variants. First - get rid of the pre-processor redirection via #defines.
 */
<<<<<<< HEAD
#undef gtm_init
#undef gtm_jinit
#undef gtm_exit
#undef gtm_cij
#undef gtm_zstatus

ydb_status_t gtm_init()
{
	return ydb_init();
=======
int gtm_init()
{
	rhdtyp          	*base_addr;
	unsigned char   	*transfer_addr;
	char			*dist;
	char			gtmsecshr_path[GTM_PATH_MAX];
	unsigned int		gtmsecshr_path_len;
	struct stat		stat_buf;
	char			file_perm[MAX_PERM_LEN];
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	if (NULL == lcl_gtm_threadgbl)
	{	/* This will likely need some attention before going to a threaded model */
		assert(!gtm_startup_active);
		GTM_THREADGBL_INIT;
	}
	/* A prior invocation of gtm_exit would have set process_exiting = TRUE. Use this to disallow gtm_init to be
	 * invoked after a gtm_exit
	 */
	if (process_exiting)
	{
		gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_CALLINAFTERXIT);
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_CALLINAFTERXIT);
		return ERR_CALLINAFTERXIT;
	}
	if (!gtm_startup_active)
	{	/* call-in invoked from C as base. GT.M hasn't been started up yet. */
		common_startup_init(GTM_IMAGE);
		err_init(stop_image_conditional_core);
		UTF8_ONLY(gtm_strToTitle_ptr = &gtm_strToTitle);
		/* Ensure that $gtm_dist exists */
		if (NULL == (dist = (char *)GETENV(GTM_DIST)))
			RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_GTMDISTUNDEF);
		/* Ensure that $gtm_dist is non-zero and does not exceed GTM_DIST_PATH_MAX */
		gtm_dist_len = (unsigned int)strlen(dist);
		if (!gtm_dist_len)
			RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_GTMDISTUNDEF);
		else if (GTM_DIST_PATH_MAX <= gtm_dist_len)
			RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(3) ERR_DISTPATHMAX, 1, GTM_DIST_PATH_MAX);
		/* Verify that $gtm_dist/gtmsecshr is available with setuid root */
		memcpy(gtmsecshr_path, dist, gtm_dist_len);
		gtmsecshr_path[gtm_dist_len] =  '/';
		memcpy(gtmsecshr_path + gtm_dist_len + 1, GTMSECSHR_EXECUTABLE, sizeof(GTMSECSHR_EXECUTABLE)); /* Includes null */
		gtmsecshr_path_len = gtm_dist_len + sizeof(GTMSECSHR_EXECUTABLE); /* Includes null, so don't add 1 for the slash */
		if (-1 == Stat(gtmsecshr_path, &stat_buf))
			RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(8) ERR_SYSCALL, 5,
				LEN_AND_LIT("stat for $gtm_dist/gtmsecshr"), CALLFROM, errno);
		/* Ensure that the call-in can execute $gtm_dist/gtmsecshr. This not sufficient for security purposes */
		if ((ROOTUID != stat_buf.st_uid) || !(stat_buf.st_mode & S_ISUID))
		{
			SNPRINTF(file_perm, SIZEOF(file_perm), "%04o", stat_buf.st_mode & PERMALL);
			RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(7) ERR_GTMSECSHRPERM, 5,
				gtmsecshr_path_len, gtmsecshr_path,
				RTS_ERROR_STRING(file_perm), stat_buf.st_uid);
		}
		else
		{	/* $gtm_dist validated */
			gtm_dist_ok_to_use = TRUE;
			memcpy(gtm_dist, dist, gtm_dist_len);
			gtm_post_startup_check_init();
		}
		cli_lex_setup(0, NULL);
		/* Initialize msp to the maximum so if errors occur during GT.M startup below,
		 * the unwind logic in gtmci_ch() will get rid of the whole stack.
		 */
		msp = (unsigned char *)-1L;
		GTMTRIG_DBG_ONLY(ch_at_trigger_init = &mdb_condition_handler);
	}
	ESTABLISH_RET(gtmci_ch, mumps_status);
	if (!gtm_startup_active)
	{	/* GT.M is not active yet. Create GT.M startup environment */
		invocation_mode = MUMPS_CALLIN;
		init_gtm();			/* Note - this initializes fgncal_stackbase */
		gtm_savetraps(); /* nullify default $ZTRAP handling */
		assert(IS_VALID_IMAGE && (n_image_types > image_type));	/* assert image_type is initialized */
		assert(gtm_startup_active);
		assert(frame_pointer->flags & SFF_CI);
		TREF(gtmci_nested_level) = 1;
		/* Now that GT.M is initialized. Mark the new stack pointer (msp) so that errors
		 * while executing an M routine do not unwind stack below this mark. It important that
		 * the call-in frames (SFF_CI) that hold nesting information (eg. $ECODE/$STACK data
		 * of the previous stack) are kept from being unwound.
		 */
		SAVE_FGNCAL_STACK;
	} else if (!(frame_pointer->flags & SFF_CI))
	{	/* Nested call-in: setup a new CI environment (SFF_CI frame on top of base-frame).
		 * Temporarily mark the beginning of the new stack so that initialization errors in
		 * call-in frame do not unwind entries of the previous stack (see gtmci_ch). For the
		 * duration that temp_fgncal_stack has a non-NULL value, it overrides fgncal_stack.
		 */
		TREF(temp_fgncal_stack) = msp;
		/* Generate CIMAXLEVELS error if gtmci_nested_level > CALLIN_MAX_LEVEL */
		if (CALLIN_MAX_LEVEL < TREF(gtmci_nested_level))
			RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(3) ERR_CIMAXLEVELS, 1, TREF(gtmci_nested_level));
		/* Disallow call-ins within a TP boundary since TP restarts are not supported
		 * currently across nested call-ins. When we implement TP restarts across call-ins,
		 * this error needs be changed to a Warning or Notification. Tp allowed if a filter
		 * call is being made from inside GT.M.
		 */
		if (dollar_tlevel && ((!TREF(comm_filter_init)) || (TREF(gtmci_nested_level))))
			RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_CITPNESTED);
		base_addr = make_cimode();
		transfer_addr = PTEXT_ADR(base_addr);
		gtm_init_env(base_addr, transfer_addr);
#if defined(__x86_64__)
		SET_CI_ENV(opp_ciret);
#else
		SET_CI_ENV(ci_ret_code_exit);
#endif
		gtmci_isv_save();
		(TREF(gtmci_nested_level))++;
		/* Now that the base-frames for this call-in level have been created, we can create the mv_stent
		 * to save the previous call-in level's fgncal_stack value and clear the override. When this call-in
		 * level pops, fgncal_stack will be restored to the value for the previous level. When a given call
		 * at *this* level finishes, this current value of fgncal_stack is where the stack is unrolled to to
		 * be ready for the next call.
		 */
		SAVE_FGNCAL_STACK;
		TREF(temp_fgncal_stack) = NULL;		/* Drop override */
	}
	REVERT;
	assert(NULL == TREF(temp_fgncal_stack));
	return 0;
>>>>>>> 451ab477 (GT.M V7.0-000)
}

#ifdef GTM_PTHREAD
ydb_status_t gtm_jinit()
{
<<<<<<< HEAD
	gtm_jvm_process = TRUE;
	return ydb_init();
=======
        DCL_THREADGBL_ACCESS;

        SETUP_THREADGBL_ACCESS;
	if (!gtm_startup_active)
		return 0;		/* GT.M environment not setup yet - quietly return */
	ESTABLISH_RET(gtmci_ch, mumps_status);
	assert(NULL != frame_pointer);
	/* Do not allow gtm_exit() to be invoked from external calls */
	if (!(SFF_CI & frame_pointer->flags) || !(MUMPS_CALLIN & invocation_mode) || (1 < TREF(gtmci_nested_level)))
		RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_INVGTMEXIT);
	/* Now get rid of the whole M stack - end of GT.M environment */
	while (NULL != frame_pointer)
	{
		while ((NULL != frame_pointer) && !(frame_pointer->flags & SFF_CI))
		{
#			ifdef GTM_TRIGGER
			if (SFT_TRIGR & frame_pointer->type)
				gtm_trigger_fini(TRUE, FALSE);
			else
#			endif
				op_unwind();
		}
		if (NULL != frame_pointer)
		{	/* unwind the current invocation of call-in environment */
			assert(frame_pointer->flags & SFF_CI);
			ci_ret_code_quit();
		}
	}
	gtm_exit_handler(); /* rundown all open database resource */
	/* If libgtmshr was loaded via (or on account of) dlopen() and is later unloaded via dlclose()
	 * the exit handler on AIX and HPUX still tries to call the registered atexit() handler causing
	 * 'problems'. AIX 5.2 and later have the below unatexit() call to unregister the function if
	 * our exit handler has already been called. Linux and Solaris don't need this, looking at the
	 * other platforms we support to see if resolutions can be found. SE 05/2007
	 */
#	ifdef _AIX
	unatexit(gtm_exit_handler);
#	endif
	REVERT;
	gtm_startup_active = FALSE;
	return 0;
}

/* Routine to fetch $ZSTATUS after an error has been raised */
void gtm_zstatus(char *msg, int len)
{
	int msg_len;
	msg_len = (len <= dollar_zstatus.str.len) ? len - 1 : dollar_zstatus.str.len;
	memcpy(msg, dollar_zstatus.str.addr, msg_len);
	msg[msg_len] = 0;
}

#ifdef _AIX
/* If libgtmshr was loaded via (or on account of) dlopen() and is later unloaded via dlclose()
 * the exit handler on AIX and HPUX still tries to call the registered atexit() handler causing
 * 'problems'. AIX 5.2 and later have the below unatexit() call to unregister the function if
 * our exit handler has already been called. Linux and Solaris don't need this, looking at the
 * other platforms we support to see if resolutions can be found. This routine will be called
 * by the OS when libgtmshr is unloaded. Specified with the -binitfini loader option on AIX
 * to be run when the shared library is unloaded. 06/2007 SE
 */
void gtmci_cleanup(void)
{	/* This code is only for callin cleanup */
	if (MUMPS_CALLIN != invocation_mode)
		return;
	/* If we have already run the exit handler, no need to do so again */
	if (gtm_startup_active)
	{
		gtm_exit_handler();
		gtm_startup_active = FALSE;
	}
	/* Unregister exit handler .. AIX only for now */
	unatexit(gtm_exit_handler);
>>>>>>> 451ab477 (GT.M V7.0-000)
}
#endif

ydb_status_t gtm_exit()
{
	return ydb_exit();
}

ydb_status_t gtm_cij(const char *c_rtn_name, char **arg_blob, int count, int *arg_types,
		     unsigned int *io_vars_mask, unsigned int *has_ret_value)
{
	return ydb_cij(c_rtn_name, arg_blob, count, arg_types, io_vars_mask, has_ret_value);
}

void gtm_zstatus(char* msg, int len)
{
	ydb_zstatus(msg, len);
}
