#include "core/event_bus.hpp"

#include <doctest/doctest.h>

#include <string>

using yoru::core::EventBus;

namespace {

struct TestEventA {
    int value = 0;
};

struct TestEventB {
    std::string text;
};

} // namespace

TEST_CASE("subscriber receives a published event with correct data") {
    EventBus bus;
    int received_value = 0;
    int call_count = 0;

    bus.subscribe<TestEventA>([&](const TestEventA& event) {
        received_value = event.value;
        ++call_count;
    });

    bus.publish(TestEventA{42});

    CHECK(call_count == 1);
    CHECK(received_value == 42);
}

TEST_CASE("multiple subscribers to the same event type all receive it") {
    EventBus bus;
    int first_received = 0;
    int second_received = 0;
    int third_received = 0;

    bus.subscribe<TestEventA>([&](const TestEventA& event) { first_received = event.value; });
    bus.subscribe<TestEventA>([&](const TestEventA& event) { second_received = event.value; });
    bus.subscribe<TestEventA>([&](const TestEventA& event) { third_received = event.value; });

    bus.publish(TestEventA{7});

    CHECK(first_received == 7);
    CHECK(second_received == 7);
    CHECK(third_received == 7);
}

TEST_CASE("a subscriber of one event type never receives another type") {
    EventBus bus;
    int a_calls = 0;
    int b_calls = 0;

    bus.subscribe<TestEventA>([&](const TestEventA&) { ++a_calls; });
    bus.subscribe<TestEventB>([&](const TestEventB&) { ++b_calls; });

    bus.publish(TestEventA{1});

    CHECK(a_calls == 1);
    CHECK(b_calls == 0);
}

TEST_CASE("publishing with no subscribers does not crash or have side effects") {
    EventBus bus;

    CHECK_NOTHROW(bus.publish(TestEventA{99}));
    CHECK_NOTHROW(bus.publish(TestEventB{"unheard"}));
}
