# KairoAI

KairoAI is the provider-neutral AI boundary for Kairo development tools and
optional game services. It deliberately separates request orchestration from
editor permissions, cloud transports, local inference, and gameplay policy.

## Current Status

The initial executable foundation includes:

- bounded messages, tools, context bytes, output tokens, and temperature;
- ordered text and structured-tool streaming;
- cooperative cancellation with owned `std::jthread` lifetime;
- provider-reported response and usage contracts;
- a deterministic, credential-free mock provider for CI.
- Ask/Plan/Agent capability policy with exact-call approvals;
- a host-owned tool registry that never invokes denied side effects.
- a local device-agent intent router that converts verified gesture, speech,
  screen, audio, and selection evidence into registered app-action proposals.
- adapter-neutral execution receipts, state verification results, and exact-call
  replay records for local audit and correction curation.

The optional `KairoAICloud` target provides a real OpenAI-compatible Chat
Completions adapter over pinned CPR and nlohmann/json revisions. It supports
fragmented SSE text, tool-call assembly, usage, cancellation, HTTPS enforcement,
bounded responses, and secret-safe errors. Tests use an injected transport and
never require credentials or network access.

It does **not** currently claim an editor chat panel, gameplay behavior tree, or
local model execution. Those integrate in separately tested layers.

## Build

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
cmake --build build
ctest --test-dir build --output-on-failure
```

The default core build fetches no cloud transport dependency. Build and test
the optional pinned CPR adapter explicitly when cloud access is required:

```bash
cmake -S . -B build-cloud -G Ninja \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DKAIRO_AI_BUILD_CLOUD_PROVIDER=ON
cmake --build build-cloud
ctest --test-dir build-cloud --output-on-failure
```

## Architecture

```text
Kairo.AI.Contracts      bounded persistent request/response values
Kairo.AI.Provider       synchronous provider boundary + owned async task
Kairo.AI.ToolPolicy     capability modes, exact approvals, host tool registry
Kairo.AI.DeviceAgent    multimodal evidence to app-specific action proposals
Kairo.AI.MockProvider   deterministic scripted provider for tests
Kairo.AI                public umbrella module
Kairo.AI.OpenAICompatible  validated Chat Completions stream protocol
Kairo.AI.CprTransport      pinned production HTTPS transport
Kairo.AI.Cloud             optional cloud umbrella module
```

Providers execute on a worker owned by `RequestTask`; render, physics, and UI
threads never need to wait for inference. Destroying the task requests stop and
joins the worker, so stream callbacks cannot outlive their owner.

Tool calls are data, not authority. A future editor integration must translate
them into typed editor commands, show a preview/diff, request permission, and
retain undo and provenance. Project text is always untrusted input.

`Kairo.AI.DeviceAgent` is deliberately not a screen scraper, an OS automation
adapter, or a language-model control loop. Host-owned perception adapters emit
typed evidence with calibrated confidence; host-owned app adapters construct
canonical arguments only after confirming visible application state. The router
then creates a `ToolCall` only when a registered action, active application,
required evidence, and threshold all match. The normal `ToolPolicy` approval
path remains mandatory before any mutation runs. An adapter reports an
`ActionReceipt`; independently observed post-action state produces an
`ActionVerification`; `MakeReplayRecord` refuses to join records whose call IDs
do not exactly match the original proposal.

Credentials are outside this core and must come from the OS keychain or process
environment. They must never be serialized into Kairo projects or logs.
