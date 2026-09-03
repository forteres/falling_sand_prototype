#include <iostream>
#include <chrono>
#include <vector>
#include <glad/gl.h>
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
            bool updated = false;
        };
        vector<Cell> cells;

        struct Color {
            uint8_t r, g, b, a;
        };
        vector<Color> pixels;
        GLuint texture = 0;
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint shaderProgram = 0;

        static void keyCallback(
            GLFWwindow* window,
            int key,
            int scancode,
            int action,
            int mods
        );

        bool running = true;

        GLFWwindow* window = nullptr;

        Material brushMaterial = Material::Sand;
        uint8_t BRUSH_RADIUS = 5;

        void init();
        void update(double dt);
        void render(double alpha);
        void initRenderer();
        GLuint createShaderProgram();
        Cell& getCell(int x, int y);        
        bool isInBounds(int x, int y);
        Color materialColor(Material material);    
        void handleInput();   
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
            handleInput();
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
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    glfwMakeContextCurrent(window);
    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, keyCallback);
    
    if (!gladLoadGL(glfwGetProcAddress)) {
        cerr << "Failed to initialize GLAD\n";
        running = false;
        return;
    }
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

    cells.resize(WIDTH * HEIGHT);
    pixels.resize(WIDTH * HEIGHT);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    initRenderer();

    getCell(100, 100).material = Material::Sand;
}

void Game::update(double dt) {
    for (Cell& cell : cells)
        cell.updated = false;
    for (int y = HEIGHT - 1; y >= 0; --y) {
        for (int x = 0; x < WIDTH; ++x) {
            Cell& cell = getCell(x, y);
            if (cell.updated)
                continue;
            if (cell.material == Material::Sand) {
                if (isInBounds(x, y + 1)) {
                    Cell& below = getCell(x, y + 1);
                    if (below.material == Material::Air || below.material == Material::Water) {
                        swap(cell, below);
                        below.updated = true;
                    }
                }
            }
            else if (cell.material == Material::Water) {
                if (isInBounds(x, y + 1)) {
                    Cell& below = getCell(x, y + 1);

                    if (below.material == Material::Air) {
                        swap(cell, below);
                        below.updated = true;
                        continue;
                    }
                }
                if (isInBounds(x - 1, y + 1)) {
                    Cell& belowLeft = getCell(x - 1, y + 1);

                    if (belowLeft.material == Material::Air) {
                        swap(cell, belowLeft);
                        belowLeft.updated = true;
                        continue;
                    }
                }
                if (isInBounds(x + 1, y + 1)) {
                    Cell& belowRight = getCell(x + 1, y + 1);

                    if (belowRight.material == Material::Air) {
                        swap(cell, belowRight);
                        belowRight.updated = true;
                        continue;
                    }
                }
                if (isInBounds(x - 1, y)) {
                    Cell& left = getCell(x - 1, y);

                    if (left.material == Material::Air) {
                        swap(cell, left);
                        left.updated = true;
                        continue;
                    }
                }
                if (isInBounds(x + 1, y)) {
                    Cell& right = getCell(x + 1, y);

                    if (right.material == Material::Air) {
                        swap(cell, right);
                        right.updated = true;
                        continue;
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
    glUseProgram(shaderProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glBindVertexArray(vao);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glfwSwapBuffers(window);
}

void Game::initRenderer() {
    float vertices[] = {
        // position      // texture coordinates
        -1.f, -1.f,      0.f, 1.f,
         1.f, -1.f,      1.f, 1.f,
         1.f,  1.f,      1.f, 0.f,

        -1.f, -1.f,      0.f, 1.f,
         1.f,  1.f,      1.f, 0.f,
        -1.f,  1.f,      0.f, 0.f
    };

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        (void*)0
    );

    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        (void*)(2 * sizeof(float))
    );

    glEnableVertexAttribArray(1);

    shaderProgram = createShaderProgram();
}

GLuint Game::createShaderProgram() {
    const char* vertexSource = R"(
        #version 330 core

        layout(location = 0) in vec2 position;
        layout(location = 1) in vec2 uv;

        out vec2 texCoord;

        void main() {
            texCoord = uv;
            gl_Position = vec4(position, 0.0, 1.0);
        }
    )";

    const char* fragmentSource = R"(
        #version 330 core

        in vec2 texCoord;
        out vec4 fragColor;

        uniform sampler2D screenTexture;

        void main() {
            fragColor = texture(screenTexture, texCoord);
        }
    )";

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexSource, nullptr);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
    glCompileShader(fragmentShader);

    GLuint program = glCreateProgram();

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);

    glLinkProgram(program);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

void Game::handleInput() { // some of them
    // keyboard
    if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS)
        brushMaterial = Material::Sand;

    if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS)
        brushMaterial = Material::Water;

    if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS)
        brushMaterial = Material::Stone;

    // mouse
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        int x = static_cast<int>(mouseX);
        int y = static_cast<int>(mouseY);

        for (int dy = -BRUSH_RADIUS; dy <= BRUSH_RADIUS; ++dy) {
            for (int dx = -BRUSH_RADIUS; dx <= BRUSH_RADIUS; ++dx) {

                if (dx * dx + dy * dy > BRUSH_RADIUS * BRUSH_RADIUS)
                    continue;

                int px = x + dx;
                int py = y + dy;

                if (isInBounds(px, py))
                    getCell(px, py).material = brushMaterial;
            }
        }
    }
}

void Game::keyCallback(
    GLFWwindow* window,
    int key,
    int scancode,
    int action,
    int mods
) {
    if (action != GLFW_PRESS)
        return;

    Game* game = static_cast<Game*>(
        glfwGetWindowUserPointer(window)
    );

    if (key == GLFW_KEY_KP_ADD)
        game->BRUSH_RADIUS = min<uint8_t>(50, game->BRUSH_RADIUS + 1);

    if (key == GLFW_KEY_KP_SUBTRACT)
        game->BRUSH_RADIUS = max<uint8_t>(1, game->BRUSH_RADIUS - 1);
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