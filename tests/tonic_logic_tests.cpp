#include "test_support.hpp"
#include "tonic_logic.hpp"

#include <chrono>

int main()
{
    using voxtonic::logic::decideShouldPress;
    using voxtonic::logic::DecisionParams;
    using voxtonic::logic::DecisionState;
    using std::chrono::milliseconds;
    using std::chrono::steady_clock;

    const auto t0 = steady_clock::time_point {};
    const DecisionParams params;

    // Fenêtre de démarrage : pas de press avant startupDelay (2000 ms), puis
    // press même sans activation manuelle (auto-transform au chargement).
    {
        DecisionState state;
        CHECK(!decideShouldPress(false, false, t0, params, state));
        CHECK(!decideShouldPress(false, true, t0 + milliseconds {500}, params, state));
        CHECK(!decideShouldPress(false, false, t0 + milliseconds {1999}, params, state));
        CHECK(decideShouldPress(false, false, t0 + milliseconds {2001}, params, state));
    }

    // Monté pendant la fenêtre de démarrage : le démont arme lastPressAt,
    // donc la première tentative attend rePressDelay après la réception (le
    // saut de démont bloque l'usage d'objet en l'air).
    {
        DecisionState state;
        CHECK(!decideShouldPress(false, true, t0, params, state));
        CHECK(!decideShouldPress(false, true, t0 + milliseconds {2500}, params, state));
        CHECK(!decideShouldPress(false, false, t0 + milliseconds {2600}, params, state));
        CHECK(decideShouldPress(false, false, t0 + milliseconds {4601}, params, state));
    }

    // Après une keypress initiale, aucune seconde keypress tant que l'effet
    // n'a pas été confirmé actif : le bind est un toggle. Une presse avalée
    // (confirmTimeout écoulé sans transformation) réessaie, throttlée par
    // rePressDelay.
    {
        DecisionState state;
        CHECK(!decideShouldPress(false, false, t0, params, state));
        CHECK(decideShouldPress(false, false, t0 + milliseconds {2001}, params, state));
        CHECK(!decideShouldPress(false, false, t0 + milliseconds {4000}, params, state));
        const auto tActive = t0 + milliseconds {11000};
        CHECK(!decideShouldPress(true, false, tActive, params, state));
        // Après confirmation, une disparition réelle autorise le re-press.
        CHECK(decideShouldPress(false, false, tActive + milliseconds {500}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {1000}, params, state));
        // Presse avalée: retry à +2000 après la dernière tentative.
        CHECK(decideShouldPress(false, false, tActive + milliseconds {2501}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {3100}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {4601}, params, state));
        // Back-off 8s depuis le dernier press avalé (+2501) -> press à +10501.
        CHECK(!decideShouldPress(false, false,
            tActive + milliseconds {2501} + milliseconds {7900}, params, state));
        CHECK(decideShouldPress(false, false,
            tActive + milliseconds {2501} + milliseconds {8050}, params, state));
    }

    // Actif pendant la fenêtre de démarrage => pas de press, état mémorisé.
    {
        DecisionState state;
        const auto tActive = t0 + milliseconds {500};
        CHECK(!decideShouldPress(true, false, tActive, params, state));
        CHECK(state.everSeenActive);
    }

    // Disparition => press après le délai de grâce (300 ms), pas avant.
    {
        DecisionState state;
        const auto tActive = t0 + milliseconds {1000};
        CHECK(!decideShouldPress(true, false, tActive, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {100}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {299}, params, state));
        CHECK(decideShouldPress(false, false, tActive + milliseconds {301}, params, state));
    }

    // Monté => jamais de press; au démont, première tentative différée de
    // rePressDelay.
    {
        DecisionState state;
        const auto tActive = t0 + milliseconds {1000};
        CHECK(!decideShouldPress(true, false, tActive, params, state));
        CHECK(!decideShouldPress(false, true, tActive + milliseconds {2000}, params, state));
        CHECK(!decideShouldPress(false, true, tActive + milliseconds {10000}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {10100}, params, state));
        CHECK(decideShouldPress(false, false, tActive + milliseconds {12101}, params, state));
    }

    // Après un montage compétitif, le tonic est retiré par GW2 et doit être
    // réactivé dès le retour à pied — après rePressDelay (saut de démont).
    {
        DecisionState state;
        CHECK(!decideShouldPress(true, false, t0 + milliseconds {1000}, params, state));
        CHECK(!decideShouldPress(false, true, t0 + milliseconds {2000}, params, state));
        CHECK(!decideShouldPress(false, false, t0 + milliseconds {2100}, params, state));
        CHECK(decideShouldPress(false, false, t0 + milliseconds {4101}, params, state));
    }

    // Anti-spam : une presse non confirmée réessaie après confirmTimeout +
    // rePressDelay, mais jamais plus vite.
    {
        DecisionState state;
        const auto tActive = t0 + milliseconds {1000};
        CHECK(!decideShouldPress(true, false, tActive, params, state));
        CHECK(decideShouldPress(false, false, tActive + milliseconds {500}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {1000}, params, state));
        CHECK(decideShouldPress(false, false, tActive + milliseconds {2501}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {3100}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {4601}, params, state));
        // Back-off 8s depuis le dernier press avalé (+2501) -> press à +10501.
        CHECK(!decideShouldPress(false, false,
            tActive + milliseconds {2501} + milliseconds {7900}, params, state));
        CHECK(decideShouldPress(false, false,
            tActive + milliseconds {2501} + milliseconds {8050}, params, state));
    }

    // Réactivation => pending clear, puis nouveau press.
    {
        DecisionState state;
        const auto tActive = t0 + milliseconds {1000};
        CHECK(!decideShouldPress(true, false, tActive, params, state));
        CHECK(decideShouldPress(false, false, tActive + milliseconds {500}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {1500}, params, state));
        const auto tAgain = tActive + milliseconds {5000};
        CHECK(!decideShouldPress(true, false, tAgain, params, state));
        CHECK(decideShouldPress(false, false, tAgain + milliseconds {500}, params, state));
    }

    // Pending bloque le toggle immédiat, puis retry throttlé.
    {
        DecisionState state;
        const auto tActive = t0 + milliseconds {1000};
        CHECK(!decideShouldPress(true, false, tActive, params, state));
        // Premier demorph -> press à +500
        CHECK(decideShouldPress(false, false, tActive + milliseconds {500}, params, state));
        // Snapshot encore absent à +2000 : pending expire (1200) mais le
        // throttle lastPressAt retient jusqu'à +2500.
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {2000}, params, state));
        CHECK(decideShouldPress(false, false, tActive + milliseconds {2501}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {3100}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {4601}, params, state));
        // Back-off 8s depuis le dernier press avalé (+2501) -> press à +10501.
        CHECK(!decideShouldPress(false, false,
            tActive + milliseconds {2501} + milliseconds {7900}, params, state));
        CHECK(decideShouldPress(false, false,
            tActive + milliseconds {2501} + milliseconds {8050}, params, state));
    }

    // Gate de mode (non-régression du press storm) : l'état de décision ne doit
    // être réinitialisé QUE sur un vrai changement de mode. Le réinitialiser à
    // chaque tick compétitif faisait perdre lastPressAt -> une presse par tick
    // au lieu d'une par rePressDelay.
    {
        using voxtonic::logic::evaluateModeGate;
        std::optional<bool> last;

        auto gate = evaluateModeGate(true, true, true, last);
        CHECK(gate.enabled && gate.clearMount && gate.resetDecision); // 1er tick
        gate = evaluateModeGate(true, true, true, last);
        CHECK(gate.enabled && gate.clearMount && !gate.resetDecision); // même mode
        gate = evaluateModeGate(true, true, true, last);
        CHECK(!gate.resetDecision);
        gate = evaluateModeGate(false, true, true, last);   // comp -> PvE
        CHECK(gate.enabled && !gate.clearMount && gate.resetDecision);
        gate = evaluateModeGate(false, false, false, last); // gate coupé
        CHECK(!gate.enabled && gate.resetDecision);

        // 100 ticks compétitifs (100 ms) sans transformation, le gate appliqué
        // exactement comme dans tick().
        DecisionState state;
        std::optional<bool> lastC;
        int presses = 0;
        auto t = steady_clock::time_point {};
        for (int tick = 0; tick < 100; ++tick) {
            t += milliseconds {100};
            const auto g = evaluateModeGate(true, true, true, lastC);
            if (g.resetDecision) state = {};
            if (!g.enabled) continue;
            if (decideShouldPress(false, false, t, params, state)) ++presses;
        }

        // Contre-épreuve : l'ancien comportement (état réinitialisé à chaque
        // tick) presse à chaque tick.
        DecisionState storm;
        int stormPresses = 0;
        auto t2 = steady_clock::time_point {};
        for (int tick = 0; tick < 100; ++tick) {
            t2 += milliseconds {100};
            storm = {};
            storm.started = true;
            storm.startedAt = t2 - params.startupDelay;
            storm.lastActiveAt = t2 - params.absenceGrace;
            if (decideShouldPress(false, false, t2, params, storm)) ++stormPresses;
        }

        CHECK(presses >= 1 && presses <= 5);
        CHECK(stormPresses >= 90);
        CHECK(presses * 10 <= stormPresses);
    }

    std::fprintf(stderr, "tonic_logic_tests: all checks passed\n");
    return 0;
}
