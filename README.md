# SimpleTinyScript
STS is an experiment in trying to make a really tiny scripting language that is still mostly usable in an environment where it is embedded in another project.

I am not proud of the code, but it functions.


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

## License
Unlicense. This project is released into the public domain.