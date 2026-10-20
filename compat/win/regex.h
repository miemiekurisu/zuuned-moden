/* POSIX <regex.h> stand-in.  libzune's filename decoder uses POSIX extended
 * regexes with REG_ICASE, and relies on ERE semantics (for example `\(` is a
 * literal parenthesis), so the C++ <regex> ECMAScript grammar is not a drop-in
 * replacement.  PCRE2 ships a POSIX wrapper that implements regcomp/regexec
 * with REG_EXTENDED semantics; CMake links pcre2-posix + pcre2 statically. */
#ifndef ZUUNED_WIN_REGEX_H
#define ZUUNED_WIN_REGEX_H
#include <pcre2posix.h>
#endif /* ZUUNED_WIN_REGEX_H */