#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace {

// ---------------------------------------------------------------- materials

enum class Material : uint16_t { Air, Sand, Water, Stone, Count };
enum class TypeG : uint16_t { Solid, Liquid, Gas, Static };

constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    // Byte order R,G,B,A in memory on little-endian machines.
    return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(a) << 24;
}

// Air / drag constants.
constexpr float GRAVITY               = 100.0f;
constexpr float AIR_DENSITY           = 1.225f;
constexpr float DRAG_COEFFICIENT      = 1.0f;
constexpr float FRONTAL_AREA_COEFF    = 1.0f;
constexpr float HALF_DRAG_FACTOR      = 0.5f * AIR_DENSITY * DRAG_COEFFICIENT * FRONTAL_AREA_COEFF;

struct MaterialInfo {
    uint32_t color;
    TypeG    type;
    bool     dynamic;   // needs simulation (not Air, not Static)
    float    dragK;     // (0.5 * rho * Cd * A) / mass, precomputed
};

constexpr MaterialInfo makeInfo(uint32_t color, TypeG type, float mass) {
    return { color, type, type == TypeG::Solid || type == TypeG::Liquid, HALF_DRAG_FACTOR / mass };
}

// Indexed by Material. Mass only matters through dragK (gravity cancels out of F = ma).
constexpr std::array<MaterialInfo, size_t(Material::Count)> MATERIALS = {{
    makeInfo(rgba( 25,  25,  25), TypeG::Gas,    1.225f),  // Air
    makeInfo(rgba(220, 190,  90), TypeG::Solid,  1600.0f), // Sand
    makeInfo(rgba( 50, 100, 230), TypeG::Liquid, 1000.0f), // Water
    makeInfo(rgba(120, 120, 120), TypeG::Static, 2500.0f), // Stone
}};

inline const MaterialInfo& info(Material m) { return MATERIALS[size_t(m)]; }

inline bool canDisplace(Material moving, Material target) {
    return target == Material::Air ||
           (moving == Material::Sand && target == Material::Water);
}

// ---------------------------------------------------------------- shaders

constexpr const char* VERTEX_SRC = R"(#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
out vec2 texCoord;
void main() {
    texCoord = uv;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

constexpr const char* FRAGMENT_SRC = R"(#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D screenTexture;
void main() {
    fragColor = texture(screenTexture, texCoord);
}
)";

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Shader compile error:\n" << log << '\n';
    }
    return shader;
}

GLuint createShaderProgram() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, VERTEX_SRC);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, FRAGMENT_SRC);

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "Program link error:\n" << log << '\n';
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

} // namespace

// ---------------------------------------------------------------- game

class Game {
public:
    void run();

private:
    static constexpr int CANVAS_WIDTH  = 800;
    static constexpr int CANVAS_HEIGHT = 600;

    struct Cell {
        Material material = Material::Air;
        float velocityX   = 0.0f;
        float velocityY   = 0.0f;
        float remainderVX = 0.0f;
        float remainderVY = 0.0f;
        bool  updated     = false;
    };

    enum class MoveResult { None, Moved, Blocked };

    struct Viewport { int x = 0, y = 0, w = 1, h = 1; };

    std::vector<Cell>     cells;
    std::vector<uint32_t> pixels;

    GLuint texture = 0, vao = 0, vbo = 0, shaderProgram = 0;
    GLFWwindow* window = nullptr;

    int windowWidth  = 1200;
    int windowHeight = 900;
    int framebufferWidth  = 1200;
    int framebufferHeight = 900;
    Viewport viewport;

    bool running = true;
    bool dirty   = true;   // pixel buffer needs re-upload

    Material brushMaterial = Material::Sand;
    int      brushRadius   = 5;

    uint64_t simulationFrame = 0;

    // setup
    bool init();
    void initRenderer();
    void onResize(int width, int height);
    static void keyCallback(GLFWwindow*, int key, int, int action, int);
    static void framebufferSizeCallback(GLFWwindow*, int width, int height);

    // per-frame
    void handleInput();
    void paint(int cx, int cy);
    void update(float dt);
    void render();

    // simulation
    Cell& cellAt(int x, int y) { return cells[size_t(y) * CANVAS_WIDTH + x]; }
    static bool inBounds(int x, int y) {
        return unsigned(x) < unsigned(CANVAS_WIDTH) && unsigned(y) < unsigned(CANVAS_HEIGHT);
    }

    static void applyForces(Cell& cell, const MaterialInfo& mat, float dt);
    void        moveCell(int x, int y, float dt);
    MoveResult  moveByVelocity(int& x, int& y, float dt);
    bool        moveTo(int& x, int& y, int targetX, int targetY);
    bool        tryDiagonals(int& x, int& y);
    bool        moveSolid(int& x, int& y);
    bool        moveLiquid(int& x, int& y);
};

void Game::run() {
    if (!init()) {
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
        return;
    }

    using clock = std::chrono::steady_clock;
    constexpr double dt = 1.0 / 60.0;

    double accumulator = 0.0;
    auto last = clock::now();

    while (running && !glfwWindowShouldClose(window)) {
        const auto now = clock::now();
        accumulator += std::min(std::chrono::duration<double>(now - last).count(), 0.25);
        last = now;

        while (accumulator >= dt) {
            update(float(dt));
            accumulator -= dt;
            dirty = true;
        }

        render();
        glfwPollEvents();
        handleInput();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}

// ---------------------------------------------------------------- setup

bool Game::init() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(windowWidth, windowHeight, "Falling Sand Prototype", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        return false;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);   // vsync: don't spin the CPU at 100%
    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, keyCallback);

    if (!gladLoadGL(glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        return false;
    }

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    onResize(framebufferWidth, framebufferHeight);

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

    cells.resize(size_t(CANVAS_WIDTH) * CANVAS_HEIGHT);
    pixels.assign(cells.size(), info(Material::Air).color);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, CANVAS_WIDTH, CANVAS_HEIGHT, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    initRenderer();
    return true;
}

void Game::initRenderer() {
    // position (x, y), texture coordinates (u, v)
    constexpr float vertices[] = {
        -1.f, -1.f,  0.f, 1.f,
         1.f, -1.f,  1.f, 1.f,
         1.f,  1.f,  1.f, 0.f,

        -1.f, -1.f,  0.f, 1.f,
         1.f,  1.f,  1.f, 0.f,
        -1.f,  1.f,  0.f, 0.f,
    };

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    constexpr GLsizei stride = 4 * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    shaderProgram = createShaderProgram();
    glUseProgram(shaderProgram);
    glActiveTexture(GL_TEXTURE0);   // sampler defaults to unit 0
}

// Letterboxed viewport that keeps the canvas aspect ratio.
void Game::onResize(int width, int height) {
    if (width <= 0 || height <= 0) return;

    framebufferWidth  = width;
    framebufferHeight = height;

    // Compare aspect ratios with integer math: w/h > CW/CH  <=>  w*CH > h*CW
    if (int64_t(width) * CANVAS_HEIGHT > int64_t(height) * CANVAS_WIDTH) {
        viewport.h = height;
        viewport.w = int(int64_t(height) * CANVAS_WIDTH / CANVAS_HEIGHT);
        viewport.x = (width - viewport.w) / 2;
        viewport.y = 0;
    } else {
        viewport.w = width;
        viewport.h = int(int64_t(width) * CANVAS_HEIGHT / CANVAS_WIDTH);
        viewport.x = 0;
        viewport.y = (height - viewport.h) / 2;
    }

    glViewport(viewport.x, viewport.y, viewport.w, viewport.h);
}

void Game::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    if (auto* game = static_cast<Game*>(glfwGetWindowUserPointer(window)))
        game->onResize(width, height);
}

void Game::keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;

    auto* game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;

    if (key == GLFW_KEY_KP_ADD)      game->brushRadius = std::min(50, game->brushRadius + 1);
    if (key == GLFW_KEY_KP_SUBTRACT) game->brushRadius = std::max(1,  game->brushRadius - 1);
}

// ---------------------------------------------------------------- input

void Game::handleInput() {
    if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) brushMaterial = Material::Sand;
    if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) brushMaterial = Material::Water;
    if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) brushMaterial = Material::Stone;

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) return;

    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);
    if (winW <= 0 || winH <= 0) return;

    // Window coordinates -> framebuffer coordinates (HiDPI).
    const double fbX = mouseX * framebufferWidth  / winW;
    const double fbY = mouseY * framebufferHeight / winH;

    // Ignore clicks outside the canvas.
    if (fbX < viewport.x || fbX >= viewport.x + viewport.w ||
        fbY < viewport.y || fbY >= viewport.y + viewport.h) {
        return;
    }

    paint(int((fbX - viewport.x) * CANVAS_WIDTH  / viewport.w),
          int((fbY - viewport.y) * CANVAS_HEIGHT / viewport.h));
}

void Game::paint(int cx, int cy) {
    const int r2 = brushRadius * brushRadius;

    for (int dy = -brushRadius; dy <= brushRadius; ++dy) {
        const int py = cy + dy;
        if (unsigned(py) >= unsigned(CANVAS_HEIGHT)) continue;

        for (int dx = -brushRadius; dx <= brushRadius; ++dx) {
            if (dx * dx + dy * dy > r2) continue;

            const int px = cx + dx;
            if (unsigned(px) >= unsigned(CANVAS_WIDTH)) continue;

            Cell& cell = cellAt(px, py);
            if (cell.material != brushMaterial) {
                cell = Cell{};                 // reset velocity on material change
                cell.material = brushMaterial;
                dirty = true;
            }
        }
    }
}

// ---------------------------------------------------------------- simulation

void Game::update(float dt) {
    ++simulationFrame;

    // Reset flags and integrate forces for every dynamic cell.
    for (Cell& cell : cells) {
        cell.updated = false;

        const MaterialInfo& mat = info(cell.material);
        if (mat.dynamic) applyForces(cell, mat, dt);
    }

    // Alternate X direction each frame to reduce bias. Bottom-up traversal.
    const bool leftToRight = simulationFrame % 2 == 0;

    for (int y = CANVAS_HEIGHT - 1; y >= 0; --y) {
        for (int i = 0; i < CANVAS_WIDTH; ++i) {
            const int x = leftToRight ? i : CANVAS_WIDTH - 1 - i;

            const Cell& cell = cellAt(x, y);
            if (cell.updated || !info(cell.material).dynamic) continue;

            moveCell(x, y, dt);
        }
    }
}

// a = F/m with F = m*g - 0.5*rho*Cd*A*|v|*v. The mass cancels, leaving a
// precomputed per-material drag constant and no divisions or normalisation.
void Game::applyForces(Cell& cell, const MaterialInfo& mat, float dt) {
    const float speed = std::sqrt(cell.velocityX * cell.velocityX +
                                  cell.velocityY * cell.velocityY);
    const float k = mat.dragK * speed;

    cell.velocityX += -k * cell.velocityX * dt;
    cell.velocityY += (GRAVITY - k * cell.velocityY) * dt;
}

void Game::moveCell(int x, int y, float dt) {
    const TypeG type = info(cellAt(x, y).material).type;

    const MoveResult result = moveByVelocity(x, y, dt);

    // Velocity move blocked: fall back to material-specific behaviour.
    if (result == MoveResult::Blocked) {
        if (type == TypeG::Solid)       moveSolid(x, y);
        else if (type == TypeG::Liquid) moveLiquid(x, y);
    }

    cellAt(x, y).updated = true;
}

Game::MoveResult Game::moveByVelocity(int& x, int& y, float dt) {
    Cell& cell = cellAt(x, y);

    // Convert velocity to movement, keeping the fractional leftover.
    const float movementX = cell.velocityX * dt + cell.remainderVX;
    const float movementY = cell.velocityY * dt + cell.remainderVY;

    const int moveX = int(movementX);
    const int moveY = int(movementY);

    cell.remainderVX = movementX - float(moveX);
    cell.remainderVY = movementY - float(moveY);

    // Not enough accumulated for a whole cell yet.
    if (moveX == 0 && moveY == 0) return MoveResult::None;

    const int   startX = x;
    const int   startY = y;
    const int   steps  = std::max(std::abs(moveX), std::abs(moveY));
    const float invSteps = 1.0f / float(steps);

    int  previousX = x;
    int  previousY = y;
    bool moved = false;

    // Walk every intermediate cell so we can't tunnel through obstacles.
    for (int step = 1; step <= steps; ++step) {
        const float t = float(step) * invSteps;

        const int targetX = startX + int(std::lround(float(moveX) * t));
        const int targetY = startY + int(std::lround(float(moveY) * t));

        if (targetX == previousX && targetY == previousY) continue;

        previousX = targetX;
        previousY = targetY;

        if (!moveTo(x, y, targetX, targetY)) return MoveResult::Blocked;
        moved = true;
    }

    return moved ? MoveResult::Moved : MoveResult::None;
}

bool Game::moveTo(int& x, int& y, int targetX, int targetY) {
    if (!inBounds(targetX, targetY)) return false;
    if (targetX == x && targetY == y) return false;

    Cell& moving = cellAt(x, y);
    Cell& target = cellAt(targetX, targetY);

    if (!canDisplace(moving.material, target.material)) return false;

    std::swap(moving, target);

    // Both the displaced cell (now at the old position) and the mover
    // are done for this frame.
    moving.updated = true;
    target.updated = true;

    x = targetX;
    y = targetY;
    return true;
}

// Shared by solids and liquids: try to slide down-left / down-right,
// alternating preferred side each frame.
bool Game::tryDiagonals(int& x, int& y) {
    const int first = (simulationFrame % 2 == 0) ? -1 : 1;

    return moveTo(x, y, x + first, y + 1) ||
           moveTo(x, y, x - first, y + 1);
}

bool Game::moveSolid(int& x, int& y) {
    if (tryDiagonals(x, y)) return true;

    // Resting against something: lose most speed.
    Cell& cell = cellAt(x, y);
    cell.velocityY *= 0.1f;
    cell.velocityX *= 0.7f;
    return false;
}

bool Game::moveLiquid(int& x, int& y) {
    constexpr int SPREAD_DISTANCE = 6;

    if (tryDiagonals(x, y)) return true;

    const Material self = cellAt(x, y).material;
    const int first = (simulationFrame % 2 == 0) ? -1 : 1;

    // Search sideways for a spot where the liquid can drop.
    for (int direction : { first, -first }) {
        int furthestOpenX = x;

        for (int distance = 1; distance <= SPREAD_DISTANCE; ++distance) {
            const int targetX = x + direction * distance;

            if (!inBounds(targetX, y) ||
                !canDisplace(self, cellAt(targetX, y).material)) {
                break;
            }

            furthestOpenX = targetX;

            // Room underneath: head there.
            if (inBounds(targetX, y + 1) &&
                canDisplace(self, cellAt(targetX, y + 1).material)) {
                return moveTo(x, y, targetX, y);
            }
        }

        // No drop found, but still spread horizontally.
        if (furthestOpenX != x) return moveTo(x, y, furthestOpenX, y);
    }

    // Trapped / resting.
    Cell& cell = cellAt(x, y);
    cell.velocityY *= 0.1f;
    cell.velocityX *= 0.9f;
    return false;
}

// ---------------------------------------------------------------- rendering

void Game::render() {
    glClear(GL_COLOR_BUFFER_BIT);

    // Only rebuild and upload the texture if something changed.
    if (dirty) {
        const size_t count = cells.size();
        for (size_t i = 0; i < count; ++i)
            pixels[i] = info(cells[i].material).color;

        glBindTexture(GL_TEXTURE_2D, texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CANVAS_WIDTH, CANVAS_HEIGHT,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        dirty = false;
    }

    glDrawArrays(GL_TRIANGLES, 0, 6);
    glfwSwapBuffers(window);
}

int main() {
    Game game;
    game.run();
    return 0;
}