#include <catch2/catch_test_macros.hpp>

import Kairo.AI.DeviceAgent;

using namespace kairo::ai::device;

namespace
{
    [[nodiscard]] ActionBinding PremiereTrimBinding()
    {
        return { "com.adobe.Premiere", IntentKind::TrimSelectedClip, "premiere.clip.trim",
            R"({"operation":"trim-selected-clip"})", { EvidenceKind::Gesture, EvidenceKind::ScreenState,
                EvidenceKind::SelectionState }, 0.90f };
    }
}

TEST_CASE("Device agent proposes only a verified registered app action", "[KairoAI][DeviceAgent]")
{
    IntentRouter router;
    router.Register(PremiereTrimBinding());

    const IntentRequest request{ "com.adobe.Premiere", IntentKind::TrimSelectedClip,
        { { EvidenceKind::Gesture, "gesture-17", 0.97f, 10u },
          { EvidenceKind::ScreenState, "timeline-visible", 0.99f, 11u },
          { EvidenceKind::SelectionState, "clip-selected", 0.98f, 12u } } };
    const ActionProposal proposal = router.Propose(request);

    REQUIRE(proposal.ReadyForAuthorization());
    CHECK(proposal.Decision == ProposalDecision::Proposed);
    REQUIRE(proposal.Call.has_value());
    CHECK(proposal.Call->Name == "premiere.clip.trim");
    CHECK(proposal.EvidenceIDs.size() == 3u);
}

TEST_CASE("Device agent refuses weak, missing, mismatched, and unbound requests", "[KairoAI][DeviceAgent]")
{
    IntentRouter router;
    router.Register(PremiereTrimBinding());

    IntentRequest weak{ "com.adobe.Premiere", IntentKind::TrimSelectedClip,
        { { EvidenceKind::Gesture, "gesture-weak", 0.5f, 1u },
          { EvidenceKind::ScreenState, "timeline-visible", 0.99f, 2u },
          { EvidenceKind::SelectionState, "clip-selected", 0.99f, 3u } } };
    CHECK(router.Propose(weak).Decision == ProposalDecision::InsufficientConfidence);

    weak.EvidenceSet.erase(weak.EvidenceSet.begin());
    CHECK(router.Propose(weak).Decision == ProposalDecision::MissingEvidence);

    weak.ActiveApplicationID = "com.apple.finder";
    weak.EvidenceSet.insert(weak.EvidenceSet.begin(), { EvidenceKind::Gesture, "gesture-strong", 0.99f, 1u });
    CHECK(router.Propose(weak).Decision == ProposalDecision::ApplicationMismatch);

    weak.ActiveApplicationID = "com.adobe.Premiere";
    weak.Intent = IntentKind::SplitSelectedClip;
    CHECK(router.Propose(weak).Decision == ProposalDecision::IntentNotBound);
}

TEST_CASE("Device agent joins only exact execution and verification records into replay", "[KairoAI][DeviceAgent]")
{
    IntentRouter router;
    router.Register(PremiereTrimBinding());
    const ActionProposal proposal = router.Propose({ "com.adobe.Premiere", IntentKind::TrimSelectedClip,
        { { EvidenceKind::Gesture, "gesture-17", 0.97f, 10u },
          { EvidenceKind::ScreenState, "timeline-visible", 0.99f, 11u },
          { EvidenceKind::SelectionState, "clip-selected", 0.98f, 12u } } });
    REQUIRE(proposal.Call.has_value());

    ActionReceipt receipt{ proposal.Call->ID, "premiere.uxp.v25", ExecutionStatus::Succeeded,
        "undo-42", "Trim command accepted.", 20u };
    ActionVerification verification{ proposal.Call->ID, VerificationDecision::Verified,
        "timeline:selected-clip:trimmed", "Timeline reflects the requested trim.", 21u };
    const ReplayRecord record = MakeReplayRecord(proposal, "com.adobe.Premiere", receipt, verification);
    CHECK(record.Call == *proposal.Call);
    CHECK(record.Receipt.UndoToken == "undo-42");
    CHECK(record.Verification.Decision == VerificationDecision::Verified);

    receipt.CallID = "wrong-call";
    CHECK_THROWS_AS(MakeReplayRecord(proposal, "com.adobe.Premiere", receipt, verification), std::invalid_argument);
}
