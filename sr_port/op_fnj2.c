/****************************************************************
 *								*
 * Copyright (c) 2001-2022 Fidelity National Information	*
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

#include "stringpool.h"
#include "op.h"

GBLREF spdesc stringpool;
error_def(ERR_MAXSTRLEN);

#ifdef UTF8_SUPPORTED
#include "gtm_utf8.h"
GBLREF	boolean_t	badchar_inhibit;

void op_fnj2(mval *src, int len, mval *dst)
{
	unsigned char 	*cp;
	int 		n, size;

	if (MAX_STRLEN < len)
		RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_MAXSTRLEN);
	else if (0 > len)
		len = 0;

	MV_FORCE_STR(src);
	MV_FORCE_LEN(src);
	n = len - src->str.char_len;
	if (n <= 0)
	{
		*dst = *src;
		dst->mvtype &= ~MV_ALIASCONT;	/* Make sure alias container property does not pass */
	} else
	{
		size = src->str.len + n;
		if (size > MAX_STRLEN)
			RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_MAXSTRLEN);
		ENSURE_STP_FREE_SPACE(size);
		cp = stringpool.free;
		stringpool.free += size;
		memset(cp, SP, n);
		memcpy(cp + n, src->str.addr, src->str.len);
		MV_INIT_STRING(dst, size, (char *)cp);
	}
	return;
}
#endif /* UTF8_SUPPORTED */

void op_fnzj2(mval *src, int len, mval *dst)
{
	unsigned char	*cp;
	int 		n;

	if (MAX_STRLEN < len)
		RTS_ERROR_CSA_ABT(NULL, VARLSTCNT(1) ERR_MAXSTRLEN);
	else if (0 > len)
		len = 0;

	MV_FORCE_STR(src);
	n = len - src->str.len;
	if (n <= 0)
	{
		*dst = *src;
		dst->mvtype &= ~MV_ALIASCONT;	/* Make sure alias container property does not pass */
	} else
	{
		ENSURE_STP_FREE_SPACE(len);
		cp = stringpool.free;
		stringpool.free += len;
		memset(cp, SP, n);
		memcpy(cp + n, src->str.addr, src->str.len);
		MV_INIT_STRING(dst, len, (char *)cp);
	}
	return;
}
