# KairoAI — source reading path and API interactions

Start with request/provider contracts, deterministic mock provider and tool permissions, then streaming/HTTPS cloud adapters and device-agent intent routing. Tool calls from model output are *data*, never authority; host approval is separate. Gameplay AI is a distinct optional layer.

## Key interfaces and cross-repository calls

AIProvider APIs produce bounded responses; AIToolPolicy gates exact side effects, DeviceAgent turns observations into proposals and receipts, while OpenAICompatibleProvider/CprChatTransport implement cloud streaming behind a transport boundary. Tests use mock/injected transport; they do not require credentials.

## Read the source and tests in this order (14 files)

1. [`KairoAI.cppm`](KairoAI.cppm) — inspect its callers and imports, owned resources, validated inputs, failure states, and matching tests.
2. [`AIContracts.cppm`](AIContracts.cppm) — inspect its callers and imports, owned resources, validated inputs, failure states, and matching tests.
3. [`AIProvider.cppm`](AIProvider.cppm) — inspect its callers and imports, owned resources, validated inputs, failure states, and matching tests.
4. [`MockAIProvider.cppm`](MockAIProvider.cppm) — inspect its callers and imports, owned resources, validated inputs, failure states, and matching tests.
5. [`AIToolPolicy.cppm`](AIToolPolicy.cppm) — inspect its callers and imports, owned resources, validated inputs, failure states, and matching tests.
6. [`DeviceAgent.cppm`](DeviceAgent.cppm) — inspect its callers and imports, owned resources, validated inputs, failure states, and matching tests.
7. [`OpenAICompatibleProvider.cppm`](OpenAICompatibleProvider.cppm) — inspect its callers and imports, owned resources, validated inputs, failure states, and matching tests.
8. [`CprChatTransport.cppm`](CprChatTransport.cppm) — inspect its callers and imports, owned resources, validated inputs, failure states, and matching tests.
9. [`KairoAICloud.cppm`](KairoAICloud.cppm) — inspect its callers and imports, owned resources, validated inputs, failure states, and matching tests.
10. [`GameplayAI.cppm`](GameplayAI.cppm) — inspect its callers and imports, owned resources, validated inputs, failure states, and matching tests.
11. [`tests/DeviceAgentTests.cpp`](tests/DeviceAgentTests.cpp) — inspect its callers and imports, owned resources, validated inputs, failure states, and matching tests.
12. [`tests/GameplayAITests.cpp`](tests/GameplayAITests.cpp) — inspect its callers and imports, owned resources, validated inputs, failure states, and matching tests.
13. [`tests/KairoAICloudTests.cpp`](tests/KairoAICloudTests.cpp) — inspect its callers and imports, owned resources, validated inputs, failure states, and matching tests.
14. [`tests/KairoAITests.cpp`](tests/KairoAITests.cpp) — inspect its callers and imports, owned resources, validated inputs, failure states, and matching tests.

## Licensing and verification

This repository already has its own [LICENSE](LICENSE). The existing MIT terms permit learning, modification, redistribution and further development of the original code. Native DCC app APIs and third-party assets retain their own conditions. Read [README](README.md) for host-specific setup; pure tests cannot establish all native integration gates. Further in-file function annotations will complement this guide.
