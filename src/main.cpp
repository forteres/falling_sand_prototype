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

        struct Color {
            uint8_t r, g, b, a;
        };
        vector<Color> pixels;
        GLuint texture = 0;

        bool running = true;

        GLFWwindow* window = nullptr;

        void init();
        void update(double dt);
        void render(double alpha);
        Cell& getCell(int x, int y);        
        bool isInBounds(int x, int y);
        Color materialColor(Material material);
};

void Game::run(){
        init();
        using clock = chrono::steady_clock;
        constexpr double dt = 1.0 / 60.0;
        double accumulator = 0.0;
        auto last = clock::now();

        while (running && !glfwWindowShouldClose(window))
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
            glfwPollEvents();
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
    pixels.resize(WIDTH * HEIGHT);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

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
    for (int i = 0; i < WIDTH * HEIGHT; ++i) {
        pixels[i] = materialColor(cells[i].material);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0, 0,
        WIDTH, HEIGHT,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels.data()
    );
    glfwSwapBuffers(window);
}

Game::Cell& Game::getCell(int x, int y) {
    return cells[y * WIDTH + x];
}

bool Game::isInBounds(int x, int y) {
    return x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT;
}

Game::Color Game::materialColor(Material material) {
    switch (material) {
        case Material::Air:   return {25, 25, 25, 255};
        case Material::Sand:  return {220, 190, 90, 255};
        case Material::Water: return {50, 100, 230, 255};
        case Material::Stone: return {120, 120, 120, 255};
    }

    return {255, 0, 255, 255};
}

int main() {
    Game game;
    game.run();
    return 0;
}