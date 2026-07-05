#include "domains/session/service_state.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <utility>

using yoru::session::can_transition;
using yoru::session::ServiceState;

namespace {

constexpr std::array<ServiceState, 4> kAllStates = {
    ServiceState::Idle,
    ServiceState::Recording,
    ServiceState::Processing,
    ServiceState::Error,
};

// The exhaustive set of transitions the domain model allows. Everything
// outside this list must be rejected by can_transition.
constexpr std::array<std::pair<ServiceState, ServiceState>, 7> kAllowedTransitions = {{
    {ServiceState::Idle, ServiceState::Recording},
    {ServiceState::Recording, ServiceState::Processing},
    {ServiceState::Recording, ServiceState::Idle},
    {ServiceState::Recording, ServiceState::Error},
    {ServiceState::Processing, ServiceState::Idle},
    {ServiceState::Processing, ServiceState::Error},
    {ServiceState::Error, ServiceState::Idle},
}};

bool is_allowed(ServiceState from, ServiceState to) {
    return std::find(kAllowedTransitions.begin(), kAllowedTransitions.end(), std::pair{from, to}) !=
           kAllowedTransitions.end();
}

} // namespace

TEST_CASE("can_transition matches the domain model for every state pair") {
    for (const auto from : kAllStates) {
        for (const auto to : kAllStates) {
            CAPTURE(from);
            CAPTURE(to);
            CHECK(can_transition(from, to) == is_allowed(from, to));
        }
    }
}

TEST_CASE("transitioning to the same state is always rejected") {
    for (const auto state : kAllStates) {
        CHECK_FALSE(can_transition(state, state));
    }
}
