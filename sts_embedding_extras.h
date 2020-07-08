/* this file is released into the public domain */

/* this file isnt a critical part of the interpreter.
It is just heplful to use when embedding */


#ifndef STS_EMBEDDING_EXTRAS_H__
#define STS_EMBEDDING_EXTRAS_H__

#include "simpletinyscript.h"


sts_value_t *sts_value_create(sts_script_t *script, unsigned int type);

sts_value_t *sts_value_from_nstring(sts_script_t *script, char *string, unsigned int size);

sts_value_t *sts_value_from_string(sts_script_t *script, char *string);

sts_value_t *sts_value_from_number(sts_script_t *script, double num);

void sts_array_resize(sts_value_t *value, unsigned int size);

void sts_array_append_insert(sts_value_t *value_ptr, sts_value_t *value_insert, unsigned int position);

int sts_array_remove(sts_script_t *script, sts_value_t *value_ptr, unsigned int position);

unsigned int sts_hash(void *data, unsigned int size);

void sts_string_assemble(char **dest, unsigned int current_size, char *middle_str, unsigned int middle_size, char *end_str, unsigned int end_size);

int sts_destroy_map(sts_map_row_t *row);


#ifdef STS_EMBEDDING_EXTRAS_IMPLEMENTATION

#define STS_IMPLEMENTATION
#include "simpletinyscript.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


sts_value_t *sts_value_create(sts_script_t *script, unsigned int type)
{
	sts_value_t *ret = NULL;


	if(!STS_CREATE_VALUE(ret))
	{
		fprintf(stderr, "could not create value of type %u\n", type);
		return NULL;
	}

	ret->references = 1;
	ret->type = type;

	return ret;
}

sts_value_t *sts_value_from_nstring(sts_script_t *script, char *string, unsigned int size)
{
	sts_value_t *ret = NULL;


	if(!(ret = sts_value_create(script, STS_STRING)))
	{
		fprintf(stderr, "could not create string value\n");
		return NULL;
	}

	if(!(ret->string.data = (char *)sts_memdup(string, size)))
	{
		fprintf(stderr, "could not duplicate string value\n");
		STS_FREE(ret);
		return NULL;
	}

	ret->string.length = size;

	return ret;
}

sts_value_t *sts_value_from_string(sts_script_t *script, char *string)
{
	return sts_value_from_nstring(script, string, strlen(string));
}

sts_value_t *sts_value_from_number(sts_script_t *script, double num)
{
	sts_value_t *ret = NULL;


	if(!(ret = sts_value_create(script, STS_NUMBER)))
	{
		fprintf(stderr, "could not create number value\n");
		return NULL;
	}

	ret->number = num;

	return ret;
}

void sts_array_resize(sts_value_t *value, unsigned int size)
{
	STS_ARRAY_RESIZE(value, size);
}

void sts_array_append_insert(sts_value_t *value_ptr, sts_value_t *value_insert, unsigned int position)
{
	STS_ARRAY_APPEND_INSERT(value_ptr, value_insert, position);
}

int sts_array_remove(sts_script_t *script, sts_value_t *value_ptr, unsigned int position)
{
	STS_ARRAY_REMOVE(script, value_ptr, position, {STS_ERROR_SIMPLE("could not remove value in array"); return 0;});

	return 1;
}

unsigned int sts_hash(void *data, unsigned int size)
{
	unsigned int ret = 0;

	STS_HASH(ret, data, size);

	return ret;
}

void sts_string_assemble(char **dest, unsigned int current_size, char *middle_str, unsigned int middle_size, char *end_str, unsigned int end_size)
{
	STS_STRING_ASSEMBLE(*dest, current_size, middle_str, middle_size, end_str, end_size);
}

int sts_destroy_map(sts_map_row_t *row)
{
	STS_DESTROY_MAP(row, 0);

	return 1;
}

#endif

#endif /* end STS_EMBEDDING_EXTRAS_H__ */