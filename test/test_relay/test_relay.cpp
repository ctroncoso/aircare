// test_relay.cpp — unit tests for the independent relay actuator (improvement A3).
#include "actuators/relay.h"
#include "core/board.h" // rlPin1, rlPin2
#include <unity.h>

// Active-low relay module: LOW = energized (ON), HIGH = de-energized (OFF).
// g_pinLevel[] is defined in host_hal.cpp (one per test binary).
extern int g_pinLevel[64];

static void resetPins()
{
    g_pinLevel[rlPin1] = 0;
    g_pinLevel[rlPin2] = 0;
}

void setUp(void)
{
    // Start from a known OFF state before init() drives the pins HIGH.
    resetPins();
    relay::init();
}

void tearDown(void) {}

// After init() both channels must report OFF and the pins driven HIGH (OFF).
void test_init_defaults_off(void)
{
    TEST_ASSERT_FALSE(relay::state(1));
    TEST_ASSERT_FALSE(relay::state(2));
    TEST_ASSERT_EQUAL(HIGH, g_pinLevel[rlPin1]);
    TEST_ASSERT_EQUAL(HIGH, g_pinLevel[rlPin2]);
}

// Channels are independent: turning relay 1 ON must not affect relay 2.
void test_independent_channels(void)
{
    relay::set(1, true);
    TEST_ASSERT_TRUE(relay::state(1));
    TEST_ASSERT_FALSE(relay::state(2));
    TEST_ASSERT_EQUAL(LOW, g_pinLevel[rlPin1]);
    TEST_ASSERT_EQUAL(HIGH, g_pinLevel[rlPin2]);

    relay::set(2, true);
    TEST_ASSERT_TRUE(relay::state(1));
    TEST_ASSERT_TRUE(relay::state(2));
    TEST_ASSERT_EQUAL(LOW, g_pinLevel[rlPin1]);
    TEST_ASSERT_EQUAL(LOW, g_pinLevel[rlPin2]);

    relay::set(1, false);
    TEST_ASSERT_FALSE(relay::state(1));
    TEST_ASSERT_TRUE(relay::state(2)); // relay 2 stays ON
}

// setBoth() drives both channels together (the "tandem" default used by the scheduler).
void test_setBoth(void)
{
    relay::setBoth(true);
    TEST_ASSERT_TRUE(relay::state(1));
    TEST_ASSERT_TRUE(relay::state(2));
    TEST_ASSERT_TRUE(relay::bothOn());

    relay::setBoth(false);
    TEST_ASSERT_FALSE(relay::state(1));
    TEST_ASSERT_FALSE(relay::state(2));
    TEST_ASSERT_FALSE(relay::bothOn());
}

// anyOn() is true if EITHER channel is energized.
void test_anyOn(void)
{
    TEST_ASSERT_FALSE(relay::anyOn());
    relay::set(2, true);
    TEST_ASSERT_TRUE(relay::anyOn());
    relay::set(2, false);
    TEST_ASSERT_FALSE(relay::anyOn());
}

// Edge-triggered: writing the same state again must NOT toggle the pin.
void test_edge_triggered_no_chatter(void)
{
    relay::set(1, true);
    int levelAfterFirst = g_pinLevel[rlPin1];
    relay::set(1, true); // redundant ON
    TEST_ASSERT_EQUAL(levelAfterFirst, g_pinLevel[rlPin1]);
    TEST_ASSERT_EQUAL(LOW, g_pinLevel[rlPin1]);
}

// Id overloads behave identically to the enum overloads.
void test_int_overload(void)
{
    relay::set(2, true);
    TEST_ASSERT_TRUE(relay::state(relay::Id::Uv));
    relay::set((int)relay::Id::Uv, false);
    TEST_ASSERT_FALSE(relay::state(2));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_defaults_off);
    RUN_TEST(test_independent_channels);
    RUN_TEST(test_setBoth);
    RUN_TEST(test_anyOn);
    RUN_TEST(test_edge_triggered_no_chatter);
    RUN_TEST(test_int_overload);
    return UNITY_END();
}