// SOURCE STUDY NOTE:
// Core umbrella module exports contracts, provider, policy, mock, device-agent and gameplay APIs
// independently of optional cloud dependencies.
// See docs/SOURCE_READING_ORDER.md.

export module Kairo.AI;

export import Kairo.AI.Contracts;
export import Kairo.AI.Provider;
export import Kairo.AI.ToolPolicy;
export import Kairo.AI.MockProvider;
export import Kairo.AI.DeviceAgent;
export import Kairo.AI.Gameplay;
