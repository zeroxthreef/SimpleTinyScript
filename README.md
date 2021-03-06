# SimpleTinyScript
STS is an experiment in trying to make a really tiny scripting language that is still mostly usable in an environment where it is embedded in another project.

The code quality for this is not near acceptable. This project is not meant for any serious use.


```
import stdlib.sts


global i 0

loop(< $i 10) {
    print hello world
    ++ $i
}
```

## Usage (when compiling cli.c)
`./sts` to enter repl. Works like a shell and will fall back on using the native shell if no global or local function is found

`./sts file.sts` to eval a script

## Libraries Used
The core of sts lives in ``simpletinyscript.h`` which only depends on the C standard library. However, ``cli.c`` depends on system libraries and on the following public domain libraries which exist in ``ext/``:
* [pdjson](https://github.com/skeeto/pdjson)

## Documentation

**print ...**<br />
turns all values into printable strings and adds spaces between them. Also appends a newline and prints it to stdout

**pass var**<br />
passes the value in 'var'. Useful when replacing 'return'

**string ...**<br />
same as ``print``, but adds no spaces and no newline

**global varname (value_to_set)**<br />
adds a variable of 'varname' to the global scope. Will return 1 or 0 if it exists when no value_to_set is passed

**local varname (value_to_set)**<br />
same as ``global`` but works in the local scope (global scope if not in any function)

**string-hash var**<br />
returns a numeric hash of the string using FNV-1a

**typeof var**<br />
returns a number of STS_* for the type declared in an enum

**sizeof var**<br />
returns a number set to the size of the value passed

**if cond eval_expr**<br />
tests if the cond is recognized as true and evaluates eval_expr if so. Returns a 1 or 0 if it ran

**elseif cond eval_expr**<br />
same as ``if`` but will not do anything if the previous value is 1

**else eval_expr**<br />
only evaluates eval_expr if the previous value is not 1

**function name (parameters) eval_expr**<br />
2 major cases:

1. if 'name' is nil, then it is not automatically added to the current local scope
2. if 'name' is a string, it is added to the local scope automatically

requires 0 or more parameters. The last argument is always the eval_expr.
this always returns the function value regardless of a nil name

**copy var**<br />
recursively copies the value passed

**self-name**<br />
retrieves the script filename from the AST

**number var**<br />
turns a string value into a number

**char string index**<br />
returns the numeric value of the character in 'string' at position 'index'

**get var index**<br />
returns the value in 'var' at 'index'. Only works on arrays and strings

**set val_to_set val_to_shallow_copy**<br />
shallow copies 'val_to_shallow_copy' and overwrites 'val_to_set' with the new value

**array ...**<br />
returns an array of 0 or more values passed

**remove var index**<br />
removes the value in 'var' at position 'index'. Only works on arrays

**insert var index insert**<br />
inserts 'insert' into 'var' at position 'index'. only works on arrays

**import file**<br />
look for file relative to the interpreter PWD, and if it cant find the file and compiled with cli.c, it will search the system install directory

**call function ...**<br />
calls function value 'function' and supplies arguments from '...'

**&&**, **||**<br />
work just like in C

**==**, **!=**, **<**, **<=**, **>**, **>=**<br />
work just like in C, but let you do string comparisons too. Need to be the same type

**+**, **-**, **\***, **/**, **\*\***, **%**, **>>**, **<<**, **&**, **^**, **|**, **~**, **!**, **++**, **--**<br />
work mostly just like C, but bit operations conver numbers to integers internally then back to doubles. ``**`` is exponential. ``++`` and ``--`` are prefix only

**C's math.h trig functions**<br />
mostly all present

The following functions are if embedding into cli.c
---

**pipeout var \*command\***<br />
runs popen() and the stdout from the command provided is sent to 'var' as a string. The return value is the return value of the command. This uses the native shell

**file-read file**<br />
returns nil if not found, and a string of the file's contents

**file-write file string**<br />
returns nil if file doesnt exist and the number of bytes written from 'string' if it does

**file-append file string**<br />
same as ``file-write`` but appends to the file instead of overwrites

**stdin-read number_or_char**<br />
2 possible options:
1. if 'number_or_char' is a number and equal to 0, it will read until EOF in stream and if >0, it will read until the size of the buffer reaches this
2. if 'number_or_char' is a single width string, the character will be tested and read until encountered in the stream

**stdout-write ...**<br />
similar to ``print`` but no spaces between values and no newline and prints to stdout

**stderr-write ...**<br />
same as ``stdout-write`` but to stderr instead

**getenv name**<br />
returns nil if not found or a string of the environment variable

**setenv name value**<br />
sets the environment variable to the value if found or makes a new variable of that name

**sleep seconds**<br />
sleeps for the number of seconds provided in 'seconds'

The following functions are documentation for ``stdlib.sts``
---

**STS_EXTERNAL**, **STS_NIL**, **STS_NUMBER**, **STS_STRING**, **STS_ARRAY**, **STS_FUNCTION**<br />
returns the number of the type returned by ``typeof``

**hashmap ...**<br />
returns a hashmap of "key, value". To make easily readable, append a '\' at the end of every key and value passed so the function arguments cascade vertically

**hashmap-get map key**<br />
returns nil if not found and the value in 'map' of 'key'

**hashmap-exists map key**<br />
returns 1 if the key does and 0 if not

**hashmap-set map key value**<br />
sets the key if it exists, and if it doesnt it will make a new one to set to 'value'

**hashmap-remove map key**<br />
removes 'key' and its value from 'map'

**hashmap-keys map**<br />
returns an array of the strings used as keys in the hashmap

**hashmap-values map**<br />
same as ``hashmap-keys`` but for the values instead

**hashmap-update map**<br />
because keys are passed by reference and stored in the row rather than copied into the row, the key could change at some point and thats mostly ok, but the string hash needs to be updated as well. Call this if theres a possibility that the keys updated in the hashmap

**string-tokenize string token**<br />
returns an array of the string separated into smaller strings divided by 'token'

**string-combine array between_string**<br />
undoes ``string-tokenize** if passed the same token string

**string-range string start end**<br />
returns an inclusive substring from 'start' and 'end'

**string-search string needle**<br />
returns -1 if not found, and the position in the string where 'needle' is first found

**string-rsearch string needle**<br />
same as ``string-search`` but searches from the back of the string

**string-insert string position instring**<br />
returns a new string with the instring between 'string' at 'position'. Works at the end of the string as well

**string-remove string position**<br />
returns a new string with the character at 'position' removed

**string-replace string replacee replacement**<br />
returns a new string with 'replacee' replaced with 'replacement' everywhere

**getpwd self**<br />
returns the pwd when ``self-name`` is passed

**relimport self script**<br />
imports and uses ``getpwd`` internally when passed ``self-name`` in 'self'

**stdlib-get-error**<br />
returns nil if no error, and the string of the current stdlib error

**stdlib-set-error component_str error_str**<br />
sets the current stdlib error to the format "component_str: error_str"

**string-value-print value**<br />
returns a string of recursively created stringification of 'value'

**typeof-string value**<br />
returns a string of the type instead of a number. Looks like "STS_NUMBER"

**expr ...**<br />
!WIP

**enum ...**<br />
!WIP

**+=**, **-=**, **\*=**, **/=**, **%=**, **\*\*=**, 
work like C's operators, but ``**=`` is exponential equal

## License
Unlicense. This project is released into the public domain.