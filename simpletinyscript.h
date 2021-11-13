/* this file is released into the public domain */

#ifndef SIMPLETINYSCRIPT_H__
#define SIMPLETINYSCRIPT_H__


/* syntax
 *
 * comment #..\n,\r
 *
 * value literals:
 * string: "...", anything that isnt whitespace or any other literal
 * - an identifier is a string with a $ prefix
 * number: (+,-)0-9,.
 * end of expression: \n,\r,;
 * list: [...], {...}, (...)
 *
*/

enum sts_value_types
{
	STS_EXTERNAL,
	STS_NIL,
	STS_NUMBER,
	STS_STRING,
	STS_ARRAY,
	STS_FUNCTION
};

enum sts_node_types
{
	STS_NODE_EXPRESSION,
	STS_NODE_VALUE,
	STS_NODE_IDENTIFIER
};

enum sts_row_types
{
	STS_ROW_VALUE,
	STS_ROW_VOID
};

/* typedefs */

typedef struct sts_script_t sts_script_t;
typedef struct sts_value_t sts_value_t;
typedef struct sts_node_t sts_node_t;
typedef struct sts_ast_container_t sts_ast_container_t;
typedef struct sts_name_container_t sts_name_container_t;
typedef struct sts_map_row_t sts_map_row_t;
typedef struct sts_scope_t sts_scope_t;

/* structures */

struct sts_scope_t
{
	sts_scope_t *uplevel;
	sts_map_row_t *locals;
};

struct sts_map_row_t
{
	sts_map_row_t *next;
	unsigned int hash;
	char type;
	void *value;
};

struct sts_node_t
{
	sts_node_t *next, *child;
	char type;
	sts_value_t *value;
	unsigned int line;
	sts_name_container_t *name;
	#ifdef STS_GOTO_JIT
	void *label, *router_id;
	#endif
};

struct sts_ast_container_t
{
	sts_node_t *node;
	unsigned int references;
};

struct sts_name_container_t
{
	char *script_name;
	unsigned int references;
};

struct sts_value_t
{
	char type;
	unsigned int references;
	union
	{
		struct
		{
			void *data_ptr, *data_other; /* able to store qualities about the external type without having to make a container */
			int (*refdec)(sts_script_t *script, sts_value_t *value);
			void (*refinc)(sts_script_t *script, sts_value_t *value);
		} external;
		double number;
		struct
		{
			char *data;
			unsigned int length;
		} string;
		struct
		{
			unsigned int length, allocated;
			sts_value_t **data;
		} array;
		struct
		{
			sts_value_t *argument_identifiers; /* this is an array. Not a double ptr array for a reason */
			sts_ast_container_t *body;
		} function;
	};
};

struct sts_script_t
{
	char *name;
	void *userdata;
	sts_node_t *script;
	sts_scope_t *globals;
	char *(*read_file)(sts_script_t *script, char *file, unsigned int *size);
	char *(*import_file)(sts_script_t *script, char *file);
	sts_value_t *(*router)(sts_script_t *script, sts_value_t *action, sts_node_t *args, sts_scope_t *locals, sts_value_t **previous);
};

/* function prototypes */

/* parse text into a list of values */
sts_node_t *sts_parse(sts_script_t *script, sts_node_t *parent, char *script_text, char *script_name, unsigned int *offset, unsigned int *line);

/* evaluate value/value list */
sts_value_t *sts_eval(sts_script_t *script, sts_node_t *ast, sts_scope_t *locals, sts_value_t **previous, int single, int newscope);

/* cleanup */
int sts_destroy(sts_script_t *script);

/* the default functions and behavior */
sts_value_t *sts_defaults(sts_script_t *script, sts_value_t *action, sts_node_t *args, sts_scope_t *locals, sts_value_t **previous);

/* apply a name container to the ast */
int sts_ast_apply_name(sts_script_t *script, sts_node_t *node, sts_name_container_t *name);

/* copy an ast from the provided node */
sts_node_t *sts_ast_copy(sts_script_t *script, sts_node_t *node);

/* delete an ast */
void sts_ast_delete(sts_script_t *script, sts_node_t *node);

/* decrement references recursively */
int sts_value_reference_decrement(sts_script_t *script, sts_value_t *value);

/* copy values just once or recursively */
int sts_value_copy(sts_script_t *script, sts_value_t *dest, sts_value_t *source, int recursive);

/* test if value is "true" or "false". 1 is true, 0 is false */
int sts_value_test(sts_value_t *value);

/* simple hash map functions */
sts_map_row_t *sts_map_add_set(sts_map_row_t **row, void *key, unsigned int key_size, void *value);
sts_map_row_t *sts_map_get(sts_map_row_t **row, void *key, unsigned int key_size);
int sts_map_remove(sts_map_row_t **row, void *key, unsigned int key_size);

/* duplicates chunks of memory */
void *sts_memdup(void *src, unsigned int size);


#endif

#ifdef STS_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <math.h>

/* config */

#ifndef STS_ERROR_PRINT
	#define STS_ERROR_PRINT fprintf
#endif
#ifndef STS_ERROR_PRINT_ARG0
	#define STS_ERROR_PRINT_ARG0 stderr,
#endif
#ifndef STS_ERROR_CONCAT
	#define STS_ERROR_CONCAT "\n"
#endif
#define STS_ERROR_SIMPLE(str) STS_ERROR_PRINT(STS_ERROR_PRINT_ARG0 str STS_ERROR_CONCAT)

#ifndef STS_CALLOC
	#define STS_CALLOC calloc
#endif

#ifndef STS_REALLOC
	#define STS_REALLOC realloc
#endif

#ifndef STS_FNV_PRIME
	#define STS_FNV_PRIME 0x01000193u /* 32-bit version */
#endif

#ifndef STS_FNV_OFFSET
	#define STS_FNV_OFFSET 0x811c9dc5u /* 32-bit version */
#endif

#ifndef STS_FREE
	#define STS_FREE free
#endif
/* util macros */

#define STS_VALUE_REFINC(script_ptr, value_ptr) do{value_ptr->references++;}while(0)

#define STS_CREATE_VALUE(value_ptr) (value_ptr = STS_CALLOC(1, sizeof(sts_value_t)))

#define STS_CREATE_NODE(node_ptr) (node_ptr = STS_CALLOC(1, sizeof(sts_node_t)))

#define STS_CREATE_ROW(row_ptr) (row_ptr = STS_CALLOC(1, sizeof(sts_map_row_t)))

#define STS_ARRAY_RESIZE(value_ptr, size) do{	\
		if(!((value_ptr)->array.data = STS_REALLOC((value_ptr)->array.data, (size) * sizeof(sts_value_t **)))) {STS_ERROR_SIMPLE("could not resize array");}	\
		else (value_ptr)->array.allocated = (size);	\
	}while(0)

#define STS_ARRAY_APPEND_INSERT(value_ptr, value_insert, position) do{	\
		if((value_ptr)->array.length + 1 > (value_ptr)->array.allocated) STS_ARRAY_RESIZE((value_ptr), (value_ptr)->array.length + 1);	\
		if(position >= (value_ptr)->array.length) (value_ptr)->array.data[(value_ptr)->array.length] = (value_insert);	\
		else{ memmove(&(value_ptr)->array.data[position + 1], &(value_ptr)->array.data[position], ((value_ptr)->array.length - position) * sizeof(sts_value_t **)); (value_ptr)->array.data[position] = (value_insert);}	\
		(value_ptr)->array.length++;	\
	}while(0)

#define STS_ARRAY_REMOVE(script, value_ptr, position, on_error) do{	\
		if((position) >= (value_ptr)->array.length){ STS_ERROR_SIMPLE("position out of bounds"); on_error}	\
		else if((position) == (value_ptr)->array.length - 1){ if(!sts_value_reference_decrement(script, (value_ptr)->array.data[(position)])){ STS_ERROR_SIMPLE("could not refdec value at position specified"); on_error} (value_ptr)->array.length--;}	\
		else{ if(!sts_value_reference_decrement(script, (value_ptr)->array.data[(position)])){ STS_ERROR_SIMPLE("could not refdec value at position specified"); on_error} memmove(&(value_ptr)->array.data[(position)], &(value_ptr)->array.data[(position) + 1], ((value_ptr)->array.length - ((position) + 1)) * sizeof(sts_value_t **)); (value_ptr)->array.length--;}	\
	}while(0)

#define STS_HASH(variable, data, size) do{unsigned int i; for(i = 0; i < (size); ++i){(variable) ^= ((char *)(data))[i]; (variable) *= STS_FNV_PRIME;} }while(0)

#define STS_DESTROY_MAP(row, on_error) do{sts_map_row_t *current, *temp; current = row; do{	\
		if(current->value && current->type == STS_ROW_VALUE) if(!sts_value_reference_decrement(script, (sts_value_t *)current->value)){STS_ERROR_SIMPLE("could not decrement reference in map"); {on_error}}	\
		temp = current; current = current->next;	\
		STS_FREE(temp);	\
	}while(current); }while(0)

#define STS_STRING_ASSEMBLE(dest, current_size, middle_str, middle_size, end_str, end_size) do{	\
		if(!((dest) = STS_REALLOC((dest), (current_size) + (middle_size) + (end_size) + 1))) STS_ERROR_SIMPLE("could not resize assembled string");	\
		memmove(&(dest)[current_size], (middle_str), (middle_size));	\
		memmove(&(dest)[current_size + (middle_size)], (end_str), (end_size));	\
		(dest)[current_size + (middle_size) + (end_size)] = 0x0;	\
		current_size += (middle_size) + (end_size);	\
	}while(0)

#define STS_STRING_ASSEMBLE_FMT(dest, current_size, fmt, fmt_arg, end_str, end_size) do{char buf[512];	\
		snprintf(buf, 512, (fmt), (fmt_arg));	\
		STS_STRING_ASSEMBLE((dest), (current_size), buf, strlen(buf), (end_str), (end_size));	\
	}while(0)

#define STS_STRING_UNESCAPE_STRING(string, length) do{ unsigned int i, modified = 0, orig_len = 0;	\
		for(i = 0; i < (length); ++i){ orig_len = (length);	\
			if((string)[i] == '\\'){	\
				switch((string)[i + 1]){	\
					case 'a': ++modified; (string)[i] = 0x07; break;	\
					case 'b': ++modified; (string)[i] = 0x08; break;	\
					case 'e': ++modified; (string)[i] = 0x1B; break;	\
					case 'f': ++modified; (string)[i] = 0x0C; break;	\
					case 'n': ++modified; (string)[i] = 0x0A; break;	\
					case 'r': ++modified; (string)[i] = 0x0D; break;	\
					case 't': ++modified; (string)[i] = 0x09; break;	\
					case 'v': ++modified; (string)[i] = 0x0B; break;	\
					case '\\': ++modified; (string)[i] = 0x5C; break;	\
					case '\'': ++modified; (string)[i] = 0x27; break;	\
					case '\"': ++modified; (string)[i] = 0x22; break;	\
					case '\?': ++modified; (string)[i] = 0x3F; break;	\
					case '0': ++modified; (string)[i] = 0x00; break;	\
				}	\
				if(modified){ memmove(&(string)[i + 1], &(string)[i + 2], (length) - (i + 2)); (string)[orig_len - 1] = 0x0; (length)--;}	\
			} modified = 0;	\
		}	\
	}while(0)

#define STS_SCOPE_CREATE (STS_CALLOC(1, sizeof(sts_scope_t)))

/* can be used to initialize a root scope */
#define STS_SCOPE_PUSH(scope, on_error) do{ sts_scope_t *push_placeholder = NULL;	\
		if(!(scope)){	\
			if(!((scope) = STS_SCOPE_CREATE)){ STS_ERROR_SIMPLE("could not initialize scope"); {on_error}}	\
		}else{	\
			push_placeholder = (scope);	\
			if(!((scope) = STS_SCOPE_CREATE)){ STS_ERROR_SIMPLE("could not initialize new scope stack level"); (scope) = push_placeholder; {on_error}}	\
			(scope)->uplevel = push_placeholder;	\
		}	\
	}while(0)

#define STS_SCOPE_POP(scope, on_error) do{ sts_scope_t *pop_placeholder = NULL;	\
		if((scope)){	\
			if((scope)->locals) STS_DESTROY_MAP((scope)->locals, {on_error});	\
			pop_placeholder = (scope)->uplevel;	\
			STS_FREE((scope)); (scope) = pop_placeholder;	\
		}	\
	}while(0)

#define STS_SCOPE_SEARCH(scope, data, size, result, on_error) do{ sts_map_row_t *internal_row; sts_scope_t *internal_temp = (scope); if((scope)){	\
		do{	\
			if((internal_row = sts_map_get(&internal_temp->locals, (data), (size)))){	\
				(result) = internal_row; break;	\
			}	\
		} while((internal_temp = internal_temp->uplevel));	\
	}}while(0)

/* definitions */

sts_node_t *sts_parse(sts_script_t *script, sts_node_t *parent, char *script_text, char *script_name, unsigned int *offset, unsigned int *line)
{
	sts_node_t *container_node = NULL, *container_progress = NULL, *expression_node = NULL, *expression_progress = NULL, *temp_expression_node = NULL;
	unsigned int start = 0, break_out = 0;
	sts_value_t *value = NULL;
	sts_name_container_t *name = NULL;

	#define PARSER_ERROR(str) do{ STS_ERROR_PRINT(STS_ERROR_PRINT_ARG0 "parser error: line %u, " str STS_ERROR_CONCAT, *line); return NULL;}while(0)
	#define PARSER_SKIP_WHITESPACE() while(script_text[*offset] && script_text[*offset] != '\n' && isspace(script_text[*offset]))(*offset)++
	#define PARSER_SKIP_NOT_WHITESPACE() while(script_text[*offset] && script_text[(*offset) + 1] && script_text[(*offset) + 1] != ']' && script_text[(*offset) + 1] != '[' && script_text[(*offset) + 1] != ')' && script_text[(*offset) + 1] != '(' && script_text[(*offset) + 1] != '}' && script_text[(*offset) + 1] != '{' && script_text[(*offset) + 1] != ';' && script_text[(*offset) + 1] != '\"' && !isspace(script_text[(*offset) + 1]))(*offset)++
	#define PARSER_ADD_EXPRESSION(expr_start, expr_progress, set_value, set_line) do{ sts_node_t *temp_node = NULL;	\
			if(!expr_progress){expr_start->value = set_value; expr_start->line = set_line; expr_start->type = STS_NODE_VALUE; expr_progress = expr_start;}	\
			else{	\
				if(!STS_CREATE_NODE(temp_node)) PARSER_ERROR("could not create new node in expression");	\
				temp_node->value = set_value; temp_node->line = set_line; temp_node->type = STS_NODE_VALUE; expr_progress->next = temp_node; expr_progress = temp_node;}	\
		}while(0)
	#define PARSER_ADD_NODE(cont_start, cont_progress, node, set_line) do{ sts_node_t *temp_node = NULL;	\
			if(!cont_progress){cont_start->child = node; cont_start->line = set_line; cont_start->type = STS_NODE_EXPRESSION; cont_progress = cont_start;}	\
			else{	\
				if(!STS_CREATE_NODE(temp_node)) PARSER_ERROR("could not create new node in container node list");	\
				temp_node->child = node; temp_node->line = set_line, temp_node->type = STS_NODE_EXPRESSION; cont_progress->next = temp_node; cont_progress = temp_node;}	\
		}while(0)

	if(!script->name) script->name = script_name;
	if(!STS_CREATE_NODE(container_node)) PARSER_ERROR("could not create container node");
	if(!STS_CREATE_NODE(expression_node)) PARSER_ERROR("could not create expression node");
	if(!parent)
	{
		if(!(temp_expression_node = sts_parse(script, container_node, script_text, script_name, offset, line)))
			PARSER_ERROR("could not parse script");
		container_node->child = temp_expression_node;
		STS_FREE(expression_node);
		if(!(name = calloc(1, sizeof(sts_name_container_t))) || !(name->script_name = sts_memdup(script_name, strlen(script_name))) || sts_ast_apply_name(script, container_node, name)) PARSER_ERROR("could not apply the name container to the ast");
		return container_node;
	}
	
	do
	{
		PARSER_SKIP_WHITESPACE();
		if(!script_text[*offset]) break; /* in case there was whitespace before the EOF */
		switch(script_text[*offset])
		{
			case '#': while(script_text[(*offset) + 1] && script_text[(*offset) + 1] != '\n')(*offset)++; break;
			case '\\': /* let expressions continue onto the next line */
				if(script_text[(*offset) + 1] == '\r' && script_text[(*offset) + 2] == '\n'){ (*offset) += 2; (*line)++;}
				else if(script_text[(*offset) + 1] == '\n'){ (*offset)++; (*line)++;}
			break;
			case '\n': (*line)++;
			case ';': /* append in progress expression node to container node */
				/* printf("expression ended\n"); */
				if(!expression_progress) continue;
				PARSER_ADD_NODE(container_node, container_progress, expression_node, *line);
				if(!STS_CREATE_NODE(expression_node)) PARSER_ERROR("could not create expression node");
				expression_progress = NULL;
			break;
			case '\"':
				start = ++(*offset);
				while(1)
				{
					while(script_text[*offset] && script_text[*offset] != '\"')(*offset)++;
					if(!script_text[*offset] || ((*offset) > start ? script_text[(*offset) - 1] : 0) != '\\') break;
					(*offset)++;
				}
				if(!STS_CREATE_VALUE(value)) PARSER_ERROR("could not create value"); /* create new string value, duplicate the string from the script, and add it to the expression */
				if(!(value->string.data = sts_memdup(&script_text[start], *offset - start))) PARSER_ERROR("could not duplicate string for value");
				value->string.length = *offset - start;
				/* printf("adding string literal '%s'\n", value->string); */
				value->type = STS_STRING; value->references = 1; STS_STRING_UNESCAPE_STRING(value->string.data, value->string.length);
				PARSER_ADD_EXPRESSION(expression_node, expression_progress, value, *line);
			break;
			case '(': case '{': case '[':
				/* printf("starting new recursive expression\n"); */
				++(*offset);
				if(!(temp_expression_node = sts_parse(script, expression_progress, script_text, script_name, offset, line)))
					PARSER_ERROR("could not parse expression");
				PARSER_ADD_NODE(expression_node, expression_progress, temp_expression_node, *line);
			break;
			case ')': case '}': case ']':/* printf("exitting sub expression\n"); */ break_out++; break;
			default: /* test if starting with number OR make a string to the next \0 or whitespace */
				if(!STS_CREATE_VALUE(value)) PARSER_ERROR("could not create value");
				if(isdigit(script_text[*offset]) || (script_text[*offset] == '-' && isdigit(script_text[(*offset) + 1])) || (script_text[*offset] == '+' && isdigit(script_text[(*offset) + 1])))
				{
					value->number = strtod(&script_text[*offset], NULL); value->type = STS_NUMBER; value->references = 1;
					/* printf("adding num %f\n", value->number); */
					PARSER_SKIP_NOT_WHITESPACE();
				}
				else
				{
					start = *offset; PARSER_SKIP_NOT_WHITESPACE();
					if(!(value->string.data = sts_memdup(&script_text[start], *offset + 1 - start))) PARSER_ERROR("could not duplicate string for value");
					value->string.length = *offset + 1 - start;
					/* printf("adding str %s\n", value->string); */
					value->type = STS_STRING; value->references = 1;
				}
				PARSER_ADD_EXPRESSION(expression_node, expression_progress, value, *line);
				if(value->type == STS_STRING && value->string.length > 1 && value->string.data[0] == '$' && value->string.data[1] != '$'){expression_progress->type = STS_NODE_IDENTIFIER; value->references = 1;}
			break;
		}
		if(break_out) break;
	} while(script_text[(*offset)++]);
	/* add whatever's left of the current expression if there is anything */
	if(expression_progress) PARSER_ADD_NODE(container_node, container_progress, expression_node, *line);/*if(!container_progress) STS_FREE(container_node); //if(!expression_progress) STS_FREE(expression_node);*/
	else STS_FREE(expression_node);
	return container_node;
}

sts_value_t *sts_eval(sts_script_t *script, sts_node_t *ast, sts_scope_t *locals, sts_value_t **previous, int single, int newscope)
{
	int created_previous = 0;
	sts_map_row_t *row = NULL;
	sts_value_t *ret = NULL, *temp = NULL;
	#define EVAL_PREVIOUS_REFDEC() if(!sts_value_reference_decrement(script, *previous)) STS_ERROR_SIMPLE("could not decrement references in previous")
	#define STS_EVAL_ERROR_PRINT do{ STS_ERROR_PRINT(STS_ERROR_PRINT_ARG0 "eval error: %s: line %u" STS_ERROR_CONCAT, ast->name->script_name, ast->line);}while(0)
	if(!script->globals) STS_SCOPE_PUSH(script->globals, {STS_ERROR_SIMPLE("couldnt initialize global scope"); return NULL;}); /* initialize global scope */
	if(!locals) locals = script->globals; /* make locals be pretty much globals outside of functions */
	if(newscope) STS_SCOPE_PUSH(locals, {STS_ERROR_SIMPLE("could not create new locals in eval"); return NULL;});
	if(!previous)
	{
		if(!STS_CREATE_VALUE(temp)) STS_ERROR_SIMPLE("could not create previous value");
		previous = &temp; temp->type = STS_NUMBER; temp->references = 1;
		created_previous = 1;
	}
	switch(ast->type)
	{
		case STS_NODE_IDENTIFIER: /* search entire scope */
			if(strcmp(ast->value->string.data + 1, "nil") != 0)
			{
				STS_SCOPE_SEARCH(locals, ast->value->string.data + 1, ast->value->string.length - 1, row, {});
				if(row){ret = row->value; STS_VALUE_REFINC(script, ret);}
			}
			if(!ret){ if(!(STS_CREATE_VALUE(ret))) STS_ERROR_SIMPLE("could not create and initialize nil value"); else{ret->references = 1; ret->type = STS_NIL;} }
		break;
		case STS_NODE_VALUE:
			ret = ast->value; STS_VALUE_REFINC(script, ret);
		break;
		case STS_NODE_EXPRESSION:
			do
			{
				if(ret) if(!sts_value_reference_decrement(script, ret)){STS_ERROR_SIMPLE("could not decrement references for previous value"); return NULL;} /* if this is a list of expressions, the previous value needs to be destroyed before another is run */
				if(!ast->child){ret = *previous; STS_VALUE_REFINC(script, (*previous)); return ret;} /* substitute the previous value as the return value */
				else if(ast->child->type != STS_NODE_EXPRESSION) /* an expression with a starting value */
				{
					if(!(temp = sts_eval(script, ast->child, locals, previous, single, 0))){STS_ERROR_SIMPLE("could not eval action"); return NULL;} /* make an action value for the router. Because it is evaluated, it means any kind of substitution works */
					if(!(ret = script->router(script, temp, ast->child, locals, previous))){STS_EVAL_ERROR_PRINT; return NULL;} /* decide what to do based off of the starting value */
					if(!sts_value_reference_decrement(script, temp)){STS_ERROR_SIMPLE("could not decrement action references"); return NULL;} /* clean up the action value used to control what the router does */
					EVAL_PREVIOUS_REFDEC(); *previous = ret; STS_VALUE_REFINC(script, ret); /* carry the previous value along the list of expressions */
				}
				else if(ast->child)
					if(!(ret = sts_eval(script, ast->child, locals, previous, single, 0))){return NULL;} /* eval the nested expression */
			} while((ast = ast->next) && !single);
		break;
	}
	if(created_previous) if(!sts_value_reference_decrement(script, *previous)) {STS_ERROR_SIMPLE("could not decrement previous value references"); return NULL;}
	if(newscope) STS_SCOPE_POP(locals, {STS_ERROR_SIMPLE("couldnt pop eval scope");});
	if(!ret){ STS_EVAL_ERROR_PRINT;}
	return ret;
}

int sts_destroy(sts_script_t *script)
{
	if(script->globals) STS_SCOPE_POP(script->globals, {STS_ERROR_SIMPLE("could not clean up globals");});
	sts_ast_delete(script, script->script);

	return 1;
}

int sts_value_reference_decrement(sts_script_t *script, sts_value_t *value)
{
	unsigned int i;
	if(!value) return 0;
	if(!value->references || !--value->references)
	{
		switch(value->type)
		{
			case STS_ARRAY:
				for(i = 0; i < value->array.length; ++i)
					if(!sts_value_reference_decrement(script, value->array.data[i])) STS_ERROR_SIMPLE("could not decrement references in array");
				STS_FREE(value->array.data);
			break;
			case STS_STRING:
				STS_FREE(value->string.data);
			break;
			case STS_EXTERNAL:
				if(value->external.refdec) value->external.refdec(script, value);
			break;
			case STS_FUNCTION:
				if(value->function.argument_identifiers)
					if(!sts_value_reference_decrement(script, value->function.argument_identifiers)) STS_ERROR_SIMPLE("could not decrement references in function arguments array");
				if(value->function.body && ((--value->function.body->references) <= 0))
				{
					sts_ast_delete(script, value->function.body->node);
					STS_FREE(value->function.body);
				}
			break;
		}
		STS_FREE(value);
	}
	return 1;
}

sts_value_t *sts_defaults(sts_script_t *script, sts_value_t *action, sts_node_t *args, sts_scope_t *locals, sts_value_t **previous)
{
	unsigned int i = 0, can_loop = 0, temp_uint = 0, temp0_uint = 0;
	char *temp_str = NULL;
	sts_node_t *temp_node = NULL;
	sts_map_row_t *row = NULL, *new_locals = NULL;
	sts_ast_container_t *temp_container = NULL;
	sts_value_t *ret = NULL, *eval_value = NULL, *temp_value_arg = NULL, *temp_value = NULL;
	#define EVAL_ARG(argument) do{if(!(eval_value = sts_eval(script, argument, locals, previous, 1, 0))){STS_ERROR_SIMPLE("could not eval argument"); } }while(0)
	#define EVAL_ARG_ALL(argument) do{if(!(eval_value = sts_eval(script, argument, locals, previous, 0, 0))){STS_ERROR_SIMPLE("could not eval argument"); } }while(0)
	#define VALUE_FROM_NUMBER(value_ptr, set_number) do{if(!(STS_CREATE_VALUE(value_ptr))) STS_ERROR_SIMPLE("could not create value for number"); else{value_ptr->references = 1; value_ptr->type = STS_NUMBER; value_ptr->number = (double)(set_number);} }while(0)
	#define VALUE_INIT(value_ptr, set_type) do{if(!(STS_CREATE_VALUE(value_ptr))) STS_ERROR_SIMPLE("could not create and initialize value"); else{value_ptr->references = 1; value_ptr->type = set_type;} }while(0)
	#define ACTION(test, str) test(strcmp(str, action->string.data) == 0)
	#define ACTION_BEGIN_ARGLOOP while((args = args->next))	\
		{ if(!(eval_value = sts_eval(script, args, locals, previous, 1, 0))){STS_ERROR_SIMPLE("could not eval argument in loop"); break;}
	#define ACTION_END_ARGLOOP if(!sts_value_reference_decrement(script, eval_value)){STS_ERROR_SIMPLE("could not decrement references in eval argument"); break;} }
	#ifdef STS_GOTO_JIT
		#define GOTO_LABEL_CAT_(a, b) a ## b
		#define GOTO_LABEL_CAT(a, b) GOTO_LABEL_CAT_(a, b)
		#define GOTO_SET(id) do{args->label = && GOTO_LABEL_CAT(sts_jit_, __LINE__); args->router_id = (void *)(id); GOTO_LABEL_CAT(sts_jit_, __LINE__):;}while(0)
		#define GOTO_JMP(id) do{if(args->router_id == (void *)(id)){goto *(args->label);}}while(0)
		#define GOTO_ACTIVATED (args->router_id)
	#else
		#define GOTO_SET(id)
		#define GOTO_JMP(id)
		#define GOTO_ACTIVATED
	#endif

	GOTO_JMP(&sts_defaults);
	if(!action){STS_ERROR_SIMPLE("action is NULL"); return NULL;}
	if(action->type == STS_STRING)
	{
		ACTION(if, "print")
		{
			GOTO_SET(&sts_defaults);
			ACTION_BEGIN_ARGLOOP
				switch(eval_value->type)
				{
					case STS_NUMBER: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%g", eval_value->number, " ", 1); break;
					case STS_NIL: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%s", "nil", " ", 1); break;
					case STS_ARRAY: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "[array passed and is %u elements long]", eval_value->array.length,  " ", 1); break;
					case STS_STRING: STS_STRING_ASSEMBLE(temp_str, temp_uint, eval_value->string.data, eval_value->string.length, " ", 1); break;
					case STS_EXTERNAL: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%p", eval_value->external.data_ptr, " ", 1); break;
					case STS_FUNCTION: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "[function passed and it takes %u arguments]", eval_value->function.argument_identifiers->array.length, " ", 1); break;
				}
			ACTION_END_ARGLOOP
			STS_STRING_ASSEMBLE(temp_str, temp_uint, "\n", 1, "", 0);
			for(i = 0; i < temp_uint; ++i)
				fputc(temp_str[i], stdout);
			STS_FREE(temp_str);
			VALUE_FROM_NUMBER(ret, 1);
		}
		ACTION(else if, "pass")
		{
			GOTO_SET(&sts_defaults);
			ACTION_BEGIN_ARGLOOP
				if(ret) if(!sts_value_reference_decrement(script, ret)){STS_ERROR_SIMPLE("could not decrement references in pass action"); sts_value_reference_decrement(script, eval_value); return NULL; }
				ret = eval_value; STS_VALUE_REFINC(script, ret);
			ACTION_END_ARGLOOP
		}
		ACTION(else if, "string")
		{
			GOTO_SET(&sts_defaults);
			ACTION_BEGIN_ARGLOOP
				switch(eval_value->type)
				{
					case STS_NUMBER: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%g", eval_value->number, "", 0); break;
					case STS_NIL: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%s", "nil", "", 0); break;
					case STS_ARRAY: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "[array passed and is %u elements long]", eval_value->array.length,  "", 0); break;
					case STS_STRING: STS_STRING_ASSEMBLE(temp_str, temp_uint, eval_value->string.data, eval_value->string.length, "", 0); break;
					case STS_EXTERNAL: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%p", eval_value->external.data_ptr, "", 0); break;
					case STS_FUNCTION: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "[function passed and it takes %u arguments]", eval_value->function.argument_identifiers->array.length, "", 0); break;
				}
			ACTION_END_ARGLOOP
			VALUE_INIT(ret, STS_STRING); ret->string.data = temp_str; ret->string.length = temp_uint;
		}
		ACTION(else if, "global")
		{
			GOTO_SET(&sts_defaults);
			if(args->next)
			{
				EVAL_ARG(args->next);
				if(eval_value->type != STS_STRING){STS_ERROR_SIMPLE("could not lookup global because first argument is not a string"); sts_value_reference_decrement(script, eval_value); return NULL;}
				temp_value_arg = eval_value; /* temp value becomes the global name */

				if(script->globals) row = sts_map_get(&script->globals->locals, temp_value_arg->string.data, temp_value_arg->string.length); /* get the row if it exists */

				if(args->next->next) /* set global */
				{
					EVAL_ARG(args->next->next);
					if(row)
					{
						if(row->type == STS_ROW_VALUE){if(!sts_value_reference_decrement(script, row->value)){STS_ERROR_SIMPLE("could not decrement references for second argument in global action"); return NULL;}}
						VALUE_INIT(temp_value, eval_value->type); if(sts_value_copy(script, temp_value, eval_value, 0)){ STS_ERROR_SIMPLE("could not set a new value to evaluated argument in global action"); return NULL;}
						row->value = temp_value; row->type = STS_ROW_VALUE;
						ret = temp_value; STS_VALUE_REFINC(script, temp_value);
					}
					else
					{
						VALUE_INIT(temp_value, eval_value->type); if(sts_value_copy(script, temp_value, eval_value, 0)){ STS_ERROR_SIMPLE("could not set a new value to evaluated argument in global action"); return NULL;}
						if(!(row = sts_map_add_set(&script->globals->locals, temp_value_arg->string.data, temp_value_arg->string.length, temp_value))){STS_ERROR_SIMPLE("could not add value to global in global action"); return NULL;}
						row->type = STS_ROW_VALUE;
						ret = temp_value; STS_VALUE_REFINC(script, temp_value);
					}
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in global action");
				}
				else /* test if global exists */
				{
					if(row){VALUE_INIT(ret, STS_NUMBER); ret->number = 1.0;}
					else{VALUE_INIT(ret, STS_NUMBER); ret->number = 0.0;}
				}

				if(!sts_value_reference_decrement(script, temp_value_arg)) STS_ERROR_SIMPLE("could not decrement references for first argument in global action");
			}
			else {STS_ERROR_SIMPLE("global action requires at least 1 argument"); return NULL;}
		}
		ACTION(else if, "local") /* will only run if locals exist */
		{
			GOTO_SET(&sts_defaults);
			if(args->next)
			{
				EVAL_ARG(args->next);
				if(eval_value->type != STS_STRING){STS_ERROR_SIMPLE("could not lookup local because first argument is not a string"); sts_value_reference_decrement(script, eval_value); return NULL;}
				temp_value_arg = eval_value; /* temp value becomes the local name */

				if(locals) row = sts_map_get(&locals->locals, temp_value_arg->string.data, temp_value_arg->string.length); /* get the row if it exists */

				if(args->next->next) /* set local */
				{
					EVAL_ARG(args->next->next);
					if(row)
					{
						if(row->type == STS_ROW_VALUE){if(!sts_value_reference_decrement(script, row->value)){STS_ERROR_SIMPLE("could not decrement references for second argument in local action"); return NULL;}}
						VALUE_INIT(temp_value, eval_value->type); if(sts_value_copy(script, temp_value, eval_value, 0)){ STS_ERROR_SIMPLE("could not set a new value to evaluated argument in local action"); return NULL;}
						row->value = temp_value; row->type = STS_ROW_VALUE;
						ret = temp_value; STS_VALUE_REFINC(script, temp_value);
					}
					else
					{
						VALUE_INIT(temp_value, eval_value->type); if(sts_value_copy(script, temp_value, eval_value, 0)){ STS_ERROR_SIMPLE("could not set a new value to evaluated argument in local action"); return NULL;}
						if(!(row = sts_map_add_set(&locals->locals, temp_value_arg->string.data, temp_value_arg->string.length, temp_value))){STS_ERROR_SIMPLE("could not add value to locals in local action"); return NULL;}
						row->type = STS_ROW_VALUE;
						ret = temp_value; STS_VALUE_REFINC(script, temp_value);
					}
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in local action");
				}
				else /* test if local exists */
				{
					if(row){VALUE_INIT(ret, STS_NUMBER); ret->number = 1.0;}
					else{VALUE_INIT(ret, STS_NUMBER); ret->number = 0.0;}
				}

				if(!sts_value_reference_decrement(script, temp_value_arg)) STS_ERROR_SIMPLE("could not decrement references for first argument in local action");
			}
			else {STS_ERROR_SIMPLE("local action requires at least 1 argument"); return NULL;}
		}
		ACTION(else if, "string-hash")
		{
			GOTO_SET(&sts_defaults);
			if(args->next)
			{
				EVAL_ARG(args->next); if(eval_value->type == STS_STRING){ temp_uint = STS_FNV_OFFSET; STS_HASH(temp_uint, eval_value->string.data, eval_value->string.length);}
				VALUE_FROM_NUMBER(ret, (double)temp_uint);
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in typeof action");
			}
			else {STS_ERROR_SIMPLE("string-hash action requires at least 1 argument"); return NULL;}
		}
		ACTION(else if, "typeof")
		{
			GOTO_SET(&sts_defaults);
			if(args->next)
			{
				EVAL_ARG(args->next); switch(eval_value->type)
				{
					case STS_EXTERNAL: VALUE_INIT(ret, STS_NUMBER); ret->number = (double)STS_EXTERNAL; break;
					case STS_NIL: VALUE_INIT(ret, STS_NUMBER); ret->number = (double)STS_NIL; break;
					case STS_NUMBER: VALUE_INIT(ret, STS_NUMBER); ret->number = (double)STS_NUMBER; break;
					case STS_STRING: VALUE_INIT(ret, STS_NUMBER); ret->number = (double)STS_STRING; break;
					case STS_ARRAY: VALUE_INIT(ret, STS_NUMBER); ret->number = (double)STS_ARRAY; break;
					case STS_FUNCTION: VALUE_INIT(ret, STS_NUMBER); ret->number = (double)STS_FUNCTION; break;
				}
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in typeof action");
			}
			else {STS_ERROR_SIMPLE("typeof action requires at least 1 argument"); return NULL;}
		}
		ACTION(else if, "sizeof")
		{
			GOTO_SET(&sts_defaults);
			if(args->next)
			{
				EVAL_ARG(args->next); switch(eval_value->type)
				{
					case STS_STRING: VALUE_INIT(ret, STS_NUMBER); ret->number = (double)eval_value->string.length; break;
					case STS_ARRAY: VALUE_INIT(ret, STS_NUMBER); ret->number = (double)eval_value->array.length; break;
					case STS_FUNCTION: VALUE_INIT(ret, STS_NUMBER); ret->number = (double)eval_value->function.argument_identifiers->array.length; break;
					default: VALUE_INIT(ret, STS_NUMBER); ret->number = 1.0;
				}
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in sizeof action");
			}
			else {STS_ERROR_SIMPLE("sizeof action requires at least 1 argument"); return NULL;}
		}
		else if(strcmp("if", action->string.data) == 0 || strcmp("elseif", action->string.data) == 0 || strcmp("loop", action->string.data) == 0)
		{
			GOTO_SET(&sts_defaults);
			if(strcmp("elseif", action->string.data) == 0 && (*previous)->type == STS_NUMBER && (*previous)->number) {VALUE_INIT(ret, STS_NUMBER); ret->number = 1.0; return ret;} /* cascade previous value and only run if zero */
			if(strcmp("loop", action->string.data) == 0) can_loop = 1;
			if(args->next && args->next->next)
			{
				do
				{
					EVAL_ARG(args->next);
					if(!sts_value_test(eval_value))
					{
						if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in conditional action");
						VALUE_INIT(ret, STS_NUMBER); ret->number = 0.0; return ret;
					}
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in conditional action");
					/* the value was interpreted as true and can eval whatever's below */
					EVAL_ARG_ALL(args->next->next); if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for evaluated body argument in conditional action");
				} while(can_loop);
				VALUE_INIT(ret, STS_NUMBER); ret->number = 1.0; return ret;
			}
			else {STS_ERROR_SIMPLE("conditional action requires at least 2 arguments"); return NULL;}
		}
		ACTION(else if, "else")
		{
			GOTO_SET(&sts_defaults);
			if((*previous)->type == STS_NUMBER && (*previous)->number) {VALUE_INIT(ret, STS_NUMBER); ret->number = 1.0; return ret;} /* cascade previous value and only run if zero */
			if(args->next)
			{
				EVAL_ARG_ALL(args->next); if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for evaluated body argument in conditional action");
				VALUE_INIT(ret, STS_NUMBER); ret->number = 1.0; return ret;
			}
			else {STS_ERROR_SIMPLE("conditional action requires at least 1 argument"); return NULL;}
		}
		ACTION(else if, "function")
		{
			GOTO_SET(&sts_defaults);
			if(args->next && args->next->next)
			{
				EVAL_ARG(args->next);
				if(eval_value->type != STS_STRING && eval_value->type != STS_NIL){STS_ERROR_SIMPLE("expected a string or nil for the function name"); sts_value_reference_decrement(script, eval_value); return NULL;}
				temp_value_arg = eval_value; /* temp value becomes the function name. If nil/non-string, don't define the function in the global var map */

				args = args->next; /* fast forward to next value */

				if(!(temp_container = STS_CALLOC(1, sizeof(sts_ast_container_t)))){ STS_ERROR_SIMPLE("could not allocate an ast container"); sts_value_reference_decrement(script, eval_value); return NULL;}
				VALUE_INIT(ret, STS_FUNCTION);VALUE_INIT(ret->function.argument_identifiers, STS_ARRAY); temp_container->references = 1;
				if(!args->next->next) /* if a function with no arguments */
				{
					if(!(temp_container->node = sts_ast_copy(script, args->next))) {STS_ERROR_SIMPLE("could not copy ast to function body in function action"); return NULL;}
					ret->function.body = temp_container;
				}
				else
				{
					ACTION_BEGIN_ARGLOOP
						if(eval_value->type == STS_STRING) /* only add argument identifier if a string */
						{
							VALUE_INIT(temp_value, STS_STRING);
							temp_value->string.data = sts_memdup(eval_value->string.data, eval_value->string.length);
							temp_value->string.length = eval_value->string.length;
							STS_ARRAY_APPEND_INSERT(ret->function.argument_identifiers, temp_value, ret->function.argument_identifiers->array.length);
						}
						if(!args->next->next) /* the very last argument is the function body. Checked early as to not eval the argument */
						{
							if(!(temp_container->node = sts_ast_copy(script, args->next))) {STS_ERROR_SIMPLE("could not copy ast to function body in function action"); return NULL;}
							ret->function.body = temp_container;
							if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for current argument in function action");
							break;
						}
					ACTION_END_ARGLOOP
				}

				if(temp_value_arg->type == STS_STRING) /* only put the function in local var space if a string for function name */
				{
					if((row = sts_map_get(&locals->locals, temp_value_arg->string.data, temp_value_arg->string.length))) if(!sts_value_reference_decrement(script, row->value)) STS_ERROR_SIMPLE("could not decrement references for old function value");
					sts_map_add_set(&locals->locals, temp_value_arg->string.data, temp_value_arg->string.length, ret);
					STS_VALUE_REFINC(script, ret);
				}

				if(!sts_value_reference_decrement(script, temp_value_arg)) STS_ERROR_SIMPLE("could not decrement references for first argument in function action");
			}
			else {STS_ERROR_SIMPLE("function action requires at least 2 arguments"); return NULL;}
		}
		ACTION(else if, "copy")
		{
			GOTO_SET(&sts_defaults);
			if(args->next)
			{
				EVAL_ARG(args->next); VALUE_INIT(ret, eval_value->type);
				if(sts_value_copy(script, ret, eval_value, 1)) {STS_ERROR_SIMPLE("could not copy value in copy action"); return NULL;}
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in copy action");
			}
			else {STS_ERROR_SIMPLE("copy action requires 1 argument"); return NULL;}
		}
		ACTION(else if, "self-name")
		{
			GOTO_SET(&sts_defaults);
			VALUE_INIT(ret, STS_STRING);
			if(!(ret->string.data = sts_memdup(args->name->script_name, strlen(args->name->script_name)))){STS_ERROR_SIMPLE("could not duplicate script string"); return NULL;} ret->string.length = strlen(args->name->script_name);
		}
		ACTION(else if, "number")
		{
			GOTO_SET(&sts_defaults);
			if(args->next)
			{
				EVAL_ARG(args->next); if(eval_value->type == STS_STRING){ VALUE_FROM_NUMBER(ret, strtod(eval_value->string.data, NULL)); }
				else {STS_ERROR_SIMPLE("number action requires the argument to be a string"); sts_value_reference_decrement(script, eval_value); return NULL;}
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in number action");
			}
			else {STS_ERROR_SIMPLE("number action requires 1 argument string"); return NULL;}
		}
		/* undo char (make string from number as char, not as a number). I dont like this syntax but sts is on its way out in my internal usage so i dont care that much */
		ACTION(else if, "asc")
		{
			GOTO_SET(&sts_defaults);
			if(args->next)
			{
				EVAL_ARG(args->next); if(eval_value->type == STS_NUMBER){ STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%c", (int)eval_value->number, "", 0); VALUE_INIT(ret, STS_STRING); ret->string.data = temp_str; ret->string.length = temp_uint; }
				else {STS_ERROR_SIMPLE("asc action requires the argument to be a number"); sts_value_reference_decrement(script, eval_value); return NULL;}
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in asc action");
			}
			else {STS_ERROR_SIMPLE("asc action requires 1 argument number"); return NULL;}
		}
		ACTION(else if, "char")
		{
			GOTO_SET(&sts_defaults);
			if(args->next && args->next->next)
			{
				EVAL_ARG(args->next); temp_value_arg = eval_value;
				EVAL_ARG(args->next->next);
				if(temp_value_arg->type == STS_STRING && eval_value->type == STS_NUMBER && (unsigned int)eval_value->number < temp_value_arg->string.length){ VALUE_FROM_NUMBER(ret, temp_value_arg->string.data[(unsigned int)eval_value->number]); }
				else {STS_ERROR_SIMPLE("char action requires the arguments to be a string and a number to fall within the bounds of the string"); sts_value_reference_decrement(script, temp_value_arg); sts_value_reference_decrement(script, eval_value); return NULL;}
				if(!sts_value_reference_decrement(script, temp_value_arg)) STS_ERROR_SIMPLE("could not decrement references for first argument in char action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in char action");
			}
			else {STS_ERROR_SIMPLE("char action requires 1 argument string and 1 number argument"); return NULL;}
		}
		ACTION(else if, "get") /* indexes arrays and gets members for more complex types  */
		{
			GOTO_SET(&sts_defaults);
			if(args->next && args->next->next)
			{
				EVAL_ARG(args->next); temp_value_arg = eval_value;
				EVAL_ARG(args->next->next);
				switch(temp_value_arg->type)
				{
					case STS_ARRAY:
						if(eval_value->type == STS_NUMBER && (eval_value->number < temp_value_arg->array.length && eval_value->number >= 0))
						{
							ret = temp_value_arg->array.data[(unsigned int)eval_value->number]; STS_VALUE_REFINC(script, ret);
						}
						else {STS_ERROR_SIMPLE("get action cannot index array without a number value and the number must be within the array bounds"); return NULL;}
					break;
					case STS_STRING:
						if(eval_value->type == STS_NUMBER && (eval_value->number < temp_value_arg->string.length && eval_value->number >= 0))
						{
							VALUE_INIT(ret, STS_STRING);
							ret->string.data = sts_memdup(&(temp_value_arg->string.data[(unsigned int)eval_value->number]), 1); ret->string.length = 1;
						}
						else {STS_ERROR_SIMPLE("get action cannot index string without a number value and the number must be within the string bounds"); return NULL;}
					break;
					default:
						ret = temp_value_arg; STS_VALUE_REFINC(script, ret);
				}
				if(!sts_value_reference_decrement(script, temp_value_arg)) STS_ERROR_SIMPLE("could not decrement references for first argument in get action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in get action");
			}
			else {STS_ERROR_SIMPLE("get action requires at least 2 arguments"); return NULL;}
		}
		ACTION(else if, "set") /* sets values to other values */
		{
			GOTO_SET(&sts_defaults);
			if(args->next && args->next->next)
			{
				EVAL_ARG(args->next); temp_value_arg = eval_value;
				EVAL_ARG(args->next->next);
				if(sts_value_copy(script, temp_value_arg, eval_value, 0)) {STS_ERROR_SIMPLE("could not shallow copy in set action"); return NULL;}
				VALUE_FROM_NUMBER(ret, 1.0);
				if(!sts_value_reference_decrement(script, temp_value_arg)) STS_ERROR_SIMPLE("could not decrement references for first argument in set action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in set action");
			}
			else {STS_ERROR_SIMPLE("set action requires at least 2 arguments"); return NULL;}
		}
		ACTION(else if, "array") /* creates an array of arguments */
		{
			GOTO_SET(&sts_defaults);
			VALUE_INIT(ret, STS_ARRAY);
			ACTION_BEGIN_ARGLOOP
				STS_ARRAY_APPEND_INSERT(ret, eval_value, i); STS_VALUE_REFINC(script, eval_value); ++i;
			ACTION_END_ARGLOOP
		}
		ACTION(else if, "remove") /* removes values at array */
		{
			GOTO_SET(&sts_defaults);
			if(args->next && args->next->next)
			{
				EVAL_ARG(args->next); temp_value_arg = eval_value;
				EVAL_ARG(args->next->next);
				if(temp_value_arg->type != STS_ARRAY){STS_ERROR_SIMPLE("the remove action requires the first argument to be an array"); return NULL;}
				if(eval_value->type != STS_NUMBER){STS_ERROR_SIMPLE("the remove action requires the second argument to be an number"); return NULL;}
					if(eval_value->number < 0.0 || eval_value->number >= temp_value_arg->array.length){STS_ERROR_SIMPLE("could not remove value at the position requested because it is out of bounds of the array"); return NULL;}
					else STS_ARRAY_REMOVE(script, temp_value_arg, (unsigned int)(eval_value->number), {return NULL;});
				VALUE_FROM_NUMBER(ret, 1.0);
				if(!sts_value_reference_decrement(script, temp_value_arg)) STS_ERROR_SIMPLE("could not decrement references for first argument in remove action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in remove action");
			}
			else {STS_ERROR_SIMPLE("remove action requires at least 2 arguments"); return NULL;}
		}
		ACTION(else if, "insert") /* inserts values at array */
		{
			GOTO_SET(&sts_defaults);
			if(args->next && args->next->next && args->next->next->next)
			{
				EVAL_ARG(args->next); temp_value_arg = eval_value;
				EVAL_ARG(args->next->next); temp_value = eval_value;
				EVAL_ARG(args->next->next->next);
				if(temp_value_arg->type != STS_ARRAY){STS_ERROR_SIMPLE("the insert action requires the first argument to be an array or a string"); return NULL;}
				if(temp_value->type != STS_NUMBER){STS_ERROR_SIMPLE("the insert action requires the second argument to be an number"); return NULL;}
					if(temp_value->number < 0.0){STS_ERROR_SIMPLE("could not remove value at the position requested because it is below the bounds of the array"); return NULL;}
					else { STS_ARRAY_APPEND_INSERT(temp_value_arg, eval_value, (unsigned int)(temp_value->number)); STS_VALUE_REFINC(script, eval_value);}
				VALUE_FROM_NUMBER(ret, 1.0);
				if(!sts_value_reference_decrement(script, temp_value_arg)) STS_ERROR_SIMPLE("could not decrement references for first argument in insert action");
				if(!sts_value_reference_decrement(script, temp_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in insert action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for third argument in insert action");
			}
			else {STS_ERROR_SIMPLE("insert action requires at least 3 arguments"); return NULL;}
		}
		ACTION(else if, "import") /* import file in the current working directory (NOT WHERE THE SCRIPT IS), if there is no file found, in the system */
		{
			GOTO_SET(&sts_defaults);
			if(args->next)
			{
				EVAL_ARG(args->next);
				if(eval_value->type != STS_STRING) STS_ERROR_SIMPLE("import requires the import file argument to be a string");
				else if(script->read_file)
				{
					if((temp_str = script->read_file(script, eval_value->string.data, &temp_uint)));
					else if(!script->import_file || !(temp_str = script->import_file(script, eval_value->string.data)))
						STS_ERROR_SIMPLE("could not find file requested in import action");
					if(temp_str)
					{
						temp_uint = temp0_uint = 0;
						if(!(temp_node = sts_parse(script, NULL, temp_str, eval_value->string.data, &temp_uint, &temp0_uint)))
							STS_ERROR_SIMPLE("could not parse imported file");
						else if(!(temp_value = sts_eval(script, temp_node, locals, previous, 0, 0)))
							STS_ERROR_SIMPLE("could not evaluate the imported file");
						else
						{
							if(!sts_value_reference_decrement(script, temp_value)) STS_ERROR_SIMPLE("could not refdec returned value after eval in import action");
							i = 1;
							sts_ast_delete(script, temp_node);
						}
					}
					STS_FREE(temp_str);
				} else STS_ERROR_SIMPLE("to import correctly, a read_file member in the script struct must be a pointer to a function");
				VALUE_FROM_NUMBER(ret, (double)i);
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in import action");
			}
			else {STS_ERROR_SIMPLE("import action requires at least 2 arguments"); return NULL;}
		}
		ACTION(else if, "eval") /* parse and eval a string into the local scope */
		{
			GOTO_SET(&sts_defaults);
			if(args->next)
			{
				EVAL_ARG(args->next);
				if(eval_value->type != STS_STRING) STS_ERROR_SIMPLE("eval requires the script argument to be a string");
				temp_uint = temp0_uint = 0;
				if(!(temp_node = sts_parse(script, NULL, eval_value->string.data, args->name ? args->name->script_name : "generated eval string", &temp_uint, &temp0_uint)))
				{
					STS_ERROR_SIMPLE("could not parse eval string");
					VALUE_INIT(ret, STS_NIL);
				}
				else if(!(ret = sts_eval(script, temp_node, locals, previous, 0, 0)))
				{
					STS_ERROR_SIMPLE("could not evaluate the string");
					VALUE_INIT(ret, STS_NIL);
				}
				sts_ast_delete(script, temp_node);
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for script string argument in eval action");
			}
			else {STS_ERROR_SIMPLE("eval action requires a string argument"); return NULL;}
		}
		ACTION(else if, "call") /* call function */
		{
			GOTO_SET(&sts_defaults);
			if(args->next)
			{
				EVAL_ARG(args->next); temp_value_arg = eval_value;
				if(temp_value_arg->type != STS_FUNCTION){ STS_ERROR_SIMPLE("the call action requires the first argument to be a function value"); return NULL;}
				args = args->next;
				VALUE_INIT(temp_value, STS_ARRAY); if(!temp_value){STS_ERROR_SIMPLE("could not create elipses value in call action"); return NULL;}
				STS_SCOPE_PUSH(locals, {STS_ERROR_SIMPLE("couldnt create new scope level"); return NULL;});
				if(!sts_map_add_set(&locals->locals, "...", strlen("..."), temp_value))
				{
					STS_ERROR_SIMPLE("could not create local scope in call action"); return NULL;
				}
				ACTION_BEGIN_ARGLOOP
					STS_VALUE_REFINC(script, eval_value);
					if(i < temp_value_arg->function.argument_identifiers->array.length) /* create identifiers for each argument */
					{
						if(!sts_map_add_set(&locals->locals, temp_value_arg->function.argument_identifiers->array.data[i]->string.data, temp_value_arg->function.argument_identifiers->array.data[i]->string.length, eval_value))
						{
							STS_ERROR_SIMPLE("could not create local scope in call action"); return NULL;
						}
					}
					else /* if extra arguments passed, put in elipses */
						STS_ARRAY_APPEND_INSERT(temp_value, eval_value, i);
					++i;
				ACTION_END_ARGLOOP
				if(i < temp_value_arg->function.argument_identifiers->array.length){STS_ERROR_SIMPLE("too few arguments provided to function provided to call action"); return NULL;}
				ret = sts_eval(script, temp_value_arg->function.body->node, locals, NULL, 0, 0);
				STS_SCOPE_POP(locals, {STS_ERROR_SIMPLE("could not pop scope level"); return NULL;});
				if(!sts_value_reference_decrement(script, temp_value_arg)) STS_ERROR_SIMPLE("could not decrement references for first argument in call action");
			}
			else {STS_ERROR_SIMPLE("call action requires at least 1 argument"); return NULL;}
		}
		ACTION(else if, "&&")
		{
			GOTO_SET(&sts_defaults);
			if(args->next && args->next->next)
			{
				VALUE_FROM_NUMBER(ret, 1);
				ACTION_BEGIN_ARGLOOP
					if(!sts_value_test(eval_value))
					{
						ret->number = 0.0; if(!sts_value_reference_decrement(script, eval_value)){STS_ERROR_SIMPLE("could not decrement references in && argument");}
						return ret;
					}
				ACTION_END_ARGLOOP
			}
		}
		ACTION(else if, "||")
		{
			GOTO_SET(&sts_defaults);
			if(args->next && args->next->next)
			{
				VALUE_FROM_NUMBER(ret, 0);
				ACTION_BEGIN_ARGLOOP
					if(sts_value_test(eval_value))
					{
						ret->number = 1.0; if(!sts_value_reference_decrement(script, eval_value)){STS_ERROR_SIMPLE("could not decrement references in || argument");}
						return ret;
					}
				ACTION_END_ARGLOOP
			}
		}
		#define ACTION_RELATIONAL(operator) ACTION(else if, #operator)	\
		{	\
			GOTO_SET(&sts_defaults);	\
			if(args->next && args->next->next)	\
			{	\
				EVAL_ARG(args->next); temp_value_arg = eval_value;	\
				EVAL_ARG(args->next->next);	\
				if(temp_value_arg->type != eval_value->type)	\
				{	\
					if(!sts_value_reference_decrement(script, temp_value_arg)) STS_ERROR_SIMPLE("could not decrement references for first argument in " #operator " action");	\
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in " #operator " action");	\
					VALUE_INIT(ret, STS_NUMBER); ret->number = 0.0; return ret;	\
				} 	\
				switch(eval_value->type)	\
				{	\
					case STS_EXTERNAL:	\
						if(temp_value_arg->external.data_ptr operator eval_value->external.data_ptr) {VALUE_INIT(ret, STS_NUMBER); ret->number = 1.0;}	\
						else {VALUE_INIT(ret, STS_NUMBER); ret->number = 0.0;}	\
					break;	\
					case STS_NIL:	\
						VALUE_INIT(ret, STS_NUMBER); ret->number = 1.0;	\
					break;	\
					case STS_NUMBER:	\
						if(temp_value_arg->number operator eval_value->number) {VALUE_INIT(ret, STS_NUMBER); ret->number = 1.0;}	\
						else {VALUE_INIT(ret, STS_NUMBER); ret->number = 0.0;}	\
					break;	\
					case STS_STRING:	\
						if(strcmp(temp_value_arg->string.data, eval_value->string.data) operator 0) {VALUE_INIT(ret, STS_NUMBER); ret->number = 1.0;}	\
						else {VALUE_INIT(ret, STS_NUMBER); ret->number = 0.0;}	\
					break;	\
					case STS_ARRAY:	\
						if(temp_value_arg->array.length operator eval_value->array.length) {VALUE_INIT(ret, STS_NUMBER); ret->number = 1.0;}	\
						else {VALUE_INIT(ret, STS_NUMBER); ret->number = 0.0;}	\
					break;	\
					case STS_FUNCTION:	\
						if(temp_value_arg->function.argument_identifiers->array.length operator eval_value->function.argument_identifiers->array.length) {VALUE_INIT(ret, STS_NUMBER); ret->number = 1.0;}	\
						else {VALUE_INIT(ret, STS_NUMBER); ret->number = 0.0;}	\
					break;	\
				}	\
				if(!sts_value_reference_decrement(script, temp_value_arg)) STS_ERROR_SIMPLE("could not decrement references for first argument in " #operator " action");	\
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in " #operator" action");	\
			}	\
			else {STS_ERROR_SIMPLE(#operator " action requires 2 arguments"); return NULL;}	\
		}
		#define ACTION_BINOP_MULTI(operator, single_arg, multi_arg) ACTION(else if, #operator)	\
		{	\
			GOTO_SET(&sts_defaults);	\
			if(args->next && !args->next->next)	\
			{	\
				EVAL_ARG(args->next);	\
				if(eval_value->type == STS_NUMBER)	\
				single_arg	\
				else {STS_ERROR_SIMPLE("can only perform " #operator " operation on numbers");}	\
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in " #operator " action");	\
			}	\
			else if(args->next)	\
			{	\
				while((args = args->next))	\
				{	\
					EVAL_ARG(args);	\
					if(eval_value->type == STS_NUMBER)	\
					{	\
						if(!i) {VALUE_FROM_NUMBER(ret, eval_value->number); i++;}	\
						else	\
						multi_arg	\
					}else {STS_ERROR_SIMPLE("can only perform " #operator " operation on numbers");}	\
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for current argument in " #operator " action");	\
				}	\
			}	\
		}
		#define ACTION_BINOP(operator, single_arg) ACTION_BINOP_MULTI(operator, single_arg, {ret->number operator##= eval_value->number;})
		#define ACTION_SINGLE_NUMERIC(script, func) ACTION(else if, #func) {	\
			GOTO_SET(&sts_defaults);	\
			if(args->next){	\
				EVAL_ARG(args->next); if(eval_value->type != STS_NUMBER) STS_ERROR_SIMPLE(#func " action requires the first agument to be a number");	\
				else VALUE_FROM_NUMBER(ret, func(eval_value->number));	\
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for current argument in " #func " action");	\
			}	\
			else STS_ERROR_SIMPLE(#func " action requires at least one numeric argument");	\
		}
		ACTION_RELATIONAL(==)
		ACTION_RELATIONAL(!=)
		ACTION_RELATIONAL(<)
		ACTION_RELATIONAL(<=)
		ACTION_RELATIONAL(>)
		ACTION_RELATIONAL(>=)
		ACTION_BINOP(+, {VALUE_FROM_NUMBER(ret, fabs(eval_value->number));})
		ACTION_BINOP(-, {VALUE_FROM_NUMBER(ret, -fabs(eval_value->number));})
		ACTION_BINOP(*, {VALUE_FROM_NUMBER(ret, eval_value->number);})
		ACTION_BINOP(/, {VALUE_FROM_NUMBER(ret, eval_value->number);})
		ACTION_BINOP_MULTI(**, {VALUE_FROM_NUMBER(ret, eval_value->number);}, {ret->number = pow(ret->number, eval_value->number);})
		ACTION_BINOP_MULTI(%, {VALUE_FROM_NUMBER(ret, eval_value->number);}, {ret->number = fmod(ret->number, eval_value->number);})
		ACTION_BINOP_MULTI(>>, {VALUE_FROM_NUMBER(ret, eval_value->number);}, {ret->number = (int)ret->number >> (int)eval_value->number;})
		ACTION_BINOP_MULTI(<<, {VALUE_FROM_NUMBER(ret, eval_value->number);}, {ret->number = (int)ret->number << (int)eval_value->number;})
		ACTION_BINOP_MULTI(&, {VALUE_FROM_NUMBER(ret, eval_value->number);}, {ret->number = (int)ret->number & (int)eval_value->number;})
		ACTION_BINOP_MULTI(^, {VALUE_FROM_NUMBER(ret, eval_value->number);}, {ret->number = (int)ret->number ^ (int)eval_value->number;})
		ACTION_BINOP_MULTI(|, {VALUE_FROM_NUMBER(ret, eval_value->number);}, {ret->number = (int)ret->number | (int)eval_value->number;})
		ACTION_BINOP_MULTI(~, {VALUE_FROM_NUMBER(ret, ~(int)eval_value->number);}, {ret->number = ret->number;})
		ACTION_BINOP_MULTI(!, {VALUE_FROM_NUMBER(ret, !sts_value_test(eval_value));}, {ret->number = ret->number;})
		ACTION_BINOP_MULTI(++, {VALUE_FROM_NUMBER(ret, ++eval_value->number);}, {ret->number = ret->number;})
		ACTION_BINOP_MULTI(--, {VALUE_FROM_NUMBER(ret, --eval_value->number);}, {ret->number = ret->number;})
		ACTION_SINGLE_NUMERIC(script, sin)
		ACTION_SINGLE_NUMERIC(script, cos)
		ACTION_SINGLE_NUMERIC(script, tan)
		ACTION_SINGLE_NUMERIC(script, asin)
		ACTION_SINGLE_NUMERIC(script, acos)
		ACTION_SINGLE_NUMERIC(script, atan)
		ACTION_SINGLE_NUMERIC(script, sinh)
		ACTION_SINGLE_NUMERIC(script, cosh)
		ACTION_SINGLE_NUMERIC(script, tanh)
		ACTION_SINGLE_NUMERIC(script, exp)
		ACTION_SINGLE_NUMERIC(script, log)
		ACTION_SINGLE_NUMERIC(script, log10)
		ACTION_SINGLE_NUMERIC(script, sqrt)
		ACTION_SINGLE_NUMERIC(script, fabs)
		ACTION_SINGLE_NUMERIC(script, floor)
		ACTION_SINGLE_NUMERIC(script, ceil)
	} /* end of string comparison for action. Up next is global (local really) function search */
	if(!ret && action->type == STS_STRING) /* look for functions to call */
	{
		GOTO_SET(&sts_defaults);
		STS_SCOPE_SEARCH(locals, action->string.data, action->string.length, row, {});
		if(row && ((sts_value_t *)row->value)->type == STS_FUNCTION)
		{
			VALUE_INIT(temp_value, STS_ARRAY); if(!temp_value){STS_ERROR_SIMPLE("could not create elipses value"); return NULL;}
			STS_SCOPE_PUSH(locals, {STS_ERROR_SIMPLE("couldnt create new scope level"); return NULL;});
			if(!sts_map_add_set(&locals->locals, "...", strlen("..."), temp_value))
			{
				STS_ERROR_SIMPLE("could not create local scope"); return NULL;
			}
			ACTION_BEGIN_ARGLOOP
				STS_VALUE_REFINC(script, eval_value);
				if(i < ((sts_value_t *)row->value)->function.argument_identifiers->array.length) /* create identifiers for each argument */
				{
					if(!sts_map_add_set(&locals->locals, ((sts_value_t *)row->value)->function.argument_identifiers->array.data[i]->string.data, ((sts_value_t *)row->value)->function.argument_identifiers->array.data[i]->string.length, eval_value))
					{
						STS_ERROR_SIMPLE("could not create local scope"); return NULL;
					}
				}
				else /* if extra arguments passed, put in elipses */
					STS_ARRAY_APPEND_INSERT(temp_value, eval_value, i);
				++i;
			ACTION_END_ARGLOOP
			if(i < ((sts_value_t *)row->value)->function.argument_identifiers->array.length){STS_ERROR_SIMPLE("too few arguments provided"); return NULL;}
			ret = sts_eval(script, ((sts_value_t *)row->value)->function.body->node, locals, NULL, 0, 0);
			STS_SCOPE_POP(locals, {STS_ERROR_SIMPLE("could not pop scope level"); return NULL;});
		}
	}
	return ret;
}

int sts_ast_apply_name(sts_script_t *script, sts_node_t *node, sts_name_container_t *name)
{
	do
	{
		if(node->name && (--node->name->references) <= 0) STS_FREE(node->name);
		node->name = name; name->references++;
		if(node->child && sts_ast_apply_name(script, node->child, name)) return 1;
	} while((node = node->next));

	return 0;
}

sts_node_t *sts_ast_copy(sts_script_t *script, sts_node_t *node)
{
	sts_node_t *ret = NULL, *progress_node = NULL;
	do
	{
		if(!ret)
		{
			if(!(ret = calloc(1, sizeof(sts_node_t))))
			{
				STS_ERROR_SIMPLE("could not copy node");
				continue;
			}
			progress_node = ret;
		}
		else
		{
			if(!(progress_node->next = calloc(1, sizeof(sts_node_t))))
			{
				STS_ERROR_SIMPLE("could not copy node");
				continue;
			}
			progress_node = progress_node->next;
		}
		progress_node->line =node->line;
		progress_node->type = node->type;
		progress_node->value = node->value;
		progress_node->name = node->name;
		node->name->references++;
		switch(node->type)
		{
			case STS_NODE_EXPRESSION:
				if(node->child)
				{
					if(!(progress_node->child = sts_ast_copy(script, node->child)))
					{
						STS_ERROR_SIMPLE("could not copy child node");
						continue;
					}
				}
			break;
			case STS_NODE_IDENTIFIER:
			case STS_NODE_VALUE:
				STS_VALUE_REFINC(script, node->value);
			break;
		}
	} while((node = node->next));
	return ret;
}

void sts_ast_delete(sts_script_t *script, sts_node_t *node)
{
	sts_node_t *temp = NULL;
	if(!node)
		return;
	do
	{
		switch(node->type)
		{
			case STS_NODE_EXPRESSION:
				if(node->child) sts_ast_delete(script, node->child);
			break;
			case STS_NODE_IDENTIFIER:
			case STS_NODE_VALUE:
				if(!sts_value_reference_decrement(script, node->value))
					STS_ERROR_SIMPLE("could not decrement references in ast");
			break;
		}
		if((--node->name->references) <= 0){ STS_FREE(node->name->script_name); STS_FREE(node->name);}
		temp = node;
		node = node->next;
		STS_FREE(temp);
	} while(node);
}

int sts_value_copy(sts_script_t *script, sts_value_t *dest, sts_value_t *source, int recursive)
{
	sts_value_t *temp = NULL; unsigned int i; int ret = 0;
	if(dest == source) return 0;
	switch(dest->type) /* destroy any info in the old dest type */
	{
		case STS_ARRAY: for(i = 0; i < dest->array.length; ++i) /* decrement references in array members */
			{
				if(!sts_value_reference_decrement(script, dest->array.data[i])) STS_ERROR_SIMPLE("could not decrement references in array value");
			}
			if(dest->array.data) STS_FREE(dest->array.data); dest->array.data = NULL; dest->array.length = dest->array.allocated = 0;
		break;
		case STS_STRING: if(dest->string.data) STS_FREE(dest->string.data); break;
		case STS_EXTERNAL: if(dest->external.refdec) if(dest->external.refdec((script), dest)) STS_ERROR_SIMPLE("could not decrement external data"); break;
		case STS_FUNCTION:
			if(dest->function.argument_identifiers) if(!sts_value_reference_decrement(script, dest->function.argument_identifiers)) STS_ERROR_SIMPLE("could not decrement references for argument identifiers in the destination value");
			if(dest->function.body && ((--dest->function.body->references) <= 0))
			{
				sts_ast_delete(script, dest->function.body->node);
				STS_FREE(dest->function.body);
			}
		break;
	}
	dest->type = source->type;
	switch(source->type)
	{
		case STS_NUMBER: dest->number = source->number; break;
		case STS_ARRAY: for(i = 0; i < source->array.length; ++i)
			{
				if(recursive)
				{
					if(!STS_CREATE_VALUE(temp))
					{
						STS_ERROR_SIMPLE("could not create value to copy into new recursive array"); ret = 1; break;
					}
					if((ret = sts_value_copy(script, temp, source->array.data[i], recursive)))
					{
						STS_ERROR_SIMPLE("could not copy value to copy into new recursive array temp value"); sts_value_reference_decrement(script, temp); break;
					}
					STS_VALUE_REFINC(script, temp); STS_ARRAY_APPEND_INSERT(dest, temp, i);
				}
				else
				{
					STS_VALUE_REFINC(script, source->array.data[i]);
					STS_ARRAY_APPEND_INSERT(dest, source->array.data[i], i);
				}
			}
		break;
		case STS_STRING: if(source->string.data) dest->string.data = sts_memdup(source->string.data, source->string.length); dest->string.length = source->string.length; break;
		case STS_EXTERNAL: memmove(&dest->external, &source->external, sizeof(source->external)); if(source->external.refinc) source->external.refinc((script), source); break;
		case STS_FUNCTION:
			dest->function.body = source->function.body;
			dest->function.body->references++;
			if(recursive)
			{
				if(!STS_CREATE_VALUE(temp))
				{
					STS_ERROR_SIMPLE("could not create value to copy into new function args"); ret = 1; break;
				}
				if((ret = sts_value_copy(script, temp, source->function.argument_identifiers, recursive)))
				{
					STS_ERROR_SIMPLE("could not copy value to copy into new rfunction args"); sts_value_reference_decrement(script, temp); break;
				}
				STS_VALUE_REFINC(script, temp); dest->function.argument_identifiers = temp;
			}
			else
			{
				dest->function.argument_identifiers = source->function.argument_identifiers;
				dest->function.argument_identifiers->references++;
			}
		break;
	}
	return ret;
}

int sts_value_test(sts_value_t *value) /* STS_NIL is always 0 */
{
	if(!value) return 0;
	switch(value->type) /* only if not nil, a 0 length string, a 0 lenght array, zero */
	{
		case STS_EXTERNAL: if(!value->external.data_ptr) return 0; break;
		case STS_NIL: return 0; break;
		case STS_NUMBER: if(!value->number) return 0; break;
		case STS_STRING: if(!value->string.length) return 0; break;
		case STS_ARRAY: if(!value->array.length) return 0; break;
		/* case STS_FUNCTION: do nothing. Its just always valid */
	}
	return 1;
}

sts_map_row_t *sts_map_add_set(sts_map_row_t **row, void *key, unsigned int key_size, void *value)
{
	sts_map_row_t *ret = NULL;
	unsigned int hash = STS_FNV_OFFSET;
	if(!*row)
	{
		STS_HASH(hash, key, key_size);
		if(!STS_CREATE_ROW(ret)) return NULL;
		*row = ret;
		ret->hash = hash;
	}
	else if(!(ret = sts_map_get(row, key, key_size)))
	{
		STS_HASH(hash, key, key_size);
		if(!STS_CREATE_ROW(ret)) return NULL;
		ret->next = *row;
		*row = ret;
		ret->hash = hash;
	}
	ret->value = value;
	return ret;
}

sts_map_row_t *sts_map_get(sts_map_row_t **row, void *key, unsigned int key_size)
{
	sts_map_row_t *ret = *row;
	unsigned int hash = STS_FNV_OFFSET;
	if(!*row) return NULL;
	STS_HASH(hash, key, key_size);
	do
	{
		if(ret->hash == hash) return ret;
	} while((ret = ret->next));
	return NULL;
}

int sts_map_remove(sts_map_row_t **row, void *key, unsigned int key_size)
{
	sts_map_row_t *current = *row, *previous = *row;
	unsigned int hash = STS_FNV_OFFSET;
	if(!*row) return 0;
	STS_HASH(hash, key, key_size);
	do
	{
		if(current->hash == hash)
		{
			if(current != previous) previous->next = current->next;
			else *row = current->next;
			STS_FREE(current);
			return 1;
		}
		previous = current;
	} while((current = current->next));
	return 0;
}

void *sts_memdup(void *src, unsigned int size)
{
	void *ret = NULL;
	if((ret = malloc(size + 1))) memcpy(ret, src, size), ((char *)ret)[size] = 0x0; /* allocate just one more. Mostly used for strings and it isnt that big of a deal */
	return ret;
}


#endif /* STS_IMPLEMENTATION */