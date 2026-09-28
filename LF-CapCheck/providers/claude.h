#ifndef LFCC_CLAUDE_H
#define LFCC_CLAUDE_H

#include "providers/provider.h"

/*
 * Claude (Pro/Max). Usage comes from the snapshot that `lf-capcheck
 * statusline` saves from Claude Code's status line data; no credentials are
 * involved. LFCC_ERR_NOT_CONNECTED means no snapshot exists yet.
 */
const provider_t *claude_provider(void);

#endif /* LFCC_CLAUDE_H */
