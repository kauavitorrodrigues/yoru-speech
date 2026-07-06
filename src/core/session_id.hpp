#pragma once

#include <cstdint>

namespace yoru::core {

// Strong type over a session's unique identifier. Generation is the
// responsibility of the Session Manager. Lives in core rather than
// domains/session: it's a cross-cutting value type used to tag events
// across the audio and speech domains, both of which the Session Manager
// depends on, which would create a dependency cycle if this type lived in
// domains/session instead.
enum class SessionId : std::uint64_t {};

} // namespace yoru::core
