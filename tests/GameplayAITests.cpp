#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

import Kairo.AI.Gameplay;

namespace gameplay = kairo::ai::gameplay;

TEST_CASE("Gameplay navigation uses deterministic bounded A star",
    "[KairoAI][Gameplay][Navigation]")
{
    gameplay::NavigationGraph graph;
    graph.AddNode(1u, { 0.0, 0.0, 0.0 });
    graph.AddNode(2u, { 1.0, 0.0, 0.0 });
    graph.AddNode(3u, { 2.0, 0.0, 0.0 });
    graph.AddNode(4u, { 1.0, 0.0, 2.0 });
    graph.AddBidirectionalEdge(1u, 2u);
    graph.AddBidirectionalEdge(2u, 3u);
    graph.AddBidirectionalEdge(1u, 4u);
    graph.AddBidirectionalEdge(4u, 3u);

    const auto direct = graph.FindPath(1u, 3u);
    REQUIRE(direct.Found());
    CHECK(direct.Nodes == std::vector<gameplay::NavNodeID>{ 1u, 2u, 3u });
    CHECK(direct.Cost == 2.0);

    graph.SetEnabled(2u, false);
    const auto detour = graph.FindPath(1u, 3u);
    REQUIRE(detour.Found());
    CHECK(detour.Nodes == std::vector<gameplay::NavNodeID>{ 1u, 4u, 3u });
    CHECK(detour.Cost > direct.Cost);

    REQUIRE_THROWS_AS(graph.FindPath(1u, 3u,
        gameplay::NavigationQuery{ .MaximumVisitedNodes = 1u }), std::runtime_error);
}

TEST_CASE("Gameplay blackboard validates typed bounded state",
    "[KairoAI][Gameplay][Blackboard]")
{
    gameplay::Blackboard blackboard;
    blackboard.Set("alert", true);
    blackboard.Set("target", gameplay::EntityReference{ 42u });
    blackboard.Set("destination", gameplay::WorldPosition{ 4.0, 0.0, -2.0 });
    CHECK(blackboard.Get<bool>("alert"));
    CHECK(blackboard.Get<gameplay::EntityReference>("target").Value == 42u);
    CHECK(blackboard.Get<gameplay::WorldPosition>("destination").X == 4.0);
    REQUIRE_THROWS_AS(blackboard.Get<double>("alert"), std::invalid_argument);
}

TEST_CASE("Behavior tree preserves running composite progress",
    "[KairoAI][Gameplay][Behavior]")
{
    gameplay::BehaviorTree tree;
    gameplay::Blackboard blackboard;
    unsigned actionTicks = 0u;
    unsigned completionTicks = 0u;

    const auto condition = tree.AddCondition([](gameplay::Blackboard&,
        const gameplay::BehaviorContext&) { return gameplay::BehaviorStatus::Success; });
    const auto action = tree.AddAction([&](gameplay::Blackboard&,
        const gameplay::BehaviorContext&)
    {
        ++actionTicks;
        return actionTicks == 1u
            ? gameplay::BehaviorStatus::Running
            : gameplay::BehaviorStatus::Success;
    });
    const auto completion = tree.AddAction([&](gameplay::Blackboard&,
        const gameplay::BehaviorContext&)
    {
        ++completionTicks;
        return gameplay::BehaviorStatus::Success;
    });
    const auto root = tree.AddSequence({ condition, action, completion });
    tree.SetRoot(root);

    CHECK(tree.Tick(blackboard, { 0.016, 1.0 }) == gameplay::BehaviorStatus::Running);
    CHECK(actionTicks == 1u);
    CHECK(completionTicks == 0u);

    CHECK(tree.Tick(blackboard, { 0.016, 1.016 }) == gameplay::BehaviorStatus::Success);
    CHECK(actionTicks == 2u);
    CHECK(completionTicks == 1u);
}

TEST_CASE("Behavior selector falls back after failed branch",
    "[KairoAI][Gameplay][Behavior]")
{
    gameplay::BehaviorTree tree;
    gameplay::Blackboard blackboard;
    const auto unavailable = tree.AddCondition([](gameplay::Blackboard&,
        const gameplay::BehaviorContext&) { return gameplay::BehaviorStatus::Failure; });
    const auto idle = tree.AddAction([](gameplay::Blackboard&,
        const gameplay::BehaviorContext&) { return gameplay::BehaviorStatus::Success; });
    tree.SetRoot(tree.AddSelector({ unavailable, idle }));
    CHECK(tree.Tick(blackboard, {}) == gameplay::BehaviorStatus::Success);
}

TEST_CASE("Perception memory is bounded age-aware and strength-aware",
    "[KairoAI][Gameplay][Perception]")
{
    gameplay::PerceptionMemory memory(3u, 10.0);
    memory.Remember({ gameplay::StimulusKind::Sound, { 1u }, { 0.0, 0.0, 0.0 }, 0.5, 1.0 });
    memory.Remember({ gameplay::StimulusKind::Sound, { 2u }, { 5.0, 0.0, 0.0 }, 2.0, 8.0 });
    memory.Remember({ gameplay::StimulusKind::Sight, { 3u }, { 1.0, 0.0, 0.0 }, 4.0, 9.0 });

    const auto strongest = memory.Strongest(gameplay::StimulusKind::Sound, 10.0);
    REQUIRE(strongest.has_value());
    CHECK(strongest->Source.Value == 2u);

    memory.Prune(12.0);
    CHECK(memory.Entries().size() == 2u);
}
