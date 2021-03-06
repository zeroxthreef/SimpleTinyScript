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

void sts_array_resize(sts_script_t *script, sts_value_t *value, unsigned int size);

void sts_array_append_insert(sts_script_t *script, sts_value_t *value_ptr, sts_value_t *value_insert, unsigned int position);

int sts_array_remove(sts_script_t *script, sts_value_t *value_ptr, unsigned int position);

unsigned int sts_hash(void *data, unsigned int size);

void sts_string_assemble(sts_script_t *script, char **dest, unsigned int current_size, char *middle_str, unsigned int middle_size, char *end_str, unsigned int end_size);

int sts_destroy_map(sts_script_t *script, sts_map_row_t *row);

sts_scope_t *sts_scope_push(sts_script_t *script, sts_scope_t *scope);

sts_scope_t *sts_scope_pop(sts_script_t *script, sts_scope_t *scope);

sts_map_row_t *sts_scope_search(sts_script_t *script, sts_scope_t *scope, void *key, unsigned int key_size);

#endif /* end STS_EMBEDDING_EXTRAS_H__ */

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

void sts_array_resize(sts_script_t *script, sts_value_t *value, unsigned int size)
{
	STS_ARRAY_RESIZE(value, size);
}

void sts_array_append_insert(sts_script_t *script, sts_value_t *value_ptr, sts_value_t *value_insert, unsigned int position)
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

void sts_string_assemble(sts_script_t *script, char **dest, unsigned int current_size, char *middle_str, unsigned int middle_size, char *end_str, unsigned int end_size)
{
	STS_STRING_ASSEMBLE(*dest, current_size, middle_str, middle_size, end_str, end_size);
}

int sts_destroy_map(sts_script_t *script, sts_map_row_t *row)
{
	if(!row) return 1;
	
	STS_DESTROY_MAP(row, {return 0;});

	return 1;
}

sts_scope_t *sts_scope_push(sts_script_t *script, sts_scope_t *scope)
{
	sts_scope_t *ret = scope;

	STS_SCOPE_PUSH(ret, {return NULL;});

	return ret;
}

sts_scope_t *sts_scope_pop(sts_script_t *script, sts_scope_t *scope)
{
	sts_scope_t *ret = scope;

	STS_SCOPE_POP(ret, {return NULL;});

	return ret;
}

sts_map_row_t *sts_scope_search(sts_script_t *script, sts_scope_t *scope, void *key, unsigned int key_size)
{
	sts_map_row_t *ret = NULL;

	STS_SCOPE_SEARCH(scope, key, key_size, ret, {});

	return ret;
}

int sts_hashmap_set(sts_script_t *script, sts_value_t *hashmap, char *key, unsigned long key_length, sts_value_t *value)
{
	/*
	# hashmap structure:
	#   [array
	#       [array *hash* *key string* *value*]
	#       [array *hash* *key string* *value*]
	#       ...
	#   ]
	*/

	sts_value_t *row_val = NULL, *hash_val = NULL, *key_val = NULL;
	unsigned long hash = 0;


	if(!hashmap || hashmap->type != STS_ARRAY)
	{
		STS_ERROR_SIMPLE("hashmap value is null or not array");
		return 1;
	}

	if(!key)
	{
		STS_ERROR_SIMPLE("key is null");
		return 1;
	}

	if(!value)
	{
		STS_ERROR_SIMPLE("value value is null");
		return 1;
	}

	/* create the parts */

	STS_HASH(hash, key, key_length);

	if(!(hash_val = sts_value_from_number(script, (double)hash)))
	{
		STS_ERROR_SIMPLE("could not create hash value from number");
		return 1;
	}


	if(!(key_val = sts_value_from_nstring(script, key, key_length)))
	{
		STS_ERROR_SIMPLE("could not create string value from key str");
		return 1;
	}

	/* create the row */

	if(!(row_val = sts_value_create(script, STS_ARRAY)))
	{
		STS_ERROR_SIMPLE("could not create row for hashmap");
		return 1;
	}

	sts_array_append_insert(script, row_val, hash_val, 0);
	sts_array_append_insert(script, row_val, key_val, 1);
	sts_array_append_insert(script, row_val, value, 2);

	/* add the row */

	sts_array_append_insert(script, hashmap, row_val, hashmap->array.length);



	return 0;
}

int sts_hashmap_del(sts_script_t *script, sts_value_t *hashmap, char *key, unsigned long key_length)
{
	/*
	# hashmap structure:
	#   [array
	#       [array *hash* *key string* *value*]
	#       [array *hash* *key string* *value*]
	#       ...
	#   ]
	*/

	sts_value_t *row = NULL;
	unsigned long i, hash = 0;
	double converted_hash;


	STS_HASH(hash, key, key_length);

	converted_hash = (double)hash;


	if(!hashmap || hashmap->type != STS_ARRAY)
	{
		STS_ERROR_SIMPLE("hashmap value is null or not array");
		return 1;
	}

	if(!key)
	{
		STS_ERROR_SIMPLE("key is null");
		return 1;
	}


	for(i = 0; i < hashmap->array.length; ++i)
	{
		if(hashmap->array.data[i]->type != STS_ARRAY && !hashmap->array.data[i]->array.length)
		{
			STS_ERROR_SIMPLE("invalid row in hashmap");
			return 1;
		}

		if(hashmap->array.data[i]->array.data[0]->type != STS_NUMBER)
		{
			STS_ERROR_SIMPLE("invalid row hash index in hashmap");
			return 1;
		}

		if(hashmap->array.data[i]->array.data[0]->type != STS_NUMBER)
		{
			STS_ERROR_SIMPLE("invalid row hash index in hashmap");
			return 1;
		}

		if(hashmap->array.data[i]->array.data[0]->number == converted_hash)
		{
			row = hashmap->array.data[i];

			/* remove the pointer from the array */
			sts_array_remove(script, hashmap, i);

			/* refdec the row */
			if(!sts_value_reference_decrement(script, row))
			{
				STS_ERROR_SIMPLE("could not refdec row in hashmap");
				return 1;
			}
		}
	}


	return 0;
}

sts_value_t *sts_hashmap_get(sts_script_t *script, sts_value_t *hashmap, char *key, unsigned long key_length)
{
	/*
	# hashmap structure:
	#   [array
	#       [array *hash* *key string* *value*]
	#       [array *hash* *key string* *value*]
	#       ...
	#   ]
	*/

	sts_value_t *row = NULL, *ret = NULL;
	unsigned long i, hash = 0;
	double converted_hash;


	STS_HASH(hash, key, key_length);

	converted_hash = (double)hash;


	if(!hashmap || hashmap->type != STS_ARRAY)
	{
		STS_ERROR_SIMPLE("hashmap value is null or not array");
		return ret;
	}

	if(!key)
	{
		STS_ERROR_SIMPLE("key is null");
		return ret;
	}


	for(i = 0; i < hashmap->array.length; ++i)
	{
		if(hashmap->array.data[i]->type != STS_ARRAY && !hashmap->array.data[i]->array.length)
		{
			STS_ERROR_SIMPLE("invalid row in hashmap");
			return ret;
		}

		if(hashmap->array.data[i]->array.data[0]->type != STS_NUMBER)
		{
			STS_ERROR_SIMPLE("invalid row hash index in hashmap");
			return ret;
		}

		if(hashmap->array.data[i]->array.data[0]->type != STS_NUMBER)
		{
			STS_ERROR_SIMPLE("invalid row hash index in hashmap");
			return ret;
		}

		if(hashmap->array.data[i]->array.data[0]->number == converted_hash)
		{
			ret = hashmap->array.data[i];
			break;
		}
	}


	return ret;
}

#endif