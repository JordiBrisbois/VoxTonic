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

    // Monté pendant la fenêtre de démarrage : press différé, pas perdu.
    {
        DecisionState state;
        CHECK(!decideShouldPress(false, true, t0, params, state));
        CHECK(!decideShouldPress(false, true, t0 + milliseconds {2500}, params, state));
        CHECK(decideShouldPress(false, false, t0 + milliseconds {2600}, params, state));
    }

    // Re-press au démarrage : espacés par startupRetryDelay (8000 ms) tant que
    // l'effet n'a jamais été vu actif, puis rePressDelay (2000 ms) après.
    {
        DecisionState state;
        CHECK(!decideShouldPress(false, false, t0, params, state));
        CHECK(decideShouldPress(false, false, t0 + milliseconds {2001}, params, state));
        CHECK(!decideShouldPress(false, false, t0 + milliseconds {4000}, params, state));
        const auto tActive = t0 + milliseconds {11000};
        CHECK(!decideShouldPress(true, false, tActive, params, state));
        CHECK(decideShouldPress(false, false, tActive + milliseconds {500}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {1000}, params, state));
        CHECK(decideShouldPress(false, false, tActive + milliseconds {2501}, params, state));
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

    // Monté => jamais de press, même après grâce.
    {
        DecisionState state;
        const auto tActive = t0 + milliseconds {1000};
        CHECK(!decideShouldPress(true, false, tActive, params, state));
        CHECK(!decideShouldPress(false, true, tActive + milliseconds {2000}, params, state));
        CHECK(!decideShouldPress(false, true, tActive + milliseconds {10000}, params, state));
        CHECK(decideShouldPress(false, false, tActive + milliseconds {10100}, params, state));
    }

    // Anti-spam : deux press espacés par rePressDelay (2000 ms).
    {
        DecisionState state;
        const auto tActive = t0 + milliseconds {1000};
        CHECK(!decideShouldPress(true, false, tActive, params, state));
        CHECK(decideShouldPress(false, false, tActive + milliseconds {500}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {1000}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {2499}, params, state));
        CHECK(decideShouldPress(false, false, tActive + milliseconds {2501}, params, state));
    }

    // Réactivation => réarme le cycle.
    {
        DecisionState state;
        const auto tActive = t0 + milliseconds {1000};
        CHECK(!decideShouldPress(true, false, tActive, params, state));
        CHECK(decideShouldPress(false, false, tActive + milliseconds {500}, params, state));
        CHECK(!decideShouldPress(false, false, tActive + milliseconds {1500}, params, state));
        const auto tAgain = tActive + milliseconds {3000};
        CHECK(!decideShouldPress(true, false, tAgain, params, state));
        CHECK(decideShouldPress(false, false, tAgain + milliseconds {1000}, params, state));
    }

    std::fprintf(stderr, "tonic_logic_tests: all checks passed\n");
    return 0;
}
