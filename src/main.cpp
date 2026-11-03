#include <cstdio>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "app/App.h"
#include "bili/BiliService.h"
#include "ui/WindowFrame.h"

namespace {

void glfwErrorCallback(int code, const char* description) {
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), "GLFW error %d: %s", code,
                  description ? description : "(no description)");
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
    std::fputs(buffer, stderr);
    std::fputc('\n', stderr);
}

}  // namespace

int main(int argc, char** argv) {
    // 控制台自检：.ilimusic.exe --check-bili BV1xx411c7mD
    // 用来在不启动 UI 的情况下验证 Cookie / WBI 签名 / 取流链路。
    if (argc > 2 && std::string(argv[1]) == "--check-bili") {
        bili::BiliService service;
        service.init();
        const bili::NavInfo nav = service.fetchNav();
        std::printf("nav ok=%d loggedIn=%d uname=%s mid=%lld msg=%s\n", nav.ok ? 1 : 0,
                    nav.loggedIn ? 1 : 0, nav.userName.c_str(), nav.userId, nav.message.c_str());

        const std::string bvid = bili::BiliService::parseBvid(argv[2]);
        std::printf("bvid=%s\n", bvid.c_str());
        if (bvid.empty()) return 2;

        const bili::VideoInfo video = service.fetchVideo(bvid);
        std::printf("video ok=%d bvid=%s cid=%lld title=%s artist=%s msg=%s\n", video.ok ? 1 : 0,
                    video.bvid.c_str(), video.cid, video.title.c_str(), video.artist.c_str(),
                    video.message.c_str());
        if (!video.ok) return 3;

        const bili::AudioStreamInfo stream = service.resolveAudio(bvid, video.cid, 0);
        std::printf("stream ok=%d quality=%s downgraded=%d url=%.120s msg=%s\n", stream.ok ? 1 : 0,
                    stream.qualityLabel().c_str(), stream.downgraded ? 1 : 0, stream.url.c_str(),
                    stream.message.c_str());
        return stream.ok ? 0 : 4;
    }

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // 无边框窗口：标题栏由 Ui::DrawTitleBar 自己画（和 Java 版一致）
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    // 窗口标题和 Java 版保持一致（"bilimusic"），方便沿用旧的截图/自动化脚本
    GLFWwindow* window = glfwCreateWindow(1240, 860, "bilimusic", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetWindowSizeLimits(window, 1000, 640, GLFW_DONT_CARE, GLFW_DONT_CARE);
    // 无边框窗口交给原生窗口过程：标题栏原生拖动 + 四边原生缩放（Java 版被 AWT 挡住做不到）
    Ui::InstallWindowFrame(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // 不写 imgui.ini，窗口布局由代码决定
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    App app;
    app.init(window);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.draw();

        ImGui::Render();
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.039f, 0.039f, 0.047f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    app.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
