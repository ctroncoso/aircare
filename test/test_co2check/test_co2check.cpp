// test_co2check.cpp — unit tests for the CO2 sample validity check (improvement A2).
#include "core/co2check.h"
#include <unity.h>

// Valid range is 1..9999 ppm. Zero (not-ready) and >=10000 (error/over-range)
// must be rejected so a bad reading is never trusted as "Green".
void test_valid_midrange(void)
{
    TEST_ASSERT_TRUE(co2Valid(400));
    TEST_ASSERT_TRUE(co2Valid(700));
    TEST_ASSERT_TRUE(co2Valid(800));
    TEST_ASSERT_TRUE(co2Valid(9999));
}

void test_invalid_zero(void)
{
    // A zero reading usually means the measurement hasn't completed yet.
    TEST_ASSERT_FALSE(co2Valid(0));
}

void test_invalid_overrange(void)
{
    // 0xFFFF (65535) is the sensor error/over-range sentinel; >=10000 rejected.
    TEST_ASSERT_FALSE(co2Valid(10000));
    TEST_ASSERT_FALSE(co2Valid(65535));
}

void test_boundary(void)
{
    TEST_ASSERT_TRUE(co2Valid(1));      // smallest valid
    TEST_ASSERT_FALSE(co2Valid(10000)); // smallest invalid
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_midrange);
    RUN_TEST(test_invalid_zero);
    RUN_TEST(test_invalid_overrange);
    RUN_TEST(test_boundary);
    return UNITY_END();
}