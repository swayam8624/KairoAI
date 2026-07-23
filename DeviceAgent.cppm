module;

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module Kairo.AI.DeviceAgent;

import Kairo.AI.Contracts;

export namespace kairo::ai::device
{
    /// Input modalities accepted from trusted, host-owned perception adapters.
    /// Output: typed evidence attached to an intent proposal.
    /// Task: make it explicit which signal produced an automation proposal.
    enum class EvidenceKind : std::uint8_t
    {
        Gesture,
        Speech,
        ScreenState,
        AudioCue,
        SelectionState
    };

    enum class IntentKind : std::uint8_t
    {
        CaptureScreenRegion,
        AdjustVolume,
        MoveSelectionToBin,
        TrimSelectedClip,
        SplitSelectedClip
    };

    enum class ProposalDecision : std::uint8_t
    {
        Proposed,
        InsufficientConfidence,
        MissingEvidence,
        ApplicationMismatch,
        IntentNotBound
    };

    /// The adapter reports execution facts; it does not decide whether a
    /// request was authorized. `Kairo.AI.ToolPolicy` remains that boundary.
    enum class ExecutionStatus : std::uint8_t
    {
        NotExecuted,
        Succeeded,
        RejectedByAdapter,
        Failed
    };

    /// Verification is intentionally separate from execution. A native API
    /// call can succeed while the visible application state remains stale or
    /// does not match the requested postcondition.
    enum class VerificationDecision : std::uint8_t
    {
        NotAttempted,
        Verified,
        Failed,
        Ambiguous
    };

    struct Evidence final
    {
        EvidenceKind Kind = EvidenceKind::Gesture;
        std::string ID;
        float Confidence = 0.0f;
        std::uint64_t MonotonicMilliseconds = 0u;
    };

    /// A host-constructed, canonical action binding. `Arguments` are never
    /// derived from a model response; the application adapter owns their schema
    /// and builds them from the verified screen/selection state.
    struct ActionBinding final
    {
        std::string ApplicationID;
        IntentKind Intent = IntentKind::CaptureScreenRegion;
        std::string ToolName;
        std::string Arguments;
        std::vector<EvidenceKind> RequiredEvidence;
        float MinimumConfidence = 0.90f;
    };

    struct IntentRequest final
    {
        std::string ActiveApplicationID;
        IntentKind Intent = IntentKind::CaptureScreenRegion;
        std::vector<Evidence> EvidenceSet;
    };

    struct ActionProposal final
    {
        ProposalDecision Decision = ProposalDecision::IntentNotBound;
        std::string Reason;
        std::optional<ToolCall> Call;
        std::vector<std::string> EvidenceIDs;

        [[nodiscard]] bool ReadyForAuthorization() const noexcept
        {
            return Decision == ProposalDecision::Proposed && Call.has_value();
        }
    };

    /// Input: the exact authorized call and application-adapter result.
    /// Output: a receipt that can be joined with a proposal and verification.
    /// Task: record host execution without giving an adapter authority to alter
    /// the call identity or evidence provenance.
    struct ActionReceipt final
    {
        std::string CallID;
        std::string AdapterID;
        ExecutionStatus Status = ExecutionStatus::NotExecuted;
        std::string UndoToken;
        std::string Detail;
        std::uint64_t CompletedAtMonotonicMilliseconds = 0u;
    };

    /// Input: a state observation made after execution.
    /// Output: deterministic verification status and an opaque state fingerprint.
    /// Task: retain the evidence that the intended visible/app state occurred
    /// without forcing each adapter into a shared UI representation.
    struct ActionVerification final
    {
        std::string CallID;
        VerificationDecision Decision = VerificationDecision::NotAttempted;
        std::string StateFingerprint;
        std::string Detail;
        std::uint64_t ObservedAtMonotonicMilliseconds = 0u;
    };

    /// The in-memory canonical record for fixtures, local persistence adapters,
    /// and correction curation. A storage layer may serialize it, but must not
    /// invent or alter any of the identity-bearing fields.
    struct ReplayRecord final
    {
        ToolCall Call;
        std::string ActiveApplicationID;
        std::vector<std::string> EvidenceIDs;
        ActionReceipt Receipt;
        ActionVerification Verification;
    };

    [[nodiscard]] inline ReplayRecord MakeReplayRecord(const ActionProposal& proposal,
        std::string activeApplicationID, ActionReceipt receipt, ActionVerification verification)
    {
        if (!proposal.ReadyForAuthorization() || activeApplicationID.empty())
            throw std::invalid_argument("Replay records require a proposed call and active application id.");
        if (receipt.CallID != proposal.Call->ID || verification.CallID != proposal.Call->ID)
            throw std::invalid_argument("Replay receipt and verification must match the exact proposed call id.");
        if (receipt.AdapterID.empty())
            throw std::invalid_argument("Replay receipts require an application adapter id.");
        if (verification.Decision != VerificationDecision::NotAttempted && verification.StateFingerprint.empty())
            throw std::invalid_argument("Completed verification requires an observed state fingerprint.");
        return { *proposal.Call, std::move(activeApplicationID), proposal.EvidenceIDs,
            std::move(receipt), std::move(verification) };
    }

    /// Input: registered application bindings and trusted perception evidence.
    /// Output: a proposal containing an exact `ToolCall`, or a refusal reason.
    /// Task: bind multimodal intent to a narrow, visible app action without
    /// performing side effects. The caller must still pass the proposal to
    /// `Kairo.AI.ToolPolicy` for capability and approval enforcement.
    class IntentRouter final
    {
    public:
        void Register(ActionBinding binding)
        {
            ValidateBinding(binding);
            const auto duplicate = std::find_if(m_Bindings.begin(), m_Bindings.end(),
                [&binding](const ActionBinding& existing)
                {
                    return existing.ApplicationID == binding.ApplicationID && existing.Intent == binding.Intent;
                });
            if (duplicate != m_Bindings.end())
                throw std::invalid_argument("A device-agent action binding already exists for this application and intent.");
            m_Bindings.push_back(std::move(binding));
        }

        [[nodiscard]] ActionProposal Propose(const IntentRequest& request) const
        {
            ValidateRequest(request);
            const auto binding = std::find_if(m_Bindings.begin(), m_Bindings.end(),
                [&request](const ActionBinding& candidate)
                {
                    return candidate.Intent == request.Intent;
                });
            if (binding == m_Bindings.end())
                return { ProposalDecision::IntentNotBound, "No action is registered for this intent.", std::nullopt, {} };
            if (binding->ApplicationID != request.ActiveApplicationID)
                return { ProposalDecision::ApplicationMismatch,
                    "The active application does not match the registered action binding.", std::nullopt, {} };

            std::vector<std::string> evidenceIDs;
            for (const EvidenceKind required : binding->RequiredEvidence)
            {
                const auto evidence = std::find_if(request.EvidenceSet.begin(), request.EvidenceSet.end(),
                    [required, &binding](const Evidence& candidate)
                    {
                        return candidate.Kind == required && candidate.Confidence >= binding->MinimumConfidence;
                    });
                if (evidence == request.EvidenceSet.end())
                {
                    const bool kindPresent = std::any_of(request.EvidenceSet.begin(), request.EvidenceSet.end(),
                        [required](const Evidence& candidate) { return candidate.Kind == required; });
                    return { kindPresent ? ProposalDecision::InsufficientConfidence : ProposalDecision::MissingEvidence,
                        kindPresent ? "Required evidence did not meet the action confidence threshold."
                                    : "A required evidence modality is missing.",
                        std::nullopt, std::move(evidenceIDs) };
                }
                evidenceIDs.push_back(evidence->ID);
            }

            const std::string callID = "device." + binding->ApplicationID + "." + std::to_string(++m_NextCallID);
            return { ProposalDecision::Proposed, "Verified intent matched a registered application action.",
                ToolCall{ callID, binding->ToolName, binding->Arguments }, std::move(evidenceIDs) };
        }

        [[nodiscard]] std::size_t BindingCount() const noexcept { return m_Bindings.size(); }

    private:
        std::vector<ActionBinding> m_Bindings;
        mutable std::uint64_t m_NextCallID = 0u;

        static void ValidateBinding(const ActionBinding& binding)
        {
            if (binding.ApplicationID.empty() || !IsToolName(binding.ToolName) || binding.Arguments.empty())
                throw std::invalid_argument("Device-agent bindings require an application id, valid tool name, and canonical arguments.");
            if (binding.RequiredEvidence.empty() || binding.MinimumConfidence <= 0.0f || binding.MinimumConfidence > 1.0f)
                throw std::invalid_argument("Device-agent bindings require evidence and a confidence threshold within (0, 1].");
        }

        static void ValidateRequest(const IntentRequest& request)
        {
            if (request.ActiveApplicationID.empty() || request.EvidenceSet.empty())
                throw std::invalid_argument("Device-agent requests require an active application and evidence.");
            for (const Evidence& evidence : request.EvidenceSet)
            {
                if (evidence.ID.empty() || evidence.Confidence < 0.0f || evidence.Confidence > 1.0f)
                    throw std::invalid_argument("Device-agent evidence requires an id and confidence within [0, 1].");
            }
        }
    };
}
