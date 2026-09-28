# cJSON (vendored, unmodified)

- Project: https://github.com/DaveGamble/cJSON
- Version: v1.7.19 (tag `v1.7.19`, published 2025-09-09)
- License: MIT (`third_party/cJSON.LICENSE`)
- Files: `LF-CapCheck/vendor/cJSON.c`, `LF-CapCheck/vendor/cJSON.h`, fetched from
  `https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.19/`

SHA-256 of the files as vendored (re-check with `shasum -a 256 <file>` before any update):

```
298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d  LF-CapCheck/vendor/cJSON.c
25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee  LF-CapCheck/vendor/cJSON.h
a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c  third_party/cJSON.LICENSE
```

Rules:
- Never edit the vendored files by hand. To upgrade, fetch the new tag, update this note, and re-run the tests.
- The vendored files are compiled with warnings disabled (`-w`); project code is compiled with `-Werror`.
- Input handed to cJSON must be size-capped and depth-checked by the caller.
