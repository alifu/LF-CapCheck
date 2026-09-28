#ifndef LFCC_UTF8_H
#define LFCC_UTF8_H

#include <stdbool.h>

/*
 * True when `text` is well-formed UTF-8 (no overlong forms, no surrogates,
 * nothing above U+10FFFF) and contains no control characters: not the ASCII
 * controls U+0000..U+001F and U+007F, and not the C1 controls U+0080..U+009F
 * (some terminals act on them). An empty string is plain text; NULL is not.
 */
bool utf8_is_plain_text(const char *text);

#endif /* LFCC_UTF8_H */
