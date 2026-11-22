// TEACHING MAP: The AI model can *propose* a tool name and argument
// string, but only host-registered ToolRegistry handlers can execute.
// AuthorizeTool applies a capability policy before invoking a handler:
// credential access is always denied, Ask/Plan permit only ReadProject,
// and Agent requires explicit approval for each side-effecting call.
//
// Approval is bound to exact ToolCall.ID, Name and Arguments, not just a
// generic "yes" for the application or a tool category. A changed
// argument string after approval must fail. The host retains network,
// process, filesystem and credential authority; the language model
// cannot grant itself those capabilities.
//
// Tool output may be untrusted and must not bypass authorization on
// subsequent calls. Tests/KairoAITests.cpp should verify every mode,
// capability, missing approval, mismatch and denied handler invocation.

module;

#include <cstdint>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

export module Kairo.AI.ToolPolicy;

import Kairo.AI.Contracts;

export namespace kairo::ai
{
    enum class InteractionMode : std::uint8_t { Ask, Plan, Agent };
    enum class ToolCapability : std::uint8_t
    {
        ReadProject,
        MutateProject,
        ExecuteProcess,
        UseNetwork,
        AccessCredential
    };

    struct RegisteredTool final
    {
        std::string Name;
        ToolCapability Capability = ToolCapability::ReadProject;
        std::string Description;
    };

    /// Approval is deliberately supplied out-of-band from model output and is
    /// bound to every call field. Text in a project, prompt, or model response
    /// therefore cannot manufacture permission for a modified invocation.
    struct ToolApproval final
    {
        std::string CallID;
        std::string ToolName;
        std::string Arguments;
        bool Granted = false;
    };

    enum class AuthorizationDecision : std::uint8_t
    {
        Allowed,
        ApprovalRequired,
        DeniedByMode,
        CredentialAccessDenied,
        ApprovalMismatch,
        ApprovalRejected
    };

    struct Authorization final
    {
        AuthorizationDecision Decision = AuthorizationDecision::DeniedByMode;
        std::string Reason;
        [[nodiscard]] bool Allowed() const noexcept
        { return Decision == AuthorizationDecision::Allowed; }
    };

    // Enforce capability, interaction mode and exact-call approval before
    // any host side effect. This function only returns a decision; it
    // does not run the registered handler.
    [[nodiscard]] inline Authorization AuthorizeTool(InteractionMode mode,
        const RegisteredTool& tool, const ToolCall& call,
        const std::optional<ToolApproval>& approval = std::nullopt)
    {
        if (!IsToolName(tool.Name) || tool.Name != call.Name || call.ID.empty())
            throw std::invalid_argument("AI tool registration and call identity must be valid and equal.");
        if (tool.Capability == ToolCapability::AccessCredential)
            return { AuthorizationDecision::CredentialAccessDenied,
                "AI tools cannot access credentials; providers receive secrets through their host adapter." };
        if (mode != InteractionMode::Agent && tool.Capability != ToolCapability::ReadProject)
            return { AuthorizationDecision::DeniedByMode,
                "Ask and Plan modes cannot mutate projects, launch processes, or use external networks." };
        if (tool.Capability == ToolCapability::ReadProject)
            return { AuthorizationDecision::Allowed, "Read-only project context is allowed." };
        if (!approval)
            return { AuthorizationDecision::ApprovalRequired,
                "This Agent tool requires explicit approval for the exact invocation." };
        if (approval->CallID != call.ID || approval->ToolName != call.Name ||
            approval->Arguments != call.Arguments)
            return { AuthorizationDecision::ApprovalMismatch,
                "Approval does not match the exact tool call identity and arguments." };
        if (!approval->Granted)
            return { AuthorizationDecision::ApprovalRejected, "The user rejected this tool call." };
        return { AuthorizationDecision::Allowed, "Exact Agent invocation was approved." };
    }

    struct ToolExecution final
    {
        std::string Output;
        Authorization AuthorizationResult;
        bool Invoked = false;
    };

    using ToolHandler = std::function<std::string(std::string_view arguments)>;

    /// Typed host registry for model-proposed calls. The registry owns policy
    /// metadata and executable handlers; models can select only registered
    /// names. Authorization is evaluated before handler invocation.
    class ToolRegistry final
    {
    public:
        void Register(RegisteredTool tool, ToolHandler handler)
        {
            if (!IsToolName(tool.Name) || tool.Description.empty() || !handler)
                throw std::invalid_argument("AI tool registration requires a valid name, description, and handler.");
            const std::string name = tool.Name;
            if (!m_Tools.emplace(name, Entry{ std::move(tool), std::move(handler) }).second)
                throw std::invalid_argument("AI tool is already registered.");
        }

        // Look up a host-owned handler, authorize the proposed call, and
        // invoke only after Allowed(). An unregistered tool is an error,
        // not permission to execute a guessed shell operation.
        [[nodiscard]] ToolExecution Execute(InteractionMode mode, const ToolCall& call,
            const std::optional<ToolApproval>& approval = std::nullopt) const
        {
            const auto found = m_Tools.find(call.Name);
            if (found == m_Tools.end()) throw std::out_of_range("AI requested an unregistered tool.");
            Authorization authorization = AuthorizeTool(mode, found->second.Tool, call, approval);
            if (!authorization.Allowed()) return { {}, std::move(authorization), false };
            return { found->second.Handler(call.Arguments), std::move(authorization), true };
        }

        [[nodiscard]] std::size_t Size() const noexcept { return m_Tools.size(); }

    private:
        struct Entry final { RegisteredTool Tool; ToolHandler Handler; };
        std::map<std::string, Entry, std::less<>> m_Tools;
    };
}
