# Fortune Generator

A small C program that prints a randomly selected fortune each time it runs.

## Project structure

- `src/main.c` — program entry point and fortune selection
- `docs/` — project requirements, design notes, and change log
- `tests/test_plan.md` — manual test plan
- `data/tempature_log.csv` — example temperature log data
- `status.txt` — current project status

## Build and run

Compile with a C compiler:

```sh
gcc -Wall -Wextra -std=c11 src/main.c -o fortune
./fortune
```

The program prints one fortune to standard output. The temperature CSV is sample project data and is not currently read by the program.

## Future improvements

- Add automated tests for fortune selection.
- Consider reading project data from a file.
