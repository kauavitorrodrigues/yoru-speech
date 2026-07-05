#include "service_state.hpp"

namespace yoru::session {

bool can_transition(ServiceState from, ServiceState to) {
    switch (from) {
    case ServiceState::Idle:
        return to == ServiceState::Recording;
    case ServiceState::Recording:
        return to == ServiceState::Processing || to == ServiceState::Idle ||
               to == ServiceState::Error;
    case ServiceState::Processing:
        return to == ServiceState::Idle || to == ServiceState::Error;
    case ServiceState::Error:
        return to == ServiceState::Idle;
    }
    return false;
}

} // namespace yoru::session
