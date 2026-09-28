#ifndef LFCC_CODEX_H
#define LFCC_CODEX_H

#include "providers/provider.h"

/*
 * Codex placeholder: listed in the menu as "coming soon"; load_usage always
 * returns LFCC_ERR_UNAVAILABLE.
 *
 * Why it is not implemented (spike of 2026-09-28): Codex has no custom status
 * line command (its status line is a list of built-in items), and its notify
 * and hook payloads carry no usage limits. The ChatGPT token in ~/.codex is
 * subscription credentials, which this tool never collects. The only remaining
 * source is Codex's undocumented, sometimes-null session files, which could not
 * be verified against real data. Add a real provider only once an official
 * source exists or a verified sample of the data is available.
 */
const provider_t *codex_provider(void);

#endif /* LFCC_CODEX_H */
