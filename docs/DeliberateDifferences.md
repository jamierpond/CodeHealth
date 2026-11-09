# Deliberate differences from CodeScene

codehealth uses CodeScene as a benchmark, not a specification. These are the places
where it differs on purpose. Each entry says what CodeScene does, what codehealth
does, and why.

## Python docstrings are not lines of code

CodeScene counts a docstring's lines as code, so documenting a function makes it
longer and can turn it into a Large Method or push the file into Large File.
codehealth leaves module, class and function docstrings out of every line count, the
same way it leaves out comments. Documentation does not make code harder to
change, and a tool that charges for it gives people a reason to delete it.

## Function clones need 90% token similarity

CodeScene calls two functions duplicates at 75% similarity. codehealth needs 90%
(after renaming identifiers and literals). At 75%, functions that only share a shape
count as copies: two three-line wrappers that call different formatters, or two tests
that build different objects with the same assertion calls. Merging them would not
make the code simpler, so flagging them is noise.

Function clones within one file are found however common their code is in the
repository. Before, a trigram shared by more than 256 functions anywhere was ignored,
so in a large repository a copy-pasted pair like pytorch's `get_rocm_compiler` and
`get_rocm_bundler` (96% similar) was never compared. This applies to every language;
it moved 1,047 C and C++ files in pytorch and tensorflow, and the C++ calibration
stays within 0.02 of where it was.

## Large File stays at 1000 lines of code

CodeScene flags Python files from 600 lines. codehealth uses 1000 for every language.
Between 600 and 1000 lines the call depends on what the file holds:
`django/urls/resolvers.py` (667 lines, one concept in nine classes) is fine as it is,
while a grab bag of helpers is not. Low Cohesion and Number of Functions already
catch the grab bag, so a line count alone should not.

## Decisions taken to match CodeScene

These were differences until the comparison showed CodeScene's answer was the better
one; codehealth now does the same for Python.

- Bumpy Road: each `elif` or `else` branch of a shallow conditional that nests again
  is its own bump. `_cast_to_dtensor` in pytorch has two (an `if` and an `elif`, each
  with a nested `if`); codehealth used to count one.
- Large Method: a Python method's cost grows in proportion to its length, not its
  logarithm, so a 584-line method costs 2.8 points, as in CodeScene, instead of 1.6
  at most. Other languages keep the logarithm they were calibrated with.
