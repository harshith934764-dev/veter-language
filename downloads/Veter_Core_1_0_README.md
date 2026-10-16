# Veter Core 1.0 — first prototype

This prototype adds a new readable front-end syntax to the uploaded Veter C++20 compiler while preserving the existing syntax.

## New commands in this prototype

```text
remember score as 95
display score

choose when score >= 50
    display "passed"
else choose
    display "try again"
close choice

remember n as 1
repeat 3 rounds
    display n
    revise n as n + 1
close cycle

while n < 5
    display n
    revise n as n + 1
close cycle
```

- `remember name as expression`: create/store a value.
- `revise name as expression`: update a value.
- `display expression`: print a value.
- `ask for name`: read a line of input.
- `choose when condition` / `else choose` / `close choice`: conditional block.
- `repeat N rounds` / `close cycle`: counted loop.
- `while condition` / `close cycle`: condition-controlled loop.

The legacy commands (`make`, `set`, `say`, `take`, `check`, `otherwise`, `finish`, `repeat N times`, `end repeat`, and `keep`) remain available.

## Build

```bash
g++ -std=c++20 -Wall -Wextra -pedantic veter_core_1_0.cpp -o veter
```

## Run

```bash
./veter veter_core_1_0_test.vtr
```

On Windows Git Bash, use `./veter.exe veter_core_1_0_test.vtr` if the output executable is named `veter.exe`.

## Prototype status

Compiled successfully with C++20 and tested for:
- new variable/display commands
- if/else conditions
- counted loops
- while loops
- compatibility with a legacy-syntax sample

This is an early prototype, not a stable language release. The new commands currently lower into the existing compiler's instruction pipeline; later milestones should formalize the grammar, improve diagnostics, and add automated regression tests before expanding functions, collections, or web features.
