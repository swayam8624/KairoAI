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

It does **not** currently claim a cloud provider, editor agent, gameplay behavior
tree, or local model execution. Those integrate in separately tested layers.

## Build

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
cmake --build build
ctest --test-dir build --output-on-failure
```

## Architecture

```text
Kairo.AI.Contracts      bounded persistent request/response values
Kairo.AI.Provider       synchronous provider boundary + owned async task
Kairo.AI.ToolPolicy     capability modes, exact approvals, host tool registry
Kairo.AI.MockProvider   deterministic scripted provider for tests
Kairo.AI                public umbrella module
```

Providers execute on a worker owned by `RequestTask`; render, physics, and UI
threads never need to wait for inference. Destroying the task requests stop and
joins the worker, so stream callbacks cannot outlive their owner.

Tool calls are data, not authority. A future editor integration must translate
them into typed editor commands, show a preview/diff, request permission, and
retain undo and provenance. Project text is always untrusted input.

Credentials are outside this core and must come from the OS keychain or process
environment. They must never be serialized into Kairo projects or logs.
