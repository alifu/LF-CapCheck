#include "testkit.h"

#include "providers/registry.h"

static lfcc_status_t fake_load(const provider_t *self, time_t now,
                               usage_snapshot_t *out)
{
    (void)self;
    (void)now;
    (void)out;
    return LFCC_OK;
}

static const provider_t CLAUDE = {"claude", "Claude", PROVIDER_SORT_CLAUDE, fake_load};
static const provider_t CODEX = {"codex", "Codex", PROVIDER_SORT_CODEX, fake_load};
static const provider_t OTHER = {"other", "Other", 20, fake_load};

static void test_sorts_claude_first_whatever_the_input_order(void)
{
    const provider_t *const orders[][3] = {
        {&CLAUDE, &CODEX, &OTHER}, {&CODEX, &OTHER, &CLAUDE},
        {&OTHER, &CLAUDE, &CODEX}, {&OTHER, &CODEX, &CLAUDE},
    };

    for (size_t i = 0; i < sizeof orders / sizeof orders[0]; i++) {
        registry_t registry;

        CHECK_INT_EQ(LFCC_OK, registry_build(orders[i], 3, &registry));
        CHECK_INT_EQ(3, registry_count(&registry));
        CHECK_STR_EQ("claude", registry_at(&registry, 0)->id);
        CHECK_STR_EQ("codex", registry_at(&registry, 1)->id);
        CHECK_STR_EQ("other", registry_at(&registry, 2)->id);
    }
}

static void test_at_returns_null_when_index_is_out_of_range(void)
{
    const provider_t *const input[] = {&CLAUDE};
    registry_t registry;

    CHECK_INT_EQ(LFCC_OK, registry_build(input, 1, &registry));

    CHECK(registry_at(&registry, 0) == &CLAUDE);
    CHECK(registry_at(&registry, 1) == NULL);
    CHECK(registry_at(NULL, 0) == NULL);
}

static void test_find_returns_provider_by_id_or_null(void)
{
    const provider_t *const input[] = {&CODEX, &CLAUDE};
    registry_t registry;

    CHECK_INT_EQ(LFCC_OK, registry_build(input, 2, &registry));

    CHECK(registry_find(&registry, "codex") == &CODEX);
    CHECK(registry_find(&registry, "gemini") == NULL);
    CHECK(registry_find(&registry, NULL) == NULL);
    CHECK(registry_find(NULL, "claude") == NULL);
}

static void test_rejects_duplicate_id_and_leaves_registry_empty(void)
{
    const provider_t clone = {"claude", "Claude again", 5, fake_load};
    const provider_t *const input[] = {&CLAUDE, &clone};
    registry_t registry;

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, registry_build(input, 2, &registry));
    CHECK_INT_EQ(0, registry_count(&registry));
}

static void test_rejects_duplicate_sort_order_because_ordering_would_be_ambiguous(void)
{
    const provider_t clash = {"clash", "Clash", PROVIDER_SORT_CLAUDE, fake_load};
    const provider_t *const input[] = {&CLAUDE, &clash};
    registry_t registry;

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, registry_build(input, 2, &registry));
    CHECK_INT_EQ(0, registry_count(&registry));
}

static void test_rejects_incomplete_providers(void)
{
    const provider_t no_id = {NULL, "X", 1, fake_load};
    const provider_t empty_id = {"", "X", 2, fake_load};
    const provider_t no_name = {"a", NULL, 3, fake_load};
    const provider_t empty_name = {"b", "", 4, fake_load};
    const provider_t no_loader = {"c", "C", 5, NULL};
    const provider_t *const bad[] = {&no_id, &empty_id, &no_name, &empty_name,
                                     &no_loader, NULL};

    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        const provider_t *const input[] = {bad[i]};
        registry_t registry;

        CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, registry_build(input, 1, &registry));
        CHECK_INT_EQ(0, registry_count(&registry));
    }
}

static void test_rejects_empty_input_and_null_arguments(void)
{
    const provider_t *const input[] = {&CLAUDE};
    registry_t registry;

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, registry_build(input, 0, &registry));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, registry_build(NULL, 1, &registry));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, registry_build(input, 1, NULL));
}

static void test_reports_capacity_error_when_too_many_providers(void)
{
    enum { TOTAL = REGISTRY_MAX_PROVIDERS + 1 };
    char ids[TOTAL][8];
    provider_t providers[TOTAL];
    const provider_t *pointers[TOTAL];
    registry_t registry;

    for (int i = 0; i < TOTAL; i++) {
        snprintf(ids[i], sizeof ids[i], "p%d", i);
        providers[i] = (provider_t){ids[i], "Provider", i, fake_load};
        pointers[i] = &providers[i];
    }

    CHECK_INT_EQ(LFCC_ERR_CAPACITY, registry_build(pointers, TOTAL, &registry));
    CHECK_INT_EQ(0, registry_count(&registry));
    CHECK_INT_EQ(LFCC_OK, registry_build(pointers, REGISTRY_MAX_PROVIDERS, &registry));
    CHECK_INT_EQ(REGISTRY_MAX_PROVIDERS, registry_count(&registry));
}

int main(void)
{
    RUN_TEST(test_sorts_claude_first_whatever_the_input_order);
    RUN_TEST(test_at_returns_null_when_index_is_out_of_range);
    RUN_TEST(test_find_returns_provider_by_id_or_null);
    RUN_TEST(test_rejects_duplicate_id_and_leaves_registry_empty);
    RUN_TEST(test_rejects_duplicate_sort_order_because_ordering_would_be_ambiguous);
    RUN_TEST(test_rejects_incomplete_providers);
    RUN_TEST(test_rejects_empty_input_and_null_arguments);
    RUN_TEST(test_reports_capacity_error_when_too_many_providers);
    return TESTKIT_RESULT();
}
