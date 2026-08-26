#include <iostream>
#include <chrono>

using namespace std;

class Game {
    public:
        void run();
    private:
        void update(double dt);
        void render(double alpha);
        bool running = true;
};

void Game::run(){
        using clock = chrono::steady_clock;
        constexpr double dt = 1.0 / 60.0;
        double accumulator = 0.0;
        auto last = clock::now();

        while (running)
        {
            const auto now = clock::now();
            double frame = chrono::duration<double>(now - last).count();
            last = now;

            frame = min(frame, 0.25);
            accumulator += frame;

            while (accumulator >= dt)
            {
                update(dt);
                accumulator -= dt;
            }

            render(accumulator / dt);
        }
    }

int main() {
    Game game;
    game.run();
    return 0;
}