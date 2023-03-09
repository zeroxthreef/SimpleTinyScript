#!/bin/sh
cc -Wall -fsanitize=undefined -fsanitize=address -g -o sts cli.c -DSTS_GOTO_JIT -DCLI_SYSTEM_SHELLPREFIX -lm