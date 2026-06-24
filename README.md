# Khan Programming Language

![Language](https://img.shields.io/badge/language-C11-blue)
![Build](https://img.shields.io/badge/build-make-green)
![License](https://img.shields.io/badge/license-MIT-orange)
![Status](https://img.shields.io/badge/status-lexer--stage-yellow)

**Khan** is a custom, indentation-based programming language being built from scratch in C. Created by **Irfan Khan**, this project is a hands-on exploration of compiler design, lexer construction, and language runtime development. The language draws inspiration from Python's clean indentation syntax while keeping a minimal, low-level implementation in pure C.

> **Repository**: [github.com/khandev1211-cpu/Khan](https://github.com/khandev1211-cpu/Khan)

---

## Table of Contents

- [Overview](#overview)
- [Current Status](#current-status)
- [Language Features](#language-features)
- [Syntax & Examples](#syntax--examples)
- [Project Structure](#project-structure)
- [Building from Source](#building-from-source)
- [Usage](#usage)
- [Architecture](#architecture)
- [Token Reference](#token-reference)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)

---

## Overview

Khan is a from-scratch programming language implementation focused on understanding the fundamentals of how programming languages work under the hood. The project currently implements a complete **lexer (tokenizer)** that converts raw source code into a stream of tokens, handling:

- Python-style indentation tracking (INDENT/DEDENT tokens)
- Multi-character operators (`==`, `!=`, `<=`, `>=`)
- String literals with newline support
- Number literals (integers and floating-point)
- Identifier and keyword recognition
- Line comments (`#`)
- Error reporting with line numbers

The project is actively developed and serves as both a learning resource for compiler construction and a foundation for a full-featured scripting language.

---

## Current Status

**Phase: Lexer (Tokenizer) — Complete ✅**

The lexer is fully functional and can tokenize any valid `.kh` source file. The next phases will involve building a parser, AST (Abstract Syntax Tree), and eventually a runtime/interpreter.

| Component       | Status     |
|-----------------|------------|
| Lexer/Tokenizer | ✅ Complete |
| Parser          | ❌ Not started |
| AST Builder     | ❌ Not started |
| Interpreter     | ❌ Not started |
| Standard Library| ❌ Not started |

---

## Language Features

### Implemented (Lexer Stage)

- ✅ **Indentation-based scoping** — Uses spaces for block structure (like Python)
- ✅ **Variable declarations** — `let` keyword
- ✅ **Function definitions** — `fn` keyword
- ✅ **Print statements** — `print` keyword
- ✅ **Import system** — `import` keyword
- ✅ **Conditionals** — `if` / `else`
- ✅ **Loops** — `while`
- ✅ **Return statements** — `return`
- ✅ **Boolean literals** — `true` / `false`
- ✅ **Logical operators** — `and`, `or`, `not`
- ✅ **Arithmetic operators** — `+`, `-`, `*`, `/`, `%`
- ✅ **Comparison operators** — `==`, `!=`, `<`, `<=`, `>`, `>=`
- ✅ **Assignment** — `=`
- ✅ **String literals** — Double-quoted strings
- ✅ **Number literals** — Integers and floating-point numbers
- ✅ **Identifiers** — Alphanumeric with underscores
- ✅ **Line comments** — `#` style comments
- ✅ **Error reporting** — Descriptive error messages with line numbers

### Planned

- 🔲 Parser (syntax analysis)
- 🔲 Abstract Syntax Tree (AST)
- 🔲 Tree-walk interpreter
- 🔲 Type checking
- 🔲 Standard library functions
- 🔲 Module/import system runtime

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
```

### Functions

```khan
fn greet:
    print "Hello!"
    let x = 5

print "done"
```

Functions are defined using the `fn` keyword followed by the function name and a colon. The function body is indented (using spaces). The indentation-based scoping means no braces are needed — the block ends when the indentation returns to the previous level.

### Comments

```khan
# This is a comment
let x = 10  # Inline comment
```

### Conditionals (Planned Syntax)

```khan
let x = 10
if x > 5:
    print "x is greater than 5"
else:
    print "x is 5 or less"
```

### Loops (Planned Syntax)

```khan
let i = 0
while i < 10:
    print i
    i = i + 1
```

---

## Project Structure

```
Khan/
├── khan.exe              # Compiled binary (Windows)
├── makefile              # Build configuration
├── README.md             # This file
├── examples/
│   ├── hello.kh          # Hello World example
│   └── funcs.kh          # Function definition example
└── src/
    ├── main.c            # Entry point, file I/O, token dumping
    ├── main.o            # Compiled object file
    ├── lexer.c           # Lexer implementation (267 lines)
    ├── lexer.h           # Lexer header / struct definitions
    ├── lexer.o           # Compiled object file
    └── token.h           # Token type enum and Token struct
```

### File Descriptions

| File | Purpose |
|------|---------|
| `src/token.h` | Defines the `TokenType` enum (46 token types) and the `Token` struct with type, source pointer, length, and line number |
| `src/lexer.h` | Defines the `Lexer` struct with source pointers, line tracking, and indentation stack (max depth 64) |
| `src/lexer.c` | Full lexer implementation — character-by-character scanning, indentation handling, keyword matching, and token construction |
| `src/main.c` | Program entry point — reads a `.kh` file, initializes the lexer, and prints all tokens to stdout |
| `makefile` | GNU Make build file — compiles with `gcc -std=c11 -Wall -Wextra -g` |

---

## Building from Source

### Prerequisites

- **C compiler**: GCC (MinGW on Windows, or any POSIX GCC)
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

The build produces a `khan` executable (or `khan.exe` on Windows).

### Build Configuration

The `makefile` uses:
- **Compiler**: `gcc`
- **Standard**: `-std=c11`
- **Warnings**: `-Wall -Wextra`
- **Debug symbols**: `-g`

All `.c` files in `src/` are automatically discovered and compiled.

---

## Usage

```bash
# Run a Khan source file through the lexer
./khan examples/hello.kh
```

The current build outputs a token dump showing each token's line number, type name, and lexeme:

```
1    LET              'let'
1    IDENTIFIER       'message'
1    EQUAL            '='
1    STRING           '"Hello from Khan!"'
2    NEWLINE          '\n'
2    PRINT            'print'
2    IDENTIFIER       'message'
3    NEWLINE          '\n'
3    EOF              ''
```

### Exit Codes

| Code | Meaning |
|------|---------|
| `0`  | Success |
| `64` | Incorrect usage (wrong number of arguments) |
| `74` | I/O error (could not open source file) |

---

## Architecture

### Lexer Design

The lexer operates as a single-pass, character-by-character scanner with the following key components:

#### 1. Indentation Engine

Khan uses Python-style indentation to define block structure. The lexer maintains an **indentation stack** (up to 64 levels deep) to track the current indentation level:

- When a new line starts with more spaces than the current level → emits `TOKEN_INDENT`
- When a new line starts with fewer spaces → emits `TOKEN_DEDENT`
- Blank lines and comment-only lines are skipped without affecting indentation
- At EOF, remaining indentation levels are unwound with `TOKEN_DEDENT` tokens

#### 2. Token Recognition Pipeline

```
Source Code → Character-by-character scan → Token construction → Token stream
```

The lexer processes characters in this order:
1. **Start-of-line handling** — Check indentation
2. **Inline whitespace skipping** — Spaces, tabs, carriage returns, comments
3. **End-of-file** — Emit remaining DEDENTs, then EOF
4. **Newlines** — Increment line counter, emit NEWLINE token
5. **Identifiers/Keywords** — Alpha characters start identifier scanning; keyword lookup
6. **Numbers** — Digit characters start number scanning (supports decimals)
7. **Strings** — Double-quote starts string literal scanning
8. **Single-character tokens** — Operators, punctuation
9. **Multi-character operators** — `==`, `!=`, `<=`, `>=` via lookahead

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
    const char *start;       // start of current lexeme
    const char *current;     // current scan position
    int line;                // current line number
    int indent_stack[64];    // indentation level stack
    int indent_top;          // top of indentation stack
    int at_line_start;       // flag: at beginning of a line
    int pending_dedents;     // queued dedent tokens
} Lexer;
```

#### 4. Error Handling

The lexer produces `TOKEN_ERROR` tokens for:
- Unterminated string literals
- Unexpected characters (e.g., lone `!`)
- Any character that doesn't match a known token pattern

Error tokens carry a human-readable message string instead of a source lexeme.

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
| `RETURN` | `return` |
| `TRUE` | `true` |
| `FALSE` | `false` |
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

## Roadmap

### Phase 1: Lexer ✅ (Complete)
- [x] Character-by-character scanning
- [x] Indentation tracking (INDENT/DEDENT)
- [x] All token types defined
- [x] String, number, identifier literals
- [x] Keyword recognition
- [x] Comment handling
- [x] Error reporting

### Phase 2: Parser 🔲 (Next)
- [ ] Recursive descent parser
- [ ] Expression parsing (prefix, infix, postfix)
- [ ] Statement parsing
- [ ] AST node definitions
- [ ] Error recovery

### Phase 3: Interpreter 🔲
- [ ] Tree-walk interpreter
- [ ] Environment/scope management
- [ ] Variable assignment and lookup
- [ ] Function calls and stack frames
- [ ] Control flow (if/while)
- [ ] Boolean and logical operations

### Phase 4: Standard Library 🔲
- [ ] Built-in functions
- [ ] I/O operations
- [ ] String manipulation
- [ ] Math utilities
- [ ] Module system

### Phase 5: Advanced Features 🔲
- [ ] Type system
- [ ] Garbage collection or arena allocation
- [ ] Bytecode compiler
- [ ] Virtual machine
- [ ] Self-hosting compiler

---

## Technical Details

### Language Design Choices

- **Indentation-based scoping**: Eliminates braces and reduces visual clutter, inspired by Python
- **Explicit `let` for variables**: Makes variable declaration explicit, similar to JavaScript's `let` or Rust
- **`fn` for functions**: Short, clean keyword for function definitions
- **`print` as a keyword**: Built-in output for simplicity (may become a standard library function later)
- **`#` for comments**: Familiar from Python, Ruby, and shell scripting
- **No semicolons**: Line-based parsing with NEWLINE tokens as statement terminators

### Implementation Notes

- The lexer uses **raw pointers into the source buffer** rather than copying substrings, making it memory-efficient
- The **indentation stack** has a fixed maximum depth of 64 levels, which is sufficient for practical programs
- **Blank lines** and **comment-only lines** are transparent to the indentation system — they don't affect the indent/dedent tracking
- The lexer is a **single-pass scanner** — it reads the source once and produces tokens on demand (lazy tokenization via `lexer_next_token`)
- **Error tokens** contain descriptive messages rather than source text, making debugging easier

---

## Contributing

Contributions are welcome! Since this is an early-stage project, there are many opportunities to contribute:

1. **Parser implementation** — Build the next major component
2. **Test cases** — Create `.kh` test files covering edge cases
3. **Bug fixes** — Report or fix issues in the lexer
4. **Documentation** — Improve docs, add examples
5. **Features** — Implement new language features

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
./khan examples/hello.kh

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