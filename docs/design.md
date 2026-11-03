# Design

The application is a single C program. `main` stores the available fortunes in
an array of string literals, seeds the standard pseudo-random number generator
with the current time, and uses a random array index to select a message.

The program writes its title and selected fortune to standard output, then
returns success. The example temperature log in `data/tempature_log.csv` is
included as project data and is not consumed by this program.
