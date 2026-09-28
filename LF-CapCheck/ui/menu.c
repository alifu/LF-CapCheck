#include "ui/menu.h"

#include <ctype.h>
#include <string.h>

#include "ui/connect_snippet.h"
#include "ui/time_text.h"
#include "ui/usage_view.h"
#include "util/time_math.h"

#define ANSWER_MAX_CHARS 64
#define CHART_TEXT_MAX 2048
#define SNIPPET_MAX 1024
#define STATE_TEXT_MAX 96
#define MAX_CHOICE_DIGITS 3

typedef enum { NEXT_MAIN, NEXT_QUIT } next_t;
typedef enum { ANSWER_TEXT, ANSWER_TOO_LONG, ANSWER_END } answer_kind_t;
typedef enum { KEY_NONE = 0, KEY_RELOAD = 'r', KEY_BACK = 'b', KEY_QUIT = 'q' } menu_key_t;

/* ---- reading answers ---- */

static void trim(char *text)
{
    size_t length = strlen(text);
    size_t start = 0;

    while (length > 0 && isspace((unsigned char)text[length - 1])) {
        text[--length] = '\0';
    }
    while (text[start] != '\0' && isspace((unsigned char)text[start])) {
        start++;
    }
    memmove(text, text + start, length - start + 1);
}

/* Discards the rest of an overlong line so it is not mistaken for further answers. */
static void skip_rest_of_line(FILE *in)
{
    int character = 0;

    while ((character = fgetc(in)) != EOF && character != '\n') {
    }
}

static answer_kind_t read_answer(FILE *in, char *line, size_t cap)
{
    size_t length = 0;

    if (fgets(line, (int)cap, in) == NULL) {
        return ANSWER_END;
    }
    length = strlen(line);
    if (length == cap - 1 && line[length - 1] != '\n' && !feof(in)) {
        skip_rest_of_line(in);
        return ANSWER_TOO_LONG;
    }
    trim(line);
    return ANSWER_TEXT;
}

/* 1..count from a plain number; 0 for anything else. */
static size_t parse_choice(const char *line, size_t count)
{
    size_t value = 0;
    size_t length = strlen(line);

    if (length == 0 || length > MAX_CHOICE_DIGITS) {
        return 0;
    }
    for (size_t i = 0; i < length; i++) {
        if (!isdigit((unsigned char)line[i])) {
            return 0;
        }
        value = value * 10 + (size_t)(line[i] - '0');
    }
    return value >= 1 && value <= count ? value : 0;
}

static menu_key_t parse_key(const char *line)
{
    int key = line[0] != '\0' && line[1] == '\0' ? tolower((unsigned char)line[0]) : 0;

    return (key == KEY_RELOAD || key == KEY_BACK || key == KEY_QUIT) ? (menu_key_t)key : KEY_NONE;
}

/* ---- small helpers ---- */

static time_t current_time(const menu_env_t *env)
{
    return env->now != 0 ? env->now : time(NULL);
}

static provider_env_t provider_env_for(const menu_env_t *env)
{
    provider_env_t provider_env = {current_time(env), env->data_dir};

    return provider_env;
}

/* One-line state shown next to a provider on the main page. */
static void describe_state(const provider_t *provider, const menu_env_t *env, char *text, size_t cap)
{
    provider_env_t provider_env = provider_env_for(env);
    usage_snapshot_t usage = {0};
    char age[STATE_TEXT_MAX];

    switch (provider->load_usage(provider, &provider_env, &usage)) {
    case LFCC_OK:
        if (usage_all_expired(&usage)) {
            snprintf(text, cap, "limits reset, waiting for activity");
        } else {
            time_text_age(time_seconds_between(usage.as_of, provider_env.now), age, sizeof age);
            snprintf(text, cap, "updated %s%s", age,
                     usage_is_stale(&usage, provider_env.now) ? ", may be out of date" : "");
        }
        break;
    case LFCC_ERR_NOT_CONNECTED:
        snprintf(text, cap, "not connected");
        break;
    case LFCC_ERR_UNAVAILABLE:
        snprintf(text, cap, "coming soon");
        break;
    default:
        snprintf(text, cap, "saved data cannot be read");
        break;
    }
}

/* ---- main page ---- */

static void print_main_page(const registry_t *registry, const menu_env_t *env)
{
    size_t name_width = 0;

    for (size_t i = 0; i < registry_count(registry); i++) {
        size_t length = strlen(registry_at(registry, i)->display_name);

        name_width = length > name_width ? length : name_width;
    }

    fprintf(env->out, "\nLF-CapCheck - AI usage limits\n\n");
    for (size_t i = 0; i < registry_count(registry); i++) {
        const provider_t *provider = registry_at(registry, i);
        char state[STATE_TEXT_MAX];

        describe_state(provider, env, state, sizeof state);
        fprintf(env->out, "  %zu) %-*s  %s\n", i + 1, (int)name_width, provider->display_name, state);
    }
}

static void print_choice_prompt(const registry_t *registry, FILE *out)
{
    size_t count = registry_count(registry);

    if (count == 1) {
        fprintf(out, "\nChoose a provider (1) or q to quit: ");
    } else {
        fprintf(out, "\nChoose a provider (1-%zu) or q to quit: ", count);
    }
    fflush(out);
}

/* Returns the chosen provider, or NULL when the user quits or input ends. */
static const provider_t *ask_provider(const registry_t *registry, const menu_env_t *env)
{
    char line[ANSWER_MAX_CHARS];

    for (;;) {
        answer_kind_t kind = ANSWER_TEXT;
        size_t choice = 0;

        print_choice_prompt(registry, env->out);
        kind = read_answer(env->in, line, sizeof line);
        if (kind == ANSWER_END) {
            fprintf(env->out, "\n");
            return NULL;
        }
        if (kind == ANSWER_TEXT && parse_key(line) == KEY_QUIT) {
            return NULL;
        }
        choice = kind == ANSWER_TEXT ? parse_choice(line, registry_count(registry)) : 0;
        if (choice != 0) {
            return registry_at(registry, choice - 1);
        }
        fprintf(env->out, "Please enter a number from 1 to %zu, or q.\n", registry_count(registry));
    }
}

/* ---- provider screens ---- */

static void show_usage(const provider_t *provider, const usage_snapshot_t *usage,
                       const menu_env_t *env, time_t now)
{
    char text[CHART_TEXT_MAX];
    lfcc_status_t status = usage_view_render(provider, usage, &env->style, now, text, sizeof text);

    if (status != LFCC_OK) {
        fprintf(env->out, "\nCannot draw the chart: %s.\n", lfcc_status_str(status));
        return;
    }
    fprintf(env->out, "\n%s\n[r] reload   [b] back   [q] quit\n", text);
}

static void show_claude_connect(const menu_env_t *env)
{
    char snippet[SNIPPET_MAX];
    lfcc_status_t status = connect_snippet_build(env->executable, snippet, sizeof snippet);

    fprintf(env->out,
            "\nClaude is not connected yet.\n\n"
            "Claude Code shares your usage limits through its status line. To connect:\n\n"
            "  1. Add this to ~/.claude/settings.json. If you already have a \"statusLine\"\n"
            "     setting it would be replaced: keep your script and call this command from it.\n\n");
    if (status == LFCC_OK) {
        fprintf(env->out, "%s\n", snippet);
    } else {
        fprintf(env->out, "  (cannot write the snippet for this program's location: %s)\n\n",
                lfcc_status_str(status));
    }
    fprintf(env->out,
            "  2. Send a message in Claude Code. Your limits appear after its first reply.\n\n"
            "[r] check again   [b] back   [q] quit\n");
}

static void show_not_connected(const provider_t *provider, const menu_env_t *env)
{
    if (strcmp(provider->id, "claude") == 0) {
        show_claude_connect(env);
    } else {
        fprintf(env->out, "\n%s is not connected.\n", provider->display_name);
    }
}

/* Asks for r/b/q until it gets one. End of input counts as quit. */
static menu_key_t ask_key(const menu_env_t *env)
{
    char line[ANSWER_MAX_CHARS];

    for (;;) {
        answer_kind_t kind = ANSWER_TEXT;
        menu_key_t key = KEY_NONE;

        fprintf(env->out, "> ");
        fflush(env->out);
        kind = read_answer(env->in, line, sizeof line);
        if (kind == ANSWER_END) {
            fprintf(env->out, "\n");
            return KEY_QUIT;
        }
        key = kind == ANSWER_TEXT ? parse_key(line) : KEY_NONE;
        if (key != KEY_NONE) {
            return key;
        }
        fprintf(env->out, "Press r to reload, b to go back or q to quit.\n");
    }
}

/* Loads and shows one provider until the user goes back or quits. */
static next_t open_provider(const provider_t *provider, const menu_env_t *env)
{
    for (;;) {
        provider_env_t provider_env = provider_env_for(env);
        usage_snapshot_t usage = {0};
        lfcc_status_t status = provider->load_usage(provider, &provider_env, &usage);
        menu_key_t key = KEY_NONE;

        if (status == LFCC_ERR_UNAVAILABLE) {
            fprintf(env->out, "\n%s is not available yet.\n", provider->display_name);
            return NEXT_MAIN;
        }
        if (status != LFCC_OK && status != LFCC_ERR_NOT_CONNECTED) {
            fprintf(env->out, "\nCannot show %s: %s.\n", provider->display_name,
                    lfcc_status_str(status));
            return NEXT_MAIN;
        }
        if (status == LFCC_OK) {
            show_usage(provider, &usage, env, provider_env.now);
        } else {
            show_not_connected(provider, env);
        }

        key = ask_key(env);
        if (key == KEY_QUIT) {
            return NEXT_QUIT;
        }
        if (key == KEY_BACK) {
            return NEXT_MAIN;
        }
    }
}

int menu_run(const registry_t *registry, const menu_env_t *env)
{
    if (registry == NULL || env == NULL || env->in == NULL || env->out == NULL ||
        env->data_dir == NULL || env->executable == NULL) {
        return MENU_EXIT_ERROR;
    }

    for (;;) {
        const provider_t *provider = NULL;

        print_main_page(registry, env);
        provider = ask_provider(registry, env);
        if (provider == NULL || open_provider(provider, env) == NEXT_QUIT) {
            return MENU_EXIT_OK;
        }
    }
}
