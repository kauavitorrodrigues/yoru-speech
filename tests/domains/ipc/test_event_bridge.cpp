#include "domains/ipc/event_bridge.hpp"

#include "domains/ipc/ipc_server.hpp"
#include "domains/session/events.hpp"
#include "test_client.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <optional>
#include <string>

using yoru::core::EventBus;
using yoru::core::SessionId;
using yoru::ipc::EventBridge;
using yoru::ipc::IpcServer;
using yoru::ipc::test::TestClient;
using yoru::ipc::test::unique_test_socket_path;
using yoru::session::SessionCancelled;

TEST_CASE("is_subscribed() reflects subscribe()/unsubscribe()") {
    EventBus bus;
    IpcServer server(unique_test_socket_path("yoru-speech-test-event-bridge"));
    EventBridge bridge(bus, server);

    CHECK_FALSE(bridge.is_subscribed(42));

    bridge.subscribe(42);
    CHECK(bridge.is_subscribed(42));

    bridge.unsubscribe(42);
    CHECK_FALSE(bridge.is_subscribed(42));
}

TEST_CASE("subscribe() twice and unsubscribe() of a never-subscribed client are no-ops") {
    EventBus bus;
    IpcServer server(unique_test_socket_path("yoru-speech-test-event-bridge"));
    EventBridge bridge(bus, server);

    bridge.subscribe(1);
    bridge.subscribe(1);
    CHECK(bridge.is_subscribed(1));

    bridge.unsubscribe(2);
    CHECK_FALSE(bridge.is_subscribed(2));
}

TEST_CASE("a subscribed client receives a pushed event; an unsubscribed one does not") {
    const auto path = unique_test_socket_path("yoru-speech-test-event-bridge");
    std::filesystem::remove(path);
    EventBus bus;
    IpcServer server(path);
    REQUIRE_FALSE(server.start().has_value());
    EventBridge bridge(bus, server);

    TestClient subscribed_client(path);
    TestClient unsubscribed_client(path);

    // Accept both connections first. Neither on_line callback fires yet,
    // since neither client has sent anything.
    for (int i = 0; i < 20; ++i) {
        server.poll_once(
            10, [](IpcServer::ClientId, const std::string&) {}, [](IpcServer::ClientId) {});
    }

    // Each client identifies itself so the test can tell their ids
    // apart, the same way a real client would before subscribing.
    subscribed_client.send_line("hello from subscribed");
    unsubscribed_client.send_line("hello from unsubscribed");

    std::optional<IpcServer::ClientId> subscribed_id;
    std::optional<IpcServer::ClientId> unsubscribed_id;
    for (int i = 0; i < 20 && (!subscribed_id.has_value() || !unsubscribed_id.has_value()); ++i) {
        server.poll_once(
            50,
            [&](IpcServer::ClientId id, const std::string& line) {
                if (line == "hello from subscribed") {
                    subscribed_id = id;
                } else if (line == "hello from unsubscribed") {
                    unsubscribed_id = id;
                }
            },
            [](IpcServer::ClientId) {});
    }
    REQUIRE(subscribed_id.has_value());
    REQUIRE(unsubscribed_id.has_value());

    bridge.subscribe(subscribed_id.value());

    bus.publish(SessionCancelled{.session_id = SessionId{5}});

    const std::string received = subscribed_client.read_line();
    CHECK(received.find(R"("event":"session_cancelled")") != std::string::npos);
    CHECK(received.find(R"("session_id":5)") != std::string::npos);

    // The unsubscribed client never opted in, so it gets nothing: give
    // it a few short polls to (not) receive anything, same budget the
    // subscribed client's read_line() used.
    const std::string not_received = unsubscribed_client.read_line(5);
    CHECK(not_received.empty());

    server.stop();
}

TEST_CASE("publishing with no subscribed clients does not crash or block") {
    EventBus bus;
    IpcServer server(unique_test_socket_path("yoru-speech-test-event-bridge"));
    EventBridge bridge(bus, server);
    (void)bridge;

    bus.publish(SessionCancelled{.session_id = SessionId{1}});
}
