/* this file is released into the public domain */

#define STS_IMPLEMENTATION
#include "simpletinyscript.h"

#include <stdio.h>
#include <string.h>


#define PS1 "#"

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
				printf("ident: '%s'\n", node->value->string);
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
							printf("string(%u): '%s'\n", node->value->references, node->value->string);
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
		while(!equal_open_close(current_buffer))
		{
			getline(&temp_buffer, &size, stdin);

			if(!strcmp(temp_buffer, "exit\n")) /* exit isnt a real action but it needs to clean up in a simple way so this works */
			{
				free(temp_buffer);
				free(current_buffer);
				if(!sts_destroy(script, NULL))
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

		if(script->script)
		{
			temp_node = script->script;

			while(temp_node->next && (temp_node = temp_node->next));
		}

		if(!(current_node = sts_parse(script, NULL, current_buffer, "repl.sts", &offset, &line)))
		{
			fprintf(stderr, "parser error\n");
		}

		if(!script->script)
		{
			script->script = current_node;
		}
		else
		{
			temp_node->next = current_node;
		}

		/* eval */
		ret = sts_eval(script, current_node, NULL, NULL, 0);
		if(ret && !sts_value_reference_decrement(script, ret)) fprintf(stderr, "could not clean up returned value\n");
		else if(!ret) fprintf(stderr, "command error\n");

		free(current_buffer);
		current_buffer = NULL;
		blank = 1;
		offset = line = 0;
	}

	/* cleanup */

	if(!sts_destroy(script, NULL))
		fprintf(stderr, "problem cleaning up script");
	
	return 0;
}

char *read_file(sts_script_t *script, char *file, unsigned int *size)
{
	FILE *script_file = NULL;
	char *ret = NULL;


	if(!(script_file = fopen(file, "r")))
	{
		fprintf(stderr, "could not open file '%s'\n", file);
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

sts_value_t *cli_actions(sts_script_t *script, sts_value_t *action, sts_node_t *args, sts_map_row_t **locals, sts_value_t **previous)
{
	sts_value_t *ret = NULL, *eval_value = NULL, *temp_value = NULL, *first_arg_value = NULL, *second_arg_value = NULL;
	FILE *proc_pipe = NULL, *file = NULL;
	char *temp_str = NULL, *popen_buf = NULL, buf[1024];
	unsigned int size = 0, total = 0, popen_buf_init = 0, temp_uint;


	ACTION(if, "pipeout")
	{
		/* the first arg is the string that comes from stdout and
		the remaining arguments are the command part */
		
		EVAL_ARG(args->next);
		first_arg_value = eval_value;

		args = args->next;

		ACTION_BEGIN_ARGLOOP
			switch(eval_value->type)
			{
				case STS_NUMBER: STS_STRING_ASSEMBLE(temp_str, "%g", eval_value->number, temp_str, " "); break;
				case STS_STRING: STS_STRING_ASSEMBLE(temp_str, "%s", eval_value->string, temp_str, " "); break;
			}
		ACTION_END_ARGLOOP

		if((proc_pipe = popen(temp_str, "r")))
		{
			while((size = fread(buf, 1, 1024, proc_pipe)) >= 1024 || !total)
			{
				if(!(popen_buf = realloc(popen_buf, total + size + 1)))
				{
					fprintf(stderr, "could not resize pipe buffer\n");
					break;
				}

				buf[size] = 0x0;

				if(!popen_buf_init)
				{
					popen_buf[0] = 0x0;
					popen_buf_init = 1;
				}

				strcat(popen_buf, buf);
				total += size;
			}

			/* make string into sts value */
			VALUE_INIT(temp_value, STS_STRING);

			/* copy it to the first arg to set it to a string only */
			if(sts_value_copy(script, first_arg_value, temp_value, 0))
			{
				fprintf(stderr, "could not set value to string type\n");
			}

			first_arg_value->string = popen_buf;

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
		if(args->next)
		{
			EVAL_ARG(args->next);
			if(eval_value->type != STS_STRING)
			{
				fprintf(stderr, "the file-read action requires a string argument for the path\n");
				if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in file-read action");
				return NULL;
			}

			if(!(file = fopen(eval_value->string, "r")))
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
				ret->string = temp_str;

				fclose(file);
			}

			if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in file-read action");
		}
		else {STS_ERROR_SIMPLE("file-read action requires a single string path argument"); return NULL;}
	}
	ACTION(else if, "file-write") /* writes a string to a file */
	{
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

			if(!(file = fopen(first_arg_value->string, "w")))
			{
				VALUE_INIT(ret, STS_NIL);
			}
			else
			{
				VALUE_FROM_NUMBER(ret, fwrite(eval_value->string, sizeof(char), strlen(eval_value->string), file));
				fclose(file);
			}
			
			if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in file-write action");
			if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in file-write action");
		}
		else {STS_ERROR_SIMPLE("file-write action requires at least 2 string arguments"); return NULL;}
	}
	ACTION(else if, "file-append") /* appends a string to a file */
	{
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

			if(!(file = fopen(first_arg_value->string, "a")))
			{
				VALUE_INIT(ret, STS_NIL);
			}
			else
			{
				VALUE_FROM_NUMBER(ret, fwrite(eval_value->string, sizeof(char), strlen(eval_value->string), file));
				fclose(file);
			}
			
			if(!sts_value_reference_decrement(script, first_arg_value)) STS_ERROR_SIMPLE("could not decrement references for first argument in file-append action");
			if(!sts_value_reference_decrement(script, eval_value)) STS_ERROR_SIMPLE("could not decrement references for second argument in file-append action");
		}
		else {STS_ERROR_SIMPLE("file-append action requires at least 2 string arguments"); return NULL;}
	}

	if(!ret)
		ret = sts_defaults(script, action, args, locals, previous);
	else if(!ret && action->type == STS_STRING) /* execute a shell command instead */
	{
		STS_STRING_ASSEMBLE(temp_str, "%s", action->string, temp_str, " ");
		ACTION_BEGIN_ARGLOOP
			switch(eval_value->type)
			{
				case STS_NUMBER: STS_STRING_ASSEMBLE(temp_str, "%g", eval_value->number, temp_str, " "); break;
				case STS_STRING: STS_STRING_ASSEMBLE(temp_str, "%s", eval_value->string, temp_str, " "); break;
			}
		ACTION_END_ARGLOOP

		VALUE_FROM_NUMBER(ret, (double)system(temp_str));

		free(temp_str);
	}

	return ret;
}

int main(int argc, char **argv)
{
	sts_script_t script;
	char *script_text = NULL;
	unsigned int offset = 0, line = 0, length = 0;
	sts_value_t *ret = NULL;
	
	
	memset(&script, 0, sizeof(sts_script_t));

	/* set a read file callback */
	script.read_file = &read_file;

	/* initialize the router */
	script.router = &cli_actions;
	
	/* if no arguments, send execution to repl */
	if(argc == 1)
		return repl(&script);
	
	/* open and read script text */
	
	if(!(script_text = read_file(&script, argv[1], &length)))
	{
		fprintf(stderr, "could not read file for parsing: %s\n", argv[1]);
		return 1;
	}
	
	/* parse and eval */
	
	/* parse script */
	if(!(script.script = sts_parse(&script, NULL, script_text, argv[1], &offset, &line)))
	{
		fprintf(stderr, "parser error\n");
		return 1;
	}
	
	/* debug_ast(script.script, 0); */
	
	/* eval */
	ret = sts_eval(&script, script.script, NULL, NULL, 0);
	if(ret && !sts_value_reference_decrement(&script, ret)) fprintf(stderr, "could not clean up returned value\n");
	else if(!ret) fprintf(stderr, "script error\n");

	/* cleanup */
	if(!sts_destroy(&script, NULL))
		fprintf(stderr, "problem cleaning up script");
	free(script_text);
	
	return ret ? 0 : 1;
}