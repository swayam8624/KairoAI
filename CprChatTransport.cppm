module;

#include <atomic>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>

#include <cpr/cpr.h>

export module Kairo.AI.CprTransport;

import Kairo.AI.OpenAICompatible;

export namespace kairo::ai
{
    class CprChatTransport final : public ChatStreamTransport
    {
    public:
        [[nodiscard]] ChatTransportResult Post(std::string_view url,
            const HeaderMap& headers, std::string_view body,
            std::uint32_t timeoutMilliseconds, const TransportChunkSink& chunkSink,
            std::stop_token stopToken) override
        {
            if (timeoutMilliseconds == 0u || timeoutMilliseconds > 2'147'483'647u)
                throw std::invalid_argument("CPR timeout is outside its positive 32-bit range.");
            cpr::Header cprHeaders;
            for (const auto& [name, value] : headers) cprHeaders.emplace(name, value);
            cpr::Session session;
            session.SetUrl(cpr::Url{ std::string(url) });
            session.SetHeader(cprHeaders);
            session.SetBody(cpr::Body{ std::string(body) });
            session.SetTimeout(cpr::Timeout{ static_cast<std::int32_t>(timeoutMilliseconds) });
            auto cancelled = std::make_shared<std::atomic_bool>(stopToken.stop_requested());
            session.SetCancellationParam(cancelled);
            std::stop_callback stopCallback(stopToken, [cancelled] { cancelled->store(true); });

            std::string responseBody;
            std::exception_ptr callbackFailure;
            session.SetWriteCallback(cpr::WriteCallback{
                [&](const std::string_view& chunk, std::intptr_t) -> bool
                {
                    if (cancelled->load()) return false;
                    if (responseBody.size() + chunk.size() <= 8u * 1024u * 1024u)
                        responseBody.append(chunk);
                    try { return chunkSink(chunk); }
                    catch (...) { callbackFailure = std::current_exception(); return false; }
                } });
            const cpr::Response response = session.Post();
            if (callbackFailure) std::rethrow_exception(callbackFailure);
            return { response.status_code, response.error.message, std::move(responseBody) };
        }
    };
}
