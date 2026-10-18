#pragma once
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "app.h"
#include "camera_2D.h"
#include "camera_3D.h"
#include "mesh.h"
#include "light_2D.h"
#include "light_3D.h"
#include "renderer_2D.h"
#include "renderer_3D.h"

namespace {

// Materials are model-specific: BlinnPhong3D declares uGloss, MetallicRoughness3D
// declares uRoughness. Ask the model for a compatible one rather than naming the
// concrete type, then fill in whichever it turned out to be.
std::shared_ptr<Material> makeSurface(const LightingModel3D& model,
                                      const glm::vec3& albedo, float smoothness) {
    std::shared_ptr<Material> material = model.createMaterial();

    if (auto* pbr = dynamic_cast<MetallicRoughnessMaterial*>(material.get())) {
        pbr->albedo = albedo;
        pbr->roughness = 1.0f - smoothness;
        pbr->metallic = 0.0f;
    } else if (auto* phong = dynamic_cast<BlinnPhongMaterial*>(material.get())) {
        phong->albedo = albedo;
        phong->gloss = 4.0f + smoothness * 120.0f;
    } else if (auto* unlit = dynamic_cast<Unlit3DMaterial*>(material.get())) {
        unlit->color = albedo;
    }
    return material;
}

} // namespace

// Named rather than anonymous: the render lambda captures a TileTextures, and a closure
// in an inline function may not hold a type with internal linkage.
namespace demo_detail {

// An opaque bevelled stone tile with a central stud, plus its tangent-space normal map
// (green up), generated so the demo needs no image files. Images from disk need
// flipVertically = false for both, and premultiplyAlpha = false for the normal map.
struct TileTextures {
    std::unique_ptr<Texture> colour;
    std::unique_ptr<Texture> normals;
};

inline TileTextures makeTileTextures(int size) {
    const auto heightAt = [](float u, float v) {
        const float edge = std::min(std::min(u, 1.0f - u), std::min(v, 1.0f - v));
        const float bevel = glm::smoothstep(0.0f, 0.12f, edge);
        const float r = glm::length(glm::vec2(u - 0.5f, v - 0.5f));
        const float stud = (r < 0.22f) ? std::sqrt(0.22f * 0.22f - r * r) / 0.22f : 0.0f;
        return 0.6f * bevel + 0.5f * stud;
    };
    const auto toByte = [](float value) {
        return static_cast<unsigned char>(glm::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };

    std::vector<unsigned char> colour(static_cast<size_t>(size * size) * 4);
    std::vector<unsigned char> normals(colour.size());
    const float step = 1.0f / static_cast<float>(size);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) * step;
            const float v = (static_cast<float>(y) + 0.5f) * step;
            const float dhdu = (heightAt(u + step, v) - heightAt(u - step, v)) / (2.0f * step);
            const float dhdv = (heightAt(u, v + step) - heightAt(u, v - step)) / (2.0f * step);
            // Image rows run downwards, so "up" in the normal map is -v.
            const glm::vec3 n = glm::normalize(glm::vec3(-dhdu * 0.15f, dhdv * 0.15f, 1.0f));

            const float shade = 0.75f + 0.25f * glm::smoothstep(0.0f, 0.12f,
                std::min(std::min(u, 1.0f - u), std::min(v, 1.0f - v)));
            const size_t i = static_cast<size_t>(y * size + x) * 4;
            colour[i + 0] = toByte(0.70f * shade);
            colour[i + 1] = toByte(0.66f * shade);
            colour[i + 2] = toByte(0.60f * shade);
            colour[i + 3] = 255;
            normals[i + 0] = toByte(n.x * 0.5f + 0.5f);
            normals[i + 1] = toByte(n.y * 0.5f + 0.5f);
            normals[i + 2] = toByte(n.z * 0.5f + 0.5f);
            normals[i + 3] = 255;
        }
    }

    TextureSpec spec;
    spec.width = size;
    spec.height = size;
    TileTextures tile;
    tile.colour = Texture::createEmpty(spec);
    tile.normals = Texture::createEmpty(spec);
    if (tile.colour) tile.colour->setData(colour.data(), GL_RGBA, GL_UNSIGNED_BYTE);
    if (tile.normals) tile.normals->setData(normals.data(), GL_RGBA, GL_UNSIGNED_BYTE);
    return tile;
}

} // namespace demo_detail

inline int lightEngineDemo() {
    WindowConfig windowConfig;
    windowConfig.title = "Lighting demo - TAB 2D/3D, M model, R reflection, G shadows, I instancing, F fullscreen";
    windowConfig.size = {800, 600};
    windowConfig.createImGuiContext = true;
    windowConfig.glfwState.glfw_depth_bits = 24;

    std::unique_ptr<App> app = App::create(windowConfig);
    if (!app) return 1;

    Window& window = *app->mainWindow();

    // ---- cameras ----

    std::unique_ptr<Camera3D> camera3D = Camera3D::create(window);
    std::unique_ptr<Camera2D> camera2D = Camera2D::create(window);

    // ---- meshes ----

    std::unique_ptr<Mesh> cube = Mesh::create(Mesh3D::cube(1.0f));
    std::unique_ptr<Mesh> ground = Mesh::create(Mesh3D::quad(40.0f, 40.0f));
    std::unique_ptr<Mesh> grid = Mesh::create(Mesh3D::gridLines(20.0f, 40));
    std::unique_ptr<Mesh> quad = Mesh::create(Mesh2D::quad(120.0f, 120.0f));
    if (!cube || !ground || !grid || !quad) return 1;

    std::unique_ptr<Shader> gridShader = shader_lib::grid_shader();
    if (!gridShader) return 1;

    // A 1x1 white texture, so the quad path shows its per-quad tint with no
    // asset to load. Swap in a real atlas and use UVRect::cell for sprites.
    std::unique_ptr<Texture> sprite = Texture::createSolidColor(TextureSpec{}, {255, 255, 255});
    demo_detail::TileTextures stoneTile = demo_detail::makeTileTextures(64);

    // ---- 3D lighting ----

    std::unique_ptr<LightingPipeline3D> lighting3D = LightingPipeline3D::create();
    lighting3D->environment().setGradient({0.28f, 0.38f, 0.52f},
                                          {0.20f, 0.21f, 0.24f},
                                          {0.06f, 0.05f, 0.05f});
    lighting3D->environment().setIntensity(0.9f);
    // 2048 texels over a 24-unit box that follows the camera: about 1.2 cm per texel,
    // against 2.7 cm for the old fixed 1024 x 28-unit box.
    lighting3D->setShadowResolution(2048);
    lighting3D->setDirectionalExtent(12.0f);
    lighting3D->setShadowPolygonOffset(2.5f, 4.0f);
    lighting3D->setToneMap(ToneMap::ACES);
    lighting3D->setModel(MetallicRoughness3D::create());

    auto planarReflection = ReflectionPlanar::create({0.0f, 1.0f, 0.0f}, 0.0f);
    std::vector<std::shared_ptr<ReflectionSource>> reflectionModes = {
        ReflectionEnvironment::create(),
        planarReflection,
        ReflectionScreenSpace::create(),
    };
    int reflectionIndex = 0;
    lighting3D->setReflection(reflectionModes[0]);

    ShadowSettings softShadows;
    softShadows.type = ShadowType::Soft;
    softShadows.strength = 0.85f;

    std::unique_ptr<Light3D> sun =
        Light3D::createDirectional({-0.4f, -1.0f, -0.3f}, {1.0f, 0.96f, 0.88f}, 2.2f);
    sun->setShadows(softShadows);

    std::unique_ptr<Light3D> spot =
        Light3D::createSpot({0.0f, 8.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 25.0f, 30.0f,
                            {0.4f, 0.7f, 1.0f}, 12.0f);
    spot->setShadows(softShadows);

    // Point lights cannot claim a shadow slice: shadowViewProjection does not
    // cover the six-face case, so this one stays unshadowed.
    std::unique_ptr<Light3D> orbiter =
        Light3D::createPoint({0.0f, 2.0f, 0.0f}, 14.0f, {1.0f, 0.35f, 0.15f}, 8.0f);

    // ---- 2D lighting ----

    std::unique_ptr<LightingPipeline2D> lighting2D = LightingPipeline2D::create();
    lighting2D->environment().setColor({0.10f, 0.11f, 0.16f});

    std::unique_ptr<Light2D> torch =
        Light2D::createPoint({0.0f, 0.0f}, 320.0f, {1.0f, 0.55f, 0.2f}, 2.2f);
    std::unique_ptr<Light2D> lamp =
        Light2D::createSpot({-250.0f, 180.0f}, {1.0f, -1.0f}, 500.0f, 35.0f,
                            {0.5f, 0.8f, 1.0f}, 2.0f);

    // ---- renderers ----

    Renderer3D renderer3D;
    Renderer2D renderer2D;

    // A unit cube's bounding sphere; the ground quad's is its half-diagonal.
    const float cubeRadius = 0.8660254f;
    const float groundRadius = 28.2842712f;

    std::shared_ptr<Material> cubeMaterial;
    std::shared_ptr<Material> groundMaterial;
    std::shared_ptr<Material> instanceMaterial;

    // Materials belong to a model, so switching the model invalidates them.
    auto rebuildMaterials = [&]() {
        const LightingModel3D& model = lighting3D->model();
        cubeMaterial = makeSurface(model, {0.85f, 0.82f, 0.78f}, 0.75f);
        groundMaterial = makeSurface(model, {0.35f, 0.36f, 0.38f}, 0.25f);
        instanceMaterial = makeSurface(model, {0.30f, 0.55f, 0.35f}, 0.15f);
    };
    rebuildMaterials();

    bool mode3D = true;
    bool usePBR = true;
    bool showInstances = true;
    float elapsed = 0.0f;

    FrameCallbacks callbacks;

    // ---- input and simulation ----

    callbacks.preFrame = [&](const FrameContext& f) {
        Input& input = f.window.input();
        elapsed += f.dt;

        if (input.wasKeyPressed(GLFW_KEY_F)) {f.window.toggleFullscreen();}
        if (input.wasKeyPressed(GLFW_KEY_TAB)) {mode3D = !mode3D;}
        if (input.wasKeyPressed(GLFW_KEY_I)) {showInstances = !showInstances;}
        if (input.wasKeyPressed(GLFW_KEY_G)) {
            lighting3D->setShadowsEnabled(!lighting3D->shadowsEnabled());
        }
        if (input.wasKeyPressed(GLFW_KEY_M)) {
            usePBR = !usePBR;
            if (usePBR) lighting3D->setModel(MetallicRoughness3D::create());
            else        lighting3D->setModel(nullptr);   // restores BlinnPhong3D
            rebuildMaterials();
        }
        if (input.wasKeyPressed(GLFW_KEY_R)) {
            reflectionIndex = (reflectionIndex + 1) % static_cast<int>(reflectionModes.size());
            lighting3D->setReflection(reflectionModes[static_cast<size_t>(reflectionIndex)]);
        }

        if (mode3D) {
            camera3D->handleMovement(f.dt);
            orbiter->setPosition({std::cos(elapsed) * 4.0f, 1.6f, std::sin(elapsed) * 4.0f});
            spot->setDirection({std::sin(elapsed * 0.7f) * 0.4f, -1.0f, std::cos(elapsed * 0.7f) * 0.4f});
        } else {
            camera2D->handleMovement(f.dt);
            torch->setPosition({std::cos(elapsed * 0.8f) * 220.0f, std::sin(elapsed * 0.8f) * 160.0f});
        }
    };

    // ---- drawing ----

    callbacks.render = [&](const FrameContext& f) {
        f.window.clear({0.02f, 0.02f, 0.03f, 1.0});

        if (mode3D) {
            glEnable(GL_DEPTH_TEST);

            // Centre the sun's shadow box on the ground just ahead of the camera, so its
            // texels go where you are looking. The pipeline snaps it to whole texels.
            const glm::mat4 view = camera3D->viewMatrix();
            const glm::vec3 eye = camera3D->getPosition();
            glm::vec3 ahead(-view[0][2], 0.0f, -view[2][2]);
            ahead = (glm::length(ahead) > 1e-4f) ? glm::normalize(ahead) : glm::vec3(0.0f);
            lighting3D->setShadowFocus(glm::vec3(eye.x, 0.0f, eye.z)
                                       + ahead * (lighting3D->directionalExtent() * 0.6f));

            // Shadow slices are claimed in add() order, so casters go first.
            lighting3D->clear();
            lighting3D->add(*sun);
            lighting3D->add(*spot);
            lighting3D->add(*orbiter);

            // ---- build the frame's geometry once ----

            renderer3D.clear();

            Renderable groundItem;
            groundItem.mesh = ground.get();
            // Mesh3D::quad faces +Z, so tip it to face +Y.
            groundItem.transform.rotation =
                glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            groundItem.material = groundMaterial;
            groundItem.boundsRadius = groundRadius;
            // Receives shadows but has no business casting one, and skipping it
            // removes a 40x40 quad from every shadow slice.
            groundItem.castsShadow = false;
            renderer3D.submit(groundItem);

            for (int i = 0; i < 6; ++i) {
                const float angle = glm::radians(60.0f * static_cast<float>(i));
                const float bob = std::sin(elapsed * 1.3f + angle) * 0.35f;
                Renderable item;
                item.mesh = cube.get();
                item.transform = Transform3D::axisAngle(
                    {std::cos(angle) * 3.0f, 1.1f + bob, std::sin(angle) * 3.0f},
                    {0.0f, 1.0f, 0.0f}, elapsed * 0.5f + angle);
                item.material = cubeMaterial;
                item.boundsRadius = cubeRadius;
                renderer3D.submit(item);
            }

            if (showInstances) {
                InstancedBatch batch;
                batch.mesh = cube.get();
                batch.material = instanceMaterial;
                batch.boundsRadius = cubeRadius;
                batch.instances.reserve(21 * 21);
                for (int z = -10; z <= 10; ++z) {
                    for (int x = -10; x <= 10; ++x) {
                        if (std::abs(x) < 5 && std::abs(z) < 5) continue;   // keep the centre clear
                        Transform3D t = Transform3D::at(
                            {static_cast<float>(x) * 1.6f, 0.25f, static_cast<float>(z) * 1.6f}, 0.4f);
                        t.rotation = glm::angleAxis(elapsed * 0.3f + static_cast<float>(x + z),
                                                    glm::vec3(0.0f, 1.0f, 0.0f));
                        batch.instances.push_back(MeshInstance::from(t));
                    }
                }
                renderer3D.submitInstanced(std::move(batch));
            }

            // ---- shadow pass: the same geometry, per slice ----

            lighting3D->renderShadows([&](const SceneContext& context) {
                renderer3D.draw(context);
                renderer3D.drawInstanced(context);
            });

            // ---- lit pass ----

            const glm::mat4 viewProjection = camera3D->projectionMatrix() * camera3D->viewMatrix();

            if (const Shader* shader = lighting3D->shader()) {
                shader->use();
                lighting3D->upload(*shader);
                shader->setMat4("uViewProjection", viewProjection);
                shader->setVec3("uViewPosition", camera3D->getPosition());

                SceneContext lit;
                lit.shader = shader;
                lit.viewProjection = viewProjection;
                renderer3D.draw(lit);
            }

            if (showInstances) {
                if (const Shader* instanced = lighting3D->shader(true)) {
                    instanced->use();
                    lighting3D->upload(*instanced);
                    instanced->setMat4("uViewProjection", viewProjection);
                    instanced->setVec3("uViewPosition", camera3D->getPosition());

                    SceneContext lit;
                    lit.shader = instanced;
                    lit.viewProjection = viewProjection;
                    renderer3D.drawInstanced(*instanced, lit);
                }
            }

            // Real geometry, not the ImGui overlay: ImGui submits its draw lists
            // in endFrame() with depth testing off, so an ImGui grid paints over
            // solid objects and makes them look transparent.
            gridShader->use();
            gridShader->setMat4("uViewProjection", viewProjection);
            gridShader->setVec4("uColor", {0.45f, 0.47f, 0.52f, 1.0f});
            gridShader->setFloat("uHeight", 0.01f);
            grid->draw();
        } else {
            glDisable(GL_DEPTH_TEST);

            lighting2D->clear();
            lighting2D->add(*torch);
            lighting2D->add(*lamp);

            renderer2D.clear();

            std::shared_ptr<Material> tile = lighting2D->model().createMaterial();
            std::shared_ptr<Material> tileDark = lighting2D->model().createMaterial();
            if (auto* flat = dynamic_cast<Flat2DMaterial*>(tile.get())) {
                flat->albedo = {0.90f, 0.90f, 0.92f};
            }
            if (auto* flat = dynamic_cast<Flat2DMaterial*>(tileDark.get())) {
                flat->albedo = {0.55f, 0.50f, 0.48f};
            }

            for (int y = -1; y <= 1; ++y) {
                for (int x = -1; x <= 1; ++x) {
                    Renderable2D item;
                    item.mesh = quad.get();
                    item.transform = Transform2D::at({static_cast<float>(x) * 160.0f,
                                                      static_cast<float>(y) * 160.0f});
                    item.material = ((x + y) % 2 == 0) ? tile : tileDark;
                    item.boundsRadius = 85.0f;
                    renderer2D.submit(item);
                }
            }

            const glm::mat4 viewProjection2D = camera2D->projectionMatrix();

            if (const Shader* shader = lighting2D->shader()) {
                shader->use();
                lighting2D->upload(*shader);
                shader->setMat4("uViewProjection", viewProjection2D);

                SceneContext lit;
                lit.shader = shader;
                lit.viewProjection = viewProjection2D;
                renderer2D.draw(*shader, lit);
            }

            // A row of stone tiles: the left three plain, the right three normal-mapped,
            // so the torch shows the difference as it passes over them.
            if (stoneTile.colour && stoneTile.normals) {
                for (int i = 0; i < 6; ++i) {
                    const QuadRect rect{-236.0f + static_cast<float>(i) * 80.0f, -36.0f, 72.0f, 72.0f};
                    if (i < 3) renderer2D.submitQuad(*stoneTile.colour, rect);
                    else       renderer2D.submitNormalMappedQuad(*stoneTile.colour, *stoneTile.normals, rect);
                }
            }

            // Sprite quads use the 2D pipeline's sprite variant, so the torch and
            // lamp light them the same way they light the tiles. Its output is
            // premultiplied, so blend accordingly.
            if (sprite) {
                for (int i = 0; i < 24; ++i) {
                    const float angle = elapsed * 0.4f + static_cast<float>(i) * 0.26f;
                    const float radius = 260.0f + std::sin(elapsed + static_cast<float>(i)) * 30.0f;
                    renderer2D.submitQuad(*sprite,
                        QuadRect{std::cos(angle) * radius, std::sin(angle) * radius, 18.0f, 18.0f},
                        UVRect{},
                        QuadColor::fromFloats(1.0f, 0.75f, 0.35f, 0.9f));
                }
            }

            if (const Shader* spriteShader = lighting2D->spriteShader()) {
                spriteShader->use();
                lighting2D->upload(*spriteShader);
                spriteShader->setMat4("uViewProjection", viewProjection2D);
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                renderer2D.drawQuads(*spriteShader, 0);
                glDisable(GL_BLEND);
            }

            #if IMGUI_ENABLED
                if (f.window.imGui()) {f.window.imGui()->drawGrid2D(64, *camera2D);}
            #endif
        }
    };

    // Polling, frame begin/end, buffer swap and frame pacing are handled by App.
    app->mainLoop(callbacks);

    return 0;
}