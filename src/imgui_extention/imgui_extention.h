#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if IMGUI_ENABLED

#if __has_include(<imgui.h>)
    #include <imgui.h>
#else
    #error "imgui.h was not found, add ImGui to the to the include path"
#endif

#if __has_include(<imgui_impl_glfw.h>)
    #include <imgui_impl_glfw.h>
    #include <imgui_impl_opengl3.h>
#elif __has_include(<backends/imgui_impl_glfw.h>)
    #include <backends/imgui_impl_glfw.h>
    #include <backends/imgui_impl_opengl3.h>
#else
    #error "imgui_impl_glfw.h was not found; add ImGui's backends directory to the include path"
#endif
#include <glm/glm.hpp>

#include "../core/engine_config.h"
#include "../core/platform.h"
#include "../core/logs.h"

// Allocation policy: these helpers build containers inside noexcept functions;
// allocation failure terminates by design (the engine treats OOM as fatal).
namespace ImguiTools {
    // A combo box with a type-to-filter box; returns true when the selection changes.
    inline bool filteredCombo(const char* label, int& selectedIndex,
                                 const std::vector<std::string>& items,
                                 char* filterBuf, size_t filterBufSize) noexcept {
        const bool validSel = selectedIndex >= 0 && selectedIndex < static_cast<int>(items.size());
        const char* preview = validSel ? items[selectedIndex].c_str() : "(none)";

        bool changed = false;
        if (ImGui::BeginCombo(label, preview)) {
            if (ImGui::IsWindowAppearing()) {
                filterBuf[0] = '\0';
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputText("##filter", filterBuf, filterBufSize);

            auto matches = [&](const std::string& name) {
                if (filterBuf[0] == '\0') {return true;}
                auto it = std::search(name.begin(), name.end(), filterBuf, filterBuf + std::strlen(filterBuf),
                    [](char a, char b) {return std::tolower(static_cast<unsigned char>(a))
                                            == std::tolower(static_cast<unsigned char>(b));});
                return it != name.end();
            };

            std::vector<int> visible;
            visible.reserve(items.size());
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (matches(items[i])) {visible.push_back(i);}
            }

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const int idx = visible[row];
                    if (ImGui::Selectable(items[idx].c_str(), idx == selectedIndex)) {
                        selectedIndex = idx;
                        changed = true;
                    }
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // True once ImGui has a context and its GLFW backend is initialised. Event
    // forwarding must check this: the backend dereferences both unconditionally.
    inline bool backend_ready() noexcept {
        return ImGui::GetCurrentContext() != nullptr
            && ImGui::GetIO().BackendPlatformUserData != nullptr;
    }
}

class ImguiContext {
    public:
        // Creates the ImGui context and its GLFW/OpenGL3 backends; null on failure.
        // Needs the window's GL context to be current.
        static std::unique_ptr<ImguiContext> create(GLFWwindow* window) noexcept {
            IMGUI_CHECKVERSION();
            if (!ImGui::CreateContext()) {log_error("ImGui::CreateContext failed"); return nullptr;}
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            ImGui::StyleColorsDark();

            if (!ImGui_ImplGlfw_InitForOpenGL(window, false)) {
                log_error("ImGui_ImplGlfw_InitForOpenGL failed");
                ImGui::DestroyContext();
                return nullptr;
            }
            if (!ImGui_ImplOpenGL3_Init(Platform::get_glsl_version())) {
                log_error("ImGui_ImplOpenGL3_Init failed");
                ImGui_ImplGlfw_Shutdown();
                ImGui::DestroyContext();
                return nullptr;
            }
            return std::unique_ptr<ImguiContext>(new ImguiContext(window));
        }

        // Starts an ImGui frame; call once per frame before any ImGui widgets.
        void beginFrame() noexcept {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

        // Ends the ImGui frame and draws it into the current framebuffer.
        void endFrame() noexcept {
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        // The active ImGui IO block, for config flags and input queries.
        ImGuiIO& io() const noexcept {return ImGui::GetIO();}

        // Draws a world-space tile grid over the scene, into the background draw
        // list. Window coordinates are scaled to ImGui's DisplaySize for HiDPI.
        template <typename Camera2DT>
        void drawGrid2D(int tileSize, Camera2DT& camera, ImColor color = IM_COL32(200, 200, 200, 100)) noexcept {
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
            int wWidth = 0, wHeight = 0;
            glfwGetWindowSize(_window, &wWidth, &wHeight);
            if (wWidth <= 0 || wHeight <= 0) return;

            const int maxLinesPerAxis = 200;
            const float upscaleX = displaySize.x / static_cast<float>(wWidth);
            const float upscaleY = displaySize.y / static_cast<float>(wHeight);

            auto [minWorldX, minWorldY, maxWorldX, maxWorldY] = camera.getCulledBounds();
            const int minTileX = static_cast<int>(std::floor(static_cast<float>(minWorldX) / tileSize));
            const int maxTileX = static_cast<int>(std::ceil(static_cast<float>(maxWorldX) / tileSize));
            const int minTileY = static_cast<int>(std::floor(static_cast<float>(minWorldY) / tileSize));
            const int maxTileY = static_cast<int>(std::ceil(static_cast<float>(maxWorldY) / tileSize));

            if (maxTileX - minTileX > maxLinesPerAxis) return;
            if (maxTileY - minTileY > maxLinesPerAxis) return;

            for (int tx = minTileX; tx <= maxTileX; ++tx) {
                const float worldX = static_cast<float>(tx * tileSize);
                const float screenX = static_cast<float>(camera.worldToScreen({worldX, 0.0f}).x) * upscaleX;
                dl->AddLine(ImVec2(screenX, 0.0f), ImVec2(screenX, displaySize.y), color);
            }

            for (int ty = minTileY; ty <= maxTileY; ++ty) {
                const float worldY = static_cast<float>(ty * tileSize);
                const float screenY = static_cast<float>(camera.worldToScreen({0.0f, worldY}).y) * upscaleY;
                dl->AddLine(ImVec2(0.0f, screenY), ImVec2(displaySize.x, screenY), color);
            }
        }

        // Draws a ground-plane grid into the background draw list. Segments are
        // clipped against the near plane in view space, where z = -nearPlane, so
        // a line crossing behind the camera is trimmed rather than wrapping.
        template <typename Camera3DT>
        void drawGrid3D(float tileSize, int gridHalfSize, Camera3DT& camera) noexcept {
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            const ImU32 color = IM_COL32(200, 200, 200, 80);

            const glm::mat4 view = camera.viewMatrix();
            const glm::mat4 proj = camera.projectionMatrix();
            int wWidth = 0, wHeight = 0;
            glfwGetWindowSize(_window, &wWidth, &wHeight);
            if (wWidth <= 0 || wHeight <= 0) return;

            // NDC maps onto ImGui's DisplaySize, which is the framebuffer on HiDPI
            // and the window elsewhere; drawGrid2D scales the same way.
            const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
            const float winW = displaySize.x;
            const float winH = displaySize.y;
            const float nearPlane = 0.1f;

            auto drawSegment = [&](glm::vec3 p1World, glm::vec3 p2World) {
                glm::vec4 v1 = view * glm::vec4(p1World, 1.0f);
                glm::vec4 v2 = view * glm::vec4(p2World, 1.0f);

                const float minZ = -nearPlane;
                if (v1.z > minZ && v2.z > minZ) return;

                if (v1.z > minZ) {
                    const float t = (minZ - v1.z) / (v2.z - v1.z);
                    v1 = v1 + t * (v2 - v1);
                } else if (v2.z > minZ) {
                    const float t = (minZ - v2.z) / (v1.z - v2.z);
                    v2 = v2 + t * (v1 - v2);
                }

                const glm::vec4 c1 = proj * v1;
                const glm::vec4 c2 = proj * v2;

                const glm::vec3 ndc1 = glm::vec3(c1) / c1.w;
                const glm::vec3 ndc2 = glm::vec3(c2) / c2.w;

                const ImVec2 s1((ndc1.x * 0.5f + 0.5f) * winW, (1.0f - (ndc1.y * 0.5f + 0.5f)) * winH);
                const ImVec2 s2((ndc2.x * 0.5f + 0.5f) * winW, (1.0f - (ndc2.y * 0.5f + 0.5f)) * winH);

                dl->AddLine(s1, s2, color);
            };

            const float extent = static_cast<float>(gridHalfSize) * tileSize;
            for (int i = -gridHalfSize; i <= gridHalfSize; ++i) {
                const float pos = static_cast<float>(i) * tileSize;
                drawSegment(glm::vec3(pos, 0.0f, -extent), glm::vec3(pos, 0.0f, extent));
                drawSegment(glm::vec3(-extent, 0.0f, pos), glm::vec3(extent, 0.0f, pos));
            }
        }

        ~ImguiContext() noexcept {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }

        ImguiContext(const ImguiContext&) = delete;
        ImguiContext& operator=(const ImguiContext&) = delete;
    private:
        explicit ImguiContext(GLFWwindow* window) noexcept : _window(window) {}

        GLFWwindow* _window;
};

#endif