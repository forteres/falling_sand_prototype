#include <iostream>
#include <chrono>
#include <vector>
#include <GLFW/glfw3.h>

using namespace std;

enum class Material : uint16_t {
    Air,
    Sand,
    Water,
    Stone
};

class Game {
    public:
        void run();

    private:
        static constexpr int WIDTH = 800;
        static constexpr int HEIGHT = 600;

        struct Cell {
            Material material = Material::Air;
        };
        vector<Cell> cells;
        bool running = true;

        GLFWwindow* window = nullptr;

        void init();
        void update(double dt);
        void render(double alpha);
        Cell& getCell(int x, int y);        
        bool isInBounds(int x, int y);
};

void Game::run(){
        init();
        using clock = chrono::steady_clock;
        constexpr double dt = 1.0 / 60.0;
        double accumulator = 0.0;
        auto last = clock::now();

        while (running && !glfwWindowShouldClose(window))
        {
            glfwPollEvents();

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

        glfwDestroyWindow(window);
        glfwTerminate();
    }

void Game::init() {
    if (!glfwInit()) {
        cerr << "Failed to initialize GLFW\n";
        running = false;
        return;
    }

    window = glfwCreateWindow(
        WIDTH,
        HEIGHT,
        "Falling Sand Prototype",
        nullptr,
        nullptr
    );

    if (!window) {
        cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        running = false;
        return;
    }

    glfwMakeContextCurrent(window);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

    cells.resize(WIDTH * HEIGHT);

    getCell(100, 100).material = Material::Sand;
}

void Game::update(double dt) {
    for (int y = HEIGHT - 1; y >= 0; --y) {
        for (int x = 0; x < WIDTH; ++x) {
            Cell& cell = getCell(x, y);
            if (cell.material == Material::Sand) {
                if (isInBounds(x, y + 1)) {
                    Cell& below = getCell(x, y + 1);
                    if (below.material == Material::Air) {
                        swap(cell, below);
                    }
                }
            }
        }
    }
}

void Game::render(double alpha) {
    glClear(GL_COLOR_BUFFER_BIT);
    glfwSwapBuffers(window);
}

Game::Cell& Game::getCell(int x, int y) {
    return cells[y * WIDTH + x];
}

bool Game::isInBounds(int x, int y) {
    return x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT;
}

int main() {
    Game game;
    game.run();
    return 0;
}