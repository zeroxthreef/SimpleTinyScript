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

## Configuration Definitions
```
#define STS_GOTO_JIT //enable the goto jit which requires a gcc extension

#define CLI_ALLOW_SYSTEM //allow the system() shell function to be used in last resort

#define INSTALL_DIR "/path/to/install" //change the install directory so imports work in cli.c

#define CLI_NO_SOCKETS //remove the ability to use sockets

#define CLI_NO_TLS //remove the ability to have tls sockets. Will already be in effect if there are no sockets
```

## Libraries Used
The core of sts lives in ``simpletinyscript.h`` which only depends on the C standard library. However, ``cli.c`` depends on system libraries and on the following public domain libraries which exist in ``ext/``:
* [pdjson](https://github.com/skeeto/pdjson)
* [zed_net](https://github.com/mackron/zed_net)
* [Monocypher](https://github.com/eduardsui/tlse)
* [tlse](https://github.com/LoupVaillant/Monocypher)
* [base64](https://github.com/badzong/base64)

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

**const var**<br />
marks the value as readonly

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

**asc number**<br />
returns a string consisting of the ascii character equal to 'number'

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

**replace var index insert**<br />
replaces instead of shallow copies the value at an array index

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

**json string_data|any_value (prettify)**<br />
if supplying a single string argument, it will parse the string. Two arguments with a number is json from value conversion. ``$prettify`` as 1 will make the output look nice. 0 will make the output compact

**socket-tcp port non_blocking listening**<br />
creates a tcp socket, and if creating a server socket, set listening to 1

**socket-udp port non_blocking**<br />
creates a udp socket

**socket-tcp-connect socket host port**<br />
connect to a tcp server with the supplied arguments. Returns 1 if the address is unknown, 2 if the host didnt connect, and 0 if successful

**socket-tcp-send socket data**<br />
sends string buffer to host. Returns the amount of sent bytes or -1 on error

**socket-tcp-recv socket**<br />
returns a string buffer if successful, -1 on error, or 1 if the socket would block on a nonblocking port

**socket-tcp-would-block socket**<br />
returns 1 if the socket would block

**socket-tcp-accept socket out_client_socket_reference**<br />
the out_socket_client_reference value will be set to the client socket (similar to pipeout) and returns 0 upon success, 1 upon would block, -1 upon error, and 2 upon the socket not able to listen

**socket-enable-ssl-client socket**<br />
enable ssl on the current socket. Returns nonzero on error

**crypto-argon2i password_str salt_str block_num iteration_num**<br />
returns a 32 byte string. Monocypher documentation recommends 100000 blocks and 3 iterations

**crypto-hash str**<br />
returns a 32 byte string hash using blake2b

**crypto-sign-public privkey_str**<br />
returns a 32 byte public key string. Requires a 32 byte private key string

**crypto-sign message_str privkey_str**<br />
returns a 64 byte signature string. the private key string must be 32 bytes

**crypto-check message_str signature_str pubkey_str**<br />
returns 1 if a correct 64 byte signature str is used for the 32 byte public key str on the message

**base64-encode str**<br />
return a b64 encoded string

**base64-decode str**<br />
return a decoded b64 string

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

**hashmap-replace map key value**<br />
replaces the entire row value and key if it exists, and if it doesnt it will make a new one to set to 'value'

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