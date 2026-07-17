module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

export module Kairo.AI.MockProvider;

import Kairo.AI.Contracts;
import Kairo.AI.Provider;

export namespace kairo::ai
{
    /// Deterministic provider used by CI and editor tests. Its script is copied
    /// at construction, events retain exact order, and no credentials, network,
    /// clock-derived text, or random values enter the response.
    class MockProvider final : public Provider
    {
    public:
        explicit MockProvider(std::vector<StreamEvent> script,
            std::chrono::milliseconds eventDelay = {})
            : m_Script(std::move(script)), m_EventDelay(eventDelay) {}

        [[nodiscard]] std::string_view Name() const noexcept override { return "kairo.mock"; }

        [[nodiscard]] Response Execute(const Request& request,
            const StreamSink& sink, std::stop_token stopToken) override
        {
            ValidateRequest(request);
            Response response;
            for (const StreamEvent& event : m_Script)
            {
                if (stopToken.stop_requested()) { response.Cancelled = true; break; }
                if (m_EventDelay.count() > 0)
                {
                    const auto deadline = std::chrono::steady_clock::now() + m_EventDelay;
                    while (std::chrono::steady_clock::now() < deadline)
                    {
                        if (stopToken.stop_requested()) { response.Cancelled = true; return Finalize(request, response); }
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
                if (event.Kind == StreamEventKind::TextDelta) response.Text += event.Text;
                else { response.ToolCalls.push_back(event.Tool); ++response.Metering.ToolCalls; }
                if (sink) sink(event);
            }
            return Finalize(request, response);
        }

    private:
        std::vector<StreamEvent> m_Script;
        std::chrono::milliseconds m_EventDelay;

        [[nodiscard]] static Response Finalize(const Request& request, Response response)
        {
            for (const Message& message : request.Messages)
                response.Metering.InputBytes += message.Content.size();
            response.Metering.OutputBytes = response.Text.size();
            for (const ToolCall& call : response.ToolCalls)
                response.Metering.OutputBytes += call.ID.size() + call.Name.size() + call.Arguments.size();
            // The mock uses a documented deterministic estimate; real adapters
            // replace these fields with provider-reported tokenizer usage.
            response.Metering.InputTokens = (response.Metering.InputBytes + 3u) / 4u;
            response.Metering.OutputTokens = (response.Metering.OutputBytes + 3u) / 4u;
            return response;
        }
    };
}
