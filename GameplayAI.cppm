module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
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

    [[nodiscard]] inline double Distance(WorldPosition a, WorldPosition b) noexcept
    {
        const double x = a.X - b.X;
        const double y = a.Y - b.Y;
        const double z = a.Z - b.Z;
        return std::sqrt(x * x + y * y + z * z);
    }

    using NavNodeID = std::uint32_t;
    inline constexpr NavNodeID InvalidNavNode = 0u;

    struct NavigationPath final
    {
        std::vector<NavNodeID> Nodes;
        double Cost = 0.0;
        std::size_t VisitedNodes = 0u;

        [[nodiscard]] bool Found() const noexcept { return !Nodes.empty(); }
    };

    struct NavigationQuery final
    {
        std::size_t MaximumVisitedNodes = 65'536u;
        double HeuristicWeight = 1.0;

        void Validate() const
        {
            if (MaximumVisitedNodes == 0u)
                throw std::invalid_argument("Navigation query must permit at least one visited node.");
            if (!std::isfinite(HeuristicWeight) || HeuristicWeight < 0.0 || HeuristicWeight > 4.0)
                throw std::invalid_argument("Navigation heuristic weight must be finite and in [0, 4].");
        }
    };

    /// Deterministic sparse navigation graph used by gameplay agents. The graph
    /// deliberately owns only topology and traversal cost; navmesh baking,
    /// streaming, crowd avoidance, and animation remain independent adapters.
    class NavigationGraph final
    {
        struct Edge final
        {
            NavNodeID To = InvalidNavNode;
            double Cost = 0.0;
        };

        struct Node final
        {
            WorldPosition Position{};
            bool Enabled = true;
            std::vector<Edge> Edges;
        };

    public:
        void AddNode(NavNodeID id, WorldPosition position)
        {
            if (id == InvalidNavNode)
                throw std::invalid_argument("Navigation node zero is reserved as invalid.");
            if (!position.IsFinite())
                throw std::invalid_argument("Navigation node position must be finite.");
            if (!m_Nodes.emplace(id, Node{ position }).second)
                throw std::invalid_argument("Navigation node ID is already registered.");
        }

        void SetEnabled(NavNodeID id, bool enabled)
        {
            RequireNode(id).Enabled = enabled;
        }

        [[nodiscard]] bool IsEnabled(NavNodeID id) const
        {
            return RequireNode(id).Enabled;
        }

        [[nodiscard]] WorldPosition Position(NavNodeID id) const
        {
            return RequireNode(id).Position;
        }

        void AddDirectedEdge(NavNodeID from, NavNodeID to,
            std::optional<double> traversalCost = std::nullopt)
        {
            if (from == to)
                throw std::invalid_argument("Navigation self-edges are not permitted.");
            Node& source = RequireNode(from);
            const Node& destination = RequireNode(to);
            const double cost = traversalCost.value_or(
                Distance(source.Position, destination.Position));
            if (!std::isfinite(cost) || cost <= 0.0)
                throw std::invalid_argument("Navigation edge cost must be finite and positive.");
            if (std::ranges::any_of(source.Edges,
                [to](const Edge& edge) { return edge.To == to; }))
                throw std::invalid_argument("Navigation edge is already registered.");
            source.Edges.push_back({ to, cost });
            std::ranges::sort(source.Edges, {}, &Edge::To);
        }

        void AddBidirectionalEdge(NavNodeID a, NavNodeID b,
            std::optional<double> traversalCost = std::nullopt)
        {
            AddDirectedEdge(a, b, traversalCost);
            try { AddDirectedEdge(b, a, traversalCost); }
            catch (...)
            {
                auto& edges = RequireNode(a).Edges;
                std::erase_if(edges, [b](const Edge& edge) { return edge.To == b; });
                throw;
            }
        }

        [[nodiscard]] std::size_t NodeCount() const noexcept { return m_Nodes.size(); }

        [[nodiscard]] NavigationPath FindPath(NavNodeID start, NavNodeID goal,
            NavigationQuery query = {}) const
        {
            query.Validate();
            const Node& startNode = RequireNode(start);
            const Node& goalNode = RequireNode(goal);
            if (!startNode.Enabled || !goalNode.Enabled) return {};
            if (start == goal) return { { start }, 0.0, 1u };

            struct OpenEntry final
            {
                NavNodeID Node = InvalidNavNode;
                double G = 0.0;
                double F = 0.0;
            };
            struct Worse final
            {
                bool operator()(const OpenEntry& a, const OpenEntry& b) const noexcept
                {
                    if (a.F != b.F) return a.F > b.F;
                    if (a.G != b.G) return a.G > b.G;
                    return a.Node > b.Node;
                }
            };

            std::priority_queue<OpenEntry, std::vector<OpenEntry>, Worse> open;
            std::map<NavNodeID, double> bestCost;
            std::map<NavNodeID, NavNodeID> parent;
            std::set<NavNodeID> closed;
            bestCost[start] = 0.0;
            open.push({ start, 0.0,
                query.HeuristicWeight * Distance(startNode.Position, goalNode.Position) });

            while (!open.empty())
            {
                const OpenEntry current = open.top();
                open.pop();
                const auto best = bestCost.find(current.Node);
                if (best == bestCost.end() || current.G > best->second) continue;
                if (closed.contains(current.Node)) continue;
                closed.insert(current.Node);
                if (closed.size() > query.MaximumVisitedNodes)
                    throw std::runtime_error("Navigation query exceeded its visited-node budget.");

                if (current.Node == goal)
                {
                    NavigationPath result;
                    result.Cost = current.G;
                    result.VisitedNodes = closed.size();
                    for (NavNodeID cursor = goal;; cursor = parent.at(cursor))
                    {
                        result.Nodes.push_back(cursor);
                        if (cursor == start) break;
                    }
                    std::ranges::reverse(result.Nodes);
                    return result;
                }

                const Node& node = RequireNode(current.Node);
                for (const Edge& edge : node.Edges)
                {
                    const Node& next = RequireNode(edge.To);
                    if (!next.Enabled || closed.contains(edge.To)) continue;
                    const double candidate = current.G + edge.Cost;
                    const auto known = bestCost.find(edge.To);
                    if (known != bestCost.end() && candidate >= known->second) continue;
                    bestCost[edge.To] = candidate;
                    parent[edge.To] = current.Node;
                    const double heuristic = Distance(next.Position, goalNode.Position);
                    open.push({ edge.To, candidate,
                        candidate + query.HeuristicWeight * heuristic });
                }
            }
            return { {}, 0.0, closed.size() };
        }

    private:
        std::map<NavNodeID, Node> m_Nodes;

        [[nodiscard]] Node& RequireNode(NavNodeID id)
        {
            const auto found = m_Nodes.find(id);
            if (found == m_Nodes.end())
                throw std::out_of_range("Navigation node does not exist.");
            return found->second;
        }

        [[nodiscard]] const Node& RequireNode(NavNodeID id) const
        {
            const auto found = m_Nodes.find(id);
            if (found == m_Nodes.end())
                throw std::out_of_range("Navigation node does not exist.");
            return found->second;
        }
    };

    struct EntityReference final
    {
        std::uint64_t Value = 0u;
        friend constexpr bool operator==(const EntityReference&, const EntityReference&) noexcept = default;
    };

    using BlackboardValue = std::variant<bool, std::int64_t, double,
        std::string, WorldPosition, EntityReference>;

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
            return m_Values.contains(std::string(key));
        }

        bool Remove(std::string_view key)
        {
            return m_Values.erase(std::string(key)) != 0u;
        }

        template<typename T>
        [[nodiscard]] const T& Get(std::string_view key) const
        {
            const auto found = m_Values.find(std::string(key));
            if (found == m_Values.end())
                throw std::out_of_range("Gameplay blackboard key does not exist.");
            const auto value = std::get_if<T>(&found->second);
            if (value == nullptr)
                throw std::invalid_argument("Gameplay blackboard value has a different type.");
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
                throw std::invalid_argument("Gameplay blackboard key length is invalid.");
            if (!m_Values.contains(key) && m_Values.size() >= MaximumEntries)
                throw std::length_error("Gameplay blackboard exceeds its entry budget.");
        }

        static void ValidateValue(const BlackboardValue& value)
        {
            std::visit([](const auto& entry)
            {
                using T = std::decay_t<decltype(entry)>;
                if constexpr (std::is_same_v<T, double>)
                {
                    if (!std::isfinite(entry))
                        throw std::invalid_argument("Gameplay blackboard number must be finite.");
                }
                else if constexpr (std::is_same_v<T, std::string>)
                {
                    if (entry.size() > MaximumStringBytes)
                        throw std::length_error("Gameplay blackboard string exceeds its byte budget.");
                }
                else if constexpr (std::is_same_v<T, WorldPosition>)
                {
                    if (!entry.IsFinite())
                        throw std::invalid_argument("Gameplay blackboard position must be finite.");
                }
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
    using BehaviorLeaf = std::function<BehaviorStatus(Blackboard&, const BehaviorContext&)>;

    /// Stateful deterministic behavior tree. Sequence and selector cursors are
    /// retained while children return Running, so long-lived actions do not
    /// restart every frame. Reset() explicitly cancels composite progress.
    class BehaviorTree final
    {
        enum class NodeKind : std::uint8_t { Action, Condition, Sequence, Selector, Inverter };
        struct Node final
        {
            NodeKind Kind = NodeKind::Action;
            BehaviorLeaf Leaf;
            std::vector<BehaviorNodeID> Children;
        };

    public:
        [[nodiscard]] BehaviorNodeID AddAction(BehaviorLeaf action)
        {
            if (!action) throw std::invalid_argument("Behavior action callback is required.");
            return AddNode({ NodeKind::Action, std::move(action), {} });
        }

        [[nodiscard]] BehaviorNodeID AddCondition(BehaviorLeaf condition)
        {
            if (!condition) throw std::invalid_argument("Behavior condition callback is required.");
            return AddNode({ NodeKind::Condition, std::move(condition), {} });
        }

        [[nodiscard]] BehaviorNodeID AddSequence(std::vector<BehaviorNodeID> children)
        {
            ValidateChildren(children, false);
            return AddNode({ NodeKind::Sequence, {}, std::move(children) });
        }

        [[nodiscard]] BehaviorNodeID AddSelector(std::vector<BehaviorNodeID> children)
        {
            ValidateChildren(children, false);
            return AddNode({ NodeKind::Selector, {}, std::move(children) });
        }

        [[nodiscard]] BehaviorNodeID AddInverter(BehaviorNodeID child)
        {
            ValidateChildren({ child }, true);
            return AddNode({ NodeKind::Inverter, {}, { child } });
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
            const BehaviorStatus result = TickNode(m_Root, blackboard, context, 0u);
            if (result != BehaviorStatus::Running) m_Cursors.clear();
            return result;
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
            const BehaviorNodeID id = m_Next++;
            m_Nodes.emplace(id, std::move(node));
            return id;
        }

        void ValidateChildren(const std::vector<BehaviorNodeID>& children,
            bool exactlyOne) const
        {
            if ((exactlyOne && children.size() != 1u) || (!exactlyOne && children.empty()))
                throw std::invalid_argument("Behavior composite has an invalid child count.");
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
            const auto visit = [&](const auto& self, BehaviorNodeID id, std::size_t depth) -> void
            {
                if (depth > MaximumDepth)
                    throw std::length_error("Behavior tree exceeds its maximum depth.");
                if (visited.contains(id)) return;
                if (!visiting.insert(id).second)
                    throw std::invalid_argument("Behavior tree contains a cycle.");
                const Node& node = RequireNode(id);
                for (const auto child : node.Children) self(self, child, depth + 1u);
                visiting.erase(id);
                visited.insert(id);
            };
            visit(visit, root, 0u);
        }

        [[nodiscard]] BehaviorStatus TickNode(BehaviorNodeID id,
            Blackboard& blackboard, const BehaviorContext& context, std::size_t depth)
        {
            if (depth > MaximumDepth)
                throw std::length_error("Behavior execution exceeded its maximum depth.");
            const Node& node = RequireNode(id);
            switch (node.Kind)
            {
                case NodeKind::Action:
                    return node.Leaf(blackboard, context);
                case NodeKind::Condition:
                {
                    const BehaviorStatus result = node.Leaf(blackboard, context);
                    if (result == BehaviorStatus::Running)
                        throw std::logic_error("Behavior conditions cannot return Running.");
                    return result;
                }
                case NodeKind::Inverter:
                {
                    const BehaviorStatus result = TickNode(node.Children.front(), blackboard,
                        context, depth + 1u);
                    if (result == BehaviorStatus::Running) return result;
                    return result == BehaviorStatus::Success
                        ? BehaviorStatus::Failure : BehaviorStatus::Success;
                }
                case NodeKind::Sequence:
                {
                    std::size_t& cursor = m_Cursors[id];
                    while (cursor < node.Children.size())
                    {
                        const BehaviorStatus result = TickNode(node.Children[cursor],
                            blackboard, context, depth + 1u);
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
                    std::size_t& cursor = m_Cursors[id];
                    while (cursor < node.Children.size())
                    {
                        const BehaviorStatus result = TickNode(node.Children[cursor],
                            blackboard, context, depth + 1u);
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
            if (!Position.IsFinite() || !std::isfinite(Strength) || Strength < 0.0 ||
                !std::isfinite(TimeSeconds))
                throw std::invalid_argument("Gameplay perception stimulus is invalid.");
        }
    };

    /// Bounded short-term perception memory suitable for NPC sensory systems.
    /// Entries are ordered by arrival and pruned by age; stronger recent events
    /// can be queried without retaining unbounded world history.
    class PerceptionMemory final
    {
    public:
        explicit PerceptionMemory(std::size_t capacity = 128u,
            double maximumAgeSeconds = 10.0)
            : m_Capacity(capacity), m_MaximumAge(maximumAgeSeconds)
        {
            if (capacity == 0u || capacity > 16'384u)
                throw std::invalid_argument("Perception memory capacity is invalid.");
            if (!std::isfinite(maximumAgeSeconds) || maximumAgeSeconds <= 0.0)
                throw std::invalid_argument("Perception memory age must be finite and positive.");
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
            double bestScore = -1.0;
            for (const auto& stimulus : m_Entries)
            {
                const double age = nowSeconds - stimulus.TimeSeconds;
                if (age < 0.0 || age > m_MaximumAge || stimulus.Kind != kind) continue;
                const double recency = 1.0 - age / m_MaximumAge;
                const double score = stimulus.Strength * recency;
                if (!result.has_value() || score > bestScore ||
                    (score == bestScore && stimulus.TimeSeconds > result->TimeSeconds))
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
                throw std::invalid_argument("Perception query time must be finite.");
        }
    };
}
