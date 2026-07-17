module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

export module Kairo.AI.OpenAICompatible;

import Kairo.AI.Contracts;
import Kairo.AI.Provider;

export namespace kairo::ai
{
    using HeaderMap = std::map<std::string, std::string, std::less<>>;
    using TransportChunkSink = std::function<bool(std::string_view)>;

    struct ChatTransportResult final
    {
        long StatusCode = 0;
        std::string Error;
        std::string Body;
    };

    /// Injectable HTTP boundary. Tests supply deterministic fragmented streams;
    /// production uses CprChatTransport. Implementations must stop promptly when
    /// either stopToken is requested or chunkSink returns false.
    class ChatStreamTransport
    {
    public:
        virtual ~ChatStreamTransport() = default;
        [[nodiscard]] virtual ChatTransportResult Post(std::string_view url,
            const HeaderMap& headers, std::string_view body,
            std::uint32_t timeoutMilliseconds, const TransportChunkSink& chunkSink,
            std::stop_token stopToken) = 0;
    };

    struct OpenAICompatibleConfig final
    {
        std::string Endpoint = "https://api.openai.com/v1/chat/completions";
        std::string APIKey;
        std::uint32_t TimeoutMilliseconds = 120'000u;
        std::size_t MaximumEventBytes = 8u * 1024u * 1024u;
        std::size_t MaximumResponseBytes = 64u * 1024u * 1024u;
    };

    namespace openai_detail
    {
        using Json = nlohmann::json;

        [[nodiscard]] inline bool IsSecureEndpoint(std::string_view endpoint) noexcept
        {
            return endpoint.starts_with("https://") || endpoint.starts_with("http://127.0.0.1") ||
                endpoint.starts_with("http://localhost");
        }

        class SSEDecoder final
        {
        public:
            explicit SSEDecoder(std::size_t maximumBytes) : m_MaximumBytes(maximumBytes)
            { if (maximumBytes == 0u) throw std::invalid_argument("SSE event limit must be positive."); }

            template<class Consumer>
            void Feed(std::string_view chunk, Consumer&& consume)
            {
                if (m_Buffer.size() + chunk.size() > m_MaximumBytes)
                    throw std::length_error("OpenAI-compatible SSE event exceeds its safety limit.");
                m_Buffer.append(chunk);
                for (;;)
                {
                    const std::size_t lf = m_Buffer.find("\n\n");
                    const std::size_t crlf = m_Buffer.find("\r\n\r\n");
                    std::size_t end = std::min(lf, crlf);
                    if (lf == std::string::npos) end = crlf;
                    if (crlf == std::string::npos) end = lf;
                    if (end == std::string::npos) break;
                    const std::size_t separator = end == crlf ? 4u : 2u;
                    ConsumeEvent(std::string_view(m_Buffer).substr(0u, end), consume);
                    m_Buffer.erase(0u, end + separator);
                }
            }

            [[nodiscard]] bool HasPendingData() const noexcept { return !m_Buffer.empty(); }

        private:
            std::size_t m_MaximumBytes;
            std::string m_Buffer;

            template<class Consumer>
            static void ConsumeEvent(std::string_view event, Consumer&& consume)
            {
                std::string data;
                std::size_t start = 0u;
                while (start <= event.size())
                {
                    const std::size_t end = event.find('\n', start);
                    std::string_view line = event.substr(start,
                        end == std::string_view::npos ? event.size() - start : end - start);
                    if (!line.empty() && line.back() == '\r') line.remove_suffix(1u);
                    if (line.starts_with("data:"))
                    {
                        line.remove_prefix(5u);
                        if (!line.empty() && line.front() == ' ') line.remove_prefix(1u);
                        if (!data.empty()) data.push_back('\n');
                        data.append(line);
                    }
                    if (end == std::string_view::npos) break;
                    start = end + 1u;
                }
                if (!data.empty()) consume(data);
            }
        };

        [[nodiscard]] inline std::string RoleName(MessageRole role)
        {
            switch (role)
            {
                case MessageRole::System: return "system";
                case MessageRole::User: return "user";
                case MessageRole::Assistant: return "assistant";
                case MessageRole::Tool: return "tool";
            }
            throw std::invalid_argument("AI message has an unknown role.");
        }

        /// Kairo tool names are hierarchical (`scene.create_entity`), while the
        /// Chat Completions function-name alphabet excludes dots. The adapter
        /// owns this reversible wire convention so provider restrictions never
        /// leak into editor command registration or approval identities.
        [[nodiscard]] inline std::string WireToolName(std::string_view canonical)
        {
            std::string wire;
            wire.reserve(canonical.size() + 8u);
            for (const char character : canonical)
            {
                if (character == '.') wire += "__";
                else wire.push_back(character);
            }
            if (wire.empty() || wire.size() > 64u)
                throw std::invalid_argument(
                    "AI tool name cannot be represented within the provider's 64-byte limit.");
            return wire;
        }
    }

    class OpenAICompatibleProvider final : public Provider
    {
    public:
        OpenAICompatibleProvider(OpenAICompatibleConfig config,
            std::shared_ptr<ChatStreamTransport> transport)
            : m_Config(std::move(config)), m_Transport(std::move(transport))
        {
            if (!m_Transport) throw std::invalid_argument("OpenAI-compatible transport cannot be null.");
            if (!openai_detail::IsSecureEndpoint(m_Config.Endpoint))
                throw std::invalid_argument("OpenAI-compatible endpoint must use HTTPS or loopback HTTP.");
            if (m_Config.APIKey.empty()) throw std::invalid_argument("OpenAI-compatible API key cannot be empty.");
            if (m_Config.TimeoutMilliseconds == 0u || m_Config.MaximumEventBytes == 0u ||
                m_Config.MaximumResponseBytes == 0u)
                throw std::invalid_argument("OpenAI-compatible transport limits must be positive.");
        }

        [[nodiscard]] std::string_view Name() const noexcept override { return "openai-compatible.chat"; }

        [[nodiscard]] Response Execute(const Request& request,
            const StreamSink& sink, std::stop_token stopToken) override
        {
            using openai_detail::Json;
            ValidateRequest(request);
            std::map<std::string, std::string, std::less<>> wireToCanonical;
            for (const ToolDefinition& tool : request.Tools)
            {
                const std::string wire = openai_detail::WireToolName(tool.Name);
                if (!wireToCanonical.emplace(wire, tool.Name).second)
                    throw std::invalid_argument(
                        "AI tool names collide after OpenAI-compatible wire encoding.");
            }
            const std::string payload = BuildPayload(request).dump();
            HeaderMap headers{
                { "Accept", "text/event-stream" },
                { "Authorization", "Bearer " + m_Config.APIKey },
                { "Content-Type", "application/json" }
            };

            Response response;
            std::map<std::uint32_t, ToolCall> pendingTools;
            openai_detail::SSEDecoder decoder(m_Config.MaximumEventBytes);
            bool done = false;
            const auto consumeEvent = [&](std::string_view data)
            {
                if (data == "[DONE]") { done = true; return; }
                Json chunk;
                try { chunk = Json::parse(data); }
                catch (const Json::exception& error)
                { throw std::runtime_error(std::string("OpenAI-compatible stream contains invalid JSON: ") + error.what()); }
                if (chunk.contains("error"))
                {
                    const auto& error = chunk.at("error");
                    const std::string message = error.is_object() && error.contains("message") && error.at("message").is_string()
                        ? error.at("message").get<std::string>() : "provider returned an unspecified error";
                    throw std::runtime_error("OpenAI-compatible provider error: " + message.substr(0u, 4096u));
                }
                if (chunk.contains("usage") && chunk.at("usage").is_object())
                {
                    const auto& usage = chunk.at("usage");
                    if (usage.contains("prompt_tokens") && usage.at("prompt_tokens").is_number_unsigned())
                        response.Metering.InputTokens = usage.at("prompt_tokens").get<std::uint64_t>();
                    if (usage.contains("completion_tokens") && usage.at("completion_tokens").is_number_unsigned())
                        response.Metering.OutputTokens = usage.at("completion_tokens").get<std::uint64_t>();
                }
                if (!chunk.contains("choices") || !chunk.at("choices").is_array()) return;
                for (const auto& choice : chunk.at("choices"))
                {
                    if (!choice.is_object() || !choice.contains("delta") || !choice.at("delta").is_object()) continue;
                    const auto& delta = choice.at("delta");
                    if (delta.contains("content") && delta.at("content").is_string())
                    {
                        const std::string text = delta.at("content").get<std::string>();
                        if (response.Text.size() + text.size() > m_Config.MaximumResponseBytes)
                            throw std::length_error("OpenAI-compatible text exceeds its response limit.");
                        response.Text += text;
                        response.Metering.OutputBytes += text.size();
                        if (sink) sink(StreamEvent::Delta(text));
                    }
                    if (!delta.contains("tool_calls") || !delta.at("tool_calls").is_array()) continue;
                    for (const auto& toolDelta : delta.at("tool_calls"))
                    {
                        if (!toolDelta.is_object() || !toolDelta.contains("index") ||
                            !toolDelta.at("index").is_number_unsigned())
                            throw std::runtime_error("OpenAI-compatible tool delta has no valid index.");
                        const auto index = toolDelta.at("index").get<std::uint32_t>();
                        ToolCall& tool = pendingTools[index];
                        if (toolDelta.contains("id") && toolDelta.at("id").is_string())
                            tool.ID += toolDelta.at("id").get<std::string>();
                        if (toolDelta.contains("function") && toolDelta.at("function").is_object())
                        {
                            const auto& function = toolDelta.at("function");
                            if (function.contains("name") && function.at("name").is_string())
                                tool.Name += function.at("name").get<std::string>();
                            if (function.contains("arguments") && function.at("arguments").is_string())
                                tool.Arguments += function.at("arguments").get<std::string>();
                        }
                        if (tool.ID.size() + tool.Name.size() + tool.Arguments.size() > m_Config.MaximumResponseBytes)
                            throw std::length_error("OpenAI-compatible tool call exceeds its response limit.");
                    }
                }
            };

            std::exception_ptr callbackFailure;
            const auto chunkSink = [&](std::string_view chunk) -> bool
            {
                if (stopToken.stop_requested()) return false;
                try { decoder.Feed(chunk, consumeEvent); return !done; }
                catch (...) { callbackFailure = std::current_exception(); return false; }
            };
            const ChatTransportResult transport = m_Transport->Post(
                m_Config.Endpoint, headers, payload, m_Config.TimeoutMilliseconds,
                chunkSink, stopToken);
            if (callbackFailure) std::rethrow_exception(callbackFailure);
            if (stopToken.stop_requested()) { response.Cancelled = true; return response; }
            if (transport.StatusCode < 200 || transport.StatusCode >= 300)
                throw std::runtime_error(HTTPError(transport));
            if (!done) throw std::runtime_error("OpenAI-compatible stream ended before the [DONE] marker.");
            for (auto& [index, tool] : pendingTools)
            {
                const auto canonical = wireToCanonical.find(tool.Name);
                if (tool.ID.empty() || canonical == wireToCanonical.end())
                    throw std::runtime_error("OpenAI-compatible stream produced an invalid tool call.");
                tool.Name = canonical->second;
                response.Metering.OutputBytes += tool.ID.size() + tool.Name.size() + tool.Arguments.size();
                response.ToolCalls.push_back(tool);
                ++response.Metering.ToolCalls;
                if (sink) sink(StreamEvent::Call(tool));
            }
            for (const Message& message : request.Messages) response.Metering.InputBytes += message.Content.size();
            return response;
        }

    private:
        OpenAICompatibleConfig m_Config;
        std::shared_ptr<ChatStreamTransport> m_Transport;

        [[nodiscard]] static openai_detail::Json BuildPayload(const Request& request)
        {
            using openai_detail::Json;
            Json payload{
                { "model", request.Model }, { "stream", true },
                { "stream_options", { { "include_usage", true } } },
                { "max_tokens", request.MaximumOutputTokens },
                { "temperature", request.Temperature }, { "messages", Json::array() }
            };
            for (const Message& message : request.Messages)
            {
                Json encoded{ { "role", openai_detail::RoleName(message.Role) },
                    { "content", message.Content } };
                if (message.Role == MessageRole::Tool)
                {
                    if (message.ToolCallID.empty())
                        throw std::invalid_argument("AI tool result message requires a tool call ID.");
                    encoded["tool_call_id"] = message.ToolCallID;
                }
                payload["messages"].push_back(std::move(encoded));
            }
            if (!request.Tools.empty())
            {
                payload["tools"] = Json::array();
                for (const ToolDefinition& tool : request.Tools)
                {
                    Json schema;
                    try { schema = Json::parse(tool.ParametersSchema); }
                    catch (const Json::exception& error)
                    { throw std::invalid_argument(std::string("AI tool schema is invalid JSON: ") + error.what()); }
                    payload["tools"].push_back({ { "type", "function" },
                        { "function", { { "name", openai_detail::WireToolName(tool.Name) },
                            { "description", tool.Description },
                            { "parameters", std::move(schema) }, { "strict", true } } } });
                }
            }
            return payload;
        }

        [[nodiscard]] static std::string HTTPError(const ChatTransportResult& result)
        {
            std::string detail = result.Error;
            try
            {
                const auto body = openai_detail::Json::parse(result.Body);
                if (body.contains("error") && body.at("error").is_object() &&
                    body.at("error").contains("message") && body.at("error").at("message").is_string())
                    detail = body.at("error").at("message").get<std::string>();
            }
            catch (const openai_detail::Json::exception&) {}
            if (detail.size() > 4096u) detail.resize(4096u);
            return "OpenAI-compatible HTTP request failed with status " +
                std::to_string(result.StatusCode) + (detail.empty() ? "." : ": " + detail);
        }
    };
}
