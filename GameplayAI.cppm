module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.AI.Gameplay;

export namespace kairo::ai::gameplay
{
    struct WorldPosition final
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;

        [[nodiscard]] bool IsFinite() const noexcept
        {
            return std::isfinite(X) && std::isfinite(Y) && std::isfinite(Z);
        }
    };

    struct EntityReference final
    {
        std::uint64_t Value = 0u;
        friend constexpr bool operator==(const EntityReference&,
            const EntityReference&) noexcept = default;
    };

    /// Movement request emitted by cognition. KairoSpatial remains the single
    /// owner of NavigationGraph/A*; GameEngine adapters translate this intent
    /// into the appropriate spatial path query and locomotion command.
    struct NavigationIntent final
    {
        WorldPosition Destination{};
        double AcceptanceRadius = 0.25;
        bool AllowPartialPath = false;

        void Validate() const
        {
            if (!Destination.IsFinite() || !std::isfinite(AcceptanceRadius) ||
                AcceptanceRadius < 0.0)
                throw std::invalid_argument("Gameplay navigation intent is invalid.");
        }
    };

    using BlackboardValue = std::variant<bool, std::int64_t, double,
        std::string, WorldPosition, EntityReference, NavigationIntent>;

    class Blackboard final
    {
    public:
        void Set(std::string key, BlackboardValue value)
        {
            ValidateKey(key);
            ValidateValue(value);
            m_Values.insert_or_assign(std::move(key), std::move(value));
        }

        [[nodiscard]] bool Contains(std::string_view key) const
        {
            return m_Values.contains(key);
        }

        bool Remove(std::string_view key)
        {
            return m_Values.erase(std::string(key)) != 0u;
        }

        template<typename T>
        [[nodiscard]] const T& Get(std::string_view key) const
        {
            const auto found = m_Values.find(key);
            if (found == m_Values.end())
                throw std::out_of_range("Gameplay blackboard key does not exist.");
            const auto* value = std::get_if<T>(&found->second);
            if (value == nullptr)
                throw std::invalid_argument(
                    "Gameplay blackboard value has a different type.");
            return *value;
        }

        template<typename T>
        [[nodiscard]] T& Get(std::string_view key)
        {
            const auto found = m_Values.find(key);
            if (found == m_Values.end())
                throw std::out_of_range("Gameplay blackboard key does not exist.");
            auto* value = std::get_if<T>(&found->second);
            if (value == nullptr)
                throw std::invalid_argument(
                    "Gameplay blackboard value has a different type.");
            return *value;
        }

        void Clear() noexcept { m_Values.clear(); }
        [[nodiscard]] std::size_t Size() const noexcept { return m_Values.size(); }

    private:
        static constexpr std::size_t MaximumEntries = 512u;
        static constexpr std::size_t MaximumKeyBytes = 128u;
        static constexpr std::size_t MaximumStringBytes = 4096u;
        std::map<std::string, BlackboardValue, std::less<>> m_Values;

        void ValidateKey(std::string_view key) const
        {
            if (key.empty() || key.size() > MaximumKeyBytes)
                throw std::invalid_argument(
                    "Gameplay blackboard key length is invalid.");
            if (!m_Values.contains(key) && m_Values.size() >= MaximumEntries)
                throw std::length_error(
                    "Gameplay blackboard exceeds its entry budget.");
        }

        static void ValidateValue(const BlackboardValue& value)
        {
            std::visit([](const auto& entry)
            {
                using T = std::decay_t<decltype(entry)>;
                if constexpr (std::is_same_v<T, double>)
                {
                    if (!std::isfinite(entry))
                        throw std::invalid_argument(
                            "Gameplay blackboard number must be finite.");
                }
                else if constexpr (std::is_same_v<T, std::string>)
                {
                    if (entry.size() > MaximumStringBytes)
                        throw std::length_error(
                            "Gameplay blackboard string exceeds its byte budget.");
                }
                else if constexpr (std::is_same_v<T, WorldPosition>)
                {
                    if (!entry.IsFinite())
                        throw std::invalid_argument(
                            "Gameplay blackboard position must be finite.");
                }
                else if constexpr (std::is_same_v<T, NavigationIntent>)
                    entry.Validate();
            }, value);
        }
    };

    enum class BehaviorStatus : std::uint8_t
    {
        Success,
        Failure,
        Running
    };

    struct BehaviorContext final
    {
        double DeltaSeconds = 0.0;
        double WorldTimeSeconds = 0.0;

        void Validate() const
        {
            if (!std::isfinite(DeltaSeconds) || DeltaSeconds < 0.0 ||
                !std::isfinite(WorldTimeSeconds))
                throw std::invalid_argument("Behavior context time values are invalid.");
        }
    };

    using BehaviorNodeID = std::uint32_t;
    inline constexpr BehaviorNodeID InvalidBehaviorNode = 0u;
    using BehaviorLeaf = std::function<BehaviorStatus(
        Blackboard&, const BehaviorContext&)>;

    /// Deterministic, stateful behavior tree. A composite cursor remains on the
    /// child that returned Running, allowing multi-frame actions to resume on
    /// the next simulation tick instead of restarting the tree each frame.
    class BehaviorTree final
    {
        enum class NodeKind : std::uint8_t
        {
            Action,
            Condition,
            Sequence,
            Selector,
            Inverter,
            Succeeder
        };

        struct Node final
        {
            NodeKind Kind = NodeKind::Action;
            BehaviorLeaf Leaf;
            std::vector<BehaviorNodeID> Children;
        };

    public:
        [[nodiscard]] BehaviorNodeID AddAction(BehaviorLeaf action)
        {
            if (!action)
                throw std::invalid_argument("Behavior action callback is required.");
            return AddNode({ NodeKind::Action, std::move(action), {} });
        }

        [[nodiscard]] BehaviorNodeID AddCondition(BehaviorLeaf condition)
        {
            if (!condition)
                throw std::invalid_argument("Behavior condition callback is required.");
            return AddNode({ NodeKind::Condition, std::move(condition), {} });
        }

        [[nodiscard]] BehaviorNodeID AddSequence(
            std::vector<BehaviorNodeID> children)
        {
            ValidateChildren(children, false);
            return AddNode({ NodeKind::Sequence, {}, std::move(children) });
        }

        [[nodiscard]] BehaviorNodeID AddSelector(
            std::vector<BehaviorNodeID> children)
        {
            ValidateChildren(children, false);
            return AddNode({ NodeKind::Selector, {}, std::move(children) });
        }

        [[nodiscard]] BehaviorNodeID AddInverter(BehaviorNodeID child)
        {
            ValidateChildren({ child }, true);
            return AddNode({ NodeKind::Inverter, {}, { child } });
        }

        [[nodiscard]] BehaviorNodeID AddSucceeder(BehaviorNodeID child)
        {
            ValidateChildren({ child }, true);
            return AddNode({ NodeKind::Succeeder, {}, { child } });
        }

        void SetRoot(BehaviorNodeID root)
        {
            (void)RequireNode(root);
            ValidateAcyclic(root);
            m_Root = root;
            Reset();
        }

        [[nodiscard]] BehaviorStatus Tick(Blackboard& blackboard,
            BehaviorContext context)
        {
            context.Validate();
            if (m_Root == InvalidBehaviorNode)
                throw std::logic_error("Behavior tree has no root node.");
            const auto status = TickNode(m_Root, blackboard, context, 0u);
            if (status != BehaviorStatus::Running) m_Cursors.clear();
            return status;
        }

        void Reset() noexcept { m_Cursors.clear(); }
        [[nodiscard]] std::size_t NodeCount() const noexcept { return m_Nodes.size(); }

    private:
        static constexpr std::size_t MaximumNodes = 4096u;
        static constexpr std::size_t MaximumDepth = 256u;
        BehaviorNodeID m_Next = 1u;
        BehaviorNodeID m_Root = InvalidBehaviorNode;
        std::map<BehaviorNodeID, Node> m_Nodes;
        std::map<BehaviorNodeID, std::size_t> m_Cursors;

        [[nodiscard]] BehaviorNodeID AddNode(Node node)
        {
            if (m_Nodes.size() >= MaximumNodes)
                throw std::length_error("Behavior tree exceeds its node budget.");
            if (m_Next == InvalidBehaviorNode)
                throw std::overflow_error("Behavior tree exhausted its node ID space.");
            const auto id = m_Next++;
            m_Nodes.emplace(id, std::move(node));
            return id;
        }

        void ValidateChildren(const std::vector<BehaviorNodeID>& children,
            bool exactlyOne) const
        {
            if ((exactlyOne && children.size() != 1u) ||
                (!exactlyOne && children.empty()))
                throw std::invalid_argument(
                    "Behavior composite has an invalid child count.");
            for (const auto child : children) (void)RequireNode(child);
        }

        [[nodiscard]] const Node& RequireNode(BehaviorNodeID id) const
        {
            const auto found = m_Nodes.find(id);
            if (found == m_Nodes.end())
                throw std::out_of_range("Behavior node does not exist.");
            return found->second;
        }

        void ValidateAcyclic(BehaviorNodeID root) const
        {
            std::set<BehaviorNodeID> visiting;
            std::set<BehaviorNodeID> visited;
            const auto visit = [&](const auto& self, BehaviorNodeID id,
                std::size_t depth) -> void
            {
                if (depth > MaximumDepth)
                    throw std::length_error(
                        "Behavior tree exceeds its maximum depth.");
                if (visited.contains(id)) return;
                if (!visiting.insert(id).second)
                    throw std::invalid_argument("Behavior tree contains a cycle.");
                for (const auto child : RequireNode(id).Children)
                    self(self, child, depth + 1u);
                visiting.erase(id);
                visited.insert(id);
            };
            visit(visit, root, 0u);
        }

        [[nodiscard]] BehaviorStatus TickNode(BehaviorNodeID id,
            Blackboard& blackboard, const BehaviorContext& context,
            std::size_t depth)
        {
            if (depth > MaximumDepth)
                throw std::length_error(
                    "Behavior execution exceeded its maximum depth.");
            const Node& node = RequireNode(id);
            switch (node.Kind)
            {
                case NodeKind::Action:
                    return node.Leaf(blackboard, context);

                case NodeKind::Condition:
                {
                    const auto result = node.Leaf(blackboard, context);
                    if (result == BehaviorStatus::Running)
                        throw std::logic_error(
                            "Behavior conditions cannot return Running.");
                    return result;
                }

                case NodeKind::Inverter:
                {
                    const auto result = TickNode(node.Children.front(), blackboard,
                        context, depth + 1u);
                    if (result == BehaviorStatus::Running) return result;
                    return result == BehaviorStatus::Success
                        ? BehaviorStatus::Failure : BehaviorStatus::Success;
                }

                case NodeKind::Succeeder:
                {
                    const auto result = TickNode(node.Children.front(), blackboard,
                        context, depth + 1u);
                    return result == BehaviorStatus::Running
                        ? BehaviorStatus::Running : BehaviorStatus::Success;
                }

                case NodeKind::Sequence:
                {
                    auto& cursor = m_Cursors[id];
                    while (cursor < node.Children.size())
                    {
                        const auto result = TickNode(node.Children[cursor], blackboard,
                            context, depth + 1u);
                        if (result == BehaviorStatus::Running) return result;
                        if (result == BehaviorStatus::Failure)
                        {
                            cursor = 0u;
                            return result;
                        }
                        ++cursor;
                    }
                    cursor = 0u;
                    return BehaviorStatus::Success;
                }

                case NodeKind::Selector:
                {
                    auto& cursor = m_Cursors[id];
                    while (cursor < node.Children.size())
                    {
                        const auto result = TickNode(node.Children[cursor], blackboard,
                            context, depth + 1u);
                        if (result == BehaviorStatus::Running) return result;
                        if (result == BehaviorStatus::Success)
                        {
                            cursor = 0u;
                            return result;
                        }
                        ++cursor;
                    }
                    cursor = 0u;
                    return BehaviorStatus::Failure;
                }
            }
            throw std::logic_error("Behavior node kind is invalid.");
        }
    };

    struct UtilityOption final
    {
        std::string Name;
        std::function<double(const Blackboard&, const BehaviorContext&)> Score;
        BehaviorLeaf Execute;
    };

    /// Utility scoring complements behavior trees for choices such as attack,
    /// flee, investigate, or seek cover. Ties resolve by registration order.
    class UtilitySelector final
    {
    public:
        void Add(UtilityOption option)
        {
            if (option.Name.empty() || option.Name.size() > 128u ||
                !option.Score || !option.Execute)
                throw std::invalid_argument("Utility option is incomplete.");
            if (m_Options.size() >= 256u)
                throw std::length_error("Utility selector exceeds its option budget.");
            if (std::ranges::any_of(m_Options, [&](const UtilityOption& existing)
                { return existing.Name == option.Name; }))
                throw std::invalid_argument(
                    "Utility option name is already registered.");
            m_Options.push_back(std::move(option));
        }

        [[nodiscard]] std::optional<std::string_view> Select(
            const Blackboard& blackboard, BehaviorContext context) const
        {
            const auto best = BestIndex(blackboard, context);
            if (!best.has_value()) return std::nullopt;
            return m_Options[*best].Name;
        }

        [[nodiscard]] BehaviorStatus TickBest(Blackboard& blackboard,
            BehaviorContext context) const
        {
            const auto best = BestIndex(blackboard, context);
            if (!best.has_value()) return BehaviorStatus::Failure;
            return m_Options[*best].Execute(blackboard, context);
        }

        [[nodiscard]] std::size_t Size() const noexcept { return m_Options.size(); }

    private:
        std::vector<UtilityOption> m_Options;

        [[nodiscard]] std::optional<std::size_t> BestIndex(
            const Blackboard& blackboard, BehaviorContext context) const
        {
            context.Validate();
            std::optional<std::size_t> best;
            std::optional<double> bestScore;
            for (std::size_t index = 0u; index < m_Options.size(); ++index)
            {
                const double score = m_Options[index].Score(blackboard, context);
                if (!std::isfinite(score))
                    throw std::runtime_error(
                        "Utility option returned a non-finite score.");
                if (!bestScore.has_value() || score > *bestScore)
                {
                    best = index;
                    bestScore = score;
                }
            }
            return best;
        }
    };

    enum class StimulusKind : std::uint8_t
    {
        Sight,
        Sound,
        Damage,
        Touch,
        Scripted
    };

    struct Stimulus final
    {
        StimulusKind Kind = StimulusKind::Sight;
        EntityReference Source{};
        WorldPosition Position{};
        double Strength = 1.0;
        double TimeSeconds = 0.0;

        void Validate() const
        {
            if (!Position.IsFinite() || !std::isfinite(Strength) ||
                Strength < 0.0 || !std::isfinite(TimeSeconds))
                throw std::invalid_argument(
                    "Gameplay perception stimulus is invalid.");
        }
    };

    /// Bounded short-term sensory memory. Strongest() applies linear recency
    /// decay, giving cognition a stable way to prefer recent/high-salience input.
    class PerceptionMemory final
    {
    public:
        explicit PerceptionMemory(std::size_t capacity = 128u,
            double maximumAgeSeconds = 10.0)
            : m_Capacity(capacity), m_MaximumAge(maximumAgeSeconds)
        {
            if (capacity == 0u || capacity > 16'384u)
                throw std::invalid_argument(
                    "Perception memory capacity is invalid.");
            if (!std::isfinite(maximumAgeSeconds) || maximumAgeSeconds <= 0.0)
                throw std::invalid_argument(
                    "Perception memory age must be finite and positive.");
        }

        void Remember(Stimulus stimulus)
        {
            stimulus.Validate();
            if (m_Entries.size() == m_Capacity) m_Entries.erase(m_Entries.begin());
            m_Entries.push_back(std::move(stimulus));
        }

        void Prune(double nowSeconds)
        {
            ValidateNow(nowSeconds);
            std::erase_if(m_Entries, [this, nowSeconds](const Stimulus& stimulus)
            {
                return nowSeconds - stimulus.TimeSeconds > m_MaximumAge;
            });
        }

        [[nodiscard]] std::optional<Stimulus> Strongest(StimulusKind kind,
            double nowSeconds) const
        {
            ValidateNow(nowSeconds);
            std::optional<Stimulus> result;
            std::optional<double> bestScore;
            for (const auto& stimulus : m_Entries)
            {
                const double age = nowSeconds - stimulus.TimeSeconds;
                if (age < 0.0 || age > m_MaximumAge || stimulus.Kind != kind)
                    continue;
                const double score = stimulus.Strength *
                    (1.0 - age / m_MaximumAge);
                if (!bestScore.has_value() || score > *bestScore ||
                    (score == *bestScore &&
                        stimulus.TimeSeconds > result->TimeSeconds))
                {
                    result = stimulus;
                    bestScore = score;
                }
            }
            return result;
        }

        [[nodiscard]] const std::vector<Stimulus>& Entries() const noexcept
        {
            return m_Entries;
        }

    private:
        std::size_t m_Capacity;
        double m_MaximumAge;
        std::vector<Stimulus> m_Entries;

        static void ValidateNow(double nowSeconds)
        {
            if (!std::isfinite(nowSeconds))
                throw std::invalid_argument(
                    "Perception query time must be finite.");
        }
    };
}
