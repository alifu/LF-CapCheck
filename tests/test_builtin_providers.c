#include "testkit.h"

#include "providers/builtin.h"

#define NOW 1738400000

static void test_default_registry_lists_claude_first_then_codex(void)
{
    registry_t registry;

    CHECK_INT_EQ(LFCC_OK, builtin_registry_build(&registry));

    CHECK_INT_EQ(2, registry_count(&registry));
    CHECK_STR_EQ("claude", registry_at(&registry, 0)->id);
    CHECK_STR_EQ("codex", registry_at(&registry, 1)->id);
    CHECK(registry_find(&registry, "claude") != NULL);
}

static void test_codex_is_listed_but_reports_that_it_is_not_available_yet(void)
{
    registry_t registry;
    const provider_t *codex = NULL;
    provider_env_t env = {NOW, "/tmp"};
    usage_snapshot_t usage = {0};

    CHECK_INT_EQ(LFCC_OK, builtin_registry_build(&registry));
    codex = registry_find(&registry, "codex");

    CHECK(codex != NULL);
    CHECK_STR_EQ("Codex", codex->display_name);
    CHECK_INT_EQ(LFCC_ERR_UNAVAILABLE, codex->load_usage(codex, &env, &usage));
}

int main(void)
{
    RUN_TEST(test_default_registry_lists_claude_first_then_codex);
    RUN_TEST(test_codex_is_listed_but_reports_that_it_is_not_available_yet);
    return TESTKIT_RESULT();
}
