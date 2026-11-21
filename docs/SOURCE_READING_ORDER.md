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
