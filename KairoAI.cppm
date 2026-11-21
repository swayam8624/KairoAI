// TEACHING MAP — KairoAI.cppm
// Umbrella C++23 module presenting the public AI contracts/provider/policy APIs. The
// engine Editor and optional runtime services import it without inheriting a specific
// cloud service or unauthorized action bridge.
// See READING_ORDER.md for cross-module API flow, tests and safety boundaries.

export module Kairo.AI;

export import Kairo.AI.Contracts;
export import Kairo.AI.Provider;
export import Kairo.AI.ToolPolicy;
export import Kairo.AI.MockProvider;
export import Kairo.AI.DeviceAgent;
export import Kairo.AI.Gameplay;
