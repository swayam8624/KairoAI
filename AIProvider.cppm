module;

#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>

export module Kairo.AI.Provider;

import Kairo.AI.Contracts;

export namespace kairo::ai
{
    using StreamSink = std::function<void(const StreamEvent&)>;

    class Provider
    {
    public:
        virtual ~Provider() = default;
        [[nodiscard]] virtual std::string_view Name() const noexcept = 0;

        /// Executes on the caller's worker thread. Implementations must observe
        /// stopToken between transport reads or inference steps and must emit
        /// events in provider order. The returned response is authoritative.
        [[nodiscard]] virtual Response Execute(const Request& request,
            const StreamSink& sink, std::stop_token stopToken) = 0;
    };

    /// Owns one asynchronous provider execution. Destruction requests stop and
    /// joins through std::jthread, preventing callbacks from outliving their
    /// task. Wait may be called once; Response remains deterministic thereafter.
    class RequestTask final
    {
    public:
        RequestTask(std::shared_ptr<Provider> provider, Request request,
            StreamSink sink = {}, RequestLimits limits = {})
        {
            if (!provider) throw std::invalid_argument("AI provider cannot be null.");
            ValidateRequest(request, limits);
            m_Future = m_Promise.get_future().share();
            m_Worker = std::jthread([provider = std::move(provider), request = std::move(request),
                sink = std::move(sink), promise = &m_Promise](std::stop_token stopToken) mutable
            {
                try { promise->set_value(provider->Execute(request, sink, stopToken)); }
                catch (...) { promise->set_exception(std::current_exception()); }
            });
        }

        RequestTask(const RequestTask&) = delete;
        RequestTask& operator=(const RequestTask&) = delete;
        RequestTask(RequestTask&&) = delete;
        RequestTask& operator=(RequestTask&&) = delete;

        void Cancel() noexcept { m_Worker.request_stop(); }
        [[nodiscard]] bool StopRequested() const noexcept { return m_Worker.get_stop_token().stop_requested(); }
        [[nodiscard]] bool Ready() const
        { return m_Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
        [[nodiscard]] Response Wait() const { return m_Future.get(); }

    private:
        std::promise<Response> m_Promise;
        std::shared_future<Response> m_Future;
        std::jthread m_Worker;
    };
}
