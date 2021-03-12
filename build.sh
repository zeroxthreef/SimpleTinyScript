#!/bin/sh
#clang -Wall -fsanitize=undefined -fsanitize=address -g -o sts cli.c ext/pdjson/pdjson.c -DSTS_GOTO_JIT -lm
cc -Wall -g -o sts cli.c ext/pdjson/pdjson.c -DSTS_GOTO_JIT -lm