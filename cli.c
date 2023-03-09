/* this file is released into the public domain */

/* embedding the extras util is optional. The interpreter itself
is able to run in an embedded environment without the extras, but
it is much nicer to use with these */
#define STS_EMBEDDING_EXTRAS_IMPLEMENTATION
#include "sts_embedding_extras.h"

/* ======= external public domain libs ======= */

/* absolutely terrible, but this makes the build script cleaner and stops multiple definitions of init */
#define init json_init
#include "ext/pdjson/pdjson.h"
#include "ext/pdjson/pdjson.c"
#undef init

#define base64_encode base64_encode_ext
#define base64_decode base64_decode_ext
#include "ext/base64/base64.h"
#include "ext/base64/base64.c"
#undef base64_encode
#undef base64_decode

#include "ext/Monocypher/src/monocypher.h"
#include "ext/Monocypher/src/monocypher.c"


#ifndef CLI_NO_SOCKETS

#define ZED_NET_IMPLEMENTATION
#include "ext/zed_net/zed_net.h"

#ifndef CLI_NO_TLS
#define TLS_AMALGAMATION
#include "ext/tlse/tlse.c"
#endif /* CLI_NO_TLS */

#endif /* CLI_NO_SOCKETS */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#if defined (_WIN64) || defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <windows.h>
#define CLI_WINDOWS
#else 
#include <sys/types.h>
#include <dirent.h>
#endif

/* =========================================== */


#ifndef CLI_ALLOW_SYSTEM
	#define CLI_ALLOW_SYSTEM 1
#endif

#if defined(CLI_SYSTEM_SHELLPREFIX) && !defined(CLI_ALLOW_SYSTEM)
	#undef CLI_SYSTEM_SHELLPREFIX
#endif

#define PS1 "#"

#ifndef INSTALL_DIR
	#define INSTALL_DIR "/usr/local/bin/"
#endif

typedef struct
{
	unsigned long references;
	zed_net_socket_t socket;
	SSL *ssl;
	char *name;
} cli_socket_t;

#define CLI_SOCKET(value) ((cli_socket_t *)value->external.data_ptr)
#define IS_CLI_SOCKET(value) (value->type == STS_EXTERNAL && value->external.refdec == &cli_socket_refdec)

int cli_socket_refdec(sts_script_t *script, sts_value_t *value)
{
	if(IS_CLI_SOCKET(value) && (!CLI_SOCKET(value)->references || !--CLI_SOCKET(value)->references))
	{
		zed_net_socket_close(&CLI_SOCKET(value)->socket);
		if(CLI_SOCKET(value)->ssl)
		{
			SSL_shutdown(CLI_SOCKET(value)->ssl);
			SSL_CTX_free(CLI_SOCKET(value)->ssl);
		}
		if(CLI_SOCKET(value)->name) free(CLI_SOCKET(value)->name);
		free(value->external.data_ptr);
	}

	return 0;
}

void cli_socket_refinc(sts_script_t *script, sts_value_t *value)
{
	if(IS_CLI_SOCKET(value))
		CLI_SOCKET(value)->references++;
}

sts_value_t *cli_socket_new(sts_script_t *script)
{
	sts_value_t *ret = NULL;


	if(!(ret = sts_value_create(script, STS_EXTERNAL)))
	{
		STS_ERROR_SIMPLE("could not create socket external value");
		goto error;
	}

	ret->external.refdec = &cli_socket_refdec;
	ret->external.refinc = &cli_socket_refinc;

	if(!(ret->external.data_ptr = calloc(1, sizeof(cli_socket_t))))
	{
		STS_ERROR_SIMPLE("could not create socket type value");
		goto error;
	}

	CLI_SOCKET(ret)->references = 1;

	return ret;
error:
	if(ret) free(ret);
	return NULL;
}

void debug_ast(sts_node_t *node, int level)
{
	int i;
	
	while(1)
	{
		for(i = 0; i < level; i++)
		{
			printf(" ");
		}
		
		printf("%u:", node->line);
		
		switch(node->type)
		{
			case STS_NODE_EXPRESSION:
				printf("expression:\n");
				if(node->child)
					debug_ast(node->child, level + 1);
			break;
			case STS_NODE_IDENTIFIER:
				printf("ident: '%s'\n", node->value->string.data);
			break;
			case STS_NODE_VALUE:
				if(node->value)
				{
					switch(node->value->type)
					{
						case STS_ARRAY:
							printf("array(%u): \n", node->value->references);
						break;
						case STS_EXTERNAL:
							printf("external(%u): \n", node->value->references);
						break;
						case STS_NUMBER:
							printf("number(%u): %f\n", node->value->references, node->value->number);
						break;
						case STS_STRING:
							printf("string(%u): '%s'\n", node->value->references, node->value->string.data);
						break;
					}
				}
				else
					printf("null value\n");
			break;
		}
		if(!(node = node->next))
			break;
	}
}

int amemcat(char **dest, unsigned long *dest_len, char *src, unsigned long src_len)
{
	char *temp = NULL;


	if(!dest || !dest_len) return 1;

	if(!(temp = realloc(*dest, *dest_len + src_len + 1)))
		return 1;
	
	memcpy(&temp[*dest_len], src, src_len);
	temp[*dest_len + src_len] = 0;

	*dest = temp;
	*dest_len += src_len;

	return 0;
}

int equal_open_close(char *input)
{
	unsigned int i = 0, open = 0, close = 0, instring = 0;


	if(!input) return 0;

	for(; i < strlen(input); ++i)
	{
		switch(instring)
		{
			case 0:
				if(input[i] == '\"')
					instring = 1;
				else if(input[i] == '(' || input[i] == '{' || input[i] == '[')
					open++;
				else if(input[i] == ')' || input[i] == '}' || input[i] == ']')
					close++;
			break;
			case 1:
				if(input[i] == '\\' && input[i] == '\"')
					continue;
				else if(input[i] == '\"')
					instring = 0;
			break;
		}
	}

	return (open == close);
}

/* TODO: maybe dont make this recursive */
sts_value_t *sts_value_from_json(sts_script_t *script, json_stream *json, char *json_str, unsigned long length)
{
	sts_value_t *ret = NULL, *temp = NULL, *temp_container = NULL;
	json_stream new_json;
	unsigned long i = 0, str_length = 0, init_json = 0;
	char *str = NULL;
	int type;
	

	/* initialize stream */
	if(!json)
	{
		json_open_string(&new_json, json_str);
		json = &new_json;
		init_json = 1;
	}

	type = json_next(json);

	switch(type)
	{
		case JSON_NULL:
			if(!(ret = sts_value_create(script, STS_NIL)))
			{
				fprintf(stderr, "could not create nil value\n");
				return NULL;
			}

			return ret;
		break;
		case JSON_TRUE:
			if(!(ret = sts_value_from_boolean(script, 1)))
			{
				fprintf(stderr, "could not create true value\n");
				return NULL;
			}

			return ret;
		break;
		case JSON_FALSE:
			if(!(ret = sts_value_from_boolean(script, 0)))
			{
				fprintf(stderr, "could not create false value\n");
				return NULL;
			}

			return ret;
		break;
		case JSON_NUMBER:
			if(!(ret = sts_value_from_number(script, json_get_number(json))))
			{
				fprintf(stderr, "could not create true value\n");
				return NULL;
			}

			return ret;
		break;
		case JSON_STRING:
			str = json_get_string(json, &str_length);

			/* NOTE: pdjson likes to include \0 in the string length. No idea why */
			if(!(ret = sts_value_from_nstring(script, str, str_length - 1)))
			{
				fprintf(stderr, "could not create true value");
				return NULL;
			}

			return ret;
		break;
		case JSON_ARRAY:
			if(!(ret = sts_value_create(script, STS_ARRAY)))
			{
				fprintf(stderr, "could not create array value\n");
				return NULL;
			}

			while((type = json_peek(json)) != JSON_DONE && type != JSON_ERROR && type != JSON_ARRAY_END)
			{
				if(!(temp = sts_value_from_json(script, json, json_str, length)))
				{
					fprintf(stderr, "could not add member to array\n");
					sts_value_reference_decrement(script, ret);
					return NULL;
				}

				STS_ARRAY_APPEND_INSERT(ret, temp, i);
				++i;
			}

			json_next(json);
		break;
		case JSON_OBJECT:
			if(!(ret = sts_value_create(script, STS_ARRAY)))
			{
				fprintf(stderr, "could not create hashmap array value\n");
				return NULL;
			}

			while((type = json_peek(json)) != JSON_DONE && type != JSON_ERROR && type != JSON_OBJECT_END)
			{
				/* create the key */

				/* NOTE: this would be more compact but gcc has really weird behavior. Works perfectly fine on clang */
				str = json_get_string(json, &str_length);
				str = sts_memdup(str, str_length);

				json_next(json);

				if(!(temp = sts_value_from_json(script, json, json_str, length)))
				{
					fprintf(stderr, "could not add member to object\n");
					sts_value_reference_decrement(script, ret);
					free(str);
					return NULL;
				}

				if(sts_hashmap_set(script, ret, str, str_length, temp))
				{
					fprintf(stderr, "could not add '%.*s' to hashmap\n", str_length, str);
					sts_value_reference_decrement(script, ret);
					free(str);
					return NULL;
				}

				free(str);
			}

			json_next(json);
		break;
		case JSON_ERROR:
			fprintf(stderr, "json parse error on line %ld: '%s'\n", json_get_lineno(json), json_get_error(json));
			return NULL;
		break;
	}

	if(init_json)
		json_close(json);


	return ret;
}

int sts_hashmap_verify(sts_script_t *script, sts_value_t *value)
{
	unsigned long i;

	/* hashmaps are only a concept of the stdlib itself so we can only guess if its actually a hashmap */
	if(value->type == STS_ARRAY && 
		value->array.length && 
		value->array.data[0]->type == STS_ARRAY && 
		value->array.data[0]->array.length == 3
	)
	{
		for(i = 0; i < value->array.length; ++i)
		{
			/* verify first hash number, then key string, and that every row has 3 values */

			if(value->array.data[i]->type != STS_ARRAY || 
				value->array.data[i]->array.length != 3 ||
				value->array.data[i]->array.data[0]->type != STS_NUMBER ||
				value->array.data[i]->array.data[1]->type != STS_STRING
			)
			{
				return 0;
			}

			/* TODO: determine if its worth it to hash every string and see if the number is true */
		}

		return 1;
	}

	return 0;
}

char *escape_string(char *str, unsigned long str_len, unsigned long *out_len)
{
	char *ret = NULL;
	unsigned long i;

	#define ESCAPE_WITH(offset, as) do{	\
			if(!(ret = realloc(ret, *out_len + strlen(as) + 1)))	\
			{	\
				fprintf(stderr, "could not resize escaped string\n");	\
				return NULL;	\
			}	\
			memmove(&ret[i + strlen(as)], &ret[i + 1], (*out_len + 1) - (i + 1));	\
			memcpy(&ret[i], as, strlen(as));	\
			*out_len += strlen(as) - 1;	\
			i += strlen(as) - 1;	\
		}while(0)


	if(!(ret = sts_memdup(str, str_len)))
	{
		fprintf(stderr, "could not duplicate string\n");
		return NULL;
	}

	/* initialize out length */
	*out_len = str_len;


	for(i = 0; i < *out_len; ++i)
	{
		switch(ret[i])
		{
			case '\"':
				ESCAPE_WITH(i, "\\\"");
			break;
			case '\\':
				ESCAPE_WITH(i, "\\\\");
			break;
			case '\b':
				ESCAPE_WITH(i, "\\b");
			break;
			case '\t':
				ESCAPE_WITH(i, "\\t");
			break;
			case '\n':
				ESCAPE_WITH(i, "\\n");
			break;
			case '\r':
				ESCAPE_WITH(i, "\\r");
			break;
			case '\f':
				ESCAPE_WITH(i, "\\f");
			break;
			case '/':
				ESCAPE_WITH(i, "\\/");
			break;
		}
	}

	return ret;

	#undef ESCAPE_WITH
}

int sts_json_from_value(sts_script_t *script, char **str_buf, sts_value_t *value, int pretty, int depth, unsigned long *len)
{
	char num_buf[512], *temp_str = NULL;
	unsigned long temp_len = 0, i;


	#define STS_JSON_ERR(msg) do{ fprintf(stderr, "could not concat data onto json string%s%s\n", msg ? ": " : "", msg ? msg : ""); goto error;} while(0)
	#define STS_JSON_EMIT_INDENT(n) do{ unsigned long i; for(i = 0; i < (n); ++i){ if(amemcat(str_buf, len, "\t", 1)) STS_JSON_ERR("indent mem error"); } }while(0)
	#define STS_JSON_EMIT(n_depth, str, str_len) do{ if(pretty) STS_JSON_EMIT_INDENT(n_depth); if(amemcat(str_buf, len, (str), (str_len))) STS_JSON_ERR("mem error"); }while(0)
	#define STS_JSON_EMIT_ALONE(str, str_len) do{ if(amemcat(str_buf, len, (str), (str_len))) STS_JSON_ERR("mem error"); }while(0)
	#define STS_JSON_ENDLINE do{ if(pretty) STS_JSON_EMIT_ALONE("\n", 1); }while(0)


	if(!value) STS_JSON_ERR("value is null in json string convert\n");

	switch(value->type)
	{
		case STS_NIL:
			STS_JSON_EMIT_ALONE("null", 4);
		break;
		case STS_NUMBER:
			temp_len = snprintf(num_buf, 512, "%1.17g", value->number);
			STS_JSON_EMIT_ALONE(num_buf, temp_len);
		break;
		case STS_BOOLEAN:
			STS_JSON_EMIT_ALONE(value->boolean ? "true" : "false", value->boolean ? 4 : 5);
		break;
		case STS_STRING:
			STS_JSON_EMIT_ALONE("\"", 1);
			if(!(temp_str = escape_string(value->string.data, value->string.length, &temp_len)))
				STS_JSON_ERR("could not escape string");
			STS_JSON_EMIT_ALONE(temp_str, temp_len);
			STS_JSON_EMIT_ALONE("\"", 1);
			free(temp_str);
		break;
		case STS_FUNCTION:
			/* TODO: print stringified function? */
			STS_JSON_EMIT_ALONE("null", 4);
		break;
		case STS_ARRAY:
			/* its an object */
			if(sts_hashmap_verify(script, value))
			{
				STS_JSON_EMIT_ALONE("{", 1); STS_JSON_ENDLINE;

				for(i = 0; i < value->array.length; ++i)
				{
					STS_JSON_EMIT(depth + 1, "\"", 1);
					if(!(temp_str = escape_string(value->array.data[i]->array.data[1]->string.data, value->array.data[i]->array.data[1]->string.length, &temp_len)))
						STS_JSON_ERR("could not escape string");
					STS_JSON_EMIT_ALONE(temp_str, temp_len);
					STS_JSON_EMIT_ALONE("\": ", 3);

					if(sts_json_from_value(script, str_buf, value->array.data[i]->array.data[2], pretty, depth + 1, len))
						STS_JSON_ERR("could not generate json string for array in object\n");

					if(i != value->array.length - 1)
						STS_JSON_EMIT_ALONE(",", 1);
					STS_JSON_ENDLINE;
					free(temp_str);
				}

				STS_JSON_EMIT(depth, "}", 1);
			}
			/* its an array */
			else
			{
				STS_JSON_EMIT_ALONE("[", 1);

				for(i = 0; i < value->array.length; ++i)
				{
					if(sts_json_from_value(script, str_buf, value->array.data[i], pretty, depth + 1, len))
						STS_JSON_ERR("could not generate json string for array in array\n");

					if(i != value->array.length - 1)
						STS_JSON_EMIT_ALONE(",", 1);
				}

				STS_JSON_EMIT_ALONE("]", 1);
			}
		break;
	}
	

	return 0;
error:
	if(temp_str) free(temp_str);
	return 1;

	#undef STS_JSON_ERR
	#undef STS_JSON_EMIT_INDENT
	#undef STS_JSON_EMIT
	#undef STS_JSON_EMIT_ALONE
	#undef STS_JSON_ENDLINE
}

/* control simple tiny script */

int repl(sts_script_t *script)
{
	int quit = 0, blank = 1;
	size_t size = 0;
	unsigned int offset = 0, line = 0;
	sts_value_t *ret = NULL;
	sts_node_t *temp_node = NULL, *current_node = NULL;
	char *current_buffer = NULL, *temp_buffer = NULL;
	/* know if ready to parse by making sure '['s and ']'s are even */

	while(!quit)
	{
		printf("%s ", PS1);
		while(!equal_open_close(current_buffer) || current_buffer[strlen(current_buffer) - 2] == '\\')
		{
			if(current_buffer)
				printf("> ");
			
			getline(&temp_buffer, &size, stdin);

			if(!strcmp(temp_buffer, "exit\n")) /* exit isnt a real action but it needs to clean up in a simple way so this works */
			{
				free(temp_buffer);
				free(current_buffer);
				if(!sts_destroy(script))
					fprintf(stderr, "problem cleaning up script");
				return 0;
			}

			current_buffer = realloc(current_buffer, (current_buffer ? strlen(current_buffer) : 0) + strlen(temp_buffer) + 1); /* honestly, this is a really simple and hacky repl. At this point, it really doesn't matter in this part if we check for realloc errors */
			if(blank)
			{
				current_buffer[0] = 0x0;
				blank = 0;
			}
			strcat(current_buffer, temp_buffer);

			free(temp_buffer);
			temp_buffer = NULL;
		}
		
		/* parse and eval */

		if(!(current_node = sts_parse(script, NULL, current_buffer, "repl.sts", &offset, &line)))
		{
			fprintf(stderr, "parser error\n");
		}


		/* eval */
		ret = sts_eval(script, current_node, NULL, NULL, 0, 0);
		if(ret && !sts_value_reference_decrement(script, ret)) fprintf(stderr, "could not clean up returned value\n");
		else if(!ret) fprintf(stderr, "command error\n");

		sts_ast_delete(script, current_node);
		free(current_buffer);
		current_buffer = NULL;
		blank = 1;
		offset = line = 0;
	}

	/* cleanup */

	if(!sts_destroy(script))
		fprintf(stderr, "problem cleaning up script");
	
	return 0;
}

char *read_file(sts_script_t *script, char *file, unsigned int *size)
{
	FILE *script_file = NULL;
	char *ret = NULL;


	if(!(script_file = fopen(file, "r")))
	{
		return NULL;
	}
	
	fseek(script_file, 0, SEEK_END);
	*size = ftell(script_file);
	fseek(script_file, 0, SEEK_SET);
	
	if(!(ret = calloc((*size) + 1, sizeof(char))))
	{
		fprintf(stderr, "could not calloc script text buffer\n");
		fclose(script_file);
		return NULL;
	}
	
	if(fread(ret, (*size), sizeof(char), script_file) <= 0)
	{
		fprintf(stderr, "could not read script file\n");
		free(ret);
		fclose(script_file);
		return NULL;
	}
	
	fclose(script_file);


	return ret;
}

char *import(sts_script_t *script, char *file)
{
	unsigned int size = 0;
	char *ret = NULL, *assemble_string = NULL;


	STS_STRING_ASSEMBLE(assemble_string, size, INSTALL_DIR, strlen(INSTALL_DIR), file, strlen(file));

	ret = read_file(script, assemble_string, &size);

	STS_FREE(assemble_string);

	return ret;
}

sts_value_t *cli_actions(sts_script_t *script, sts_value_t *action, sts_node_t *args, sts_scope_t *locals, sts_value_t **previous)
{
	sts_value_t *ret = NULL, *eval_value = NULL, *temp_value = NULL, *first_arg_value = NULL, *second_arg_value = NULL, *third_arg_value = NULL;
	FILE *proc_pipe = NULL, *file = NULL;
	zed_net_address_t address;
	char *temp_str = NULL, *popen_buf = NULL, buf[1024];
	unsigned int i = 0, size = 0, total = 0, temp_uint = 0;
	unsigned long temp_ulong = 0;
	void *work_area = NULL;
	int temp_int = 0;


	GOTO_JMP(&cli_actions);
	if(!GOTO_ACTIVATED && action->type == STS_STRING)
	{
		ACTION(if, "pipeout")
		{
			GOTO_SET(&cli_actions);
			/* the first arg is the string that comes from stdout and
			the remaining arguments are the command part */
			
			EVAL_ARG(args->next);
			first_arg_value = eval_value;
			STS_VALUE_EXPECT_MUTABLE(first_arg_value, return NULL);

			args = args->next;

			ACTION_BEGIN_ARGLOOP
				switch(eval_value->type)
				{
					case STS_NUMBER: STS_STRING_ASSEMBLE_FMT(temp_str, size, "%1.17g", eval_value->number, " ", 1); break;
					case STS_BOOLEAN: STS_STRING_ASSEMBLE_FMT(temp_str, size, "%s", eval_value->boolean ? "true" : "false", " ", 1); break;
					case STS_STRING: STS_STRING_ASSEMBLE(temp_str, size, eval_value->string.data, eval_value->string.length, " ", 1); break;
				}
			ACTION_END_ARGLOOP

			if((proc_pipe = popen(temp_str, "r")))
			{
				while((size = fread(buf, 1, sizeof(buf), proc_pipe)) > 0)
				{
					if(!(popen_buf = realloc(popen_buf, total + size + 1)))
					{
						fprintf(stderr, "could not resize pipe buffer\n");
						break;
					}

					memmove(&popen_buf[total], buf, size);
					total += size;
					popen_buf[total] = 0x0;
				}

				/* make string into sts value */
				VALUE_INIT(temp_value, STS_STRING);

				/* copy it to the first arg to set it to a string only */
				if(sts_value_copy(script, first_arg_value, temp_value, 0))
				{
					fprintf(stderr, "could not set value to string type\n");
				}

				first_arg_value->string.data = popen_buf;
				first_arg_value->string.length = total;

				/* cleanup temporary value */

				if(!sts_value_reference_decrement(script, temp_value))
					STS_ERROR_SIMPLE("could not decrement references for temporary value in pipeout action");
				

				VALUE_FROM_NUMBER(ret, (double)pclose(proc_pipe));
			}
			else
			{
				fprintf(stderr, "could not execute command provided\n");
			}

			free(temp_str);
			if(!sts_value_reference_decrement(script, first_arg_value))
				STS_ERROR_SIMPLE("could not decrement references for first argument in pipeout action");
		}
		ACTION(else if, "file-read") /* reads files into a string */
		{
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);
				if(eval_value->type != STS_STRING)
				{
					fprintf(stderr, "the file-read action requires a string argument for the path\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in file-read action");
					return NULL;
				}

				if(!(file = fopen(eval_value->string.data, "rb")))
				{
					VALUE_INIT(ret, STS_NIL);
				}
				else
				{
					fseek(file, 0, SEEK_END);
					temp_uint = ftell(file);
					fseek(file, 0, SEEK_SET);

					if(!(temp_str = calloc(temp_uint + 1, sizeof(char))))
					{
						fprintf(stderr, "could not alocate string in read-file\n");
						VALUE_INIT(ret, STS_NIL);
						if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in file-read action");
						return ret;
					}
					
					if(fread(temp_str, temp_uint, sizeof(char), file) <= 0)
					{
						fprintf(stderr, "could not read file in read-file\n");
					}

					VALUE_INIT(ret, STS_STRING);
					ret->string.data = temp_str;
					ret->string.length = temp_uint;

					fclose(file);
				}

				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in file-read action");
			}
			else {STS_ERROR_SIMPLE("file-read action requires a single string path argument"); return NULL;}
		}
		ACTION(else if, "file-write") /* writes a string to a file */
		{
			GOTO_SET(&cli_actions);
			if(args->next && args->next->next)
			{
				EVAL_ARG(args->next); first_arg_value = eval_value;
				EVAL_ARG(args->next->next);

				if(first_arg_value->type != STS_STRING && eval_value->type != STS_STRING)
				{
					fprintf(stderr, "file-write requires 2 string arguments only\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in file-write action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in file-write action");
					return NULL;
				}

				if(!(file = fopen(first_arg_value->string.data, "wb")))
				{
					VALUE_INIT(ret, STS_NIL);
				}
				else
				{
					VALUE_FROM_NUMBER(ret, fwrite(eval_value->string.data, sizeof(char), eval_value->string.length, file));
					fclose(file);
				}
				
				if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in file-write action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in file-write action");
			}
			else {STS_ERROR_SIMPLE("file-write action requires at least 2 string arguments"); return NULL;}
		}
		ACTION(else if, "file-append") /* appends a string to a file */
		{
			GOTO_SET(&cli_actions);
			if(args->next && args->next->next)
			{
				EVAL_ARG(args->next); first_arg_value = eval_value;
				EVAL_ARG(args->next->next);

				if(first_arg_value->type != STS_STRING && eval_value->type != STS_STRING)
				{
					fprintf(stderr, "file-append requires 2 string arguments only\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in file-append action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in file-append action");
					return NULL;
				}

				if(!(file = fopen(first_arg_value->string.data, "ab")))
				{
					VALUE_INIT(ret, STS_NIL);
				}
				else
				{
					VALUE_FROM_NUMBER(ret, fwrite(eval_value->string.data, sizeof(char), eval_value->string.length, file));
					fclose(file);
				}
				
				if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in file-append action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in file-append action");
			}
			else {STS_ERROR_SIMPLE("file-append action requires at least 2 string arguments"); return NULL;}
		}
		ACTION(else if, "stdin-read") /* if <=0, it reads until eof. If >0, it will read at least that many characters */
		{
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);
				if(eval_value->type != STS_NUMBER && eval_value->type != STS_STRING)
				{
					fprintf(stderr, "the stdin-read action requires a number argument for the amount to read or a character (single char long string) for the character to read until\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the stdin-read action");
					return NULL;
				}

				if(eval_value->type == STS_NUMBER)
				{
					while((size = fread(buf, 1, ((unsigned int)eval_value->number <= 0) ? sizeof(buf) : (unsigned int)eval_value->number, stdin)) > 0)
					{
						if(!(temp_str = realloc(temp_str, total + size + 1)))
						{
							fprintf(stderr, "could not resize stdin buffer\n");
							break;
						}

						memmove(&temp_str[total], buf, size);
						total += size;
						temp_str[total] = 0x0;

						if(eval_value->number > 0 && total == eval_value->number)
							break;
					}
				}
				else
				{
					while((size = fread(buf, 1, 1, stdin)) > 0)
					{
						if(!(temp_str = realloc(temp_str, total + size + 1))) /* not very quick */
						{
							fprintf(stderr, "could not resize stdin buffer\n");
							break;
						}

						memmove(&temp_str[total], buf, size);
						total += size;
						temp_str[total] = 0x0;

						if(*eval_value->string.data == temp_str[total - 1])
						{
							temp_str[total - 1] = 0x0;
							break;
						}
					}
				}

				VALUE_INIT(ret, STS_STRING);

				ret->string.data = temp_str;
				ret->string.length = total;

				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in stdin-read action");
			}
			else {STS_ERROR_SIMPLE("stdin-read action requires a single string path argument"); return NULL;}
		}
		ACTION(else if, "stdout-write") /* writes raw strings to stdout */
		{
			GOTO_SET(&cli_actions);
			ACTION_BEGIN_ARGLOOP
				switch(eval_value->type)
				{
					case STS_NUMBER: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%1.17g", eval_value->number, " ", 0); break;
					case STS_NIL: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%s", "nil", " ", 0); break;
					case STS_ARRAY: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "[array passed and is %u elements long]", eval_value->array.length,  " ", 0); break;
					case STS_STRING: STS_STRING_ASSEMBLE(temp_str, temp_uint, eval_value->string.data, eval_value->string.length, " ", 0); break;
					case STS_EXTERNAL: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%p", eval_value->external.data_ptr, " ", 0); break;
					case STS_FUNCTION: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "[function passed and it takes %u arguments]", eval_value->function.argument_identifiers->array.length, " ", 0); break;
					case STS_BOOLEAN: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%s", eval_value->boolean ? "true" : "false", " ", 0); break;
				}
			ACTION_END_ARGLOOP
			for(i = 0; i < temp_uint; ++i)
				fputc(temp_str[i], stdout);
			STS_FREE(temp_str);
			VALUE_FROM_NUMBER(ret, 1);
		}
		ACTION(else if, "stderr-write") /* writes raw strings to stderr */
		{
			GOTO_SET(&cli_actions);
			ACTION_BEGIN_ARGLOOP
				switch(eval_value->type)
				{
					case STS_NUMBER: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%1.17g", eval_value->number, " ", 0); break;
					case STS_NIL: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%s", "nil", " ", 0); break;
					case STS_ARRAY: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "[array passed and is %u elements long]", eval_value->array.length,  " ", 0); break;
					case STS_STRING: STS_STRING_ASSEMBLE(temp_str, temp_uint, eval_value->string.data, eval_value->string.length, " ", 0); break;
					case STS_EXTERNAL: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%p", eval_value->external.data_ptr, " ", 0); break;
					case STS_FUNCTION: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "[function passed and it takes %u arguments]", eval_value->function.argument_identifiers->array.length, " ", 0); break;
					case STS_BOOLEAN: STS_STRING_ASSEMBLE_FMT(temp_str, temp_uint, "%s", eval_value->boolean ? "true" : "false", " ", 0); break;
				}
			ACTION_END_ARGLOOP
			for(i = 0; i < temp_uint; ++i)
				fputc(temp_str[i], stderr);
			STS_FREE(temp_str);
			VALUE_FROM_NUMBER(ret, 1);
		}
		ACTION(else if, "getenv") /* gets a string from the system shell environment */
		{
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);
				if(eval_value->type != STS_STRING)
				{
					fprintf(stderr, "the getenv action requires a string argument for the env variable\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in getenv action");
					return NULL;
				}

				if(!(temp_str = getenv(eval_value->string.data)))
				{
					VALUE_INIT(ret, STS_NIL);
				}
				else
				{
					VALUE_INIT(ret, STS_STRING);
					if(!(ret->string.data = strdup(temp_str)))
					{
						fprintf(stderr, "couldnt duplicate environment variable value\n");
						if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in getenv action");
						return NULL;
					}
					ret->string.length = strlen(temp_str);
				}

				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in getenv action");
			}
			else {STS_ERROR_SIMPLE("getenv action requires a single string path argument"); return NULL;}
		}
		ACTION(else if, "setenv") /* write a new or overwrite old env variable */
		{
			GOTO_SET(&cli_actions);
			if(args->next && args->next->next)
			{
				EVAL_ARG(args->next); first_arg_value = eval_value;
				EVAL_ARG(args->next->next);

				if(first_arg_value->type != STS_STRING && eval_value->type != STS_STRING)
				{
					fprintf(stderr, "setenv requires 2 string arguments only\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in setenv action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in setenv action");
					return NULL;
				}

				VALUE_FROM_NUMBER(ret, (double)setenv(first_arg_value->string.data, eval_value->string.data, 1));
				
				if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in setenv action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in setenv action");
			}
			else {STS_ERROR_SIMPLE("setenv action requires at least 2 string arguments"); return NULL;}
		}
		ACTION(else if, "sleep") /* sleeps for the number of seconds provided */
		{
			#ifdef __unix__
			struct timespec ts;
			#endif
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);
				if(eval_value->type != STS_NUMBER )
				{
					fprintf(stderr, "the sleep action requires a number argument for the number of seconds to sleep\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the sleep action");
					return NULL;
				}

				#ifdef __unix__
				ts.tv_sec = eval_value->number;
				ts.tv_nsec = fmod(eval_value->number, 1.0) * 1000000000.0;
				nanosleep(&ts, NULL);
				#elif defined(WIN32)
				Sleep(eval_value->number);
				#else
				sleep((unsigned int)eval_value->number);
				#endif
				VALUE_FROM_NUMBER(ret, 1.0);

				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in sleep action");
			}
			else {STS_ERROR_SIMPLE("sleep action requires a single number argument"); return NULL;}
		}
		ACTION(else if, "json") /* parses json into sts values or outputs a stringified version */
		{
			GOTO_SET(&cli_actions);
			/* parsing from string */
			if(args->next && !args->next->next)
			{
				EVAL_ARG(args->next);
				if(eval_value->type != STS_STRING )
				{
					fprintf(stderr, "the json action, if passed one argument, requires a string\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the json action");
					return NULL;
				}

				if(!(ret = sts_value_from_json(script, NULL, eval_value->string.data, eval_value->string.length)))
				{
					STS_ERROR_SIMPLE("could not parse json into value");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the json action");
					return NULL;
				}

				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in json action");
			}
			else if(args->next && args->next->next)
			{
				EVAL_ARG(args->next);
				first_arg_value = eval_value;
				EVAL_ARG(args->next->next);

				if(eval_value->type != STS_NUMBER)
				{
					fprintf(stderr, "the json action, if passed two arguments, requires the second to be a number (boolean)\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the json action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the json action");
					return NULL;
				}

				if(sts_json_from_value(script, &temp_str, first_arg_value, eval_value->number, 0, &temp_ulong))
				{
					STS_ERROR_SIMPLE("could not create json string from value");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the json action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the json action");
					return NULL;
				}


				if(!(ret = sts_value_create(script, STS_STRING)))
				{
					STS_ERROR_SIMPLE("could not create sts string for json string");
					free(temp_str);
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the json action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the json action");
					return NULL;
				}

				/* dont duplicate the string just created with nstring */
				ret->string.data = temp_str;
				ret->string.length = temp_ulong;

				if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the json action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the json action");
			}
			else {STS_ERROR_SIMPLE("json action requires either single string or any value and a number for pretty printing"); return NULL;}
		}
		#ifndef CLI_NO_SOCKETS
		ACTION(else if, "socket-tcp") /* creates new tcp socket. args are (port, non_blocking, listening) */
		{
			GOTO_SET(&cli_actions);
			if(args->next && args->next->next && args->next->next->next)
			{
				EVAL_ARG(args->next);
				first_arg_value = eval_value;
				EVAL_ARG(args->next->next);
				second_arg_value = eval_value;
				EVAL_ARG(args->next->next->next);

				if(first_arg_value->type != STS_NUMBER || second_arg_value->type != STS_NUMBER || eval_value->type != STS_NUMBER)
				{
					fprintf(stderr, "the socket-tcp action requires 3 number arguments\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp action");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-tcp action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the third argument in the socket-tcp action");
					return NULL;
				}

				if(!(ret = cli_socket_new(script)))
				{
					STS_ERROR_SIMPLE("could not create return socket-tcp");

					/* return nil */

					if(!(ret = sts_value_create(script, STS_NIL)))
						STS_ERROR_SIMPLE("could not create nil socket in socket-tcp");
					

					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp action");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-tcp action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the third argument in the socket-tcp action");
					return ret;
				}

				if(zed_net_tcp_socket_open(&CLI_SOCKET(ret)->socket, first_arg_value->number, second_arg_value->number, eval_value->number))
				{
					STS_ERROR_SIMPLE("could not open socket");

					/* return nil */

					if(!sts_value_reference_decrement(ret, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the old ret in the socket-tcp action");

					if(!(ret = sts_value_create(script, STS_NIL)))
						STS_ERROR_SIMPLE("could not create nil socket in socket-tcp");
					
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp action");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-tcp action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the third argument in the socket-tcp action");
					return ret;
				}

				if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp action");
				if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-tcp action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the third argument in the socket-tcp action");
			}
			else {STS_ERROR_SIMPLE("socket-tcp action requires 3 arguments"); return NULL;}
		}
		ACTION(else if, "socket-udp") /* creates new udp socket. args are (port, non_blocking) */
		{
			GOTO_SET(&cli_actions);
			if(args->next && args->next->next && args->next->next->next)
			{
				EVAL_ARG(args->next);
				first_arg_value = eval_value;
				EVAL_ARG(args->next->next);
				second_arg_value = eval_value;

				if(first_arg_value->type != STS_NUMBER || second_arg_value->type != STS_NUMBER)
				{
					fprintf(stderr, "the socket-udp action requires 3 number arguments\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-udp action");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-udp action");
					return NULL;
				}

				if(!(ret = cli_socket_new(script)))
				{
					STS_ERROR_SIMPLE("could not create return socket-udp");

					/* return nil */

					if(!(ret = sts_value_create(script, STS_NIL)))
						STS_ERROR_SIMPLE("could not create nil socket in socket-tcp");

					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-udp action");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-udp action");
					return ret;
				}

				if(zed_net_udp_socket_open(&CLI_SOCKET(ret)->socket, first_arg_value->number, second_arg_value->number))
				{
					STS_ERROR_SIMPLE("could not open socket");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-udp action");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-udp action");
					return NULL;
				}

				if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-udp action");
				if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-udp action");
			}
			else {STS_ERROR_SIMPLE("socket-udp action requires 3 arguments"); return NULL;}
		}
		ACTION(else if, "socket-tcp-connect") /* connects to tcp server (socket, host, port) */
		{
			GOTO_SET(&cli_actions);
			if(args->next && args->next->next && args->next->next->next)
			{
				EVAL_ARG(args->next);
				first_arg_value = eval_value;
				EVAL_ARG(args->next->next);
				second_arg_value = eval_value;
				EVAL_ARG(args->next->next->next);

				if(!IS_CLI_SOCKET(first_arg_value) || second_arg_value->type != STS_STRING || eval_value->type != STS_NUMBER)
				{
					fprintf(stderr, "the socket-tcp-connect action requires a socket, astring argument for the address, and a number argument for the port\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-connect action");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-tcp-connect action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the third argument in the socket-tcp-connect action");
					return NULL;
				}
				
				temp_uint = 0;

				if(zed_net_get_address(&address, second_arg_value->string.data, eval_value->number))
				{
					fprintf(stderr, "could not get the address in socket-tcp-connect: %s\n", zed_net_get_error());
					temp_uint = 1;
				}
				else if(zed_net_tcp_connect(&CLI_SOCKET(first_arg_value)->socket, address))
				{
					fprintf(stderr, "could not connect to host in socket-tcp-connect: %s\n", zed_net_get_error());
					temp_uint = 2;
				}

				if(!(CLI_SOCKET(first_arg_value)->name = sts_memdup(second_arg_value->string.data, second_arg_value->string.length)))
				{
					STS_ERROR_SIMPLE("could not duplicate the host string in socket-tcp-connect");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-connect action");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-tcp-connect action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the third argument in the socket-tcp-connect action");
					return NULL;
				}

				VALUE_FROM_NUMBER(ret, temp_uint);

				if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-connect action");
				if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-connect action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the third argument in the socket-tcp-connect action");
			}
			else {STS_ERROR_SIMPLE("socket-tcp-connect action requires 4 arguments"); return NULL;}
		}
		ACTION(else if, "socket-tcp-send") /* sends a string buffer to the host in the socket (socket, data) */
		{
			GOTO_SET(&cli_actions);
			if(args->next && args->next->next)
			{
				EVAL_ARG(args->next);
				first_arg_value = eval_value;
				EVAL_ARG(args->next->next);

				if(!IS_CLI_SOCKET(first_arg_value) || eval_value->type != STS_STRING)
				{
					fprintf(stderr, "the socket-tcp-send action requires a socket and the data to send\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-send action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-tcp-send action");
					return NULL;
				}

				if(CLI_SOCKET(first_arg_value)->ssl)
					VALUE_FROM_NUMBER(ret, SSL_write(CLI_SOCKET(first_arg_value)->ssl, eval_value->string.data, eval_value->string.length));
				else
					VALUE_FROM_NUMBER(ret, zed_net_tcp_socket_send(&CLI_SOCKET(first_arg_value)->socket, eval_value->string.data, eval_value->string.length));

				if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-send action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-tcp-send action");
			}
			else {STS_ERROR_SIMPLE("socket-tcp-send action requires a socket and a data string"); return NULL;}
		}
		ACTION(else if, "socket-tcp-recv") /* returns a string of data from the socket. ret of 1 means would block, -1 means error, string is data */
		{
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);

				if(!IS_CLI_SOCKET(eval_value))
				{
					fprintf(stderr, "the socket-tcp-recv action requires a socket and the data to send\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-recv action");
					return NULL;
				}

				/* first check if it would block and return a 1 instead of a string */

				if(zed_net_check_would_block(&CLI_SOCKET(eval_value)->socket) == 1)
				{
					if(!(ret = sts_value_from_number(script, 1)))
					{
						STS_ERROR_SIMPLE("could not create number retval in socket-tcp-recv");
						if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-recv action");
						return NULL;
					}

					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-recv action");

					return ret;
				}

				do
				{
					if(zed_net_check_would_block(&CLI_SOCKET(eval_value)->socket))
						break;

					if(CLI_SOCKET(eval_value)->ssl)
						temp_int = SSL_read(CLI_SOCKET(eval_value)->ssl, buf, sizeof(buf));
					else
						temp_int = zed_net_tcp_socket_receive(&CLI_SOCKET(eval_value)->socket, buf, sizeof(buf));
					
					if(temp_int <= 0)
					 break;

					/* this is bad, but this whole project is for personal use primarily */
					if(!(temp_str = realloc(temp_str, temp_ulong + temp_int + 1)))
					{
						STS_ERROR_SIMPLE("could not resize temporary buffer in socket-tcp-recv");
						if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-recv action");
						return NULL;
					}

					memcpy(&temp_str[temp_ulong], buf, temp_int);

					temp_ulong += temp_int;
					temp_str[temp_ulong] = 0x0;
				} while(temp_int == sizeof(buf));


				/* return a number instead of a string */
				if(temp_int == -1)
				{
					if(!(ret = sts_value_from_number(script, -1)))
					{
						STS_ERROR_SIMPLE("could not create number retval in socket-tcp-recv");
						if(temp_str) free(temp_str);
						if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-recv action");
						return NULL;
					}

					if(temp_str) free(temp_str);
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-recv action");

					return ret;
				}
				/* create a string with the recieved data */
				else if(temp_uint != -1 && !(ret = sts_value_create(script, STS_STRING)))
				{
					STS_ERROR_SIMPLE("could not create string retval in socket-tcp-recv");
					if(temp_str) free(temp_str);
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-recv action");
					return NULL;
				}

				if(temp_str)
				{
					ret->string.length = temp_ulong;
					ret->string.data = temp_str;
				}
				else
				{
					ret->string.length = 0;
					ret->string.data = sts_memdup("", 0);
				}
				

				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-recv action");
			}
			else {STS_ERROR_SIMPLE("socket-tcp-send action requires a socket"); return NULL;}
		}
		ACTION(else if, "socket-tcp-would-block") /* tests if a socket will block */
		{
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);

				if(!IS_CLI_SOCKET(eval_value))
				{
					fprintf(stderr, "the socket-tcp-would-block action requires a socket\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-would-block action");
					return NULL;
				}

				VALUE_FROM_NUMBER(ret, zed_net_check_would_block(&CLI_SOCKET(eval_value)->socket));

				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-would-block action");
			}
			else {STS_ERROR_SIMPLE("socket-tcp-would-block action requires a socket"); return NULL;}
		}
		ACTION(else if, "socket-tcp-accept") /* returns a socket upon connection (socket, out_client_socket) */
		{
			GOTO_SET(&cli_actions);
			if(args->next && args->next->next)
			{
				EVAL_ARG(args->next);
				first_arg_value = eval_value;
				EVAL_ARG(args->next->next);

				if(!IS_CLI_SOCKET(first_arg_value))
				{
					fprintf(stderr, "the socket-tcp-accept action requires a socket\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-accept action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-tcp-accept action");
					return NULL;
				}

				if(!(temp_value = cli_socket_new(script)))
				{
					STS_ERROR_SIMPLE("could not create return socket");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-accept action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-tcp-accept action");
					return NULL;
				}

				if((temp_int = zed_net_tcp_accept(&CLI_SOCKET(first_arg_value)->socket, &CLI_SOCKET(temp_value)->socket, &address)))
				{
					if(!sts_value_reference_decrement(script, temp_value)) STS_ERROR_SIMPLE("could not decrement references for the old temp value in the socket-tcp-accept action");

					/* just dont touch the outsocket value */
				}
				else
				{
					/* set the 2nd arg reference to the new socket */

					if(sts_value_copy(script, eval_value, temp_value, 0))
					{
						STS_ERROR_SIMPLE("could not set outsock to the client socket");
						if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-accept action");
						if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-tcp-accept action");
						return NULL;
					}

					if(!sts_value_reference_decrement(script, temp_value)) STS_ERROR_SIMPLE("could not decrement references for the old temp value in the socket-tcp-accept action");
				}

				VALUE_FROM_NUMBER(ret, temp_int);

				if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the socket-tcp-would-block action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the socket-tcp-would-block action");
			}
			else {STS_ERROR_SIMPLE("socket-tcp-accept action requires a socket and out_socket which is passed byref"); return NULL;}
		}
		ACTION(else if, "socket-enable-ssl-client") /* handshake with server */
		{
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);
				if(!IS_CLI_SOCKET(eval_value))
				{
					STS_ERROR_SIMPLE("socket-enable-ssl-client: arg0 isnt socket");
					sts_value_reference_decrement(script, eval_value);
					return NULL;
				}

				if(!(CLI_SOCKET(eval_value)->ssl = SSL_CTX_new(SSLv3_client_method())))
				{
					STS_ERROR_SIMPLE("socket-enable-ssl-client: ubable to init ssl");
					++temp_uint;
				}

				if(SSL_set_fd(CLI_SOCKET(eval_value)->ssl, CLI_SOCKET(eval_value)->socket.handle) != 1)
				{
					STS_ERROR_SIMPLE("socket-enable-ssl-client: unable to set fd");
					++temp_uint;
				}
				if(tls_sni_set(CLI_SOCKET(eval_value)->ssl, CLI_SOCKET(eval_value)->name) != 1)
				{
					STS_ERROR_SIMPLE("socket-enable-ssl-client: unable to set sni");
					++temp_uint;
				}
				if(SSL_connect(CLI_SOCKET(eval_value)->ssl) != 1)
				{
					STS_ERROR_SIMPLE("socket-enable-ssl-client: unable to handshake");
					++temp_uint;
				}

				VALUE_FROM_NUMBER(ret, temp_uint);

				if(!sts_value_reference_decrement(script, eval_value))
					STS_ERROR_SIMPLE("socket-enable-ssl-client: argument isnt socket");
			}
			else {STS_ERROR_SIMPLE("socket-enable-ssl-client expects socket"); return NULL;}
		}
		#endif /* CLI_NO_SOCKETS */
		ACTION(else if, "crypto-argon2i") /* hashes a string with salt and returns the hash string buffer */
		{
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);
				first_arg_value = eval_value; /* password str */
				EVAL_ARG(args->next->next);
				second_arg_value = eval_value; /* salt str */
				EVAL_ARG(args->next->next->next);
				third_arg_value = eval_value; /* number of blocks in work area */
				EVAL_ARG(args->next->next->next->next); /* number of iterations */
				

				if(first_arg_value->type != STS_STRING || second_arg_value->type != STS_STRING || third_arg_value->type != STS_NUMBER || eval_value->type != STS_NUMBER)
				{
					fprintf(stderr, "the crypto-argon2i action requires a string, string, number, and a final number\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					if(!sts_value_reference_decrement(script, third_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					return NULL;
				}

				if(!(ret = sts_value_create(script, STS_STRING)))
				{
					fprintf(stderr, "could not create return string in crypto-argon2i action\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					if(!sts_value_reference_decrement(script, third_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					return NULL;
				}

				/* made output hashes 32 bytes instead of 64 because it may be useful to sign messages with it as well. Might be a bad idea, but you would have to know the salt. We'll see */
				if(!(ret->string.data = malloc(32 + 1)))
				{
					fprintf(stderr, "could not create return string data in crypto-argon2i action\n");
					free(ret);
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					if(!sts_value_reference_decrement(script, third_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					return NULL;
				}

				ret->string.data[32] = 0;
				ret->string.length = 32;

				if(!(work_area = malloc(third_arg_value->number * 1024)))
				{
					fprintf(stderr, "could not work area in crypto-argon2i action\n");
					sts_value_reference_decrement(script, ret);
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					if(!sts_value_reference_decrement(script, third_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
					return NULL;
				}

				crypto_argon2i(ret->string.data, ret->string.length, work_area, third_arg_value->number, eval_value->number, first_arg_value->string.data, first_arg_value->string.length, second_arg_value->string.data, second_arg_value->string.length);

				free(work_area);

				if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
				if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
				if(!sts_value_reference_decrement(script, third_arg_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for an argument in crypto-argon2i");
			}
			else {STS_ERROR_SIMPLE("crypto-argon2i action requires a string, string, number, and number"); return NULL;}
		}
		ACTION(else if, "crypto-hash") /* hashes a message with blake2b */
		{
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);

				if(eval_value->type != STS_STRING)
				{
					fprintf(stderr, "crypto-hash requires a string\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-hash action");
					return NULL;
				}

				if(!(ret = sts_value_create(script, STS_STRING)))
				{
					fprintf(stderr, "could not create ret value in crypto-hash\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-hash action");
					return NULL;
				}

				if(!(ret->string.data = calloc(1, 32 + 1)))
				{
					free(ret);
					fprintf(stderr, "could not create ret value data in crypto-hash\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-hash action");
					return NULL;
				}

				ret->string.length = 32;

				crypto_blake2b_general(ret->string.data, ret->string.length, NULL, 0, eval_value->string.data, eval_value->string.length);

				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-hash action");
			}
			else {STS_ERROR_SIMPLE("crypto-hash action requires a string"); return NULL;}
		}
		ACTION(else if, "crypto-sign-public") /* create a public key from a private key */
		{
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);

				if(eval_value->type != STS_STRING)
				{
					fprintf(stderr, "crypto-sign-public action requires a 32 byte string\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-sign-public action");
					return NULL;
				}

				if(eval_value->string.length != 32)
				{
					fprintf(stderr, "crypto-sign-public action requires a 32 byte string\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-sign-public action");
					
					VALUE_FROM_NUMBER(ret, 0);
				}
				else /* can create a key */
				{
					if(!(ret = sts_value_create(script, STS_STRING)))
					{
						fprintf(stderr, "could not create string in crypto-sign-public\n");
						if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-sign-public action");
						return NULL;
					}

					if(!(ret->string.data = calloc(1, 32 + 1)))
					{
						fprintf(stderr, "could not create string data in crypto-sign-public\n");
						if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-sign-public action");
						return NULL;
					}

					ret->string.length = 32;

					crypto_sign_public_key(ret->string.data, eval_value->string.data);
				}

				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-sign-public action");
			}
			else {STS_ERROR_SIMPLE("socket-tcp-would-block action requires a socket"); return NULL;}
		}
		ACTION(else if, "crypto-sign") /* signs a message. According to the monocypher manual, its ed25519 but with blake2b */
		{
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);
				first_arg_value = eval_value; /* message str */
				EVAL_ARG(args->next->next); /* private key str */


				if(first_arg_value->type != STS_STRING || eval_value->type != STS_STRING)
				{
					fprintf(stderr, "crypto-sign requires a string\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-sign action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the crypto-sign action");
					return NULL;
				}

				if(eval_value->string.length != 32)
				{
					fprintf(stderr, "crypto-sign requires a 32 byte private key\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-sign action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the crypto-sign action");
					
					VALUE_FROM_NUMBER(ret, 0);
				}
				else /* can go ahead and sign the key */
				{
					if(!(ret = sts_value_create(script, STS_STRING)))
					{
						fprintf(stderr, "could not create ret value in crypto-sign\n");
						if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-sign action");
						if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the crypto-sign action");
						return NULL;
					}

					/* crypto sign is the only one that generates a 64 byte key */
					if(!(ret->string.data = calloc(1, 64 + 1)))
					{
						free(ret);
						fprintf(stderr, "could not create ret value data in crypto-sign\n");
						if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-sign action");
						if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the crypto-sign action");
						return NULL;
					}

					ret->string.length = 64;

					crypto_sign(ret->string.data, eval_value->string.data, NULL, first_arg_value->string.data, first_arg_value->string.length);
				}

				if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-sign action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the crypto-sign action");
			}
			else {STS_ERROR_SIMPLE("crypto-sign action requires a string"); return NULL;}
		}
		ACTION(else if, "crypto-check") /* checks the signature of a message using its public key. According to the monocypher manual, its ed25519 but with blake2b */
		{
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);
				first_arg_value = eval_value; /* message str */
				EVAL_ARG(args->next->next);
				second_arg_value = eval_value; /* signature str */
				EVAL_ARG(args->next->next->next); /* public key str */


				if(first_arg_value->type != STS_STRING || eval_value->type != STS_STRING)
				{
					fprintf(stderr, "crypto-check requires a string\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-check action");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the crypto-check action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the crypto-check action");
					return NULL;
				}

				if(second_arg_value->string.length != 64)
				{
					fprintf(stderr, "crypto-check requires a 64 byte signature\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-check action");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the crypto-check action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the crypto-check action");
					
					VALUE_FROM_NUMBER(ret, 0);
				}
				else if(eval_value->string.length != 32)
				{
					fprintf(stderr, "crypto-check requires a 32 byte public key\n");
					if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-check action");
					if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the crypto-check action");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the crypto-check action");
					
					VALUE_FROM_NUMBER(ret, 0);
				}
				else /* can go ahead and check the key */
					VALUE_FROM_NUMBER(ret, crypto_check(second_arg_value->string.data, eval_value->string.data, first_arg_value->string.data, first_arg_value->string.length) ? 0 : 1);

				if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the crypto-check action");
				if(!sts_value_reference_decrement(script, second_arg_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the crypto-check action");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the second argument in the crypto-check action");
			}
			else {STS_ERROR_SIMPLE("crypto-check action requires a string"); return NULL;}
		}
		ACTION(else if, "base64-encode") /* encodes a string as base64 */
		{
			#define B64_SIZE(len) (4 * ((len + 2) / 3))
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);

				if(eval_value->type != STS_STRING)
				{
					fprintf(stderr, "the base64-encode action requires a string\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the base64-encode action");
					return NULL;
				}

				if(!(ret = sts_value_create(script, STS_STRING)))
				{
					fprintf(stderr, "could not create string value in base64-encode action\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the base64-encode action");
					return NULL;
				}

				ret->string.length = B64_SIZE(eval_value->string.length);

				if(!(ret->string.data = calloc(1, ret->string.length + 1)))
				{
					free(ret);
					fprintf(stderr, "could not create string value data in base64-encode action\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the base64-encode action");
					return NULL;
				}

				base64_encode_ext(ret->string.data, ret->string.length + 1, eval_value->string.data, eval_value->string.length);

				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the base64-encode action");
			}
			else {STS_ERROR_SIMPLE("base64-encode requires a string"); return NULL;}
		}
		ACTION(else if, "base64-decode") /* decodes a base64 encoded string */
		{
			/* doesnt use that padding detection removal thing in the static function because thats extra branching and honestly a few extra bytes allocated just doesnt matter */
			#define B64_DSIZE(len) (len / 4 * 3)

			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);

				if(eval_value->type != STS_STRING)
				{
					fprintf(stderr, "the base64-decode action requires a string\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the base64-decode action");
					return NULL;
				}

				if(!(ret = sts_value_create(script, STS_STRING)))
				{
					fprintf(stderr, "could not create string value in base64-decode action\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the base64-decode action");
					return NULL;
				}

				ret->string.length = B64_DSIZE(eval_value->string.length);

				if(!(ret->string.data = calloc(1, ret->string.length + 1)))
				{
					free(ret);
					fprintf(stderr, "could not create string value data in base64-decode action\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the base64-decode action");
					return NULL;
				}

				base64_decode_ext(ret->string.data, ret->string.length + 1, eval_value->string.data);

				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the base64-decode action");
			}
			else {STS_ERROR_SIMPLE("base64-decode requires a string"); return NULL;}
		}
		ACTION(else if, "exit") /* exits the interpreter with a return value */
		{
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				EVAL_ARG(args->next);
				if(eval_value->type == STS_NUMBER )
					exit(eval_value->number);
				exit(0);

				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in exit action");
			}
			exit(0);
		}
		ACTION(else if, "directory-list") /* array of strings containing filenames of a directory string */
		{
			GOTO_SET(&cli_actions);
			if(args->next)
			{
				#ifdef CLI_WINDOWS
				WIN32_FIND_DATA fd;
				HANDLE hf;
				char *t_str = NULL;
				size_t t_str_len = 0;
				#else
				DIR *d = NULL;
				struct dirent *de = NULL;
				#endif

				EVAL_ARG(args->next);

				if(eval_value->type != STS_STRING)
				{
					fprintf(stderr, "the directory-list action requires a string\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the directory-list action");
					return NULL;
				}

				if(!(ret = sts_value_create(script, STS_ARRAY)))
				{
					fprintf(stderr, "could not create array value in directory-list action\n");
					if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the directory-list action");
					return NULL;
				}

				#ifdef CLI_WINDOWS
					t_str_len = eval_value->string.length + 2;
					if(!(t_str = calloc(1, t_str_len + 1)))
					{
						fprintf(stderr, "could not create t_str directory-list action\n");
						if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the directory-list action");
						return NULL;
					}
					strncat(t_str, eval_value->string.data, eval_value->string.length);
					strncat(t_str, "\\*", 2);
					if((hf = FindFirstFile(t_str, &fd)) == INVALID_HANDLE_VALUE)
					{
						sts_value_reference_decrement(script, ret);

						if(!(ret = sts_value_create(script, STS_NIL)))
						{
							fprintf(stderr, "could not create nil value in directory-list action\n");
							free(t_str);
							if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the directory-list action");
							return NULL;
						}
					}
					else
					{
						do
						{
							sts_value_t *str = NULL;
							if(!(str = sts_value_from_string(script, df.cFileName)))
							{
								fprintf(stderr, "could not add string to array value in directory-list action\n");
								free(t_str);
								if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the directory-list action");
								return NULL;
							}

							sts_array_append_insert(script, ret, str, ret->array.length);
						} while(FindNextFile(hf, &fd));
					}
					free(t_str);
				#else
					if(!(d = opendir(eval_value->string.data)))
					{
						sts_value_reference_decrement(script, ret);

						if(!(ret = sts_value_create(script, STS_NIL)))
						{
							fprintf(stderr, "could not create nil value in directory-list action\n");
							if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the directory-list action");
							return NULL;
						}
					}
					else
						while((de = readdir(d)))
						{
							sts_value_t *str = NULL;
							if(!(str = sts_value_from_string(script, de->d_name)))
							{
								fprintf(stderr, "could not add string to array value in directory-list action\n");
								if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the directory-list action");
								return NULL;
							}

							sts_array_append_insert(script, ret, str, ret->array.length);
						}
				#endif

				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for the first argument in the directory-list action");
			}
			else {STS_ERROR_SIMPLE("directory-list requires a string"); return NULL;}
		}
		ACTION(else if, "platform") /* returns the most likely platform */
		{
			#ifdef CLI_WINDOWS
				if(!(ret = sts_value_from_string(script, "windows")))
			#else
				if(!(ret = sts_value_from_string(script, "unix")))
			#endif
			{
				fprintf(stderr, "could not create string value in platform action\n");
				return NULL;
			}
		}
		#ifdef CLI_SYSTEM_SHELLPREFIX
		ACTION(else if, "shell")
		{
			GOTO_SET(&cli_actions);
			ACTION_BEGIN_ARGLOOP
				switch(eval_value->type)
				{
					case STS_NUMBER: STS_STRING_ASSEMBLE_FMT(temp_str, size, "%1.17g", eval_value->number, " ", 1); break;
					case STS_BOOLEAN: STS_STRING_ASSEMBLE_FMT(temp_str, size, "%s", eval_value->boolean ? "true" : "false", " ", 1); break;
					case STS_STRING: STS_STRING_ASSEMBLE(temp_str, size, "\"", 1, "", 0); STS_STRING_ASSEMBLE(temp_str, size, eval_value->string.data, eval_value->string.length, "\" ", 2); break;
				}
			ACTION_END_ARGLOOP

			VALUE_FROM_NUMBER(ret, (double)system(temp_str));

			free(temp_str);
		}
		#endif
		/* end of sts_string action type */
	}

	if(!ret)
		ret = sts_defaults(script, action, args, locals, previous);
	#if CLI_ALLOW_SYSTEM && !defined(CLI_SYSTEM_SHELLPREFIX)
	if(!ret && action->type == STS_STRING) /* execute a shell command instead */
	{
		STS_STRING_ASSEMBLE(temp_str, size, action->string.data, action->string.length, " ", 1);
		ACTION_BEGIN_ARGLOOP
			switch(eval_value->type)
			{
				case STS_NUMBER: STS_STRING_ASSEMBLE_FMT(temp_str, size, "%1.17g", eval_value->number, " ", 1); break;
				case STS_BOOLEAN: STS_STRING_ASSEMBLE_FMT(temp_str, size, "%s", eval_value->boolean ? "true" : "false", " ", 1); break;
				case STS_STRING: STS_STRING_ASSEMBLE(temp_str, size, "\"", 1, "", 0); STS_STRING_ASSEMBLE(temp_str, size, eval_value->string.data, eval_value->string.length, "\" ", 2); break;
			}
		ACTION_END_ARGLOOP

		VALUE_FROM_NUMBER(ret, (double)system(temp_str));

		free(temp_str);
	}
	#endif

	return ret;
}

#ifndef NO_CLI_MAIN
int main(int argc, char **argv)
{
	int retval = 1;
	sts_script_t script;
	char *script_text = NULL;
	unsigned int i, offset = 0, line = 0, length = 0;
	sts_value_t *ret = NULL, *temp_val = NULL, *args = NULL;



	/* initialize external libs */
	#ifndef CLI_NO_SOCKETS
	if(zed_net_init())
	{
		STS_ERROR_SIMPLE("could not initialize networking");
		return 1;
	}
	#endif
	
	
	memset(&script, 0, sizeof(sts_script_t));

	/* set a read file callback */
	script.read_file = &read_file;

	/* initialize the router */
	script.router = &cli_actions;

	script.import_file = &import;
	
	/* if no arguments, send execution to repl */
	if(argc == 1)
	{
		#ifndef CLI_NO_SOCKETS
		zed_net_shutdown();
		#endif
		return repl(&script);
	}
	else /* parse the arguments */
	{
		/* need to initialize the globals here because theres something set before eval can do it instead */
		STS_SCOPE_PUSH(script.globals, {fprintf(stderr, "could not initialize global scope"); return 1;});


		if(!(args = calloc(1, sizeof(sts_value_t))))
		{
			fprintf(stderr, "could not allocate args array\n");
			goto error;
		}

		args->references++;
		args->type = STS_ARRAY;

		for(i = 2; i < argc; ++i)
		{
			if(!(temp_val = calloc(1, sizeof(sts_value_t))))
			{
				fprintf(stderr, "could not allocate argument string\n");
				goto error;
			}

			temp_val->references++;
			temp_val->type = STS_STRING;
			temp_val->string.data = sts_memdup(argv[i], strlen(argv[i]));
			temp_val->string.length = strlen(argv[i]);

			STS_ARRAY_APPEND_INSERT(args, temp_val, args->array.length);
		}

		if(!sts_map_add_set(&script.globals->locals, "args", strlen("args"), args))
		{
			fprintf(stderr, "could not add args to globals\n");
			goto error;
		}
	}
	
	/* open and read script text */
	
	if(!(script_text = read_file(&script, argv[1], &length)))
	{
		fprintf(stderr, "could not read file for parsing: %s\n", argv[1]);
		goto error;
	}
	
	/* parse and eval */
	
	/* parse script */
	if(!(script.script = sts_parse(&script, NULL, script_text, argv[1], &offset, &line)))
	{
		fprintf(stderr, "parser error\n");
		goto error;
	}
	
	/* debug_ast(script.script, 0); */
	
	/* eval */
	ret = sts_eval(&script, script.script, NULL, NULL, 0, 0);
	if(ret && ret->type == STS_NUMBER)
		retval = (int)ret->number;
	if(ret && !sts_value_reference_decrement(&script, ret)) fprintf(stderr, "could not clean up returned value\n");
	else if(!ret) fprintf(stderr, "script error\n");

	/* cleanup */
	if(!sts_destroy(&script))
		fprintf(stderr, "problem cleaning up script");
	free(script_text);

	
	
	/* deinit external libs */
	#ifndef CLI_NO_SOCKETS
	zed_net_shutdown();
	#endif
	
	return retval;
error:
	#ifndef CLI_NO_SOCKETS
	zed_net_shutdown();
	#endif
	return 1;
}
#endif