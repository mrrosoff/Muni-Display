// Host-side unit tests for the pure logic. Run with `make test`.

#include "render/row_order.hpp"
#include "render/special_dates.hpp"
#include "util/time_utils.hpp"
#include "util/utf8.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// Local-time noon on the given date, normalized (tm_wday set).
std::tm local_date(int y, int m, int d) {
    std::tm t{};
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = 12;
    t.tm_isdst = -1;
    std::mktime(&t);
    return t;
}

void test_parse_iso8601() {
    constexpr std::time_t expect = 1777552496;  // 2026-04-30T12:34:56Z
    CHECK(tu::parse_iso8601("2026-04-30T12:34:56Z") == expect);
    CHECK(tu::parse_iso8601("2026-04-30T12:34:56.123456+00:00") == expect);
    CHECK(tu::parse_iso8601("2026-04-30T05:34:56-07:00") == expect);
    CHECK(tu::parse_iso8601("2026-04-30T14:34:56+02:00") == expect);
    CHECK(tu::parse_iso8601("garbage") == 0);
    CHECK(tu::parse_iso8601("") == 0);
}

void test_special_dates() {
    CHECK(specdate::for_date(local_date(2026, 11, 26)).text == "THANKSGIVING");
    CHECK(specdate::for_date(local_date(2025, 11, 27)).text == "THANKSGIVING");
    CHECK(!specdate::for_date(local_date(2026, 11, 19)).found);
    CHECK(specdate::for_date(local_date(2026, 2, 13)).text == "FRIDAY 13TH");
    CHECK(specdate::for_date(local_date(2026, 12, 25)).text == "CHRISTMAS");
    CHECK(!specdate::for_date(local_date(2026, 10, 18)).found);
}

render::RowTimes times(std::initializer_list<int> mins) {
    render::RowTimes rt;
    for (int m : mins) rt.minutes[rt.count++] = m;
    return rt;
}

void test_row_order() {
    CHECK(render::arrives_before(times({3}), times({5})));
    CHECK(!render::arrives_before(times({5}), times({3})));
    CHECK(render::arrives_before(times({9}), times({})));
    CHECK(!render::arrives_before(times({}), times({9})));
    CHECK(!render::arrives_before(times({}), times({})));
    // Tie on first arrival: closer follow-up wins; any follow-up beats none.
    CHECK(render::arrives_before(times({3, 8}), times({3, 12})));
    CHECK(render::arrives_before(times({3, 20}), times({3})));
    CHECK(!render::arrives_before(times({3}), times({3, 20})));
}

void test_utf8() {
    const std::string s =
        "A\xc2\xb0"
        "B\xe2\x82\xac";  // "A°B€"
    size_t i = 0;
    CHECK(utf8::next(s, &i) == 'A');
    CHECK(utf8::next(s, &i) == 0xB0);
    CHECK(utf8::next(s, &i) == 'B');
    CHECK(utf8::next(s, &i) == 0x20AC);
    CHECK(i == s.size());
    // Truncated sequence decodes as the raw lead byte.
    const std::string bad = "\xc2";
    i = 0;
    CHECK(utf8::next(bad, &i) == 0xC2);
}

}  // namespace

int main() {
    setenv("TZ", "America/Los_Angeles", 1);
    tzset();

    test_parse_iso8601();
    test_special_dates();
    test_row_order();
    test_utf8();

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
