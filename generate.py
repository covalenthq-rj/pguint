import re


new_types = ['int1', 'uint1', 'uint2', 'uint4', 'uint8']
old_types = ['int2', 'int4', 'int8']

comparison_ops = ['<', '<=', '=', '<>', '>=', '>']
arithmetic_ops = ['+', '-', '*', '/', '%']

op_words = {
    '<': 'lt', '<=': 'le', '=': 'eq', '<>': 'ne', '>=': 'ge', '>': 'gt',
    '+': 'pl', '-': 'mi', '*': 'mul', '/': 'div', '%': 'mod',
    '&': 'and', '|': 'or', '#': 'xor', '~': 'not', '<<': 'shl', '>>': 'shr',
}

c_types = {
    'boolean': 'bool',
    'int1': 'int8',
    'int2': 'int16',
    'int4': 'int32',
    'int8': 'int64',
    'uint1': 'uint8',
    'uint2': 'uint16',
    'uint4': 'uint32',
    'uint8': 'uint64',
}


def c_operator(op):
    if op == '=':
        return '=='
    elif op == '<>':
        return '!='
    elif op == '#':
        return '^'
    else:
        return op


def type_bits(typ):
    m = re.search(r'(\d+)', typ)
    return int(m.group(1)) * 8


def type_unsigned(typ):
    return typ.startswith('u')


max_values = {
    'int1': '127',
    'int2': '32767',
    'int4': '2147483647',
    'int8': '9223372036854775807',
    'uint1': '255',
    'uint2': '65535',
    'uint4': '4294967295',
    'uint8': '18446744073709551615',
}


def next_bigger_type(typ):
    m = re.match(r'(\w+)(\d+)', typ)
    return m.group(1) + str(int(m.group(2)) * 2)


f_operators_c = open('operators.c', 'w')
f_operators_sql = open('operators.sql', 'w')
f_test_operators_sql = open('test/sql/operators.sql', 'w')

f_operators_c.write('#include <postgres.h>\n')
f_operators_c.write('#include <fmgr.h>\n\n')
f_operators_c.write('#include "uint.h"\n\n')


def write_c_function(f, funcname, leftarg, rightarg, op, rettype, c_check='', intermediate_type=None):
    f.write("""
PG_FUNCTION_INFO_V1(%s);
Datum
%s(PG_FUNCTION_ARGS)
{
""" % (funcname, funcname))
    argcount = 0
    if leftarg:
        f.write("\t%s arg1 = PG_GETARG_%s(%d);\n" % (c_types[leftarg], c_types[leftarg].upper(), argcount))
        argcount += 1
    if rightarg:
        f.write("\t%s arg2 = PG_GETARG_%s(%d);\n" % (c_types[rightarg], c_types[rightarg].upper(), argcount))
        argcount += 1
    f.write("\t%s result = " % c_types[intermediate_type or rettype])
    if leftarg:
        if intermediate_type:
            f.write("(%s)" % c_types[intermediate_type])
        f.write("arg1")
    f.write(" " + c_operator(op) + " ")
    if rightarg:
        if intermediate_type:
            f.write("(%s)" % c_types[intermediate_type])
        f.write("arg2")
    f.write(";\n")
    if c_check:
        f.write("""
	if (%s)
		ereport(ERROR,
			(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
			 errmsg("integer out of range")));
""" % c_check)

    f.write("""
	PG_RETURN_%s(result);
}
""" % c_types[rettype].upper())


def write_sql_operator(f, funcname, leftarg, rightarg, op, rettype):
    f.write("CREATE FUNCTION %s(%s) RETURNS %s IMMUTABLE LANGUAGE C AS '$libdir/uint', '%s';\n\n" \
            % (funcname, ', '.join([x for x in [leftarg, rightarg] if x]), rettype, funcname))

    f.write("CREATE OPERATOR %s (\n" % op)
    if leftarg:
        f.write("    LEFTARG = %s,\n" % leftarg)
    if rightarg:
        f.write("    RIGHTARG = %s,\n" % rightarg)
    f.write("    PROCEDURE = %s\n);\n" % funcname)


def coalesce(*args):
    return next((a for a in args if a is not None), None)


def write_code(f_c, f_sql, leftarg, rightarg, op, rettype, c_check='', intermediate_type=None):
    funcname = coalesce(leftarg, '') + coalesce(rightarg, '') + op_words[op]
    write_c_function(f_c, funcname, leftarg, rightarg, op, rettype, c_check, intermediate_type)
    write_sql_operator(f_sql, funcname, leftarg, rightarg, op, rettype)


for leftarg in new_types + old_types:
    for op in comparison_ops + arithmetic_ops:
        f_test_operators_sql.write("SELECT '2'::%s %s 5;\n" % (leftarg, op))
        f_test_operators_sql.write("SELECT 2 %s '5'::%s;\n" % (op, leftarg))
        f_test_operators_sql.write("SELECT '5'::%s %s 2;\n" % (leftarg, op))
        f_test_operators_sql.write("SELECT 5 %s '2'::%s;\n" % (op, leftarg))
    for rightarg in new_types + old_types:
        if leftarg in old_types and rightarg in old_types:
            continue
        for op in comparison_ops:
            write_code(f_operators_c, f_operators_sql, leftarg, rightarg, op, rettype='boolean')
            f_test_operators_sql.write("SELECT '1'::%s %s '1'::%s;\n" % (leftarg, op, rightarg))
            f_test_operators_sql.write("SELECT '5'::%s %s '2'::%s;\n" % (leftarg, op, rightarg))
            f_test_operators_sql.write("SELECT '3'::%s %s '4'::%s;\n" % (leftarg, op, rightarg))
        f_test_operators_sql.write("\n")

        for op in arithmetic_ops:
            args = sorted([leftarg, rightarg], key=lambda x: (type_bits(x), type_unsigned(x)))
            rettype = args[-1]
            if type_unsigned(rettype):
                if op == '+':
                    c_check = 'result < arg1 || result < arg2'
                elif op == '-':
                    c_check = 'result > arg1'
                else:
                    c_check = ''
            else:
                if op == '+':
                    c_check = 'SAMESIGN(arg1, arg2) && !SAMESIGN(result, arg1)'
                elif op == '-':
                    c_check = '!SAMESIGN(arg1, arg2) && !SAMESIGN(result, arg1)'
                else:
                    c_check = ''
            if op == '*':
                if type_bits(rettype) < 64:
                    intermediate_type = next_bigger_type(rettype)
                    c_check = '(%s) result != result' % c_types[rettype]
                elif type_unsigned(rettype):
                    c_check = '(arg1 != ((uint32) arg1) || arg2 != ((uint32) arg2)) && (arg2 != 0 && ((arg2 == -1 && arg1 < 0 && result < 0) || result / arg2 != arg1))'
                else:
                    c_check = '(arg1 != ((int32) arg1) || arg2 != ((int32) arg2)) && (arg2 != 0 && ((arg2 == -1 && arg1 < 0 && result < 0) || result / arg2 != arg1))'
            else:
                intermediate_type = None
            write_code(f_operators_c, f_operators_sql, leftarg, rightarg, op, rettype, c_check, intermediate_type)
            f_test_operators_sql.write("SELECT pg_typeof('1'::%s %s '1'::%s);\n" % (leftarg, op, rightarg))
            f_test_operators_sql.write("SELECT '1'::%s %s '1'::%s;\n" % (leftarg, op, rightarg))
            f_test_operators_sql.write("SELECT '3'::%s %s '4'::%s;\n" % (leftarg, op, rightarg))
            f_test_operators_sql.write("SELECT '5'::%s %s '2'::%s;\n" % (leftarg, op, rightarg))
            if op in ['+', '*']:
                f_test_operators_sql.write("SELECT '%s'::%s %s '%s'::%s;\n" % (max_values[leftarg], leftarg, op, max_values[rightarg], rightarg))
        f_test_operators_sql.write("\n")

for arg in new_types:
    for op in ['&', '|','#']:
        write_code(f_operators_c, f_operators_sql, leftarg=arg, rightarg=arg, op=op, rettype=arg)
        f_test_operators_sql.write("SELECT '1'::%s %s '1'::%s;\n" % (arg, op, arg))
        f_test_operators_sql.write("SELECT '5'::%s %s '2'::%s;\n" % (arg, op, arg))
        f_test_operators_sql.write("SELECT '5'::%s %s '4'::%s;\n" % (arg, op, arg))
    for op in ['~']:
        write_code(f_operators_c, f_operators_sql, leftarg=None, rightarg=arg, op=op, rettype=arg)
        f_test_operators_sql.write("SELECT %s '6'::%s;\n" % (op, arg))
    for op in ['<<', '>>']:
        write_code(f_operators_c, f_operators_sql, leftarg=arg, rightarg='int4', op=op, rettype=arg)
        f_test_operators_sql.write("SELECT '6'::%s %s 1;\n" % (arg, op))
        f_test_operators_sql.write("SELECT '6'::%s %s 3;\n" % (arg, op))

    f_test_operators_sql.write("\n")


f_operators_c.close()
f_operators_sql.close()
f_test_operators_sql.close()
