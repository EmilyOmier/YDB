/****************************************************************
 *								*
 * Copyright (c) 2001-2019 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2017-2019 YottaDB LLC and/or its subsidiaries.	*
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

#include "compiler.h"
#include "stringpool.h"
#include "stp_parms.h"

<<<<<<< HEAD
GBLREF mstr		**stp_array;
GBLREF gtm_uint64_t	stp_array_size;

void stp_expand_array(void)
{
	mstr		**a;
	gtm_uint64_t	n;

	n = stp_array_size;
	stp_array_size = ((MAXINT4 > n) ? (n * 2) : (n + MAXINT4));
=======
GBLREF mstr **stp_array;
GBLREF uint4 stp_array_size;

void stp_expand_array(void)
{
	mstr **a;
	uint4 n;

	n = stp_array_size;
	stp_array_size += STP_MAXITEMS;
	assert(stp_array_size > n);
>>>>>>> 91552df2... GT.M V6.3-009
	a = stp_array;
	stp_array = (mstr **)malloc(stp_array_size * SIZEOF(mstr *));
	memcpy((uchar_ptr_t)stp_array, (uchar_ptr_t)a, n * SIZEOF(mstr *));
	free(a);
	return;
}
