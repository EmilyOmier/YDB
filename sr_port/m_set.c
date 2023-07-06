/****************************************************************
 *								*
 * Copyright (c) 2001-2023 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_string.h"
#include "gtm_ctype.h"

#include "compiler.h"
#include "mdq.h"
#include "opcode.h"
#include "indir_enum.h"
#include "nametabtyp.h"
#include "toktyp.h"
#include "funsvn.h"
#include "mmemory.h"
#include "advancewindow.h"
#include "namelook.h"
#include "cmd.h"
#include "svnames.h"
#include "gdsroot.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsfhead.h"
#include "alias.h"
#include "glvn_pool.h"
#ifdef UTF8_SUPPORTED
#include "gtm_utf8.h"
#endif

GBLREF boolean_t	badchar_inhibit;
GBLREF boolean_t	gtm_utf8_mode;

error_def(ERR_ALIASEXPECTED);
error_def(ERR_COMMA);
error_def(ERR_DZWRNOALIAS);
error_def(ERR_DZWRNOPAREN);
error_def(ERR_EQUAL);
error_def(ERR_INVSVN);
error_def(ERR_NOALIASLIST);
error_def(ERR_RPARENMISSING);
error_def(ERR_SVNOSET);
error_def(ERR_VAREXPECTED);

LITREF unsigned char	svn_index[], fun_index[];
LITREF nametabent	svn_names[], fun_names[];
LITREF svn_data_type	svn_data[];
LITREF fun_data_type	fun_data[];

#define	FIRST_SETLEFT_NOTSEEN	-1	/* see comment against variable "first_setleft_invalid" for details */

/* This macro is used to insert the conditional jump triples (in SET $PIECE/$EXTRACT) ahead of the global variable
 * reference of the SET $PIECE target. This is to ensure the naked indicator is not touched in cases where the M-standard
 * says it should not be. e.g. set $piece(^x,"delim",2,1) should not touch naked indicator since 2>1.
 */
#define	DQINSCURTARGCHAIN(curtargtriple)			\
{								\
	dqins(curtargchain, exorder, curtargtriple);		\
	assert(curtargchain->exorder.fl == curtargtriple);	\
	curtargchain = curtargtriple;				\
}

#define	RESTORE_CURTCHAIN_IF_NEEDED				\
{								\
	if (curtchain_switched)					\
	{							\
		assert(NULL != save_curtchain);			\
		setcurtchain(save_curtchain);			\
		curtchain_switched = FALSE;			\
	}							\
}

#define SYNTAX_ERROR(errnum)					\
{								\
	RESTORE_CURTCHAIN_IF_NEEDED;				\
	stx_error(errnum);					\
	return FALSE;						\
}

#define SYNTAX_ERROR_NOREPORT_HERE				\
{								\
	RESTORE_CURTCHAIN_IF_NEEDED;				\
	return FALSE;						\
}

void	allow_dzwrtac_as_mident(void);

int m_set(void)
{	/* Some comment on "parse_warn". It is set to TRUE whenever the parse encounters an invalid setleft target.
	 *
	 * Note that even if "parse_warn" is TRUE, we should not return FALSE right away but need to continue the parse
	 * until the end of the current SET command. This way any remaining commands in the current parse line will be
	 * parsed and triples generated for them. This is necessary just in case the currently parsed invalid SET command
	 * does not get executed at runtime (due to postconditionals etc.)
	 *
	 * Some comment on the need for "first_setleft_invalid". This variable is needed only in the
	 * case we encounter an invalid-SVN/invalid-FCN/unsettable-SVN as a target of the SET. We need to evaluate the
	 * right-hand-side of the SET command only if at least one valid setleft target is parsed before an invalid setleft
	 * target is encountered. This is because we still need to execute the valid setlefts at runtime before triggering
	 * a runtime error for the invalid setleft. If the first setleft target is an invalid one, then there is no need
	 * to evaluate the right-hand-side. In fact, in this case, adding triples (corresponding to the right hand side)
	 * to the execution chain could cause problems with emit_code later in the compilation as the destination
	 * for the right hand side triples could now be undefined (for example a valid SVN on the left side of the
	 * SET would have generated an OC_SVPUT triple with one of its operands holding the result of the right
	 * hand side evaluation, but an invalid SVN on the left side which would have instead caused an OC_RTERROR triple
	 * to have been generated leaving no triple to receive the result of the right hand side evaluation thus causing
	 * emit_code to be confused and hitting an assertpro()). Therefore discard all triples generated by the right hand
	 * side in this case.
	 *
	 * By the same reasoning, discard all triples generated by setleft targets AFTER this invalid one as well.
	 * "first_setleft_invalid" is set to TRUE if the first setleft target is invalid and set to FALSE if the first setleft
	 * target is valid. It is initialized to -1 before the start of the parse.
	 */

	boolean_t	alias_processing, delim1char, first_is_lit, got_lparen, have_lh_alias, is_extract, last_is_lit, valid_char;
	boolean_t	parse_warn;	/* set to TRUE in case of an invalid SVN etc. */
	boolean_t	curtchain_switched;	/* set to TRUE if a setcurtchain was done */
	boolean_t	temp_subs_was_FALSE;
	boolean_t	used_glvn_slot;
	int		delimlen, first_val_lit, index, last_val_lit, nakedzalias, setop;
	int		first_setleft_invalid;	/* set to TRUE if the first setleft target is invalid */
	opctype		put_oc = OC_NOOP;
	oprtype		delimval, firstval, lastval, resptr, *result, v, control_slot, first_control_slot;
	triple		*curtargchain, *delimiter, discardcurtchain, *first = NULL, *get, *jmptrp1, *jmptrp2, *last, *obp,
			*put = NULL;
	triple		*s, *s0, *s1, save_targchain, *save_curtchain, *save_curtchain1, *sub, targchain, *tmp;
	triple		*ref;
	mint		delimlit;
	mval		*delim_mval;
	mvar		*mvarptr;
	union
	{
		uint4		unichar_val;
		unsigned char	unibytes_val[4];
	} unichar;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	TREF(temp_subs) = FALSE;
	used_glvn_slot = FALSE;
	exorder_init(&targchain);
	result = (oprtype *)mcalloc(SIZEOF(oprtype));
	resptr = put_indr(result);
	delimiter = sub = last = NULL;
	/* A SET clause must be entirely alias related or a normal set. Parenthized multiple sets of aliases are not allowed
	 * and will trigger an error. This is because the source and targets of aliases require different values and references
	 * than normal sets do and thus cannot be mixed.
	 */
	if (alias_processing = (TK_ASTERISK == TREF(window_token)))
		advancewindow();
	if (got_lparen = (TK_LPAREN == TREF(window_token)))
	{
		if (alias_processing)
			stx_error(ERR_NOALIASLIST);
		advancewindow();
		TREF(temp_subs) = TRUE;
	}
	/* Some explanation: The triples from the left hand side of the SET expression that are
	 * expressly associated with fetching (in case of set $piece/$extract) and/or storing of
	 * the target value are removed from curtchain and placed on the targchain. Later, these
	 * triples will be added to the end of curtchain to do the finishing store of the target
	 * after the righthand side has been evaluated. This is per the M standard.
	 *
	 * Note that SET $PIECE/$EXTRACT have special conditions in which the first argument is not referenced at all.
	 * (e.g. set $piece(^a," ",3,2) in this case 3 > 2 so this should not evaluate ^a and therefore should not
	 * modify the naked indicator). That is, the triples that do these conditional checks need to be inserted
	 * ahead of the OC_GVNAME of ^a, all of which need to be inserted on the targchain. But the conditionalization
	 * can be done only after parsing the first argument of the SET $PIECE and examining the remaining arguments.
	 * Therefore we maintain the "curtargchain" variable which stores the value of the "targchain" at the beginning
	 * of the iteration (at the start of the $PIECE parsing) and all the conditionalization will be inserted right
	 * here which is guaranteed to be ahead of where the OC_GVNAME gets inserted.
	 *
	 * For example, SET $PIECE(^A(x,y),delim,first,last)=RHS will generate a final triple chain as follows
	 *
	 *	A - Triples to evaluate subscripts (x,y) of the global ^A
	 *	A - Triples to evaluate delim
	 *	A - Triples to evaluate first
	 *	A - Triples to evaluate last
	 *	B - Triples to evaluate RHS
	 *	C - Triples to do conditional check (e.g. first > last etc.)
	 *	C - Triples to branch around if the checks indicate this is a null operation SET $PIECE
	 *	D - Triple that does OC_GVNAME of ^A
	 *	D - Triple that does OC_SETPIECE to determine the new value
	 *	D - Triple that does OC_GVPUT of the new value into ^A(x,y)
	 *	This is the point where the conditional check triples will branch around to if they chose to.
	 *
	 *	A - triples that evaluate the arguments/subscripts in the left-hand-side of the SET command
	 *		These triples are built in "curtchain"
	 *	B - triples that evaluate the arguments/subscripts in the right-hand-side of the SET command
	 *		These triples are built in "curtchain"
	 *	C - triples that do conditional check for any $PIECE/$EXTRACT in the left side of the SET command.
	 *		These triples are built in "curtargchain"
	 *	D - triples that generate the reference to the target of the SET and the store into the target.
	 *		These triples are built in "targchain"
	 *
	 * Note alias processing does not support the SET *(...)=.. type syntax because the type of argument
	 * created for RHS processing is dependent on the LHS receiver type and we do not support more than one
	 * type of source argument in a single SET.
	 */
	first_setleft_invalid = FIRST_SETLEFT_NOTSEEN;
	curtchain_switched = FALSE;
	nakedzalias = have_lh_alias = FALSE;
	save_curtchain = NULL;
	assert(FIRST_SETLEFT_NOTSEEN != TRUE);
	assert(FIRST_SETLEFT_NOTSEEN != FALSE);
	for (parse_warn = FALSE; ; parse_warn = FALSE)
	{
		curtargchain = targchain.exorder.bl;
		jmptrp1 = jmptrp2 = NULL;
		delim1char = is_extract = FALSE;
		allow_dzwrtac_as_mident();	/* Allows $ZWRTACxxx as target to be treated as an mident */
		switch (TREF(window_token))
		{
		case TK_IDENT:
			/* A slight diversion first. If this is a $ZWRTAC set (indication of $ in first char
			 * is currently enough to signify that), then we need to check a few conditions first.
			 * If this is a "naked $ZWRTAC", meaning no numeric suffix, then this is a flag that
			 * all the $ZWRTAC vars in the local variable tree need to be kill *'d which will not
			 * be generating a SET instruction. First we need to verify that fact and make sure
			 * we are not in PARENs and not doing alias processing. Note *any* value can be
			 * specified as the source but while it will be evaluated, it is NOT stored anywhere.
			 */
			if ('$' == *(TREF(window_ident)).addr)
			{	/* We have a $ZWRTAC<xx> target */
				if (got_lparen)
					/* We don't allow $ZWRTACxxx to be specified in a parenthesized list.
					 * Verify that first
					 */
					SYNTAX_ERROR(ERR_DZWRNOPAREN);
				if (STR_LIT_LEN(DOLLAR_ZWRTAC) == (TREF(window_ident)).len)
				{	/* Ok, this is a naked $ZWRTAC targeted set */
					if (alias_processing)
						SYNTAX_ERROR(ERR_DZWRNOALIAS);
					nakedzalias = TRUE;
					/* This opcode doesn't really need args but it is easier to fit in with the rest
					 * of m_set processing to pass it the result arg, which there may actually be
					 * a use for someday..
					 */
					put = maketriple(OC_CLRALSVARS);
					put->operand[0] = resptr;
					dqrins(&targchain, exorder, put);
					advancewindow();
					break;
				}
			}
			/* If we are doing alias processing, there are two possibilities:
			 *  1) LHS is unsubscripted - it is an alias variable being created or replaced. Need to parse
			 *  the varname as if this were a regular set.
			 *  2) LHS is subscripted - it is an alias container variable being created or replaced. The
			 *  processing here is to pass the base variable index to the store routine so bypass the
			 *  lvn() call.
			 */
			if (!alias_processing || (TK_LPAREN == TREF(director_token)))
			{	/* Normal variable processing or we have a lh alias container */
				if (!lvn(&v, OC_PUTINDX, 0))
					SYNTAX_ERROR_NOREPORT_HERE;
				if (OC_PUTINDX == v.oprval.tref->opcode)
				{
					dqdel(v.oprval.tref, exorder);
					dqrins(&targchain, exorder, v.oprval.tref);
					sub = v.oprval.tref;
					put_oc = OC_PUTINDX;
					if (TREF(temp_subs))
						create_temporaries(sub, put_oc);
				}
			} else
			{	/* Have alias variable. Argument is index into var table rather than pointer to var */
				have_lh_alias = TRUE;
				/* We only want the variable index in this case. Since the entire hash structure to which
				 * this variable is going to be pointing to is changing, doing anything that calls fetch()
				 * is somewhat pointless so we avoid it by just accessing the variable information
				 * directly.
				 */
				mvarptr = get_mvaddr(&(TREF(window_ident)));
				v = put_ilit(mvarptr->mvidx);
				advancewindow();
			}
			/* Determine correct storing triple */
			put = maketriple((!alias_processing ? OC_STO : (have_lh_alias ? OC_SETALS2ALS : OC_SETALSIN2ALSCT)));
			put->operand[0] = v;
			put->operand[1] = resptr;
			dqrins(&targchain, exorder, put);
			break;
		case TK_CIRCUMFLEX:
			if (alias_processing)
				SYNTAX_ERROR(ERR_ALIASEXPECTED);
			s1 = (TREF(curtchain))->exorder.bl;
			if (!gvn())
				SYNTAX_ERROR_NOREPORT_HERE;
			assert(OC_GVRECTARG != (TREF(curtchain))->opcode);	/* we count on gvn not having been shifted */
			for (sub = (TREF(curtchain))->exorder.bl; sub != s1; sub = sub->exorder.bl)
			{
				put_oc = sub->opcode;
				if ((OC_GVNAME == put_oc) || (OC_GVNAKED == put_oc) || (OC_GVEXTNAM == put_oc))
					break;
			}
			assert((OC_GVNAME == put_oc) || (OC_GVNAKED == put_oc) || (OC_GVEXTNAM == put_oc));
			dqdel(sub, exorder);
			dqrins(&targchain, exorder, sub);
			if (TREF(temp_subs))
				create_temporaries(sub, put_oc);
			put = maketriple(OC_GVPUT);
			put->operand[0] = resptr;
			dqrins(&targchain, exorder, put);
			break;
		case TK_ATSIGN:
			if (alias_processing)
				SYNTAX_ERROR(ERR_ALIASEXPECTED);
			if (!indirection(&v))
				SYNTAX_ERROR_NOREPORT_HERE;
			if (!got_lparen && (TK_EQUAL != TREF(window_token)))
			{
				assert(!curtchain_switched);
				put = newtriple(OC_COMMARG);
				put->operand[0] = v;
				put->operand[1] = put_ilit(indir_set);
				return TRUE;
			}
			/* save and restore the variable lookup for true left-to-right evaluation */
			INSERT_INDSAVGLVN(control_slot, v, ANY_SLOT, 0);	/* 0 flag to defer global reference */
			if (!used_glvn_slot)
			{
				used_glvn_slot = TRUE;
				first_control_slot = control_slot;
			}
			put = maketriple(OC_STOGLVN);
			put->operand[0] = control_slot;
			put->operand[1] = resptr;
			dqrins(&targchain, exorder, put);
			break;
		case TK_DOLLAR:
			if (alias_processing)
				SYNTAX_ERROR(ERR_ALIASEXPECTED);
			advancewindow();
			if (TK_IDENT != TREF(window_token))
				SYNTAX_ERROR(ERR_VAREXPECTED);
			if (TK_LPAREN != TREF(director_token))
			{	/* Look for intrinsic special variables */
				s1 = (TREF(curtchain))->exorder.bl;
				if (0 > (index = namelook(svn_index, svn_names, (TREF(window_ident)).addr,
					(TREF(window_ident)).len)))
				{	/* NOTE assignment above */
					STX_ERROR_WARN(ERR_INVSVN);	/* sets "parse_warn" to TRUE */
				} else if (!svn_data[index].can_set)
				{
					STX_ERROR_WARN(ERR_SVNOSET);	/* sets "parse_warn" to TRUE */
				}
				advancewindow();
				if (!parse_warn)
				{
					if ((SV_ETRAP != svn_data[index].opcode) && (SV_ZTRAP != svn_data[index].opcode))
					{	/* Setting of $ZTRAP or $ETRAP must go through opp_svput because they
						 * may affect the stack pointer. All others directly to op_svput().
						 */
						put = maketriple(OC_SVPUT);
					} else
						put = maketriple(OC_PSVPUT);
					put->operand[0] = put_ilit(svn_data[index].opcode);
					put->operand[1] = resptr;
					dqrins(&targchain, exorder, put);
				} else
				{	/* OC_RTERROR triple would have been inserted in curtchain by ins_errtriple
					 * (invoked by stx_error). To maintain consistency with the "if" portion of
					 * this code, we need to move this triple to the "targchain".
					 */
					tmp = (TREF(curtchain))->exorder.bl; /* corresponds to put_ilit(FALSE) in ins_errtriple */
					tmp = tmp->exorder.bl;	/* corresponds to put_ilit(in_error) in ins_errtriple */
					tmp = tmp->exorder.bl;	/* corresponds to newtriple(OC_RTERROR) in ins_errtriple */
					assert(OC_RTERROR == tmp->opcode);
					dqdel(tmp, exorder);
					dqrins(&targchain, exorder, tmp);
				}
				break;
			}
			/* Only 4 function names allowed on left side: $[Z]Piece and $[Z]Extract */
			index = namelook(fun_index, fun_names, (TREF(window_ident)).addr, (TREF(window_ident)).len);
			if (0 > index)
			{
				STX_ERROR_WARN(ERR_INVFCN);	/* sets "parse_warn" to TRUE */
				/* OC_RTERROR triple would have been inserted in "curtchain" by ins_errtriple
				 * (invoked by stx_error). We need to switch it to "targchain" to be consistent
				 * with every other codepath in this module.
				 */
				tmp = (TREF(curtchain))->exorder.bl; /* corresponds to put_ilit(FALSE) in ins_errtriple */
				tmp = tmp->exorder.bl;	/* corresponds to put_ilit(in_error) in ins_errtriple */
				tmp = tmp->exorder.bl;	/* corresponds to newtriple(OC_RTERROR) in ins_errtriple */
				assert(OC_RTERROR == tmp->opcode);
				dqdel(tmp, exorder);
				dqrins(&targchain, exorder, tmp);
				advancewindow();	/* skip past the function name */
				advancewindow();	/* skip past the left paren */
				/* Parse the remaining arguments until corresponding RIGHT-PAREN/SPACE/EOL is reached */
				if (!parse_until_rparen_or_space())
					SYNTAX_ERROR_NOREPORT_HERE;
				s = NULL;
			} else
			{
				switch (fun_data[index].opcode)
				{
				case OC_FNPIECE:
					setop = OC_SETPIECE;
					break;
				case OC_FNEXTRACT:
					is_extract = TRUE;
					setop = OC_SETEXTRACT;
					break;
				case OC_FNZPIECE:
					setop = OC_SETZPIECE;
					break;
				case OC_FNZEXTRACT:
					is_extract = TRUE;
					setop = OC_SETZEXTRACT;
					break;
				default:
					SYNTAX_ERROR(ERR_VAREXPECTED);
				}
				advancewindow();
				advancewindow();
				/* Although we see the get (target) variable first, we need to save it's processing
				 * on another chain -- the targchain -- because the retrieval of the target is bypassed
				 * and the naked indicator is not reset if the first/last parameters are not set in a
				 * logical manner (must be > 0 and first <= last). So the evaluation order is
				 * delimiter (if $piece), first, last, RHS of the set and then the target if applicable.
				 * Set up primary action triple now since it is ref'd by the put triples generated below.
				 */
				s = maketriple(setop);
				/* Even for SET[Z]PIECE and SET[Z]EXTRACT, the SETxxxxx opcodes
				 * do not do the final store, they only create the final value TO be
				 * stored so generate the triples that will actually do the store now.
				 * Note we are still building triples on the original curtchain.
				 */
				switch (TREF(window_token))
				{
				case TK_IDENT:
					if (!lvn(&v, OC_PUTINDX, 0))
						SYNTAX_ERROR(ERR_VAREXPECTED);
					if (OC_PUTINDX == v.oprval.tref->opcode)
					{
						dqdel(v.oprval.tref, exorder);
						dqrins(&targchain, exorder, v.oprval.tref);
						sub = v.oprval.tref;
						put_oc = OC_PUTINDX;
						if (TREF(temp_subs))
							create_temporaries(sub, put_oc);
					}
					get = maketriple(OC_FNGET);
					get->operand[0] = v;
					put = maketriple(OC_STO);
					put->operand[0] = v;
					put->operand[1] = put_tref(s);
					break;
				case TK_ATSIGN:
					if (!indirection(&v))
						SYNTAX_ERROR(ERR_VAREXPECTED);
					INSERT_INDSAVGLVN(control_slot, v, ANY_SLOT, 0);
					if (!used_glvn_slot)
					{
						used_glvn_slot = TRUE;
						first_control_slot = control_slot;
					}
					get = maketriple(OC_INDGET1);
					get->operand[0] = control_slot;
					put = maketriple(OC_STOGLVN);
					put->operand[0] = control_slot;
					put->operand[1] = put_tref(s);
					break;
				case TK_CIRCUMFLEX:
					s1 = (TREF(curtchain))->exorder.bl;
					if (!gvn())
						SYNTAX_ERROR_NOREPORT_HERE;
					assert(OC_GVRECTARG != (TREF(curtchain))->opcode);	/* gvn not shifted */
					put_oc = OC_NOOP;
					for (sub = (TREF(curtchain))->exorder.bl; sub != s1 ; sub = sub->exorder.bl)
					{
						put_oc = sub->opcode;
						if ((OC_GVNAME == put_oc) || (OC_GVNAKED == put_oc) || (OC_GVEXTNAM == put_oc))
							break;
					}
					assert((OC_GVNAME == put_oc) || (OC_GVNAKED == put_oc) || (OC_GVEXTNAM == put_oc));
					dqdel(sub, exorder);
					dqrins(&targchain, exorder, sub);
					if (TREF(temp_subs))
						create_temporaries(sub, put_oc);
					get = maketriple(OC_FNGVGET);
					get->operand[0] = put_str(0, 0);
					put = maketriple(OC_GVPUT);
					put->operand[0] = put_tref(s);
					break;
				default:
					SYNTAX_ERROR(ERR_VAREXPECTED);
				}
				s->operand[0] = put_tref(get);
				/* Code to fetch args for target triple are on targchain. Put get there now too. */
				dqrins(&targchain, exorder, get);
				if (!is_extract)
				{	/* Set $[z]piece */
					delimiter = newtriple(OC_PARAMETER);
					s->operand[1] = put_tref(delimiter);
					first = newtriple(OC_PARAMETER);
					delimiter->operand[1] = put_tref(first);
					/* Process delimiter string ($[z]piece only) */
					if (TK_COMMA != TREF(window_token))
						SYNTAX_ERROR(ERR_COMMA);
					advancewindow();
					if (EXPR_FAIL == expr(&delimval, MUMPS_STR))
						SYNTAX_ERROR_NOREPORT_HERE;
					assert(TRIP_REF == delimval.oprclass);
				} else
				{	/* Set $[Z]Extract */
					first = newtriple(OC_PARAMETER);
					s->operand[1] = put_tref(first);
				}
				/* Process first integer value */
				if (TK_COMMA != TREF(window_token))
					firstval = put_ilit(1);
				else
				{
					advancewindow();
					if (EXPR_FAIL == expr(&firstval, MUMPS_INT))
						SYNTAX_ERROR(ERR_COMMA);
					assert(TRIP_REF == firstval.oprclass);
				}
				first->operand[0] = firstval;
				if (first_is_lit = (OC_ILIT == firstval.oprval.tref->opcode))
				{
					assert(ILIT_REF ==firstval.oprval.tref->operand[0].oprclass);
					first_val_lit = firstval.oprval.tref->operand[0].oprval.ilit;
				} else
					first_val_lit = 0;
				if (TK_COMMA != TREF(window_token))
				{	/* There is no "last" value. Only if 1 char literal delimiter and
					 * no "last" value can we generate shortcut code to op_set[z]p1 entry
					 * instead of op_set[z]piece. Note if UTF8 mode is in effect, then this
					 * optimization applies if the literal is one UTF8 char which may in
					 * fact be up to 4 bytes but will still be passed as a single unsigned
					 * integer.
					 */
					if (!is_extract)
					{
						delim_mval = &delimval.oprval.tref->operand[0].oprval.mlit->v;
						valid_char = TRUE;	/* Basic assumption unles proven otherwise */
						if ((OC_LIT == delimval.oprval.tref->opcode)
						    && (1 == ((gtm_utf8_mode && (OC_SETPIECE == s->opcode))
							      ? MV_FORCE_LEN_DEC(delim_mval) : delim_mval->str.len)))
						{	/* Single char delimiter for set $piece */
							if (valid_char || (1 == delim_mval->str.len))
							{	/* This reference to a one character literal or a single
								 * byte invalid utf8 character that needs to be turned into
								 * an explict formated integer literal instead
								 */
								unichar.unichar_val = 0;
								if (!gtm_utf8_mode
								    UTF8_ONLY(|| (OC_SETZPIECE == s->opcode)))
								{	/* Single byte delimiter */
									assert(1 == delim_mval->str.len);
									UNIX_ONLY(s->opcode = OC_SETZP1);
									VMS_ONLY(s->opcode = OC_SETP1);
									unichar.unibytes_val[0] = *delim_mval->str.addr;
								}
#								ifdef UTF8_SUPPORTED
								else
								{	/* Potentially multiple bytes in one int */
									assert(SIZEOF(int) >= delim_mval->str.len);
									memcpy(unichar.unibytes_val,
									       delim_mval->str.addr,
									       delim_mval->str.len);
									s->opcode = OC_SETP1;
								}
#								endif
								delimlit = (mint)unichar.unichar_val;
								delimiter->operand[0] = put_ilit(delimlit);
								delim1char = TRUE;
							}
						}
					}
					if (!delim1char)
					{	/* Was not handled as a single char delim by code above either bcause it
						 * was (1) not set $piece, or (2) was not a single char delim or (3) it was
						 * not a VALID utf8 single char delim and badchar was inhibited.
						 */
						if (!is_extract)
							delimiter->operand[0] = delimval;
						last = newtriple(OC_PARAMETER);
						first->operand[1] = put_tref(last);
						last->operand[0] = first->operand[0];	/* start = end range */
					}
					/* Generate test sequences for first/last to bypass the set operation if
					 * first/last are not in a usable form
					 */
					if (first_is_lit)
					{
						if (1 > first_val_lit)
						{
							jmptrp1 = maketriple(OC_JMP);
							DQINSCURTARGCHAIN(jmptrp1);
						}
						/* note else no test necessary since first == last and are > 0 */
					} else
					{	/* Generate test for first being <= 0 */
						jmptrp1 = maketriple(OC_COBOOL);
						jmptrp1->operand[0] = first->operand[0];
						DQINSCURTARGCHAIN(jmptrp1);
						jmptrp1 = maketriple(OC_JMPLEQ);
						DQINSCURTARGCHAIN(jmptrp1);
					}
				} else
				{	/* There IS a last value */
					if (!is_extract)
						delimiter->operand[0] = delimval;
					last = newtriple(OC_PARAMETER);
					first->operand[1] = put_tref(last);
					advancewindow();
					if (EXPR_FAIL == expr(&lastval, MUMPS_INT))
						SYNTAX_ERROR_NOREPORT_HERE;
					assert(TRIP_REF == lastval.oprclass);
					last->operand[0] = lastval;
					/* Generate inline code to test first/last for usability and if found
					 * lacking, branch around the getchain and the actual store so we avoid
					 * setting the naked indicator so far as the target gvn is concerned.
					 */
					if (last_is_lit = (OC_ILIT == lastval.oprval.tref->opcode))	/* NOTE assignment */
					{	/* Case 1: last is a literal */
						assert(ILIT_REF == lastval.oprval.tref->operand[0].oprclass);
						last_val_lit = lastval.oprval.tref->operand[0].oprval.ilit;
						if ((1 > last_val_lit) || ((first_is_lit && (first_val_lit > last_val_lit))))
						{	/* .. and first is a literal and one or both of them is no good
							 * so unconditionally branch around the whole thing.
							 */
							jmptrp1 = maketriple(OC_JMP);
							DQINSCURTARGCHAIN(jmptrp1);
						} /* else case actually handled at next 'if' .. */
					} else
					{	/* Last is not literal. Do test if it is greater than 0 */
						jmptrp1 = maketriple(OC_COBOOL);
						jmptrp1->operand[0] = last->operand[0];
						DQINSCURTARGCHAIN(jmptrp1);
						jmptrp1 = maketriple(OC_JMPLEQ);
						DQINSCURTARGCHAIN(jmptrp1);
					}
					if (!last_is_lit || !first_is_lit)
					{	/* Compare to check that last >= first */
						jmptrp2 = maketriple(OC_VXCMPL);
						jmptrp2->operand[0] = first->operand[0];
						jmptrp2->operand[1] = last->operand[0];
						DQINSCURTARGCHAIN(jmptrp2);
						jmptrp2 = maketriple(OC_JMPGTR);
						DQINSCURTARGCHAIN(jmptrp2);
					}
				}
			}
			if (TK_RPAREN != TREF(window_token))
				SYNTAX_ERROR(ERR_RPARENMISSING);
			advancewindow();
			if (!parse_warn)
			{
				assert(s);
				dqrins(&targchain, exorder, s);
				dqrins(&targchain, exorder, put);
				/* Put result operand on the chain. End of chain depends on whether or not
				 * we are calling the shortcut or the full set-piece code
				 */
				if (delim1char)
				{
					assert(first);
					first->operand[1] = resptr;
				} else
					last->operand[1] = resptr;
				/* Set jump targets if we did tests above. The function "tnxtarg" operates on "curtchain"
				 * but we want to set targets based on "targchain", so temporarily switch them. Should not
				 * use "save_curtchain" here as it might already be in use.
				 */
				save_curtchain1 = setcurtchain(&targchain);
				if (NULL != jmptrp1)
					tnxtarg(&jmptrp1->operand[0]);
				if (NULL != jmptrp2)
					tnxtarg(&jmptrp2->operand[0]);
				setcurtchain(save_curtchain1);
			}
			break;
		case TK_ASTERISK:
			/* The only way an asterisk can be detected here is if we are inside a list so mention this is
			 * not possible and give error.
			 */
			stx_error(ERR_NOALIASLIST);
			return FALSE;
		default:
			SYNTAX_ERROR(ERR_VAREXPECTED);
		}
		if (FIRST_SETLEFT_NOTSEEN == first_setleft_invalid)
		{
			first_setleft_invalid = parse_warn;
			if (first_setleft_invalid)
			{	/* We are not going to evaluate the right hand side of the SET command. This means
				 * we should not evaluate any more setleft targets (whether they parse validly or not)
				 * as well since their source (the RHS of the set) is undefined. To achieve this, we
				 * switch to a temporary chain (both for curtchain and targchain) that will be discarded finally.
				 */
				/* save curtchain */
				exorder_init(&discardcurtchain);
				save_curtchain = setcurtchain(&discardcurtchain);
				assert(!curtchain_switched);
				curtchain_switched = TRUE;
				/* save targchain */
				save_targchain = targchain;
				exorder_init(&targchain);
			}
		}
		assert(FIRST_SETLEFT_NOTSEEN != first_setleft_invalid);
		if (!got_lparen)
			break;
		if (TK_COMMA == TREF(window_token))
			advancewindow();
		else
		{
			if (TK_RPAREN == TREF(window_token))
			{
				advancewindow();
				break;
			} else
				SYNTAX_ERROR(ERR_RPARENMISSING);
		}
	}
	if (TK_EQUAL != TREF(window_token))
		SYNTAX_ERROR(ERR_EQUAL);
	advancewindow();
	assert(FIRST_SETLEFT_NOTSEEN != first_setleft_invalid);
	temp_subs_was_FALSE = (FALSE == TREF(temp_subs));	/* Note down if temp_subs is FALSE at this point */
	/* If we are in alias processing mode, the RHS cannot be an expression but must be one of a subscripted or unsubscripted
	 * local variable, or a $$func(..) function call.
	 */
	if (!alias_processing)
	{	/* Normal case first - evaluate expression creating triples on the current chain */
		if (EXPR_FAIL == expr(result, MUMPS_EXPR))
			SYNTAX_ERROR_NOREPORT_HERE;
	} else
	{	/* Alias processing -- determine which of the three types of sources we have: var, subscripted var or $$func */
		allow_dzwrtac_as_mident();	/* Allow source of $ZWRTACxxx as an mident */
		if (TK_IDENT != TREF(window_token))
		{	/* Check if we have a $$func() call source */
			if ((TK_DOLLAR == TREF(window_token)) && (TK_DOLLAR == TREF(director_token)))
			{	/* Parse the function only with exfunc(). We definitely do not want an expression */
				TREF(temp_subs) = TRUE;	/* RHS $$ function detected - need temporary */
				advancewindow();
				if (!exfunc(result, TRUE))
					SYNTAX_ERROR_NOREPORT_HERE;
				assert(put);
				if (OC_SETALSIN2ALSCT == put->opcode)
					/* Change opcode to create an alias container from the returned alias */
					put->opcode = OC_SETFNRETIN2ALSCT;
				else
				{	/* Change opcode to create an alias from the returned alias */
					assert(OC_SETALS2ALS == put->opcode);
					put->opcode = OC_SETFNRETIN2ALS;
				}
			} else
				/* Else, only local variables allowed as aliases */
				SYNTAX_ERROR(ERR_ALIASEXPECTED);
		} else
		{	/* Alias var source */
			if (('$' == *(TREF(window_ident)).addr) && (STR_LIT_LEN(DOLLAR_ZWRTAC) >= (TREF(window_ident)).len))
				/* $ZWRTAC is not allowed as a "source" value. Must be a $ZWRTACn<nnn> format */
				SYNTAX_ERROR(ERR_DZWRNOALIAS);
			if (TK_LPAREN == TREF(director_token))
			{	/* Subscripted local variable - have alias container.
				 * The storing opcode set into the "put" triple at the top of this routine was
				 * set assuming the source was an alias. Now that we know the source is actually
				 * an alias container (and hence a different data type), we need to adjust the
				 * opcode accordingly.
				 */
				assert(put);
				if (OC_SETALS2ALS == put->opcode)
					put->opcode = OC_SETALSCTIN2ALS;
				else
				{
					assert(OC_SETALSIN2ALSCT == put->opcode);
					put->opcode = OC_SETALSCT2ALSCT;
				}
			}
			/* For RHS processing, both alias var and alias container vars have their lv_val addr
			 * passed so normal var processing applies.
			 */
			if (!lvn(result, OC_GETINDX, 0))
				SYNTAX_ERROR(ERR_ALIASEXPECTED);
		}
	}
	if (first_setleft_invalid)
	{	/* switch from the temporary chain back to the current execution chain */
		assert(curtchain_switched);
		RESTORE_CURTCHAIN_IF_NEEDED;	/* does a setcurtchain(save_curtchain) */
		targchain = save_targchain;
	}
	/* Now add in the left-hand side triples */
	assert(!curtchain_switched);
	obp = (TREF(curtchain))->exorder.bl;
	dqadd(obp, &targchain, exorder);		/* this is a violation of info hiding */
	/* Check if "temp_subs" was FALSE originally but got set to TRUE as part of evaluating the right hand side
	 * (for example, if rhs had $$ or $& or $INCR usages). If so need to create temporaries.
	 */
	if (TREF(temp_subs) && temp_subs_was_FALSE && (NULL != sub))
		create_temporaries(sub, put_oc);
	TREF(temp_subs) = FALSE;
	if (used_glvn_slot)
	{	/* Free up slots we're done with. */
		ref = newtriple(OC_GLVNPOP);
		ref->operand[0] = first_control_slot;
	}
	return TRUE;
}

/* Prior to Alias support, the ZWRITE command was able to dump the entire local variable environment such that it could be
 * reloaded by Xecuting the dumped lines. With the addition of Alias type variables, the output lines not only include the
 * variable content but their alias associations as well. One aspect of this is the $ZWRTAC variable which is a temporary
 * "variable" which we allow to hold the content of what would otherwise be "orphaned data" or data which has a container
 * pointing to it but is not referenced by a base variable. During the reload operation, it is this orphaned data that is
 * put into the $ZWRTAC environment. The first orphaned array is put into $ZWRTAC1(...), the second into $ZWRTAC2(..) and
 * so on. Setting the $ZWRTAC variable itself to null (or to any value actually - null only is not enforced) causes all of
 * the $ZWRTAC variables to be KILL *'d. The only syntactic allowances for using $ZWRTACxxx as an mident in GTM are in this
 * SET statement. We do not support its use in any other statement type. This routine allows us to modify the tokens so that
 * if the current token is a '$' and the next token is ZWRTACxxx, we can combine them into a single token and the parser
 * need not be further modified.
 */
void allow_dzwrtac_as_mident(void)
{
	char_ptr_t	chrp, chrpmin, chrplast;
	int		movlen;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	if (TK_DOLLAR != TREF(window_token))
		return;			/* Couldn't be $ZWRTACxxx without first $ */
	if (TK_IDENT != TREF(director_token))
		return;			/* Couldn't be $ZWRTACxxx without token 2nd part */
	if (STR_LIT_LEN("ZWRTAC") > (TREF(director_ident)).len)
		return;			/* Couldn't be $ZALAISxxx without sufficient length of name */
	if (0 != MEMCMP_LIT((TREF(director_ident)).addr, "ZWRTAC"))
		return;			/* Couldn't be $ZWRTACxxx without ZWRTAC as first part of token */
	/* We need to shift the existing token over 1 byte to make room for insertion of the '$' prefix. Normally,
	 * we wouldn't want to do this as we are verifying but since if the verification fails the code path will
	 * raise an error and since the error does not use this token buffer, we are safe in migrating while
	 * we do the verification check. Saves us having to scan the line backwards via memmove() again below. So
	 * verify the token suffix is all numeric while we do our shift.
	 */
	movlen = ((MAX_MIDENT_LEN) > (TREF(director_ident)).len) ? (TREF(director_ident)).len : MAX_MIDENT_LEN - 1;
	for (chrplast = (TREF(director_ident)).addr + movlen, chrp = chrplast - 1,
		chrpmin = (TREF(director_ident)).addr + STR_LIT_LEN("ZWRTAC") - 1;
		chrp > chrpmin; --chrp, --chrplast)
	{
		if (!ISDIGIT_ASCII((int)*chrp))
			return;		/* Couldn't be $ZWRTACxxx without all numeric suffix (if exists) */
		*chrplast = *chrp;
	}
	/* Verification (and shift) complete -- finish modifying director token */
	MEMCPY_LIT((TREF(director_ident)).addr, DOLLAR_ZWRTAC);
	(TREF(director_ident)).len = movlen + 1;
	/* Nnw forward the scan to pull director token values into window token for use by our caller */
	advancewindow();
	assert(TK_IDENT == TREF(window_token));
}
