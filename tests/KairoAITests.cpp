#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

import Kairo.AI;

using namespace kairo::ai;

namespace
{
    [[nodiscard]] Request BasicRequest()
    {
        return { "mock-v1", { { MessageRole::User, "Build a deterministic scene.", {} } }, {}, 128u, 0.0 };
    }
}

TEST_CASE("AI requests enforce bounded provider-neutral contracts", "[KairoAI][Validation]")
{
    Request request = BasicRequest();
    REQUIRE_NOTHROW(ValidateRequest(request));

    request.MaximumOutputTokens = 0u;
    REQUIRE_THROWS_AS(ValidateRequest(request), std::invalid_argument);
    request = BasicRequest();
    request.Messages.clear();
    REQUIRE_THROWS_AS(ValidateRequest(request), std::invalid_argument);
    request = BasicRequest();
    request.Tools = { { "scene.create", "Create a scene entity.", R"({"type":"object"})" },
        { "scene.create", "Duplicate declaration.", R"({"type":"object"})" } };
    REQUIRE_THROWS_AS(ValidateRequest(request), std::invalid_argument);
    request = BasicRequest();
    request.Messages[0].Content = std::string(32u, 'x');
    REQUIRE_THROWS_AS(ValidateRequest(request, { 8u, 16u, 8u, 1024u, 256u }), std::length_error);
}

TEST_CASE("Mock provider streams deterministic text and structured tool calls", "[KairoAI][Mock]")
{
    const ToolCall call{ "call-1", "scene.create", R"({"name":"Cube"})" };
    auto provider = std::make_shared<MockProvider>(std::vector<StreamEvent>{
        StreamEvent::Delta("Create "), StreamEvent::Delta("ready."), StreamEvent::Call(call) });
    std::vector<StreamEvent> observed;
    RequestTask task(provider, BasicRequest(), [&](const StreamEvent& event) { observed.push_back(event); });
    const Response response = task.Wait();
    CHECK_FALSE(response.Cancelled);
    CHECK(response.Text == "Create ready.");
    CHECK(response.ToolCalls == std::vector<ToolCall>{ call });
    CHECK(observed.size() == 3u);
    CHECK(response.Metering.ToolCalls == 1u);
    CHECK(response.Metering.InputBytes == BasicRequest().Messages[0].Content.size());
    CHECK(response.Metering.OutputTokens > 0u);
}

TEST_CASE("Asynchronous AI tasks cancel cooperatively without detached callbacks", "[KairoAI][Cancellation]")
{
    std::vector<StreamEvent> script(100u, StreamEvent::Delta("chunk"));
    auto provider = std::make_shared<MockProvider>(std::move(script), std::chrono::milliseconds(3));
    std::atomic_size_t callbacks = 0u;
    RequestTask task(provider, BasicRequest(), [&](const StreamEvent&) { ++callbacks; });
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    task.Cancel();
    const Response response = task.Wait();
    CHECK(response.Cancelled);
    CHECK(task.StopRequested());
    CHECK(callbacks.load() < 100u);
}

TEST_CASE("Tool policy separates read context from permissioned side effects", "[KairoAI][Policy]")
{
    ToolRegistry registry;
    std::size_t reads = 0u;
    std::size_t mutations = 0u;
    registry.Register({ "project.search", ToolCapability::ReadProject, "Search indexed project text." },
        [&](std::string_view query) { ++reads; return std::string("match:") + std::string(query); });
    registry.Register({ "scene.create", ToolCapability::MutateProject, "Create an undoable scene entity." },
        [&](std::string_view arguments) { ++mutations; return std::string(arguments); });
    registry.Register({ "secret.read", ToolCapability::AccessCredential, "Read a provider credential." },
        [](std::string_view) { return std::string("must-never-run"); });

    const ToolCall read{ "read-1", "project.search", "camera" };
    const ToolExecution askRead = registry.Execute(InteractionMode::Ask, read);
    CHECK(askRead.Invoked);
    CHECK(askRead.Output == "match:camera");
    CHECK(reads == 1u);

    const ToolCall mutation{ "write-1", "scene.create", R"({"name":"Cube"})" };
    CHECK(registry.Execute(InteractionMode::Plan, mutation).AuthorizationResult.Decision ==
        AuthorizationDecision::DeniedByMode);
    CHECK(registry.Execute(InteractionMode::Agent, mutation).AuthorizationResult.Decision ==
        AuthorizationDecision::ApprovalRequired);
    CHECK(mutations == 0u);

    ToolApproval mismatched{ mutation.ID, mutation.Name, R"({"name":"Sphere"})", true };
    CHECK(registry.Execute(InteractionMode::Agent, mutation, mismatched).AuthorizationResult.Decision ==
        AuthorizationDecision::ApprovalMismatch);
    CHECK(mutations == 0u);

    ToolApproval exact{ mutation.ID, mutation.Name, mutation.Arguments, true };
    const ToolExecution approved = registry.Execute(InteractionMode::Agent, mutation, exact);
    CHECK(approved.Invoked);
    CHECK(mutations == 1u);

    const ToolCall credential{ "secret-1", "secret.read", "OPENAI_API_KEY" };
    ToolApproval secretApproval{ credential.ID, credential.Name, credential.Arguments, true };
    CHECK(registry.Execute(InteractionMode::Agent, credential, secretApproval).AuthorizationResult.Decision ==
        AuthorizationDecision::CredentialAccessDenied);
}
