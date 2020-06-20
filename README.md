# SMASH
__S__tudent __M__akes a __SH__ell

----

## Overview
Smash is a shell written in C/C++ that supports the following operations:

* Basic shell commands
* File system navigation
* I/O redirection
* Unix-style pipelines

Additionally, Smash includes the GNU Readline library which gives it the following features for free:

* Tab completion
* History
* Customization

_Note: GNU Readline is required to succesfully build Smash. On Linux, run `sudo apt-get install libreadline-dev`. On an Apple, install Homebrew and run `brew install readline`._

## Design
Smash employs a lexer and parser to parse input into a `Command` object, which represents a shell directive. This object is passed to the Exec module, which handles the directive based on its type, arguments, and meta-data. Each module is discussed in more detail below.

### Lexer/Parser
In order to properly parse things like string literals, I decided to write a full blown lexer and parser that can efficiently parse a given string into a `Command` Object. The lexer is defined in the function `tokenize`, which takes a `char*` as input and returns a `std::vector` of `std::strings`. This vector is passed to a `Parser` object which uses top-down recursion to transform the input into a `Command` object.

### Command
A `Command` is an object representing a shell directive. There are three types of `Command`:

1. STANDARD - applies to most shell directives without redirection or pipelining.
2. SPECIAL - denotes commands that cannot be executed as a child process of the shell, e.g. `cd` and `exit`.
5. PIPE - a composition of the above four commands whose outputs are each connected to the subsequent process's input.

Internally, a `Command` is a tree-like structure with an enumeration representing its type, and strings or vectors of strings storing its arguments. It also stores some meta-data, e.g. whether the directive is to be made in the background, and exposes two functions, `Command::argc` and `Command::mk_argv` that provide the system call `exec` (not to be confused with the Exec module described below) with correctly typed and formatted arguments.

_Note: You might find it strange that `Command::make_argv` takes an array of c strings as an argument. This is to avoid allocating memory which will be unavoidably lost when passed to the system call `exec`. The function is meant to populate a stack bound c string array of size `Command::argc`._

### Exec
Exec exposes only one eponymous function: `exec`. This function takes a `Command` and executes it based on its type and meta-data. To execute a standard `Command`, a simple `fork` and `exec` are used to execute the process. The main loop periodically inspects and reaps these processes after they are finished. I/O redirection is accomplished by utilizing the system call `dup2` to fix a given file either to a process's stdin or stdout, depending on the symbol used. Pipelines are executed in a similar manner, except they are stored in an array of `Commands` and special care is taken to correctly fix each process' stdout to the next process's stdin. If the user wishes any of these commands (standard, special, I/O redirect, and pipelines) to be executed in the background, Exec adds this process or processes to a global vector of background processes, which are periodically reaped in the main loop.
