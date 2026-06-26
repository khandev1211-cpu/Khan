# Khan Programming Language

![Language](https://img.shields.io/badge/language-C11-blue)
![Build](https://img.shields.io/badge/build-make-green)
![License](https://img.shields.io/badge/license-MIT-orange)
![Status](https://img.shields.io/badge/status-stable--release-green)

**Khan** is a custom, indentation-based programming language built entirely from scratch in C11. Created by **Irfan Khan**, this project is a hands-on exploration of compiler design, lexer construction, parser development, runtime interpretation, and standard library implementation. The language draws inspiration from Python's clean indentation syntax and Lua's simplicity, while keeping a minimal footprint in pure C.

> **Repository**: [github.com/khandev1211-cpu/Khan](https://github.com/khandev1211-cpu/Khan)

---

## Table of Contents

- [Overview](#overview)
- [Current Status](#current-status)
- [Language Features](#language-features)
- [Syntax & Examples](#syntax--examples)
- [Standard Library](#standard-library)
- [Project Structure](#project-structure)
- [Building from Source](#building-from-source)
- [Usage](#usage)
- [Architecture](#architecture)
  - [Lexer Design](#lexer-design)
  - [Parser Design](#parser-design)
  - [Interpreter Design](#interpreter-design)
  - [Standard Library Design](#standard-library-design)
- [Token Reference](#token-reference)
- [Data Types](#data-types)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)

---

## Overview

Khan is a from-scratch programming language implementation focused on understanding the fundamentals of how programming languages work under the hood. The project implements a complete language pipeline:

1. **Lexer (Tokenizer)** — Converts raw source code into a stream of tokens
2. **Parser** — Builds an Abstract Syntax Tree (AST) using recursive descent parsing
3. **Tree-Walk Interpreter** — Executes programs by recursively evaluating AST nodes
4. **Standard Library** — 30+ built-in functions for I/O, string manipulation, math, arrays, and maps

The project serves as both a learning resource for compiler construction and a functional scripting language suitable for small programs, automation tasks, and educational use.

---

## Current Status

**Phase: Complete Language Implementation ✅**

All major components are fully implemented and operational. The language can handle complex programs with nested control flow, closures, data structures, file I/O, and more.

| Component            | Status      |
|----------------------|-------------|
| Lexer/Tokenizer      | ✅ Complete |
| Parser / AST Builder | ✅ Complete |
| Tree-Walk Interpreter| ✅ Complete |
| Standard Library     | ✅ Complete |
| Arrays               | ✅ Complete |
| Maps / Dictionaries  | ✅ Complete |
| Closures             | ✅ Complete |
| File I/O             | ✅ Complete |
| Import/Module System | ✅ Complete |

---

## Language Features

### Core Language

- ✅ **Indentation-based scoping** — Spaces define block structure (like Python)
- ✅ **Variable declarations** — `let` keyword with optional initialization
- ✅ **Function definitions** — `fn` keyword with parameters and closures
- ✅ **Print statements** — `print` keyword with any expression
- ✅ **Conditionals** — `if` / `else` with indented branches
- ✅ **While loops** — `while condition:` with indented body
- ✅ **For-in loops** — `for var in expr:` iterates over arrays
- ✅ **Return statements** — `return expr` from functions
- ✅ **Closures** — Functions capture their enclosing lexical scope
- ✅ **Assignments** — `=` for reassignment; `x[i] = v` for index assignment
- ✅ **Import/Module system** — `import "filename.kh"` to load and execute other source files

### Data Types

- ✅ **Numbers** — Integers and floating-point (double precision)
- ✅ **Strings** — Double-quoted string literals
- ✅ **Booleans** — `true` / `false` literals
- ✅ **Nil** — `nil` literal (null/void value)
- ✅ **Arrays** — Ordered collections `[1, 2, 3]` with zero-based indexing
- ✅ **Maps** — Key-value dictionaries `{"key": value}` with string keys

### Operators

- ✅ **Arithmetic** — `+`, `-`, `*`, `/`, `%` (modulo)
- ✅ **String concatenation** — `+` joins two strings
- ✅ **Comparison** — `==`, `!=`, `<`, `<=`, `>`, `>=`
- ✅ **Logical** — `and`, `or`, `not`
- ✅ **Unary negation** — `-expr`
- ✅ **Grouping** — Parentheses `(expr)` for precedence control
- ✅ **Indexing** — `arr[i]`, `map["key"]`
- ✅ **Index assignment** — `arr[i] = value`, `map["key"] = value`

### Other

- ✅ **Line comments** — `#` style comments
- ✅ **Error reporting** — Descriptive error messages with line numbers for lexer, parser, and runtime errors
- ✅ **Dynamic typing** — Type checking at runtime
- ✅ **Lexical scoping** — Environment chain with parent scope lookup
- ✅ **Deep copying** — Values are deep-copied on assignment to prevent aliasing

---

## Syntax & Examples

### Hello, World!

```khan
print "Hello from Khan!"
```

### Variables

```khan
let message = "Hello from Khan!"
print message

# Reassignment
message = "New message"
```

### Arithmetic & Expressions

```khan
let x = 10
let y = 20
let z = x + y * 2   # Multiplication has higher precedence
print z              # Output: 50

let a = 2
let b = 4
print(a * b)         # Parenthesized expression syntax works too
print 10 == 10       # Output: true
print 10 != 20       # Output: true
```

### Conditionals

```khan
let x = 10
if x > 5:
    print "x is greater than 5"
else:
    print "x is 5 or less"
```

### While Loops

```khan
let i = 0
while i < 3:
    print i
    i = i + 1
# Output: 0, 1, 2
```

### For-in Loops (over arrays)

```khan
let fruits = ["apple", "banana", "cherry"]
for fruit in fruits:
    print fruit
# Output: apple, banana, cherry
```

### Functions

```khan
# Simple function (no parameters)
fn greet:
    print "Hello!"
    let x = 5

# Function with parameters
fn greet(name):
    print name
    print "Hello!"

greet("Irfan")
greet("Khan")
```

### Functions with Return Values

```khan
fn add(a, b):
    let result = a + b
    print result

add(3, 4)  # Output: 7
```

### Closures

Functions capture their defining environment:

```khan
fn make_counter():
    let count = 0
    fn counter():
        count = count + 1
        print count

let c = make_counter()
c()  # Output: 1
c()  # Output: 2
```

### Arrays

```khan
let arr = [1, 2, 3, 4, 5]
print arr           # Output: [1, 2, 3, 4, 5]
print arr[0]        # Output: 1
print arr[2]        # Output: 3
arr[1] = 99
print arr           # Output: [1, 99, 3, 4, 5]
print len(arr)      # Output: 5

# Push to array (returns new array)
arr = push(arr, 6)
print arr           # Output: [1, 99, 3, 4, 5, 6]

# Range function
let nums = range(5)
print nums          # Output: [0, 1, 2, 3, 4]

let nums2 = range(2, 6)
print nums2         # Output: [2, 3, 4, 5]
```

### Maps / Dictionaries

```khan
let person = {"name": "Irfan", "age": 25, "is_dev": false}
print person                # Output: {"name": "Irfan", "age": 25, "is_dev": false}
print person["name"]        # Output: Irfan
print person["age"]         # Output: 25

# Update values
person["age"] = 26
print person["age"]         # Output: 26

# Add new keys
person["city"] = "Faisalabad"
print person                # Output: {"name": "Irfan", "age": 26, "is_dev": false, "city": "Faisalabad"}

# Map utilities
print keys(person)          # Output: ["name", "age", "is_dev", "city"]
print has(person, "name")   # Output: true
print has(person, "xyz")    # Output: false
print len(person)           # Output: 4
```

### Nested Data Structures

```khan
let nested = {"info": {"a": 1, "b": 2}}
print nested["info"]["a"]   # Output: 1

let matrix = [[1, 2], [3, 4]]
print matrix[0][1]          # Output: 2
```

### Boolean Logic

```khan
print true and false        # Output: false
print true or false         # Output: true
print not true              # Output: false

# Truthiness: 0 and empty strings are falsy
if 0:
    print "won't print"
else:
    print "0 is falsy"      # This prints
```

### String Operations

```khan
print "Hello " + "World"    # Output: Hello World
print len("hello")          # Output: 5
print upper("hello")        # Output: HELLO
print lower("HELLO")        # Output: hello
print contains("hello world", "world")  # Output: true
print trim("  hello  ")    # Output: hello
print substring("hello world", 0, 5)    # Output: hello
```

### Comments

```khan
# This is a comment
let x = 10  # Inline comment
```

### Imports / Modules

```khan
# Import another Khan source file — all its definitions become available
import "lib_test.kh"

# Use functions and variables defined in the imported file
say_hello()          # Calls a function from the imported library
print greeting       # Uses a variable from the imported library
```

Imported files share the same global environment as the importing file, so variables, functions, and all standard library functions defined in the imported file are available after the import statement. Paths are resolved relative to the location of the source file containing the `import` statement.

### Type Queries

```khan
print type(42)              # Output: "number"
print type("hello")         # Output: "string"
print type(true)            # Output: "bool"
print type(nil)             # Output: "nil"
print type([1, 2, 3])       # Output: "array"
print type({"a": 1})        # Output: "map"

print str(42)               # Output: "42"
print str(true)             # Output: "true"
print num("3.14")           # Output: 3.14
```

---

## Standard Library

Khan includes a comprehensive standard library with 30+ native functions registered into the global environment at startup:

### Type Conversion Functions

| Function | Description | Example |
|----------|-------------|---------|
| `num(x)` | Convert string to number | `num("3.14")` → `3.14` |
| `str(x)` | Convert any value to string | `str(42)` → `"42"` |
| `type(x)` | Return type name as string | `type(42)` → `"number"` |

### String Functions

| Function | Description | Example |
|----------|-------------|---------|
| `len(s)` | Get length of string/array/map | `len("hello")` → `5` |
| `substring(s, start, len)` | Extract substring | `substring("hello", 0, 3)` → `"hel"` |
| `upper(s)` | Convert to uppercase | `upper("hello")` → `"HELLO"` |
| `lower(s)` | Convert to lowercase | `lower("HELLO")` → `"hello"` |
| `contains(s, sub)` | Check if string contains substring | `contains("hello", "ell")` → `true` |
| `trim(s)` | Remove leading/trailing whitespace | `trim("  hi  ")` → `"hi"` |
| `split(s, delim)` | Split string into array | `split("a,b,c", ",")` → `["a", "b", "c"]` |

### Array Functions

| Function | Description | Example |
|----------|-------------|---------|
| `push(arr, val)` | Return new array with appended value | `push([1,2], 3)` → `[1,2,3]` |
| `range(n)` | Return array `[0, 1, ..., n-1]` | `range(3)` → `[0, 1, 2]` |
| `range(start, end)` | Return array `[start, ..., end-1]` | `range(2, 5)` → `[2, 3, 4]` |

### Map Functions

| Function | Description | Example |
|----------|-------------|---------|
| `keys(map)` | Return array of all keys | `keys({"a": 1})` → `["a"]` |
| `has(map, key)` | Check if key exists | `has({"a":1}, "a")` → `true` |

### Math Functions

| Function | Description | Example |
|----------|-------------|---------|
| `abs(x)` | Absolute value | `abs(-5)` → `5` |
| `min(a, b)` | Minimum of two numbers | `min(3, 7)` → `3` |
| `max(a, b)` | Maximum of two numbers | `max(3, 7)` → `7` |
| `sqrt(x)` | Square root | `sqrt(16)` → `4` |
| `round(x)` | Round to nearest integer | `round(3.7)` → `4` |
| `floor(x)` | Round down | `floor(3.7)` → `3` |
| `ceil(x)` | Round up | `ceil(3.7)` → `4` |
| `random()` | Random number in [0, 1) | `random()` → `0.374...` |
| `random(max)` | Random number in [0, max) | `random(10)` → `5.23...` |
| `pow(base, exp)` | Exponentiation | `pow(2, 10)` → `1024` |

### I/O Functions

| Function | Description | Example |
|----------|-------------|---------|
| `input()` | Read a line from stdin | `let name = input()` |
| `input(prompt)` | Show prompt and read line | `input("Enter name: ")` |
| `read_file(path)` | Read entire file as string | `read_file("data.txt")` |
| `write_file(path, content)` | Write string to file | `write_file("out.txt", "hello")` |

### Utility Functions

| Function | Description | Example |
|----------|-------------|---------|
| `sleep(ms)` | Pause execution (milliseconds) | `sleep(1000)` |
| `clock()` | CPU time in seconds | `let t = clock()` |
| `exit(code)` | Exit the program | `exit(0)` |

---

## Project Structure

```
Khan/
├── khan.exe                  # Compiled binary (Windows)
├── makefile                  # Build configuration
├── README.md                 # This file
├── examples/
│   ├── hello.kh              # Hello World example
│   ├── funcs.kh              # Function definition example
│   ├── full_test.kh          # Comprehensive feature test
│   ├── simple_test.kh        # Basic syntax test
│   ├── arrays_test.kh        # Array operations test
│   ├── arrays_stress.kh      # Array stress test
│   ├── maps_test.kh          # Map/dictionary operations test
│   ├── stdlib_test.kh        # Standard library function test
│   ├── stdlib_arrays_test.kh # Array standard library test
│   ├── complex.kh            # Complex program test
│   ├── lib_test.kh           # Library file for import testing
│   └── import_test.kh        # Import system test
└── src/
    ├── main.c                # Entry point, file I/O, REPL driver
    ├── token.h               # Token type enum and Token struct
    ├── lexer.h               # Lexer struct definitions
    ├── lexer.c               # Lexer implementation (280 lines)
    ├── ast.h                 # AST node types, struct, function declarations
    ├── ast.c                 # AST node constructors, free, debug print (473 lines)
    ├── parser.h              # Parser struct and public API
    ├── parser.c              # Recursive descent parser (499 lines)
    ├── interpreter.h         # Value types, Environment, Interpreter struct
    ├── interpreter.c         # Tree-walk interpreter (841 lines)
    ├── stdlib.h              # Standard library registration header
    └── stdlib.c              # 30+ built-in native functions (548 lines)
```

### File Descriptions

| File | Lines | Purpose |
|------|-------|---------|
| `src/token.h` | 60 | Defines `TokenType` enum (46 token types) and `Token` struct |
| `src/lexer.h` | 22 | Defines `Lexer` struct with source pointers, line tracking, indentation stack |
| `src/lexer.c` | 280 | Full lexer — character scanning, indentation handling, keyword matching |
| `src/ast.h` | 219 | Defines `AstNode` struct with tagged union for 30+ node types |
| `src/ast.c` | 473 | AST constructors, deep-free, and tree-structured debug printing |
| `src/parser.h` | 21 | Parser struct with token lookahead and error state |
| `src/parser.c` | 499 | Recursive descent parser with Pratt-style expression parsing |
| `src/interpreter.h` | 126 | Value type system, Environment (scope), MapEntry, Interpreter structs |
| `src/interpreter.c` | 900+ | Tree-walk interpreter — evaluates all AST nodes, manages scope, handles imports |
| `src/main.c` | 75 | Entry point, file reading, initialization pipeline, base path extraction for imports |
| `src/stdlib.h` | 9 | Single function to register all built-in functions |
| `src/stdlib.c` | 548 | 30+ native functions covering type conversion, strings, math, I/O, arrays, maps |

---

## Building from Source

### Prerequisites

- **C compiler**: GCC (C11 compatible) — MinGW on Windows, or any POSIX GCC
- **Build tool**: GNU Make
- **Standard**: C11

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/khandev1211-cpu/Khan.git
cd Khan

# Build the project
make

# Clean build artifacts
make clean
```

The build produces a `khan.exe` executable (on Windows) or `khan` (on Unix).

### Build Configuration

The `makefile` uses:
- **Compiler**: `gcc`
- **Standard**: `-std=c11`
- **Warnings**: `-Wall -Wextra`
- **Debug symbols**: `-g`
- **All C files in `src/`** are automatically discovered and compiled

```makefile
# From makefile
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g
SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
khan.exe: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^
```

---

## Usage

```bash
# Run a Khan source file
./khan examples/hello.kh

# Or on Windows
khan examples/hello.kh
```

The program reads the source file, lexes it, parses it into an AST, and executes it via the tree-walk interpreter, producing output directly to stdout:

```
Hello from Khan!
```

### Exit Codes

| Code | Meaning |
|------|---------|
| `0`  | Success |
| `64` | Incorrect usage (wrong number of arguments) |
| `65` | Parse completed with syntax errors |
| `70` | Runtime error occurred during execution |
| `74` | I/O error (could not open source file) |

### Example Runs

```bash
$ ./khan examples/hello.kh
Hello from Khan!

$ ./khan examples/full_test.kh
50
z is large
0
1
2
Irfan
Hello!
Khan
Hello!
false
false
true
true
Hello World

$ ./khan examples/maps_test.kh
{"name": "Irfan", "age": 25, "is_dev": false}
Irfan
25
map
4
26
{"name": "Irfan", "age": 26, "is_dev": false, "city": "Faisalabad"}
["name", "age", "is_dev", "city"]
true
false
1
{}
0
{"x": 1}
8
```

---

## Architecture

### Lexer Design

The lexer operates as a single-pass, character-by-character scanner with the following key components:

#### 1. Indentation Engine

Khan uses Python-style indentation to define block structure. The lexer maintains an **indentation stack** (up to 64 levels deep) to track the current indentation level:

- When a new line starts with more spaces than the current level → emits `TOKEN_INDENT`
- When a new line starts with fewer spaces → emits one or more `TOKEN_DEDENT` tokens
- Blank lines and comment-only lines are skipped without affecting indentation
- Structural tokens (INDENT/DEDENT) carry the current position (not whitespace text) as their source pointer

#### 2. Token Recognition Pipeline

```
Source Code → Character-by-character scan → Token construction → Token stream
```

The lexer processes tokens in this order:
1. **Start-of-line handling** — Check pending DEDENTs, then measure indentation
2. **Inline whitespace skipping** — Spaces, tabs, carriage returns, comments
3. **End-of-file** — Emit EOF token
4. **Newlines** — Increment line counter, emit NEWLINE token, set line-start flag
5. **Identifiers/Keywords** — Alpha/underscore starts identifier; keyword table lookup
6. **Numbers** — Digits start number scanning (supports decimal point)
7. **Strings** — Double-quote starts string scanning (supports multi-line)
8. **Single-character tokens** — Operators and punctuation
9. **Multi-character operators** — `==`, `!=`, `<=`, `>=` via single-character lookahead

#### 3. Data Structures

**Token** (defined in `token.h`):
```c
typedef struct {
    TokenType type;
    const char *start;   // pointer into source buffer
    int length;          // length of the lexeme
    int line;            // source line number
} Token;
```

**Lexer** (defined in `lexer.h`):
```c
typedef struct {
    const char *start;         // start of current lexeme
    const char *current;       // current scan position
    int line;                  // current line number
    int indent_stack[64];      // indentation level stack (max 64 depth)
    int indent_top;            // top of indentation stack
    int at_line_start;         // flag: at beginning of a line
    int pending_dedents;       // queued dedent tokens
} Lexer;
```

#### 4. Error Handling

The lexer produces `TOKEN_ERROR` tokens for:
- Unterminated string literals
- Unexpected characters (e.g., lone `!`)
- Any character that doesn't match a known token pattern

Error tokens carry a human-readable message string instead of a source lexeme, and the parser continues scanning past them to report multiple errors.

---

### Parser Design

The parser is a **recursive descent parser** using **Pratt-style expression parsing** with precedence climbing. It converts the flat token stream into a hierarchical Abstract Syntax Tree (AST).

#### Precedence Table (lowest to highest)

| Level | Operators | Associativity |
|-------|-----------|---------------|
| 1 | `or` | Left |
| 2 | `and` | Left |
| 3 | `==`, `!=` | Left |
| 4 | `<`, `<=`, `>`, `>=` | Left |
| 5 | `+`, `-` | Left |
| 6 | `*`, `/`, `%` | Left |
| 7 | Unary `-`, `not` | Right (prefix) |
| 8 | `f(...)`, `x[...]` | Left (postfix) |
| 9 | Primary (literals, identifiers, groups) | — |

#### AST Node Types (30+)

The AST has ~31 node types covering:

- **Literals**: `AST_NUMBER`, `AST_STRING`, `AST_BOOL`, `AST_NIL`
- **Identifiers**: `AST_IDENTIFIER`
- **Expressions**: `AST_BINARY`, `AST_UNARY`, `AST_GROUPING`, `AST_ASSIGNMENT`, `AST_CALL`, `AST_ARRAY`, `AST_INDEX`, `AST_INDEX_ASSIGN`, `AST_MAP`, `AST_MAP_ENTRY`
- **Statements**: `AST_EXPR_STMT`, `AST_PRINT_STMT`, `AST_LET_STMT`, `AST_IF_STMT`, `AST_WHILE_STMT`, `AST_FOR_STMT`, `AST_BLOCK`, `AST_FN_DECL`, `AST_RETURN_STMT`
- **Top-Level**: `AST_PROGRAM`

The AST uses a **tagged union** (C11 anonymous union) where each node type overlays its data fields in the same memory — efficient and typesafe.

#### Block Parsing

Blocks are parsed using the INDENT/DEDENT tokens from the lexer:
```
block → NEWLINE? INDENT declaration* DEDENT
```

The parser handles:
- Zero or more NEWLINE tokens between statements (blank lines)
- Compound statements (if/while/for/fn) consuming their own DEDENT when their nested block ends
- Error recovery — continues parsing after syntax errors

---

### Interpreter Design

The interpreter is a **tree-walk interpreter** that recursively evaluates AST nodes at runtime.

#### Value System

All runtime values are represented by the `Value` struct with a tagged union:

```c
typedef enum {
    VAL_NUMBER, VAL_STRING, VAL_BOOL, VAL_NIL,
    VAL_FUNCTION, VAL_NATIVE, VAL_ARRAY, VAL_MAP
} ValueType;

struct Value {
    ValueType type;
    union {
        double number;
        const char *string;
        int boolean;
        struct { const char *name; Environment *closure; AstNode *body; AstNodeList *params; } function;
        struct { const char *name; NativeFn function; } native;
        struct { Value *items; int count; int capacity; } array;
        struct { MapEntry *entries; int count; int capacity; } map;
    } as;
};
```

#### Scope Management

The interpreter uses a **linked-list of environments** (lexical scoping):

- Each environment has a pointer to its parent scope
- Variable lookup walks the chain from innermost to outermost
- Variable assignment traverses the chain (or defines in current scope if not found)
- Function closures capture the current environment chain at definition time
- Closures are created with a deep copy of the current scope's entries

#### Deep Copy Semantics

Values are **deep-copied** when:
- Reading a variable (returns a copy)
- Assigning to a variable (copies the value)
- Passing arguments to functions
- Storing in arrays and maps

This prevents aliasing bugs and ensures predictable value semantics:
```khan
let a = [1, 2, 3]
let b = a           # b gets a deep copy
b[0] = 99           # a remains unchanged
print a[0]          # Output: 1
```

#### Function Calls

Function calls follow this pipeline:
1. Evaluate the callee name in current environment
2. Evaluate each argument expression
3. If native: call C function pointer directly
4. If user-defined: create new environment with closure as parent, bind parameters to argument values, execute body block, return result
5. Argument count is checked against parameter count
6. Return values propagate up the call chain

#### Runtime Error Handling

All runtime errors include the source line number and stop execution immediately (with an option to return a partial result). Error categories:
- Undefined variable/function
- Type mismatch (e.g., arithmetic on non-numbers)
- Division by zero
- Array index out of bounds
- Map key not found
- Argument count mismatch
- Iteration over non-array

---

### Standard Library Design

The standard library consists of **native C functions** registered into the global environment. Each function:
1. Receives a `Value *result` pointer to write the return value
2. Has access to the `Interpreter` for error reporting
3. Receives `argc`/`argv` for argument handling
4. Uses helper functions for argument validation (`check_arg_count`, `expect_number`, `expect_string`)
5. Is registered via `stdlib_register_all(Environment *env)` during interpreter initialization

Native functions share the same calling convention as user-defined functions, making them first-class citizens in the language.

The library is organized into categories:
- **Type conversions** — `num()`, `str()`, `type()`
- **String manipulation** — `len()`, `substring()`, `upper()`, `lower()`, `contains()`, `trim()`, `split()`
- **Array operations** — `push()`, `range()`
- **Map operations** — `keys()`, `has()`
- **Math** — `abs()`, `min()`, `max()`, `sqrt()`, `round()`, `floor()`, `ceil()`, `random()`, `pow()`
- **I/O** — `input()`, `read_file()`, `write_file()`
- **Utilities** — `sleep()`, `clock()`, `exit()`

---

## Token Reference

### Literals

| Token | Description |
|-------|-------------|
| `IDENTIFIER` | Variable/function names (alphanumeric + underscore) |
| `NUMBER` | Integer or floating-point literal |
| `STRING` | Double-quoted string literal |

### Keywords

| Token | Keyword |
|-------|---------|
| `LET` | `let` |
| `FN` | `fn` |
| `PRINT` | `print` |
| `IMPORT` | `import` |
| `IF` | `if` |
| `ELSE` | `else` |
| `WHILE` | `while` |
| `FOR` | `for` |
| `IN` | `in` |
| `RETURN` | `return` |
| `TRUE` | `true` |
| `FALSE` | `false` |
| `NIL` | `nil` |
| `AND` | `and` |
| `OR` | `or` |
| `NOT` | `not` |

### Operators

| Token | Symbol |
|-------|--------|
| `PLUS` | `+` |
| `MINUS` | `-` |
| `STAR` | `*` |
| `SLASH` | `/` |
| `PERCENT` | `%` |
| `EQUAL` | `=` |
| `EQUAL_EQUAL` | `==` |
| `BANG_EQUAL` | `!=` |
| `LESS` | `<` |
| `LESS_EQUAL` | `<=` |
| `GREATER` | `>` |
| `GREATER_EQUAL` | `>=` |

### Punctuation

| Token | Symbol |
|-------|--------|
| `LPAREN` | `(` |
| `RPAREN` | `)` |
| `LBRACKET` | `[` |
| `RBRACKET` | `]` |
| `LBRACE` | `{` |
| `RBRACE` | `}` |
| `COLON` | `:` |
| `COMMA` | `,` |
| `DOT` | `.` |

### Structural

| Token | Description |
|-------|-------------|
| `NEWLINE` | End of a line |
| `INDENT` | Increased indentation (block start) |
| `DEDENT` | Decreased indentation (block end) |
| `EOF` | End of file |
| `ERROR` | Lexical error with message |

---

## Data Types

| Type | Description | Examples |
|------|-------------|---------|
| `number` | Double-precision floating point | `42`, `3.14`, `-1.0` |
| `string` | Immutable text (double-quoted) | `"hello"`, `"42"` |
| `bool` | Boolean value | `true`, `false` |
| `nil` | Null/void value | `nil` |
| `array` | Ordered, zero-indexed collection | `[1, 2, 3]`, `["a", "b"]` |
| `map` | Key-value dictionary (string keys) | `{"key": value}` |
| `function` | User-defined function | `fn add(a, b): ...` |
| `native` | Built-in C function | `print`, `len`, `type` |

---

## Technical Details

### Language Design Choices

- **Indentation-based scoping** — Eliminates braces and reduces visual clutter, inspired by Python
- **Explicit `let` for variables** — Makes variable declaration explicit, similar to JavaScript's `let` or Rust
- **`fn` for functions** — Short, clean keyword for function definitions
- **`print` as a keyword** — Built-in output for simplicity
- **`#` for comments** — Familiar from Python, Ruby, and shell scripting
- **No semicolons** — Line-based parsing with NEWLINE tokens as statement terminators
- **Deep copy semantics** — Values are deep-copied on assignment, preventing aliasing bugs at the cost of performance
- **Dynamic typing** — Type checking happens at runtime, enabling flexible, concise code

### Implementation Notes

- The lexer uses **raw pointers into the source buffer** rather than copying substrings, making it memory-efficient
- The **indentation stack** has a fixed maximum depth of 64 levels, sufficient for practical programs
- **Blank lines** and **comment-only lines** are transparent to the indentation system
- The lexer is a **single-pass scanner** with lazy tokenization via `lexer_next_token()`
- The AST uses a **tagged union** with zero-initialization for safe default field values
- The parser implements **Pratt parsing** with straightforward precedence climbing functions
- The interpreter uses **recursive evaluation** with deep copying to maintain value semantics
- **Memory management** is explicit — all allocations are tracked and freed via `ast_free()` and `value_free()`
- The standard library uses **native C function pointers** with a uniform calling convention

---

## Roadmap

### Phase 1: Lexer ✅ (Complete)
- [x] Character-by-character scanning
- [x] Indentation tracking (INDENT/DEDENT)
- [x] All token types defined (46 tokens)
- [x] String, number, identifier literals
- [x] Keyword recognition (16 keywords)
- [x] Comment handling
- [x] Error reporting with line numbers

### Phase 2: Parser ✅ (Complete)
- [x] Recursive descent parser with Pratt-style expression parsing
- [x] Full operator precedence (8 levels)
- [x] Statement parsing (let, print, if/else, while, for, fn, return)
- [x] AST node definitions (31 node types)
- [x] Error recovery with multiple error reporting
- [x] Indentation-based block parsing
- [x] Function calls with argument lists
- [x] Array and map literal parsing
- [x] Index expression and index assignment parsing

### Phase 3: Interpreter ✅ (Complete)
- [x] Tree-walk interpreter with recursive evaluation
- [x] Environment/scope management (lexical scoping with closures)
- [x] Variable assignment and lookup
- [x] Function calls with parameter binding and stack frames
- [x] Control flow (if/else, while, for-in)
- [x] Boolean and logical operations
- [x] String concatenation
- [x] Array and map runtime support
- [x] Indexing and index assignment
- [x] Deep copy value semantics
- [x] Runtime error reporting with line numbers

### Phase 4: Import/Module System ✅ (Complete)
- [x] `import "filename.kh"` parsing
- [x] Import execution (lex, parse, run imported file in shared environment)
- [x] Relative path resolution based on source file directory
- [x] EOF indentation unwind fix to support imports reliably

### Phase 5: Standard Library ✅ (Complete)
- [x] Type conversion functions (num, str, type)
- [x] String manipulation (len, substring, upper, lower, contains, trim, split)
- [x] Array operations (push, range)
- [x] Map operations (keys, has)
- [x] Math functions (abs, min, max, sqrt, round, floor, ceil, random, pow)
- [x] I/O operations (input, read_file, write_file)
- [x] Utility functions (sleep, clock, exit)

### Phase 6: Advanced Features 🔲 (Planned)
- [ ] Garbage collection or arena allocation
- [ ] Bytecode compiler with instruction set
- [ ] Virtual machine for faster execution
- [ ] Error handling with try/catch
- [ ] Pattern matching
- [ ] Self-hosting compiler (Khan written in Khan)

---

## Contributing

Contributions are welcome! Since this is an educational and experimental project, there are many opportunities to contribute:

1. **Bug fixes** — Report or fix issues in any component
2. **Test cases** — Create `.kh` test files covering edge cases
3. **Documentation** — Improve docs, add more examples
4. **Performance** — Optimize the interpreter or memory management
5. **Features** — Implement new language features from the roadmap
6. **Portability** — Improve cross-platform support

### Getting Started

```bash
# Fork the repository
# Clone your fork
git clone https://github.com/your-username/Khan.git
cd Khan

# Create a feature branch
git checkout -b feature/your-feature

# Make your changes
# Build and test
make
./khan examples/full_test.kh

# Commit and push
git commit -m "Add your feature"
git push origin feature/your-feature

# Open a Pull Request
```

---

## License

This project is open source and available under the MIT License.

---

## Author

**Irfan Khan** — Creator and maintainer of the Khan programming language.

- GitHub: [@khandev1211-cpu](https://github.com/khandev1211-cpu)
- Repository: [github.com/khandev1211-cpu/Khan](https://github.com/khandev1211-cpu/Khan)

---

*Built from scratch in C — because understanding the fundamentals matters.*