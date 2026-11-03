# Test Plan

## Build

- Compile `src/main.c` with a C11 compiler and warnings enabled.
- Confirm compilation completes without errors or warnings.

## Runtime

- Run the compiled program several times.
- Confirm each run prints the title followed by exactly one non-empty fortune.
- Confirm the program exits successfully.
- Confirm the output fortune is one of the messages listed in `src/main.c`.

## Data file

- Confirm `data/tempature_log.csv` has a header and consistently formatted
  timestamp and temperature values.
