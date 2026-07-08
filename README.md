# Khan Programming Language

![Language](https://img.shields.io/badge/language-C11-blue)
![Build](https://img.shields.io/badge/build-make-green)
![License](https://img.shields.io/badge/license-MIT-orange)
![Status](https://img.shields.io/badge/status-stable--release-green)
![Packages](https://img.shields.io/badge/packages-13-purple)

**Khan** is a custom, indentation-based programming language built entirely from scratch in C11. Created by **Irfan Khan**, this project is a hands-on exploration of compiler design, lexer construction, parser development, runtime interpretation, and standard library implementation. The language draws inspiration from Python's clean indentation syntax and Lua's simplicity, while keeping a minimal footprint in pure C.

> **Repository**: [github.com/khandev1211-cpu/Khan](https://github.com/khandev1211-cpu/Khan)

---

## Table of Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Package Manager — kh](#package-manager--kh)
- [Packages](#packages)
- [Web Framework — webi](#web-framework--webi)
- [Language Features](#language-features)
- [Syntax & Examples](#syntax--examples)
- [Built-in Libraries](#built-in-libraries)
- [Standard Library](#standard-library)
- [Project Structure](#project-structure)
- [Building from Source](#building-from-source)
- [Architecture](#architecture)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)

---

## Overview

Khan is a from-scratch programming language implementation focused on understanding the fundamentals of how programming languages work under the hood. The project implements a complete language pipeline:

1. **Lexer (Tokenizer)** — Converts raw source code into a stream of tokens
2. **Parser** — Builds an Abstract Syntax Tree (AST) using recursive descent parsing
3. **Tree-Walk Interpreter** — Executes programs by recursively evaluating AST nodes
4. **Standard Library** — 30+ built-in functions for I/O, strings, math, arrays, and maps
5. **Built-in Libraries** — Native C libraries for JSON, datetime, and HTTP requests
6. **Package Manager (`kh`)** — Install community packages with `kh install <name>`

---

## Quick Start

```bash
# Clone and build
git clone https://github.com/khandev1211-cpu/Khan.git
cd Khan
make

# Install globally (Linux/macOS)
sudo make install

# Run a script
khan examples/hello.kh

# Install packages
kh install math
kh install strings
kh install colors
kh install requests
kh install postman

# Use packages in your script
```

```khan
import "math"
import "colors"

print green("Khan is ready!")
print math_sqrt(144)
```

---

## Package Manager — kh

Khan ships with `kh.exe` — a package manager that downloads packages from the official registry hosted on GitHub.

### Commands

| Command | Description |
|---|---|
| `kh install <name>` | Download and install a package |
| `kh remove <name>` | Remove an installed package |
| `kh list` | Show all available packages in the registry |
| `kh installed` | Show locally installed packages |
| `kh info <name>` | Show details about a package |

### How it works

- Packages are installed to `C:\Users\<you>\.khan\packages\<name>\` on Windows, or `~/.khan/packages/<name>/` on Linux/macOS
- The registry lives at `packages/registry.json` in this repo
- `import "<name>"` in any `.kh` file automatically resolves installed packages

```bash
kh install math
```
```khan
import "math"
print math_sqrt(144)    # 12
print PI                # 3.14159...
```

---

## Packages

| Package | Version | Description |
|---|---|---|
| `math` | 2.0.0 | Advanced math: sqrt, pow, gcd, primes, factorial, mean |
| `strings` | 1.0.0 | String utilities: split, trim, replace, pad, contains, join |
| `colors` | 1.0.0 | Terminal ANSI colors: red, green, bold, print_success, print_error |
| `requests` | 1.1.0 | HTTP client with auto JSON decode: get, post, post_json, put, delete |
| `postman` | 1.0.0 | API testing toolkit: send requests, assert responses, print reports |
| `collections` | 1.0.0 | Functional array helpers: sort, filter, map_each, reduce, find, unique, chunk, zip, sort_by |
| `fs` | 1.0.0 | File system utilities: read, write, append, copy, path helpers, JSON config |
| `datetime` | 1.0.0 | Date/time utilities: timer, elapsed, human-readable duration, sleep helpers |
| `test` | 1.0.0 | Unit testing framework: assertions, test suites, pass/fail reporting |
| `events` | 1.0.0 | Event emitter system: on, emit, once, off, history |
| `logger` | 1.0.0 | Structured logger: debug/info/warn/error levels, file output, silent mode |
| `uuid` | 1.0.0 | Unique ID generation: uuid_v4, short IDs, sequential counters |
| `webi` | 1.1.3 | Web framework: routing, security, templates, static files, HTTP server — see [docs/webi.md](docs/webi.md) |
| `morgos` | 1.0.0 | Morgan-style request logger for webi — colored status, elapsed time, response size, as a swappable `after()` hook |

### math

```khan
import "math"

print math_sqrt(144)        # 12
print math_pow(2, 10)       # 1024
print math_factorial(6)     # 720
print math_gcd(48, 18)      # 6
print math_is_prime(97)     # true
print math_mean([1,2,3,4])  # 2.5
print PI                    # 3.14159265...
print E                     # 2.71828182...
```

### strings

```khan
import "strings"

print str_trim("  hello  ")               # hello
print str_replace("foo bar foo", "foo", "baz")  # baz bar baz
print str_split("a,b,c", ",")            # ["a", "b", "c"]
print str_join(["x", "y", "z"], " | ")   # x | y | z
print str_starts_with("hello", "hel")    # true
print str_reverse("Khan")                # nahK
print str_pad_left("5", 3, "0")          # 005
```

### colors

```khan
import "colors"

print red("error occurred")
print green("build passed")
print yellow("warning: low memory")
print bold("important message")
print_success("Tests passed")
print_error("File not found")
print_warn("Deprecated API")
print_info("Server started on port 8080")
```

### requests

```khan
import "requests"

let res = get("https://api.example.com/users")
if ok(res):
    let users = json(res)
    print users[0]["name"]
else:
    raise_for_status(res)

# POST JSON
let data = {"name": "Irfan", "lang": "Khan"}
let res2 = post_json("https://api.example.com/users", data)
print status(res2)
```

### postman

```khan
import "postman"

let pm = pm_new_with_base("https://jsonplaceholder.typicode.com")

pm = pm_get(pm,      "GET todo",   "/todos/1")
pm = pm_post_json(pm, "POST todo", "/todos", {"title": "Learn Khan", "completed": false})
pm = pm_assert(pm,   "math works",  1 + 1 == 2)

let fake = {"status": 200, "body": "{\"id\": 1}", "success": true}
pm = pm_expect_status(pm,     "status 200",  fake, 200)
pm = pm_expect_json_field(pm, "id equals 1", fake, "id", 1)

pm_run(pm)
```

Output:
```
  Khan Postman ─ Test Results
  ══════════════════════════════════════

  PASS  GET todo  [200]
  PASS  POST todo  [201]
  PASS  math works  [0]
  PASS  status 200  [200]
  PASS  id equals 1  [200]

  ══════════════════════════════════════
  Total: 5   Passed: 5   Failed: 0
  ══════════════════════════════════════
  ✓ All 5 tests passed!
```

---

## Web Framework — webi

`webi` is a Flask-inspired web framework: routing, request/response
helpers, middleware, opt-in security (CSRF, rate limiting, API keys,
CORS), HTML templates (including reading them off disk), static file
serving, and a small HTTP server — all built on top of Khan.

```bash
kh install webi
```

```khan
import "webi"

let app = webi_app()
app = route(app, "GET", "/", fn_index)
app = serve_static(app, "/static", "public")

fn fn_index(req):
    let name = query_get(req, "name", "World")
    return res_html(render("<h1>Hello, {{name}}!</h1>", {"name": name}))

webi_run(app, 8080)
```

Or pull in the security/HTTP-client/JSON helpers in one line with
[`from`-import](docs/from-import.md):

```khan
from webi import webi, security, requests, json
```

**Full reference:** [docs/webi.md](docs/webi.md) — routing, request/response
helpers, middleware, CSRF/rate-limiting/API-key/CORS security,
`render()`/`render_file()`, `serve_static()`, running the server, and
known limitations (single-threaded server, string/NUL-byte caveats for
binary static assets).

---

## Language Features

### Core Language

- ✅ **Indentation-based scoping** — Spaces define block structure (like Python)
- ✅ **Variable declarations** — `let` keyword
- ✅ **Function definitions** — `fn` keyword with parameters and closures
- ✅ **Recursion** — Functions can call themselves
- ✅ **Conditionals** — `if` / `elif` / `else`
- ✅ **While loops** — `while condition:`
- ✅ **For-in loops** — `for var in expr:`
- ✅ **Break & Continue** — `break` and `continue` inside loops
- ✅ **Return statements** — `return expr` from functions
- ✅ **Closures** — Functions capture their enclosing lexical scope
- ✅ **Import/Module system** — `import "filename.kh"` or `import "packagename"`, plus [`from "pkg" import A, B, C`](docs/from-import.md) for pulling out individual symbols, the package's own name, or a sibling submodule file
- ✅ **String escape sequences** — `\n`, `\t`, `\r`, `\\`, `\"`, `\0`, `\xHH`

### Data Types

- ✅ **Numbers** — Integers and floating-point (double precision)
- ✅ **Strings** — Double-quoted with full escape sequence support
- ✅ **Booleans** — `true` / `false`
- ✅ **Nil** — `nil`
- ✅ **Arrays** — `[1, 2, 3]` with zero-based indexing
- ✅ **Maps** — `{"key": value}` dictionaries

### Operators

- ✅ **Arithmetic** — `+`, `-`, `*`, `/`, `%`
- ✅ **Comparison** — `==`, `!=`, `<`, `<=`, `>`, `>=`
- ✅ **Logical** — `and`, `or`, `not`
- ✅ **String concatenation** — `+`
- ✅ **Indexing** — `arr[i]`, `map["key"]`

---

## Syntax & Examples

### Variables & Types

```khan
let name    = "Irfan"
let age     = 25
let active  = true
let nothing = nil
let scores  = [95, 87, 92]
let person  = {"name": "Irfan", "city": "Faisalabad"}
```

### Conditionals with elif

```khan
fn grade(score):
    if score >= 90:
        return "A"
    elif score >= 80:
        return "B"
    elif score >= 70:
        return "C"
    else:
        return "F"

print grade(85)   # B
```

### Loops with break and continue

```khan
# break
let i = 0
while true:
    if i >= 5:
        break
    i = i + 1
print i   # 5

# continue — skip evens
for n in [1, 2, 3, 4, 5, 6]:
    if n % 2 == 0:
        continue
    print n   # 1 3 5
```

### Recursion

```khan
fn factorial(n):
    if n <= 1:
        return 1
    return n * factorial(n - 1)

print factorial(10)   # 3628800

fn fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

print fib(10)   # 55
```

### String Escape Sequences

```khan
print "Hello\nWorld"         # newline
print "Tab:\there"           # tab
print "Quote: \"Hi\""        # double quote
print "\x1b[31mRed\x1b[0m"  # ANSI color (hex escape)
```

### JSON (built-in)

```khan
let data = {"name": "Khan", "version": 2, "ready": true}
let s = json_encode(data)
print s   # {"name": "Khan", "version": 2, "ready": true}

let decoded = json_decode(s)
print decoded["name"]   # Khan
```

### Datetime (built-in)

```khan
let today = now()
print date_format(today, "%Y-%m-%d")       # 2026-06-28
print date_format(today, "%A, %B %d %Y")   # Saturday, June 28 2026

let next_week = date_add(today, "days", 7)
print date_format(next_week, "%Y-%m-%d")
```

### HTTP Requests (built-in)

```khan
let res = http_get("https://api.example.com/data")
print res["status"]    # 200
print res["success"]   # true
print res["body"]      # raw response body

let post_res = http_post_json("https://api.example.com/users",
                               {"name": "Irfan", "lang": "Khan"})
print post_res["status"]
```

---

## Built-in Libraries

These are native C libraries included in `khan.exe` — no installation needed.

### JSON

| Function | Description |
|---|---|
| `json_encode(value)` | Any Khan value → JSON string |
| `json_decode(string)` | JSON string → Khan map/array/value |

### Datetime

| Function | Description |
|---|---|
| `now()` | Current local time as a map (`year`, `month`, `day`, `hour`, `minute`, `second`, `weekday`, `timestamp`) |
| `utcnow()` | Same but UTC |
| `timestamp()` | Unix timestamp as a number |
| `date_format(date, fmt)` | Format using strftime codes (`%Y-%m-%d`, `%H:%M:%S`, `%A`, etc.) |
| `date_parse(str, fmt)` | Parse a date string into a date map |
| `date_add(date, unit, n)` | Add `"seconds"`, `"minutes"`, `"hours"`, `"days"`, or `"weeks"` |
| `date_diff(d1, d2)` | Difference in seconds |

### HTTP Requests

| Function | Description |
|---|---|
| `http_get(url)` | GET request |
| `http_post(url, body)` | POST with form-encoded body |
| `http_post_json(url, map)` | POST with auto-encoded JSON body |
| `http_put(url, body)` | PUT request |
| `http_delete(url)` | DELETE request |

All HTTP functions return `{"status": 200, "body": "...", "success": true}`.

> **Windows**: Uses WinHTTP (built-in, no extra install).
> **Linux/macOS**: Uses `curl` (must be installed).

---

## Standard Library

30+ native built-in functions available in every Khan program:

### Type Conversion

| Function | Description |
|---|---|
| `num(x)` | String → number |
| `str(x)` | Any value → string |
| `type(x)` | Returns type name: `"number"`, `"string"`, `"bool"`, `"nil"`, `"array"`, `"map"` |

### String Functions

| Function | Description |
|---|---|
| `len(s)` | Length of string, array, or map |
| `substring(s, start, length)` | Extract substring (note: 3rd arg is **length**, not end index) |
| `upper(s)` | Uppercase |
| `lower(s)` | Lowercase |
| `contains(s, sub)` | Check if string contains substring |
| `trim(s)` | Strip leading/trailing whitespace |
| `split(s, delim)` | Split string into array |

### Array Functions

| Function | Description |
|---|---|
| `push(arr, val)` | Return new array with value appended |
| `range(n)` | Array `[0, 1, ..., n-1]` |
| `range(start, end)` | Array `[start, ..., end-1]` |

### Map Functions

| Function | Description |
|---|---|
| `keys(map)` | Array of all keys |
| `has(map, key)` | Check if key exists |

### Math Functions

| Function | Description |
|---|---|
| `abs(x)` | Absolute value |
| `min(a, b)` | Minimum |
| `max(a, b)` | Maximum |
| `sqrt(x)` | Square root |
| `round(x)` | Round to nearest integer |
| `floor(x)` | Round down |
| `ceil(x)` | Round up |
| `pow(base, exp)` | Exponentiation |
| `random()` | Random float in [0, 1) |

### I/O Functions

| Function | Description |
|---|---|
| `input()` | Read a line from stdin |
| `input(prompt)` | Show prompt then read line |
| `read_file(path)` | Read file as string |
| `write_file(path, content)` | Write string to file |
| `file_exists(path)` | Check if file exists |

### Utility Functions

| Function | Description |
|---|---|
| `sleep(ms)` | Pause execution (milliseconds) |
| `clock()` | CPU time in seconds |
| `exit(code)` | Exit program |

---

## Project Structure

```
Khan/
├── khan.exe                  # Khan interpreter (Windows)
├── kh.exe                    # Khan package manager (Windows)
├── makefile                  # Build configuration
├── README.md                 # This file
├── docs/
│   ├── webi.md                # Full webi framework reference
│   └── from-import.md         # `from X import A, B, C` reference
├── packages/
│   ├── registry.json         # Official package registry
│   ├── math/
│   ├── strings/
│   ├── colors/
│   ├── requests/
│   ├── postman/
│   ├── collections/
│   ├── fs/
│   ├── datetime/
│   ├── test/
│   ├── events/
│   ├── logger/
│   ├── uuid/
│   ├── morgos/
│   └── webi/
│       ├── package.json
│       ├── webi.kh            # entry point — pulls in every file below
│       ├── util.kh            # internal string helper other files rely on
│       ├── app.kh             # app context: webi_app / webi_debug / webi_name
│       ├── routing.kh         # route registration, path matching, serve_static()
│       ├── request.kh         # helpers for reading data off the req map
│       ├── response.kh        # res_* response builders
│       ├── middleware.kh      # middleware registration + built-ins
│       ├── security.kh        # CSRF, rate limiting, API-key auth, CORS config
│       ├── server.kh          # dispatch, native-bridge handler, webi_run
│       ├── template.kh        # render() / render_file() + html_escape()
│       ├── meta.kh            # webi_version() / webi_routes()
│       ├── requests.kh        # thin http_* re-export for `from webi import requests`
│       └── json.kh            # thin json_* re-export for `from webi import json`
├── examples/
│   ├── hello.kh
│   ├── full_test.kh
│   ├── arrays_test.kh
│   ├── maps_test.kh
│   ├── webi_security_test.kh
│   ├── webi_from_import_test.kh
│   ├── webi_phase3_test.kh
│   ├── webi_after_hook_test.kh
│   └── todo_app/
│       ├── main.kh
│       ├── todo_core.kh
│       ├── todo_storage.kh
│       └── todo_ui.kh
└── src/
    ├── main.c                 # Entry point + ANSI setup
    ├── token.h                # Token types
    ├── lexer.h / lexer.c      # Lexer — tokenizer
    ├── ast.h / ast.c          # AST node definitions
    ├── parser.h / parser.c    # Recursive descent parser
    ├── interpreter.h / interpreter.c  # Tree-walk interpreter
    ├── khan_stdlib.c          # 30+ built-in functions
    ├── json_lib.h / json_lib.c        # Native JSON encode/decode
    ├── datetime_lib.h / datetime_lib.c # Native datetime functions
    ├── requests_lib.h / requests_lib.c # Native HTTP client
    ├── webi_lib.h / webi_lib.c         # Native HTTP server, MIME table,
    │                                    path-traversal-safe file resolution
    └── kh.c                   # Package manager CLI
```

---

## Building from Source

### Prerequisites

- **GCC** (C11) — MinGW64 on Windows, or any POSIX GCC
- **GNU Make**
- **Windows only**: links `-lwinhttp` and `-lshell32` (both ship with Windows)
- **Linux/macOS**: links `-lm`, uses `curl` for HTTP

### Build & Install

#### Windows
1. Make sure you have **GCC** (MinGW-w64) installed and in your PATH.
2. Run the provided build script:
   ```cmd
   build.bat
   ```

#### Linux / macOS
```bash
git clone https://github.com/khandev1211-cpu/Khan.git
cd Khan
chmod +x configure
./configure
make

# Install globally
sudo make install
```

```bash
make clean   # remove build artifacts
```

### Exit Codes

| Code | Meaning |
|---|---|
| `0` | Success |
| `64` | Wrong number of arguments |
| `65` | Parse/syntax error |
| `70` | Runtime error |
| `74` | I/O error (file not found) |

---

## Architecture

### Pipeline

```
Source .kh file
     │
     ▼
  Lexer  ──────────────── Token stream (INDENT/DEDENT/keywords/literals)
     │
     ▼
  Parser ──────────────── Abstract Syntax Tree (31 node types)
     │
     ▼
  Interpreter ─────────── Recursive tree-walk evaluation
     │
     ▼
  Output / Side effects
```

### Interpreter Design

- **Value system**: tagged union (`VAL_NUMBER`, `VAL_STRING`, `VAL_BOOL`, `VAL_NIL`, `VAL_ARRAY`, `VAL_MAP`, `VAL_FUNCTION`, `VAL_NATIVE`)
- **Scope**: linked-list of environments (lexical scoping)
- **Closures**: functions capture enclosing scope at definition time; self-reference injected for recursion
- **Return signal**: `is_returning` flag propagates up through loops and blocks
- **Break/Continue**: `is_breaking` / `is_continuing` flags stop loop iteration cleanly
- **Deep copy semantics**: values are copied on assignment — no aliasing

### Package Resolution

`import "name"` resolves in this order:
1. Relative to current script directory (e.g. `./name`)
2. Relative path as-is
3. `~/.khan/packages/name/name.kh` (installed packages)

`from "name" import A, B, C` resolves each requested name against a
plain symbol, the package's own name (flattens the whole module), or a
sibling submodule file next to it (flattens that file's public names) —
see [docs/from-import.md](docs/from-import.md) for details, including
the privacy rule (`_`-prefixed names never get flattened) and the
closure-snapshot ordering constraint that applies to any multi-file
package.

---

## Roadmap

| Phase | Status |
|---|---|
| Lexer | ✅ Complete |
| Parser | ✅ Complete |
| Interpreter | ✅ Complete |
| Standard Library | ✅ Complete |
| Import/Module system | ✅ Complete |
| `from X import A, B, C` (symbols, self-name, submodule flatten) | ✅ Complete |
| `elif` / `break` / `continue` | ✅ Complete |
| Recursion fix | ✅ Complete |
| `\xHH` hex escape sequences | ✅ Complete |
| JSON built-in library | ✅ Complete |
| Datetime built-in library | ✅ Complete |
| HTTP requests built-in library | ✅ Complete |
| Package manager (`kh`) | ✅ Complete |
| `math` / `strings` / `colors` / `requests` / `postman` packages | ✅ Complete |
| `collections` / `fs` / `datetime` / `test` / `events` / `logger` / `uuid` packages | ✅ Complete |
| `webi` package — routing, request/response, middleware | ✅ Complete |
| `webi` security — CSRF, rate limiting, API keys, CORS, `secure_token()` | ✅ Complete |
| `webi` templates — `render()` / `render_file()` | ✅ Complete |
| `webi` static files — `serve_static()`, MIME table, path-traversal protection | ✅ Complete |
| `webi` `after()` hooks — post-response middleware, `morgos` request logger | ✅ Complete |
| `webi` threaded server (thread-per-connection, concurrency cap) | 🔲 Planned |
| Error handling (`try`/`catch`) | 🔲 Planned |
| Bytecode compiler + VM | 🔲 Planned |
| Binary-safe string type (length-prefixed, not NUL-terminated) | 🔲 Planned |
| Self-hosted compiler | 🔲 Planned |

---

## Contributing

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Build and test: `make && khan examples/full_test.kh`
4. Commit and push
5. Open a Pull Request

To publish a new package, add a folder under `packages/` and update `packages/registry.json`.

---

## License

MIT License — open source, free to use.

---

## Author

**Irfan Khan** — Creator and maintainer of the Khan programming language.

- GitHub: [@khandev1211-cpu](https://github.com/khandev1211-cpu)
- Repository: [github.com/khandev1211-cpu/Khan](https://github.com/khandev1211-cpu/Khan)

---

*Built from scratch in C — because understanding the fundamentals matters.*