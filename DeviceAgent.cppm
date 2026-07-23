module;

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
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

    enum class ActionRisk : std::uint8_t
    {
        ReadOnly,
        Low,
        Medium,
        High,
        Destructive
    };

    enum class ApprovalPolicy : std::uint8_t
    {
        None,
        Preview,
        Explicit,
        IndependentDevice
    };

    enum class Reversibility : std::uint8_t
    {
        NaturallyReversible,
        SnapshotRestorable,
        CompensatingAction,
        Irreversible
    };

    struct StateCondition final
    {
        std::string ID;
        std::string Description;
        std::string ExpectedFingerprint;
    };

    struct ExpectedEffect final
    {
        std::string ID;
        std::string Description;
        std::string Target;
    };

    struct RollbackPlan final
    {
        Reversibility Mode = Reversibility::Irreversible;
        std::string AdapterOperation;
        bool CaptureBeforeState = false;
    };

    struct VerificationPlan final
    {
        std::vector<std::string> RequiredEffectIDs;
        std::uint32_t TimeoutMilliseconds = 5'000u;
    };

    /// Versioned host-authored safety contract attached to an exact action
    /// binding. Models may select a registered binding but cannot weaken its
    /// approval, rollback, precondition, effect, or verification requirements.
    struct ActionContract final
    {
        std::uint32_t SchemaVersion = 1u;
        ActionRisk Risk = ActionRisk::Low;
        ApprovalPolicy Approval = ApprovalPolicy::Preview;
        std::vector<StateCondition> Preconditions;
        std::vector<ExpectedEffect> Effects;
        RollbackPlan Rollback;
        VerificationPlan Verification;
    };

    inline void ValidateActionContract(const ActionContract& contract)
    {
        if (contract.SchemaVersion != 1u)
            throw std::invalid_argument("Device-action contract schema version is unsupported.");
        if (contract.Preconditions.empty() || contract.Effects.empty())
            throw std::invalid_argument("Device-action contracts require preconditions and expected effects.");
        if (contract.Verification.RequiredEffectIDs.empty() || contract.Verification.TimeoutMilliseconds == 0u)
            throw std::invalid_argument("Device-action contracts require bounded effect verification.");
        if ((contract.Risk == ActionRisk::High || contract.Risk == ActionRisk::Destructive)
            && contract.Approval != ApprovalPolicy::Explicit
            && contract.Approval != ApprovalPolicy::IndependentDevice)
            throw std::invalid_argument("High-risk device actions require explicit or independent-device approval.");
        if (contract.Rollback.Mode == Reversibility::Irreversible)
        {
            if (contract.Risk != ActionRisk::Destructive
                || contract.Approval != ApprovalPolicy::IndependentDevice)
                throw std::invalid_argument("Irreversible actions require destructive risk and independent-device approval.");
        }
        else if (contract.Rollback.AdapterOperation.empty())
        {
            throw std::invalid_argument("Reversible actions require a registered rollback adapter operation.");
        }
        if (contract.Rollback.Mode == Reversibility::SnapshotRestorable
            && !contract.Rollback.CaptureBeforeState)
            throw std::invalid_argument("Snapshot-restorable actions must capture pre-action state.");

        std::set<std::string, std::less<>> conditionIDs;
        for (const StateCondition& condition : contract.Preconditions)
        {
            if (condition.ID.empty() || condition.Description.empty() || condition.ExpectedFingerprint.empty()
                || !conditionIDs.emplace(condition.ID).second)
                throw std::invalid_argument("Action preconditions require unique ids, descriptions, and fingerprints.");
        }
        std::set<std::string, std::less<>> effectIDs;
        for (const ExpectedEffect& effect : contract.Effects)
        {
            if (effect.ID.empty() || effect.Description.empty() || effect.Target.empty()
                || !effectIDs.emplace(effect.ID).second)
                throw std::invalid_argument("Expected effects require unique ids, descriptions, and targets.");
        }
        for (const std::string& effectID : contract.Verification.RequiredEffectIDs)
        {
            if (!effectIDs.contains(effectID))
                throw std::invalid_argument("Verification refers to an effect not declared by the action contract.");
        }
    }

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
        ActionContract Contract;
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
        std::optional<ActionContract> Contract;
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
        ActionContract Contract;
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
        if (!proposal.Contract)
            throw std::invalid_argument("Replay records require the exact proposed action contract.");
        return { *proposal.Call, *proposal.Contract, std::move(activeApplicationID), proposal.EvidenceIDs,
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
                return { ProposalDecision::IntentNotBound, "No action is registered for this intent.", std::nullopt, std::nullopt, {} };
            if (binding->ApplicationID != request.ActiveApplicationID)
                return { ProposalDecision::ApplicationMismatch,
                    "The active application does not match the registered action binding.", std::nullopt, std::nullopt, {} };

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
                        std::nullopt, std::nullopt, std::move(evidenceIDs) };
                }
                evidenceIDs.push_back(evidence->ID);
            }

            const std::string callID = "device." + binding->ApplicationID + "." + std::to_string(++m_NextCallID);
            return { ProposalDecision::Proposed, "Verified intent matched a registered application action.",
                ToolCall{ callID, binding->ToolName, binding->Arguments }, binding->Contract,
                std::move(evidenceIDs) };
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
            ValidateActionContract(binding.Contract);
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
