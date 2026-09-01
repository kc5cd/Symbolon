#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

extern "C" {
#include "api.h"
}

TEST_CASE("sym_command_list advertises nothing above SYM_TIER_OBSERVE yet")
{
    /* No commands are registered until the rules/QSO/store phases implement them (see
       core/api.c) -- this is a real assertion against the dispatch seam itself, proving the
       app-side (C++17/doctest) test target links against core/ (C11) cleanly, not a
       placeholder. */
    size_t count = sym_command_list(SYM_TIER_TRANSMIT, nullptr, 0);
    CHECK(count == 0);
}
