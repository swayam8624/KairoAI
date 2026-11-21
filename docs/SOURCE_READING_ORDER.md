# Teaching source map — KairoAI

Provider-neutral request, permission and tool execution boundaries.

Read these source links in order, identify the caller and callee for each public API, then check the error/ownership contract and its tests.

## Reading order

1. [`README.md`](../README.md)
2. [`CMakeLists.txt`](../CMakeLists.txt)
3. [`KairoAI.cppm`](../KairoAI.cppm)
4. [`AIContracts.cppm`](../AIContracts.cppm)
5. [`AIProvider.cppm`](../AIProvider.cppm)
6. [`MockAIProvider.cppm`](../MockAIProvider.cppm)
7. [`AIToolPolicy.cppm`](../AIToolPolicy.cppm)
8. [`DeviceAgent.cppm`](../DeviceAgent.cppm)
9. [`OpenAICompatibleProvider.cppm`](../OpenAICompatibleProvider.cppm)
10. [`KairoAICloud.cppm`](../KairoAICloud.cppm)
11. [`CprChatTransport.cppm`](../CprChatTransport.cppm)
12. [`tests`](../tests)
13. [`research`](../research)

## Repository integration and API contracts

Editor/Hub hosts approve actions; cloud adapter handles transport; tools cannot execute solely because the model requested them. Distinguish policy from transport and tests from real credentials.

## Teaching standards

- Comments explain invariants, decision reasons, lifetime, prerequisites and cross-file effects; they do not narrate obvious statements.
- Follow upstream interfaces before implementation details; distinguish implemented, host-verified and planned features.
- Run available tests at a pinned commit and consult [STATUS.md](../STATUS.md) / [STATUS.yaml](../STATUS.yaml) before declaring completion.
- Reuse original contributions under [LICENSE](../LICENSE), subject to third-party code and asset terms.


## Complete source-file inventory

After the curated teaching path, follow this deterministic inventory of first-party source, scripts, shaders, and tests. Grouping is a suggested learning order, not an indication that every function is already annotated. Vendored dependencies, ROMs, binaries, data, media, and generated evidence are excluded.

### Entry points and build (1)

1. [`CMakeLists.txt`](../CMakeLists.txt)

### Interfaces and modules (10)

1. [`AIContracts.cppm`](../AIContracts.cppm)
2. [`AIProvider.cppm`](../AIProvider.cppm)
3. [`AIToolPolicy.cppm`](../AIToolPolicy.cppm)
4. [`CprChatTransport.cppm`](../CprChatTransport.cppm)
5. [`DeviceAgent.cppm`](../DeviceAgent.cppm)
6. [`GameplayAI.cppm`](../GameplayAI.cppm)
7. [`KairoAI.cppm`](../KairoAI.cppm)
8. [`KairoAICloud.cppm`](../KairoAICloud.cppm)
9. [`MockAIProvider.cppm`](../MockAIProvider.cppm)
10. [`OpenAICompatibleProvider.cppm`](../OpenAICompatibleProvider.cppm)

### Verification and tests (4)

1. [`tests/DeviceAgentTests.cpp`](../tests/DeviceAgentTests.cpp)
2. [`tests/GameplayAITests.cpp`](../tests/GameplayAITests.cpp)
3. [`tests/KairoAICloudTests.cpp`](../tests/KairoAICloudTests.cpp)
4. [`tests/KairoAITests.cpp`](../tests/KairoAITests.cpp)

Snapshot totals: 15 first-party source/build/test files, 23 tracked files overall. Re-audit when files are renamed or added.
