#pragma once

#include <any>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yoru::core {

// Decoupled communication between components: publishers don't know who is
// listening, subscribers don't know who published. Contains no business
// logic — it only distributes.
//
// v1 is synchronous: publish() invokes every matching subscriber on the
// caller's thread, in subscription order, before returning. This keeps the
// initial flow simple to reason about and debug; an async queue can be
// introduced later without changing this public interface.
//
// Not thread-safe by design: nothing in the current architecture publishes
// or subscribes from multiple threads. Concurrent publish()/subscribe() calls
// are a data race (undefined behavior), not merely a correctness bug. Revisit
// if a concurrent publisher/subscriber is introduced — the Speech Engine's
// on-demand/background model loading (docs/runtime-architecture.md) or
// multiple IPC client threads are the most likely first offenders.
class EventBus {
public:
    EventBus() = default;

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = default;
    EventBus& operator=(EventBus&&) = default;

    template <typename Event> using Handler = std::function<void(const Event&)>;

    // Registers `handler` to be invoked whenever an `Event` is published.
    template <typename Event> void subscribe(Handler<Event> handler) {
        auto erased = [handler = std::move(handler)](const std::any& event) {
            handler(std::any_cast<const Event&>(event));
        };
        handlers_[std::type_index(typeid(Event))].push_back(std::move(erased));
    }

    // Publishes `event` to every subscriber currently registered for its
    // type. A no-op, without side effects, when there are none.
    //
    // Handlers are copied out before invocation so that a handler
    // subscribing another handler for the same event type mid-dispatch
    // cannot invalidate the list being iterated.
    //
    // If a handler throws, the exception propagates to the caller of
    // publish() and any remaining subscribers for this event are not
    // invoked. Handlers must not assume best-effort delivery to all
    // subscribers.
    template <typename Event> void publish(const Event& event) {
        const auto it = handlers_.find(std::type_index(typeid(Event)));
        if (it == handlers_.end()) {
            return;
        }

        const std::vector<ErasedHandler> handlers = it->second;
        const std::any erased_event = event;
        for (const auto& handler : handlers) {
            handler(erased_event);
        }
    }

private:
    using ErasedHandler = std::function<void(const std::any&)>;

    std::unordered_map<std::type_index, std::vector<ErasedHandler>> handlers_;
};

} // namespace yoru::core
