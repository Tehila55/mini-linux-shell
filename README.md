# Mini Linux Shell

A mini Linux shell implementation in C with support for process management, piping, threading, and virtual memory simulation.

---

## Features

- Command execution using `fork()` and `execvp()`
- Pipe support
- Background process execution
- STDERR redirection
- Dangerous command detection
- Command timing and logging
- Custom `my_tee` implementation
- Matrix calculations using pthreads
- Virtual memory paging simulation
- LRU page replacement

---

## Project Structure

### `mini_shell.c`
Main shell interface:
- Handles user input
- Executes shell commands
- Supports pipes and redirections
- Handles built-in commands
- Calls the virtual memory simulation

### `virtual_memory.c`
Virtual memory management module:
- Page table management
- RAM and swap simulation
- Load/store instructions
- Page fault handling
- LRU replacement algorithm

---

## Supported VMEM Commands

```bash
load <address>
store <address> <value>
print table
print ram
print swap
```

---

## Technologies

- C
- Linux system calls
- pthreads
- Virtual memory concepts
- Process management

---

## Build

```bash
gcc mini_shell.c virtual_memory.c -o mini_shell -pthread
```

---

## Run

```bash
./mini_shell dangerous.txt log.txt
```

---

## Author

Tehila Cohen
