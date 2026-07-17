#include <cstddef>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

import Kairo.AI;
import Kairo.AI.Cloud;

using namespace kairo::ai;

namespace
{
    class FakeTransport final : public ChatStreamTransport
    {
    public:
        std::vector<std::string> Chunks;
        ChatTransportResult Result{ 200, {}, {} };
        HeaderMap SeenHeaders;
        std::string SeenBody;
        std::uint32_t SeenTimeoutMilliseconds = 0u;

        [[nodiscard]] ChatTransportResult Post(std::string_view,
            const HeaderMap& headers, std::string_view body,
            std::uint32_t timeoutMilliseconds, const TransportChunkSink& sink,
            std::stop_token stopToken) override
        {
            SeenHeaders = headers;
            SeenBody = body;
            SeenTimeoutMilliseconds = timeoutMilliseconds;
            for (const std::string& chunk : Chunks)
                if (stopToken.stop_requested() || !sink(chunk)) break;
            return Result;
        }
    };

    [[nodiscard]] Request RequestWithTool()
    {
        return { "test-model", { { MessageRole::User, "Create a cube.", {} } },
            { { "scene.create_entity", "Create an entity.",
                R"({"type":"object","properties":{"name":{"type":"string"}},"required":["name"],"additionalProperties":false})" } },
            256u, 0.0 };
    }
}

TEST_CASE("OpenAI-compatible provider decodes fragmented text and usage streams", "[KairoAI][Cloud][SSE]")
{
    auto transport = std::make_shared<FakeTransport>();
    transport->Chunks = {
        "data: {\"choices\":[{\"delta\":{\"content\":\"Hel",
        "lo\"}}]}\n\ndata: {\"choices\":[{\"delta\":{\"content\":\" world\"}}]}\n\n",
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":9,\"completion_tokens\":2}}\n\n",
        "data: [DONE]\n\n"
    };
    OpenAICompatibleProvider provider({ .Endpoint = "https://example.test/v1/chat/completions",
        .APIKey = "test-secret" }, transport);
    std::vector<StreamEvent> events;
    const Response response = provider.Execute(RequestWithTool(),
        [&](const StreamEvent& event) { events.push_back(event); }, {});
    CHECK(response.Text == "Hello world");
    CHECK(response.Metering.InputTokens == 9u);
    CHECK(response.Metering.OutputTokens == 2u);
    CHECK(events.size() == 2u);
    CHECK(transport->SeenHeaders.at("Authorization") == "Bearer test-secret");
    CHECK(transport->SeenBody.find("test-secret") == std::string::npos);
    CHECK(transport->SeenBody.find("\"strict\":true") != std::string::npos);
    CHECK(transport->SeenBody.find("scene__create_entity") != std::string::npos);
    CHECK(transport->SeenTimeoutMilliseconds == 120'000u);
}

TEST_CASE("OpenAI-compatible provider assembles fragmented tool calls once", "[KairoAI][Cloud][Tools]")
{
    auto transport = std::make_shared<FakeTransport>();
    transport->Chunks = {
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call-1\",\"function\":{\"name\":\"scene__create_\",\"arguments\":\"{\\\"name\\\":\"}}]}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"name\":\"entity\",\"arguments\":\"\\\"Cube\\\"}\"}}]}}]}\n\n",
        "data: [DONE]\n\n"
    };
    OpenAICompatibleProvider provider({ .Endpoint = "https://example.test/chat",
        .APIKey = "test-secret" }, transport);
    std::vector<StreamEvent> events;
    const Response response = provider.Execute(RequestWithTool(),
        [&](const StreamEvent& event) { events.push_back(event); }, {});
    REQUIRE(response.ToolCalls.size() == 1u);
    CHECK(response.ToolCalls[0] == ToolCall{ "call-1", "scene.create_entity", R"({"name":"Cube"})" });
    REQUIRE(events.size() == 1u);
    CHECK(events[0].Kind == StreamEventKind::ToolCall);
}

TEST_CASE("OpenAI-compatible provider rejects ambiguous wire tool names before transport",
    "[KairoAI][Cloud][Tools]")
{
    auto transport = std::make_shared<FakeTransport>();
    Request request = RequestWithTool();
    request.Tools.push_back({ "scene__create_entity", "Conflicting wire alias.",
        R"({"type":"object","properties":{},"additionalProperties":false})" });
    OpenAICompatibleProvider provider({ .Endpoint = "https://example.test/chat",
        .APIKey = "test-secret" }, transport);
    REQUIRE_THROWS_AS(provider.Execute(request, {}, {}), std::invalid_argument);
    CHECK(transport->SeenBody.empty());
}

TEST_CASE("OpenAI-compatible failures do not disclose provider credentials", "[KairoAI][Cloud][Errors]")
{
    auto transport = std::make_shared<FakeTransport>();
    transport->Result = { 401, "unauthorized", R"({"error":{"message":"Invalid credential"}})" };
    OpenAICompatibleProvider provider({ .Endpoint = "https://example.test/chat",
        .APIKey = "never-print-this-secret" }, transport);
    try
    {
        (void)provider.Execute(RequestWithTool(), {}, {});
        FAIL("Expected provider HTTP failure");
    }
    catch (const std::runtime_error& error)
    {
        CHECK(std::string(error.what()).find("401") != std::string::npos);
        CHECK(std::string(error.what()).find("never-print-this-secret") == std::string::npos);
    }
    REQUIRE_THROWS_AS(OpenAICompatibleProvider({ .Endpoint = "http://remote.test/chat",
        .APIKey = "secret" }, transport), std::invalid_argument);
}

TEST_CASE("OpenAI-compatible provider honors cancellation before transport work",
    "[KairoAI][Cloud][Cancellation]")
{
    auto transport = std::make_shared<FakeTransport>();
    transport->Chunks = { "data: [DONE]\n\n" };
    OpenAICompatibleProvider provider({ .Endpoint = "https://example.test/chat",
        .APIKey = "test-secret" }, transport);
    std::stop_source stop;
    stop.request_stop();
    const Response response = provider.Execute(RequestWithTool(), {}, stop.get_token());
    CHECK(response.Cancelled);
    CHECK(transport->SeenTimeoutMilliseconds == 120'000u);
}
