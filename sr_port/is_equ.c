/****************************************************************
 *								*
 * Copyright 2001, 2011 Fidelity Information Services, Inc	*
 *								*
 * Copyright (c) 2019-2020 YottaDB LLC and/or its subsidiaries. *
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_string.h"
#include "is_equ.h"

/* args:
 *	u, v		- pointers to mvals
 */
boolean_t is_equ(mval *u, mval *v)
{
	int		land, lor, utyp, vtyp;
	boolean_t	result;

	utyp = u->mvtype;
	vtyp = v->mvtype;
	for ( ; ; )	/* have a dummy for loop to be able to use "break" for various codepaths below */
	{
		land = utyp & vtyp;
		lor = utyp | vtyp;
		if ((land & MV_NM) != 0 && (lor & MV_NUM_APPROX) == 0)
		{
			/* at this point, the mval's are both exact numbers, we can do a numeric comparison */
			/* If they are both integers, compare only the relevant cells */
			if (land & MV_INT)
			{
				result = (u->m[1] == v->m[1]);
				break;
			}
			/* If one is an integer and the other is not, the two values cannot be equal */
			if (lor & MV_INT)
			{
				result = 0;
				break;
			}
			/* They are both decimal floating numbers, do a full comparison */
			result = ((((mval_b *)u)->sgne == ((mval_b *)v)->sgne) && (u->m[1] == v->m[1]) && (u->m[0] == v->m[0]));
			break;
		}
		/* At least one of the numbers is not in numeric form or is not a canonical number, do a string compare */
		MV_FORCE_STR(u);
		MV_FORCE_STR(v);
		if ((u->str.len != v->str.len)
				|| (u->str.len && (u->str.addr != v->str.addr) && memcmp(u->str.addr, v->str.addr, u->str.len)))
			result = 0;
		else
			result = 1;
		break;
	}
	return result;
}
