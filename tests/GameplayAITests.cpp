#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <string_view>

import Kairo.AI.Gameplay;

namespace gameplay = kairo::ai::gameplay;

TEST_CASE("Gameplay blackboard carries validated navigation intent without duplicating pathfinding",
    "[KairoAI][Gameplay][Blackboard]")
{
    gameplay::Blackboard blackboard;
    blackboard.Set("alert", true);
    blackboard.Set("target", gameplay::EntityReference{ 42u });
    blackboard.Set("move", gameplay::NavigationIntent{
        .Destination = { 4.0, 0.0, -2.0 },
        .AcceptanceRadius = 0.75,
        .AllowPartialPath = true
    });

    CHECK(blackboard.Get<bool>("alert"));
    CHECK(blackboard.Get<gameplay::EntityReference>("target").Value == 42u);
    const auto& move = blackboard.Get<gameplay::NavigationIntent>("move");
    CHECK(move.Destination.X == 4.0);
    CHECK(move.AcceptanceRadius == 0.75);
    CHECK(move.AllowPartialPath);
    REQUIRE_THROWS_AS(blackboard.Get<double>("alert"), std::invalid_argument);

    REQUIRE_THROWS_AS(blackboard.Set("bad-move", gameplay::NavigationIntent{
        .Destination = { 0.0, 0.0, 0.0 },
        .AcceptanceRadius = -1.0
    }), std::invalid_argument);
}

TEST_CASE("Behavior tree preserves running composite progress",
    "[KairoAI][Gameplay][Behavior]")
{
    gameplay::BehaviorTree tree;
    gameplay::Blackboard blackboard;
    unsigned actionTicks = 0u;
    unsigned completionTicks = 0u;

    const auto condition = tree.AddCondition([](gameplay::Blackboard&,
        const gameplay::BehaviorContext&)
    {
        return gameplay::BehaviorStatus::Success;
    });
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
    tree.SetRoot(tree.AddSequence({ condition, action, completion }));

    CHECK(tree.Tick(blackboard, { 0.016, 1.0 }) == gameplay::BehaviorStatus::Running);
    CHECK(actionTicks == 1u);
    CHECK(completionTicks == 0u);

    CHECK(tree.Tick(blackboard, { 0.016, 1.016 }) == gameplay::BehaviorStatus::Success);
    CHECK(actionTicks == 2u);
    CHECK(completionTicks == 1u);
}

TEST_CASE("Behavior selector and decorators compose deterministic fallback logic",
    "[KairoAI][Gameplay][Behavior]")
{
    gameplay::BehaviorTree tree;
    gameplay::Blackboard blackboard;
    const auto unavailable = tree.AddCondition([](gameplay::Blackboard&,
        const gameplay::BehaviorContext&)
    {
        return gameplay::BehaviorStatus::Failure;
    });
    const auto inverted = tree.AddInverter(unavailable);
    const auto alwaysSucceeds = tree.AddSucceeder(unavailable);
    tree.SetRoot(tree.AddSequence({ inverted, alwaysSucceeds }));
    CHECK(tree.Tick(blackboard, {}) == gameplay::BehaviorStatus::Success);
}

TEST_CASE("Utility selector chooses the highest finite score and executes it",
    "[KairoAI][Gameplay][Utility]")
{
    gameplay::Blackboard blackboard;
    blackboard.Set("health", 0.2);
    blackboard.Set("decision", std::string("none"));

    gameplay::UtilitySelector selector;
    selector.Add({
        .Name = "fight",
        .Score = [](const gameplay::Blackboard& state,
            const gameplay::BehaviorContext&)
        {
            return state.Get<double>("health");
        },
        .Execute = [](gameplay::Blackboard& state,
            const gameplay::BehaviorContext&)
        {
            state.Set("decision", std::string("fight"));
            return gameplay::BehaviorStatus::Success;
        }
    });
    selector.Add({
        .Name = "flee",
        .Score = [](const gameplay::Blackboard& state,
            const gameplay::BehaviorContext&)
        {
            return 1.0 - state.Get<double>("health");
        },
        .Execute = [](gameplay::Blackboard& state,
            const gameplay::BehaviorContext&)
        {
            state.Set("decision", std::string("flee"));
            return gameplay::BehaviorStatus::Success;
        }
    });

    const auto selected = selector.Select(blackboard, { 0.016, 10.0 });
    REQUIRE(selected.has_value());
    CHECK(*selected == std::string_view("flee"));
    CHECK(selector.TickBest(blackboard, { 0.016, 10.0 }) ==
        gameplay::BehaviorStatus::Success);
    CHECK(blackboard.Get<std::string>("decision") == "flee");
}

TEST_CASE("Perception memory is bounded age-aware and strength-aware",
    "[KairoAI][Gameplay][Perception]")
{
    gameplay::PerceptionMemory memory(3u, 10.0);
    memory.Remember({ gameplay::StimulusKind::Sound, { 1u },
        { 0.0, 0.0, 0.0 }, 0.5, 1.0 });
    memory.Remember({ gameplay::StimulusKind::Sound, { 2u },
        { 5.0, 0.0, 0.0 }, 2.0, 8.0 });
    memory.Remember({ gameplay::StimulusKind::Sight, { 3u },
        { 1.0, 0.0, 0.0 }, 4.0, 9.0 });

    const auto strongest = memory.Strongest(gameplay::StimulusKind::Sound, 10.0);
    REQUIRE(strongest.has_value());
    CHECK(strongest->Source.Value == 2u);

    memory.Prune(12.0);
    CHECK(memory.Entries().size() == 2u);
}
