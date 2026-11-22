# KairoAI — why a generated tool call is not permission to act

This guide connects the model-facing API, the transport, the device
intent pipeline and host authorization. Begin with
[`AIContracts.cppm`](../AIContracts.cppm),
[`AIProvider.cppm`](../AIProvider.cppm),
[`AIToolPolicy.cppm`](../AIToolPolicy.cppm),
[`DeviceAgent.cppm`](../DeviceAgent.cppm),
[`OpenAICompatibleProvider.cppm`](../OpenAICompatibleProvider.cppm)
and [`CprChatTransport.cppm`](../CprChatTransport.cppm).

## 1. Separate five responsibilities

1. **Request:** host-authored messages and tool definitions.
2. **Provider/transport:** generate response events and candidate tool calls.
3. **Proposal:** map model or trusted perception evidence to one typed
   `ToolCall`, including exact ID/name/arguments.
4. **Authorization:** the host applies interaction mode, tool capability
   and user approval before invoking registered functions.
5. **Execution/verification:** the host executes a permitted handler,
   records its receipt and checks expected effects when required.

Only (4) can authorize side effects. Model-generated JSON, even valid
JSON matching a tool schema, does not have this power.

## 2. Capability and mode policy

[`AIToolPolicy.cppm`](../AIToolPolicy.cppm) declares `Ask`,
`Plan` and `Agent` modes and capabilities: project read,
project mutation, process execution, external network and credentials.

`AuthorizeTool` denies credential access as a tool capability.
Ask/Plan modes allow only read-only project tools. Agent mode still
requires approval for any non-read action, and that approval must
match the exact call ID, tool name and *arguments*.

A generic prior approval to “use shell” must not authorize a later
command with changed arguments. `ToolRegistry::Execute` looks up a
host-registered handler and checks `Allowed()` before calling it.

**Exercise:** present the same model-proposed command in Ask, Plan and
Agent modes; change the argument string after approval; verify no
unauthorized handler is invoked.

## 3. Streaming network data remains untrusted

[`OpenAICompatibleProvider.cppm`](../OpenAICompatibleProvider.cppm)
receives an injected `ChatStreamTransport`. The endpoint policy allows
HTTPS or loopback HTTP, rejecting other unencrypted remote endpoints.
Host secrets are passed as Authorization headers and must never
be echoed as model-context or diagnostic output.

`SSEDecoder` reconstructs stream events across arbitrary transport
chunks; tool-call arguments can arrive as fragments. The parser applies
event/total response size limits, checks JSON, preserves exact tool
IDs, decodes wire names to canonical names and rejects collisions.
It also requires a valid stream completion marker.

These measures protect transport/parsing integrity. They still do not
authorize the requested tool or certify remote content as truthful.

## 4. Evidence-driven device proposals

[`DeviceAgent.cppm`](../DeviceAgent.cppm) accepts host-supplied
gesture, speech, screen, audio cue and selection evidence. Each
`ActionBinding` is scoped to a specific active application and intent,
with required modalities and confidence threshold. `IntentRouter`
returns a proposal, not a side effect.

`ActionContract` includes preconditions, expected effects, risk,
approval policy, reversibility, rollback and bounded verification.
High-risk/irreversible actions require stronger host approval. A
`ReplayRecord` ties the proposed call to the adapter receipt and
observed-state fingerprint so the host can audit what happened.

**Exercise:** produce the same intent in a different active application,
drop one required evidence modality and lower confidence. Verify
proposal rejection occurs before host authorization. Then test
verification/rollback on a *mock* reversible action.

## 5. Host trust boundaries and tests

Use [`MockAIProvider.cppm`](../MockAIProvider.cppm) to test the
approval matrix without live credentials/network access. Read
[`tests/KairoAITests.cpp`](../tests/KairoAITests.cpp),
[`tests/DeviceAgentTests.cpp`](../tests/DeviceAgentTests.cpp)
and [`tests/KairoAICloudTests.cpp`](../tests/KairoAICloudTests.cpp).

Additional threats to test: untrusted project text instructing the
model to change behavior, tool-name spoofing, stale approvals,
argument changes, malicious stream fragments, transport failures,
schema-invalid effects and callback exceptions.

Tool capability restrictions must be enforced by the *actual host
handler* as well as by this policy registry; a C++ authorization
result is not an OS sandbox. The Editor/Hub remain responsible for
credentials and filesystem/process/network boundaries.

*Evidence note: source-learning guide. No live API credentials,
provider traffic or device actions were used in this documentation pass.*
