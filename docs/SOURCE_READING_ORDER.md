# KairoAI — source reading order and authority boundaries

This C++23 package is a **provider-neutral interface and policy layer**, not a universal agent with permission to operate a user's device. Distinguish model text, tool proposals, host-approved calls and actual action receipts.

## Read in this order

1. [README](../README.md), [CMake](../CMakeLists.txt) — default local features vs optional cloud target.
2. [AIContracts.cppm](../AIContracts.cppm) — requests, bounded messages, tool-call syntax, streaming events, usage statistics and validation.
3. [AIProvider.cppm](../AIProvider.cppm) — `Provider::Execute`, `StreamSink`, `RequestTask` and cancellation lifetime via `std::jthread`.
4. [MockAIProvider.cppm](../MockAIProvider.cppm) — deterministic credential-free CI provider, useful to learn contracts first.
5. [AIToolPolicy.cppm](../AIToolPolicy.cppm) — Ask/Plan/Agent capabilities and exact-call `ToolApproval`; untrusted prompts cannot grant permission.
6. [DeviceAgent.cppm](../DeviceAgent.cppm) — typed gesture/speech/screen evidence, intent proposal, execution receipts and independent verification.
7. [GameplayAI.cppm](../GameplayAI.cppm) — gameplay blackboard/navigation/cognition outputs; [KairoSpatial](https://github.com/swayam8624/KairoSpatial) and EngineCore own world execution.
8. [KairoAI.cppm](../KairoAI.cppm) — umbrella public module.
9. [OpenAICompatibleProvider.cppm](../OpenAICompatibleProvider.cppm), [CprChatTransport.cppm](../CprChatTransport.cppm), [KairoAICloud.cppm](../KairoAICloud.cppm) — optional HTTP/SSE transport and API adaptation; must be enabled, configured, secured and tested separately.
10. [tests/KairoAITests.cpp](../tests/KairoAITests.cpp), [tests/DeviceAgentTests.cpp](../tests/DeviceAgentTests.cpp), [tests/GameplayAITests.cpp](../tests/GameplayAITests.cpp), [tests/KairoAICloudTests.cpp](../tests/KairoAICloudTests.cpp) — contract and safety checks.

## Cross-file calls

`Request` → `ValidateRequest` → provider/model transport → `StreamEvent`/response → model may propose `ToolCall` → registered host tool looked up → `AuthorizeTool` checks mode/capability/exact-call approval → **only the host** executes an allowed tool → receipt and verification returned. DeviceAgent proposals are evidence-informed suggestions, not execution authority.

## Exercises

- Compare Ask, Plan, Agent behavior for a potentially mutating tool.
- Change one argument in an approved `ToolCall` and confirm approval no longer matches.
- Cancel `RequestTask` midway and verify the worker cannot outlive its captured callback state.
- Compare `ExecutionStatus::Succeeded` to `VerificationDecision::Verified`.
- Run mock-provider tests offline; do not need live credentials to test core policy.

[LICENSE](../LICENSE) is the existing project license; preserve cloud HTTP/client dependencies' separate notices.
