module;

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.AI.Contracts;

export namespace kairo::ai
{
    enum class MessageRole : std::uint8_t { System, User, Assistant, Tool };

    struct Message final
    {
        MessageRole Role = MessageRole::User;
        std::string Content;
        std::string ToolCallID;
        friend bool operator==(const Message&, const Message&) = default;
    };

    /// Provider-neutral tool declaration. ParametersSchema is a JSON Schema
    /// document carried opaquely by the core; transport adapters may parse it
    /// with their pinned JSON implementation without leaking that dependency.
    struct ToolDefinition final
    {
        std::string Name;
        std::string Description;
        std::string ParametersSchema;
        friend bool operator==(const ToolDefinition&, const ToolDefinition&) = default;
    };

    struct ToolCall final
    {
        std::string ID;
        std::string Name;
        std::string Arguments;
        friend bool operator==(const ToolCall&, const ToolCall&) = default;
    };

    struct RequestLimits final
    {
        std::size_t MaximumMessages = 256u;
        std::size_t MaximumContextBytes = 2u * 1024u * 1024u;
        std::size_t MaximumTools = 128u;
        std::size_t MaximumToolSchemaBytes = 256u * 1024u;
        std::uint32_t MaximumOutputTokens = 16'384u;
    };

    struct Request final
    {
        std::string Model;
        std::vector<Message> Messages;
        std::vector<ToolDefinition> Tools;
        std::uint32_t MaximumOutputTokens = 2'048u;
        double Temperature = 0.0;
    };

    struct Usage final
    {
        std::size_t InputBytes = 0u;
        std::size_t OutputBytes = 0u;
        std::uint64_t InputTokens = 0u;
        std::uint64_t OutputTokens = 0u;
        std::uint32_t ToolCalls = 0u;
        friend bool operator==(const Usage&, const Usage&) = default;
    };

    enum class StreamEventKind : std::uint8_t { TextDelta, ToolCall };

    struct StreamEvent final
    {
        StreamEventKind Kind = StreamEventKind::TextDelta;
        std::string Text;
        ToolCall Tool;

        [[nodiscard]] static StreamEvent Delta(std::string text)
        { return { StreamEventKind::TextDelta, std::move(text), {} }; }
        [[nodiscard]] static StreamEvent Call(ToolCall tool)
        { return { StreamEventKind::ToolCall, {}, std::move(tool) }; }
        friend bool operator==(const StreamEvent&, const StreamEvent&) = default;
    };

    struct Response final
    {
        std::string Text;
        std::vector<ToolCall> ToolCalls;
        Usage Metering;
        bool Cancelled = false;
        friend bool operator==(const Response&, const Response&) = default;
    };

    [[nodiscard]] inline bool IsToolName(std::string_view value) noexcept
    {
        if (value.empty() || value.size() > 128u) return false;
        return std::all_of(value.begin(), value.end(), [](unsigned char character)
        { return std::isalnum(character) != 0 || character == '_' || character == '-' || character == '.'; });
    }

    /// Input: an untrusted provider request and caller-selected safety limits.
    /// Output: no value; throws before network or local inference work begins.
    /// Task: bound memory/cost exposure and reject ambiguous tool contracts at
    /// the shared API boundary, including direct construction by tests/tools.
    inline void ValidateRequest(const Request& request, const RequestLimits& limits = {})
    {
        if (limits.MaximumMessages == 0u || limits.MaximumContextBytes == 0u ||
            limits.MaximumTools == 0u || limits.MaximumToolSchemaBytes == 0u ||
            limits.MaximumOutputTokens == 0u)
            throw std::invalid_argument("AI request limits must be positive.");
        if (request.Model.empty() || request.Model.size() > 256u)
            throw std::invalid_argument("AI model name must contain between 1 and 256 bytes.");
        if (request.Messages.empty() || request.Messages.size() > limits.MaximumMessages)
            throw std::invalid_argument("AI request message count is outside the configured limit.");
        if (request.Tools.size() > limits.MaximumTools)
            throw std::length_error("AI request exceeds the configured tool count.");
        if (request.MaximumOutputTokens == 0u || request.MaximumOutputTokens > limits.MaximumOutputTokens)
            throw std::invalid_argument("AI output token limit is outside the configured range.");
        if (request.Temperature < 0.0 || request.Temperature > 2.0)
            throw std::invalid_argument("AI temperature must be within [0, 2].");

        std::size_t contextBytes = request.Model.size();
        for (const Message& message : request.Messages)
        {
            if (message.Content.empty()) throw std::invalid_argument("AI messages cannot be empty.");
            contextBytes += message.Content.size() + message.ToolCallID.size();
            if (contextBytes > limits.MaximumContextBytes)
                throw std::length_error("AI request exceeds the configured context size.");
        }

        std::map<std::string, bool, std::less<>> names;
        for (const ToolDefinition& tool : request.Tools)
        {
            if (!IsToolName(tool.Name)) throw std::invalid_argument("AI tool name is invalid.");
            if (!names.emplace(tool.Name, true).second)
                throw std::invalid_argument("AI tool names must be unique.");
            if (tool.Description.empty()) throw std::invalid_argument("AI tool description cannot be empty.");
            if (tool.ParametersSchema.empty() || tool.ParametersSchema.size() > limits.MaximumToolSchemaBytes)
                throw std::invalid_argument("AI tool parameter schema is empty or exceeds its limit.");
            contextBytes += tool.Description.size() + tool.ParametersSchema.size();
            if (contextBytes > limits.MaximumContextBytes)
                throw std::length_error("AI request exceeds the configured context size.");
        }
    }
}
