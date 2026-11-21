// TEACHING MAP — KairoAICloud.cppm
// Public cloud-adapter facade or composition that wires OpenAI-compatible provider and CPR
// transport behind the provider-neutral AIProvider contract. This is optional; core engine
// APIs should not require credentials.
// See READING_ORDER.md for public APIs, delegated execution and test contracts.

export module Kairo.AI.Cloud;

export import Kairo.AI.OpenAICompatible;
export import Kairo.AI.CprTransport;
