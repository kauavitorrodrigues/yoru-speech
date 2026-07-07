#include "core/event_bus.hpp"
#include "domains/config/events.hpp"

#include <doctest/doctest.h>

using yoru::config::Configuration;
using yoru::config::ConfigurationChanged;
using yoru::core::EventBus;

TEST_CASE("ConfigurationChanged carries the new configuration") {
    EventBus bus;
    std::string received_language;

    bus.subscribe<ConfigurationChanged>([&](const ConfigurationChanged& event) {
        received_language = event.configuration.default_language;
    });

    bus.publish(ConfigurationChanged{
        .configuration = Configuration{.default_language = "pt", .transcription_prompt = ""},
    });

    CHECK(received_language == "pt");
}
