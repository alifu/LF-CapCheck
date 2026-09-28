# LF-CapCheck

See how much of your AI subscription limit is left, right in the terminal. Claude comes first.

```
LF-CapCheck - AI usage limits

  1) Claude  updated 3 min ago
  2) Codex   coming soon

Choose a provider (1-2) or q to quit: 1

Claude - remaining usage                                    as of 11:57 (3 min ago)
5-hour session ███████████████████░░░░░░░░░░░  62% left   resets in 2h 14m
Weekly         ██████░░░░░░░░░░░░░░░░░░░░░░░░  21% left   resets in 3d 4h

[r] reload   [b] back   [q] quit
```

The bars show what is **left**. They turn green above 50%, yellow from 20% to 50% and red below
20%. The percentage is always printed, so colour is never the only signal.

## How it works

Claude Code can pass your subscription limits (5-hour and weekly) to a "status line" command
([documented here](https://code.claude.com/docs/en/statusline)). LF-CapCheck is that command:

```
Claude Code ──(status line JSON)──▶ lf-capcheck statusline ──▶ small private snapshot file
                                                                        │
                                              lf-capcheck (menu + chart) ◀┘
```

- **No password, token or API key is ever asked for or stored.** Anthropic's terms do not allow
  third-party tools to collect Claude.ai credentials, so this tool never touches them.
- **No network access.** Nothing leaves your machine.
- The status line command also prints a short summary (`5h 62% left · 7d 21% left`) that Claude
  Code shows in its own status bar.

## Requirements

- macOS 14 or later, with the Xcode command line tools (`xcode-select --install`)
- A Claude Pro or Max plan (the limits only exist for subscribers)

## Install

Homebrew packaging is planned. For now, build from source:

```bash
git clone <this repository> && cd LF-CapCheck
make
make install PREFIX=$HOME/.local      # or PREFIX=/usr/local (needs sudo)
```

Make sure the `bin` folder under your prefix is on your `PATH`.

## Connect Claude (one time)

1. Run `lf-capcheck` and choose **1) Claude**. It shows a snippet like this, with the real path
   of the program filled in:

   ```json
   {
     "statusLine": {
       "type": "command",
       "command": "/usr/local/bin/lf-capcheck statusline"
     }
   }
   ```

2. Add it to `~/.claude/settings.json`. LF-CapCheck never edits your Claude Code settings itself.
   If the file already has a `statusLine` entry, it would be replaced: keep your own script and
   call `lf-capcheck statusline` from it instead (the command reads Claude Code's JSON on stdin
   and prints one line).
3. Send a message in Claude Code. Your limits appear after its first reply.

## Using it

| Command | What it does |
|---|---|
| `lf-capcheck` | Opens the menu. Pick a provider by number. |
| `lf-capcheck --watch [--interval SECONDS] [PROVIDER]` (or `-w`) | Keeps one provider's chart up to date. Defaults: the first provider (Claude), every 5 seconds (1 to 3600). |
| `lf-capcheck statusline` | Called by Claude Code, not by you. |
| `lf-capcheck --help`, `--version` | Help and version. |

In the menu: `r` reloads, `b` goes back, `q` quits. End of input quits too, so it can be scripted.
Set `NO_COLOR=1` for no colours. Output that is piped or redirected uses plain `#` and `-`
characters.

In watch mode the chart redraws in place on a terminal (and is appended frame by frame when the
output is redirected). Press Enter to refresh at once, or type `q` and Enter to quit; Ctrl-C works
too. Data that arrives while you watch is picked up on the next frame. The terminal mode is never
changed, so nothing needs restoring afterwards.

Data older than 30 minutes is flagged ("may be out of date") on the menu and under the chart, with
a hint on how to refresh it.

## What is stored

One file, and only on your Mac:

```
~/Library/Application Support/lf-capcheck/claude-usage.json      (folder 0700, file 0600)
{"version":1,"as_of":1790578273,"five_hour":{"used_percentage":23.5,"resets_at":1790585493},
 "seven_day":{"used_percentage":41.2,"resets_at":1790837493}}
```

Nothing else from Claude Code's payload is kept: no paths, session ids, costs or prompts. The file
is written atomically and read back with strict validation. A folder or file that is a symlink,
belongs to someone else, or is accessible to other users is refused.

## Limits of this approach

- **The data is only as fresh as your last Claude Code activity.** Claude Code only reports limits
  while it runs. The chart shows "as of" and warns when the data is over 30 minutes old.
- Claude Code reports a window only after its first reply in a session, and drops it once it has
  reset. A window that has reset shows "reset - waiting for new data" until Claude Code reports
  again.
- If a later update leaves out one window, the last known value is kept until that window resets,
  and the "as of" time then reflects the oldest data shown.
- Two Claude Code sessions updating at the very same instant can lose one update. Nothing is
  corrupted (writes are atomic) and the next update repairs it.
- Bars use block characters and expect a UTF-8 terminal.
- Codex is listed as "coming soon". A provider is only added when there is an official, terms-
  compliant source for its usage data. See [TODO.md](TODO.md) for what is blocking it.

## Uninstall

1. Remove the `statusLine` entry from `~/.claude/settings.json`.
2. Delete the data: `rm -r ~/Library/Application\ Support/lf-capcheck`
3. Delete the program: `rm <prefix>/bin/lf-capcheck`

## Development

```bash
make            # release build into build/lf-capcheck (ad-hoc signed, hardened runtime)
make test       # unit tests under AddressSanitizer + UndefinedBehaviorSanitizer
make fuzz       # mutation fuzzer on every input path (FUZZ_ITERATIONS=100000 FUZZ_SEED=7)
make analyze    # clang static analyzer, warnings are errors
make coverage   # line coverage of project code, fails below 80%
```

- Written in C17. The only dependency is a vendored copy of [cJSON](https://github.com/DaveGamble/cJSON)
  (`third_party/cJSON.md` records the version and checksums). No network code.
- The `Makefile` is the source of truth for build flags. The Xcode project mirrors it for editing
  and debugging (`tests/` is shown as a folder reference; run tests with `make test`).
- Layout: `cli/` commands, `providers/` usage sources, `store/` snapshot file, `ui/` menu and
  chart, `util/` shared helpers, `tests/` unit tests and the fuzzer.
- Adding a provider means adding one `provider_t` (see `providers/provider.h`) and one line in
  `providers/builtin.c`.
