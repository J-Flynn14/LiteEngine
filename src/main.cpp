#include "app.h"
#include <memory>
#include <iostream>
#include <unordered_map>
#include <functional>

#include "light_2D.h"
#include "light_3D.h"
#include "renderer_2D.h"
#include "renderer_3D.h"
#include "demo.h"

int main() {
    //WindowConfig windowConfig;
    //windowConfig.title = "Game";
    //auto app = App::create(windowConfig);
    //app->mainLoop();
    lightEngineDemo();
};