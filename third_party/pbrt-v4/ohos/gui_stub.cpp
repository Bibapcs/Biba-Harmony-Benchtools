// Stub implementations of pbrt::GUI for the OpenHarmony (OHOS) port.
//
// pbrt's interactive rendering mode requires OpenGL/GLFW, which is not
// available on OHOS. The GUI methods are still referenced (behind runtime
// checks of Options->interactive / Options->fullscreen) from pbrt.cpp,
// film.cpp and wavefront/integrator.cpp, so we provide stub definitions
// that fail with a fatal error if they are ever actually called.
// This file is compiled instead of src/pbrt/util/gui.cpp.

#include <pbrt/util/gui.h>

#include <pbrt/util/error.h>

namespace pbrt {

GUI::GUI(std::string title, Vector2i resolution, Bounds3f sceneBounds) {
    ErrorExit("Interactive rendering (--interactive) is not supported in this "
              "build (no OpenGL/GLFW available).");
}

GUI::~GUI() {}

DisplayState GUI::RefreshDisplay() { return DisplayState::EXIT; }

void GUI::keyboardCallback(GLFWwindow *window, int key, int scan, int action, int mods) {}

void GUI::cursorPosCallback(GLFWwindow *window, double xpos, double ypos) {}

void GUI::mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {}

void GUI::Initialize() {}

Point2i GUI::GetResolution() {
    ErrorExit("--fullscreen is not supported in this build (no OpenGL/GLFW "
              "available).");
    return Point2i(0, 0);
}

bool GUI::processKeys() { return false; }

bool GUI::processMouse() { return false; }

bool GUI::process() { return false; }

}  // namespace pbrt
