#include <postgres.h>
#include <fmgr.h>
#include <utils/builtins.h>

#include "uint.h"

#include <inttypes.h>
#include <limits.h>

#if PG_VERSION_NUM >= 150000
/*
 * Sourced from postgresql-14.7/src/backend/utils/adt/numutils.c
 * pg_atoi: convert string to integer
 *
 * allows any number of leading or trailing whitespace characters.
 *
 * 'size' is the sizeof() the desired integral result (1, 2, or 4 bytes).
 *
 * c, if not 0, is a terminator character that may appear after the
 * integer (plus whitespace).  If 0, the string must end after the integer.
 *
 * Unlike plain atoi(), this will throw ereport() upon bad input format or
 * overflow.
 */
static int32
pg_atoi(const char *s, int size, int c)
{
        long            l;
        char       *badp;

        /*
         * Some versions of strtol treat the empty string as an error, but some
         * seem not to.  Make an explicit test to be sure we catch it.
         */
        if (s == NULL)
                elog(ERROR, "NULL pointer");
        if (*s == 0)
                ereport(ERROR,
                                (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                                 errmsg("invalid input syntax for type %s: \"%s\"",
                                                "integer", s)));

        errno = 0;
        l = strtol(s, &badp, 10);

        /* We made no progress parsing the string, so bail out */
        if (s == badp)
                ereport(ERROR,
                                (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                                 errmsg("invalid input syntax for type %s: \"%s\"",
                                                "integer", s)));

        switch (size)
        {
                case sizeof(int32):
                        if (errno == ERANGE
#if defined(HAVE_LONG_INT_64)
                        /* won't get ERANGE on these with 64-bit longs... */
                                || l < INT_MIN || l > INT_MAX
#endif
                                )
                                ereport(ERROR,
                                                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                                                 errmsg("value \"%s\" is out of range for type %s", s,
                                                                "integer")));
                        break;
                case sizeof(int16):
                        if (errno == ERANGE || l < SHRT_MIN || l > SHRT_MAX)
                                ereport(ERROR,
                                                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                                                 errmsg("value \"%s\" is out of range for type %s", s,
                                                                "smallint")));
                        break;
                case sizeof(int8):
                        if (errno == ERANGE || l < SCHAR_MIN || l > SCHAR_MAX)
                                ereport(ERROR,
                                                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                                                 errmsg("value \"%s\" is out of range for 8-bit integer", s)));
                        break;
                default:
                        elog(ERROR, "unsupported result size: %d", size);
        }

        /*
         * Skip any trailing whitespace; if anything but whitespace remains before
         * the terminating character, bail out
         */
        while (*badp && *badp != c && isspace((unsigned char) *badp))
                badp++;

        if (*badp && *badp != c)
                ereport(ERROR,
                                (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                                 errmsg("invalid input syntax for type %s: \"%s\"",
                                                "integer", s)));

        return (int32) l;
}
#endif

PG_FUNCTION_INFO_V1(int1in);
Datum
int1in(PG_FUNCTION_ARGS)
{
	char	   *s = PG_GETARG_CSTRING(0);

	PG_RETURN_INT8(pg_atoi(s, sizeof(int8), '\0'));
}

PG_FUNCTION_INFO_V1(int1out);
Datum
int1out(PG_FUNCTION_ARGS)
{
	int8		arg1 = PG_GETARG_INT8(0);
	char	   *result = palloc(5);		/* sign, 3 digits, '\0' */

	sprintf(result, "%d", arg1);
	PG_RETURN_CSTRING(result);
}

static uint32
pg_atou(const char *s, int size)
{
	unsigned long int result;
	bool		out_of_range = false;
	char	   *badp;

	if (s == NULL)
		elog(ERROR, "NULL pointer");
	if (*s == 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
				 errmsg("invalid input syntax for unsigned integer: \"%s\"",
						s)));

	if (strchr(s, '-'))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
				 errmsg("invalid input syntax for unsigned integer: \"%s\"",
						s)));

	errno = 0;
	result = strtoul(s, &badp, 10);

	switch (size)
	{
		case sizeof(uint32):
			if (errno == ERANGE
#if defined(HAVE_LONG_INT_64)
				|| result > UINT_MAX
				#endif
				)
				out_of_range = true;
			break;
		case sizeof(uint16):
			if (errno == ERANGE || result > USHRT_MAX)
				out_of_range = true;
			break;
		case sizeof(uint8):
			if (errno == ERANGE || result > UCHAR_MAX)
				out_of_range = true;
			break;
		default:
			elog(ERROR, "unsupported result size: %d", size);
	}

	if (out_of_range)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("value \"%s\" is out of range for type uint%d", s, size)));

	while (*badp && isspace((unsigned char) *badp))
		badp++;

	if (*badp)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
				 errmsg("invalid input syntax for unsigned integer: \"%s\"",
						s)));

	return result;
}

PG_FUNCTION_INFO_V1(uint1in);
Datum
uint1in(PG_FUNCTION_ARGS)
{
	char	   *s = PG_GETARG_CSTRING(0);

	PG_RETURN_UINT8(pg_atou(s, sizeof(uint8)));
}

PG_FUNCTION_INFO_V1(uint1out);
Datum
uint1out(PG_FUNCTION_ARGS)
{
	uint8		arg1 = PG_GETARG_UINT8(0);
	char	   *result = palloc(4);		/* 3 digits, '\0' */

	sprintf(result, "%u", arg1);
	PG_RETURN_CSTRING(result);
}

PG_FUNCTION_INFO_V1(uint2in);
Datum
uint2in(PG_FUNCTION_ARGS)
{
	char	   *s = PG_GETARG_CSTRING(0);

	PG_RETURN_UINT16(pg_atou(s, sizeof(uint16)));
}

PG_FUNCTION_INFO_V1(uint2out);
Datum
uint2out(PG_FUNCTION_ARGS)
{
	uint16		arg1 = PG_GETARG_UINT16(0);
	char	   *result = palloc(6);		/* 5 digits, '\0' */

	sprintf(result, "%u", arg1);
	PG_RETURN_CSTRING(result);
}

PG_FUNCTION_INFO_V1(uint4in);
Datum
uint4in(PG_FUNCTION_ARGS)
{
	char	   *s = PG_GETARG_CSTRING(0);

	PG_RETURN_UINT32(pg_atou(s, sizeof(uint32)));
}

PG_FUNCTION_INFO_V1(uint4out);
Datum
uint4out(PG_FUNCTION_ARGS)
{
	uint32		arg1 = PG_GETARG_UINT32(0);
	char	   *result = palloc(11);	/* 10 digits, '\0' */

	sprintf(result, "%u", arg1);
	PG_RETURN_CSTRING(result);
}

PG_FUNCTION_INFO_V1(uint8in);
Datum
uint8in(PG_FUNCTION_ARGS)
{
	char	   *s = PG_GETARG_CSTRING(0);
	unsigned long long int result;
	char	   *badp;

	if (s == NULL)
		elog(ERROR, "NULL pointer");
	if (*s == 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
				 errmsg("invalid input syntax for unsigned integer: \"%s\"",
						s)));

	if (strchr(s, '-'))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
				 errmsg("invalid input syntax for unsigned integer: \"%s\"",
						s)));

	errno = 0;
	result = strtoull(s, &badp, 10);

	if (errno == ERANGE)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("value \"%s\" is out of range for type uint%d", s, 8)));

	while (*badp && isspace((unsigned char) *badp))
		badp++;

	if (*badp)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
				 errmsg("invalid input syntax for unsigned integer: \"%s\"",
						s)));

	PG_RETURN_UINT64(result);
}

PG_FUNCTION_INFO_V1(uint8out);
Datum
uint8out(PG_FUNCTION_ARGS)
{
	uint64		arg1 = PG_GETARG_UINT64(0);
	char	   *result = palloc(21);	/* 20 digits, '\0' */

	sprintf(result, "%"PRIu64, (uint64_t) arg1);
	PG_RETURN_CSTRING(result);
}
