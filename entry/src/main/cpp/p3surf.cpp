// p3surf.cpp — XComponent native surface 的广色域渲染面
//
// 渲染面获取：官方 OH_NativeXComponent 回调路径（XComponent 声明 libraryname='entry'，
// 框架加载本 so 时在 napi exports 上挂 __NATIVE_XCOMPONENT_OBJ__，unwrap 得到
// OH_NativeXComponent* 后注册 OnSurfaceCreated/Changed/Destroyed 回调，由回调直接发
// OHNativeWindow* 句柄，尺寸用 OH_NativeXComponent_GetXComponentSize 获取）。
// 历史教训：OH_NativeWindow_CreateNativeWindowFromSurfaceId(surfaceId) 这条路在本机
// EGL 全部调用成功（init OK / first swap OK）但合成器不出图（全黑），疑为非官方路径
// 的 buffer 队列未正确挂接——已废弃，勿回退。
//
// 色彩空间：OH_NativeWindow_SetColorSpace 把 surface 标记为 sRGB / Display P3
// （OH_COLORSPACE_SRGB_FULL / OH_COLORSPACE_P3_FULL，API 12+，external_window.h），
// 合成器按标签把内容映射到面板。真机实测：PixelMap.setColorSpace 与 ArkWeb
// CSS display-p3 均被忽略；EGL_GL_COLORSPACE_KHR 的 eglSurfaceAttrib 在本机报
// 0x3004（EGL_BAD_ATTRIBUTE）不支持 window surface——所以走 NativeWindow 这条。
//
// NAPI 导出：
//   p3surfInit(): string              在已拿到的 NativeWindow 上初始化 EGL；
//                                     返回 "OK 宽x高"；surface 未就绪返回
//                                     "ERR noWindow 0x0"（调用方可轮询重试）
//   p3surfShow(r, g, b, p3): void     r/g/b 为 0..1；p3=true 打 P3_FULL，否则 SRGB_FULL
//   p3surfRelease(): void             释放 EGL（window 句柄归框架所有，随
//                                     OnSurfaceDestroyed 失效，本模块不 Destroy）

#include "napi/native_api.h"

#include <cstdint>
#include <cstdio>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <native_window/external_window.h>
#include <native_buffer/buffer_common.h>
#include <native_buffer/native_buffer.h>
#include <hilog/log.h>
#include <unistd.h>

#undef LOG_TAG
#define LOG_TAG "p3surf"

namespace {

OHNativeWindow* g_window = nullptr; // 归 XComponent 框架所有，本模块不 Destroy
int32_t g_compW = 0;                // OnSurfaceCreated/Changed 报告的 surface 尺寸（像素）
int32_t g_compH = 0;
EGLDisplay g_dpy = EGL_NO_DISPLAY;
EGLContext g_ctx = EGL_NO_CONTEXT;
EGLSurface g_surf = EGL_NO_SURFACE;
int32_t g_eglW = -1;                // EGL surface 创建时用的尺寸（变化时重建）
int32_t g_eglH = -1;
bool g_firstSwapLogged = false;
bool g_tagEnabled = true; // 是否打色彩空间标签（自测 A/B：标签可能被合成器丢弃致全黑）

napi_value retOkOrErr(napi_env env, const char* stage, EGLint err)
{
    char buf[128];
    if (stage == nullptr) {
        snprintf(buf, sizeof(buf), "OK");
    } else {
        snprintf(buf, sizeof(buf), "ERR %s 0x%x", stage, (unsigned int)err);
        OH_LOG_ERROR(LOG_APP, "p3surf %{public}s failed, err=0x%{public}x", stage, (unsigned int)err);
    }
    napi_value ret;
    napi_create_string_utf8(env, buf, NAPI_AUTO_LENGTH, &ret);
    return ret;
}

/* 只释放 EGL 资源；g_window 属于框架，不在此销毁 */
void releaseEgl()
{
    g_firstSwapLogged = false;
    g_eglW = -1;
    g_eglH = -1;
    if (g_dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_ctx != EGL_NO_CONTEXT) {
            eglDestroyContext(g_dpy, g_ctx);
            g_ctx = EGL_NO_CONTEXT;
        }
        if (g_surf != EGL_NO_SURFACE) {
            eglDestroySurface(g_dpy, g_surf);
            g_surf = EGL_NO_SURFACE;
        }
        eglTerminate(g_dpy);
        g_dpy = EGL_NO_DISPLAY;
    }
}

void onSurfaceCreated(OH_NativeXComponent* component, void* window)
{
    g_window = static_cast<OHNativeWindow*>(window);
    uint64_t w = 0, h = 0;
    OH_NativeXComponent_GetXComponentSize(component, window, &w, &h);
    g_compW = (int32_t)w;
    g_compH = (int32_t)h;
    OH_LOG_ERROR(LOG_APP, "p3surf onSurfaceCreated window=%{public}p %{public}dx%{public}d",
                 g_window, (int)g_compW, (int)g_compH);
}

void onSurfaceChanged(OH_NativeXComponent* component, void* window)
{
    uint64_t w = 0, h = 0;
    OH_NativeXComponent_GetXComponentSize(component, window, &w, &h);
    OH_LOG_ERROR(LOG_APP, "p3surf onSurfaceChanged %{public}dx%{public}d", (int)w, (int)h);
    g_compW = (int32_t)w;
    g_compH = (int32_t)h;
    /* 尺寸变化且 EGL 已按旧尺寸创建：释放，等调用方重新 p3surfInit */
    if (g_surf != EGL_NO_SURFACE && ((int32_t)w != g_eglW || (int32_t)h != g_eglH)) {
        OH_LOG_ERROR(LOG_APP, "p3surf size changed, release EGL for re-init");
        releaseEgl();
    }
}

void onSurfaceDestroyed(OH_NativeXComponent* component, void* window)
{
    (void)component;
    (void)window;
    OH_LOG_ERROR(LOG_APP, "p3surf onSurfaceDestroyed");
    releaseEgl();
    g_window = nullptr;
    g_compW = 0;
    g_compH = 0;
}

napi_value p3surfInit(napi_env env, napi_callback_info info)
{
    (void)info;
    if (g_window == nullptr) {
        /* XComponent 尚未回调 OnSurfaceCreated，调用方可稍后重试 */
        return retOkOrErr(env, "noWindow", 0);
    }

    /* 重复 init 先释放旧 EGL（例如再次进入测量页） */
    releaseEgl();

    /* 注意：不要调 SET_BUFFER_GEOMETRY——回调路径下框架报告的 surface 尺寸
       本来就是对的，官方 sample 均不设置；历史版本设置过，疑似与全黑有关 */
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY) {
        EGLint e = eglGetError();
        releaseEgl();
        return retOkOrErr(env, "getDisplay", e);
    }
    if (!eglInitialize(g_dpy, nullptr, nullptr)) {
        EGLint e = eglGetError();
        releaseEgl();
        return retOkOrErr(env, "initialize", e);
    }

    /* RGBA8888 window surface，GLES2 可渲染 */
    const EGLint configAttrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(g_dpy, configAttrs, &config, 1, &numConfigs) || numConfigs < 1) {
        EGLint e = eglGetError();
        releaseEgl();
        return retOkOrErr(env, "chooseConfig", e);
    }
    {
        /* 诊断：实际选中的像素格式（防止非 RGBA8888 导致渲染异常） */
        EGLint cr = -1, cg = -1, cb = -1, ca = -1, cvid = -1;
        eglGetConfigAttrib(g_dpy, config, EGL_RED_SIZE, &cr);
        eglGetConfigAttrib(g_dpy, config, EGL_GREEN_SIZE, &cg);
        eglGetConfigAttrib(g_dpy, config, EGL_BLUE_SIZE, &cb);
        eglGetConfigAttrib(g_dpy, config, EGL_ALPHA_SIZE, &ca);
        eglGetConfigAttrib(g_dpy, config, EGL_NATIVE_VISUAL_ID, &cvid);
        OH_LOG_ERROR(LOG_APP, "p3surf config RGBA=%{public}d/%{public}d/%{public}d/%{public}d visual=%{public}d",
                     (int)cr, (int)cg, (int)cb, (int)ca, (int)cvid);
    }

    g_surf = eglCreateWindowSurface(g_dpy, config, (EGLNativeWindowType)g_window, nullptr);
    if (g_surf == EGL_NO_SURFACE) {
        EGLint e = eglGetError();
        releaseEgl();
        return retOkOrErr(env, "createWindowSurface", e);
    }

    const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, config, EGL_NO_CONTEXT, ctxAttrs);
    if (g_ctx == EGL_NO_CONTEXT) {
        EGLint e = eglGetError();
        releaseEgl();
        return retOkOrErr(env, "createContext", e);
    }

    if (!eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx)) {
        EGLint e = eglGetError();
        releaseEgl();
        return retOkOrErr(env, "makeCurrent", e);
    }

    /* 诊断：记录 surface 实际尺寸 */
    EGLint w = -1, h = -1;
    eglQuerySurface(g_dpy, g_surf, EGL_WIDTH, &w);
    eglQuerySurface(g_dpy, g_surf, EGL_HEIGHT, &h);
    g_eglW = w;
    g_eglH = h;

    /* 预热：连续做 3 次黑帧 swap。本机 EGL 从未 swap 过时，RAW 路径的
       RequestBuffer 报 40601000（buffer 队列未挂接/未分配）；只 swap 1 次
       则 RequestBuffer 能过但 MapWaitFence 失败（2026-08-12 实测：自测里
       RAW 遍在 EGL 遍 12 次 swap 之后正常，测量里仅预热 1 次则 map failed）。
       预热 3 次（>= 常见队列深度）把队列完全挂上，黑帧一闪无害。 */
    for (int i = 0; i < 3; i++) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!eglSwapBuffers(g_dpy, g_surf)) {
            OH_LOG_ERROR(LOG_APP, "p3surf warmup swap %{public}d failed: 0x%{public}x",
                         i, (unsigned int)eglGetError());
        }
    }

    OH_LOG_ERROR(LOG_APP, "p3surf init OK, surface %{public}dx%{public}d", (int)w, (int)h);
    napi_value ret;
    char okbuf[64];
    snprintf(okbuf, sizeof(okbuf), "OK %dx%d", (int)w, (int)h);
    napi_create_string_utf8(env, okbuf, NAPI_AUTO_LENGTH, &ret);
    return ret;
}

napi_value p3surfShow(napi_env env, napi_callback_info info)
{
    size_t argc = 4;
    napi_value args[4] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    double r = 0.0, g = 0.0, b = 0.0;
    bool p3 = false;
    napi_get_value_double(env, args[0], &r);
    napi_get_value_double(env, args[1], &g);
    napi_get_value_double(env, args[2], &b);
    napi_get_value_bool(env, args[3], &p3);

    if (g_dpy == EGL_NO_DISPLAY || g_surf == EGL_NO_SURFACE) {
        OH_LOG_ERROR(LOG_APP, "p3surf show before init");
        return nullptr;
    }

    /* 每次显示前重打色彩空间标签：NativeWindow 级色彩空间（API 12+），
       合成器按标签把该 surface 的内容映射到面板。
       g_tagEnabled=false 时跳过（自测 A/B：排查标签导致全黑的可能） */
    if (g_tagEnabled) {
        OH_NativeBuffer_ColorSpace cs = p3 ? OH_COLORSPACE_P3_FULL : OH_COLORSPACE_SRGB_FULL;
        int32_t csret = OH_NativeWindow_SetColorSpace(g_window, cs);
        if (csret != 0) {
            OH_LOG_ERROR(LOG_APP, "p3surf SetColorSpace(%{public}s) failed: %{public}d",
                         p3 ? "P3" : "SRGB", csret);
        }
    }

    glClearColor((GLfloat)r, (GLfloat)g, (GLfloat)b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!g_firstSwapLogged) {
        g_firstSwapLogged = true;
        /* 诊断：swap 前读回 GPU 侧中心像素（swap 后背缓冲内容未定义，必须在 swap 前读）——
           是红说明 GL 已画上，问题在呈现链路；是黑说明 GL 状态本身有问题 */
        unsigned char px[4] = {0};
        glReadPixels(g_eglW / 2, g_eglH / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        OH_LOG_ERROR(LOG_APP,
                     "p3surf pre-swap readback RGBA=%{public}d,%{public}d,%{public}d,%{public}d glErr=0x%{public}x",
                     (int)px[0], (int)px[1], (int)px[2], (int)px[3], (unsigned int)glGetError());
    }
    if (!eglSwapBuffers(g_dpy, g_surf)) {
        OH_LOG_ERROR(LOG_APP, "p3surf eglSwapBuffers failed: 0x%{public}x",
                     (unsigned int)eglGetError());
    }
    return nullptr;
}

/* 诊断用原始 buffer 路径：完全绕过 EGL，直接 RequestBuffer + CPU 填色 + FlushBuffer。
   此路径出颜色而 EGL 不出 → EGL 特有问题；此路径也黑 → surface/队列层问题。
   色彩空间标签同样打在 window 上，与 EGL 路径一致。 */
napi_value p3surfShowRaw(napi_env env, napi_callback_info info)
{
    size_t argc = 4;
    napi_value args[4] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    double r = 0.0, g = 0.0, b = 0.0;
    bool p3 = false;
    napi_get_value_double(env, args[0], &r);
    napi_get_value_double(env, args[1], &g);
    napi_get_value_double(env, args[2], &b);
    napi_get_value_bool(env, args[3], &p3);

    if (g_window == nullptr || g_compW <= 0 || g_compH <= 0) {
        OH_LOG_ERROR(LOG_APP, "p3surf raw show before surface ready");
        return nullptr;
    }

    if (g_tagEnabled) {
        OH_NativeBuffer_ColorSpace cs = p3 ? OH_COLORSPACE_P3_FULL : OH_COLORSPACE_SRGB_FULL;
        OH_NativeWindow_SetColorSpace(g_window, cs);
    }

    /* 队列冷态（预热 swap 次数少）时，队列里的 buffer 可能分在 GPU-only 堆
       （2026-08-12 实测：测量流程 MapWaitFence 报 40001000，fenceFd=-1）。
       强制 CPU 可映射 usage，让后续分配的 buffer 能 CPU 填色。
       raw 路径要求先 SET_BUFFER_GEOMETRY（external_window.h RequestBuffer 文档） */
    OH_NativeWindow_NativeWindowHandleOpt(g_window, SET_USAGE,
        (uint64_t)(NATIVEBUFFER_USAGE_CPU_READ_OFTEN | NATIVEBUFFER_USAGE_CPU_WRITE |
                   NATIVEBUFFER_USAGE_MEM_DMA));
    OH_NativeWindow_NativeWindowHandleOpt(g_window, SET_BUFFER_GEOMETRY, g_compW, g_compH);

    OHNativeWindowBuffer* wbuf = nullptr;
    int fenceFd = -1;
    int32_t ret = OH_NativeWindow_NativeWindowRequestBuffer(g_window, &wbuf, &fenceFd);
    if (ret != 0 || wbuf == nullptr) {
        OH_LOG_ERROR(LOG_APP, "p3surf raw RequestBuffer failed: %{public}d", ret);
        return nullptr;
    }

    OH_NativeBuffer* nb = nullptr;
    OH_NativeBuffer_FromNativeWindowBuffer(wbuf, &nb);
    void* vir = nullptr;
    int32_t mapRet = -1;
    if (nb != nullptr) {
        /* fenceFd=-1（buffer 全新、无释放 fence）时 MapWaitFence 报
           40001000 INVALID_ARGUMENTS（2026-08-12 实测，测量流程队列冷态）；
           改走无 fence 的 OH_NativeBuffer_Map */
        mapRet = fenceFd >= 0 ? OH_NativeBuffer_MapWaitFence(nb, fenceFd, &vir)
                              : OH_NativeBuffer_Map(nb, &vir);
    }
    if (nb == nullptr || mapRet != 0 || vir == nullptr) {
        OH_LOG_ERROR(LOG_APP,
            "p3surf raw map failed: nb=0x%{public}x fenceFd=%{public}d mapRet=%{public}d",
            (unsigned int)(uintptr_t)nb, fenceFd, (int)mapRet);
        if (fenceFd >= 0) {
            close(fenceFd);
        }
        /* 自愈：队列未完全挂接时（预热不足）补一次 EGL swap 推进队列，
           后续帧的 RequestBuffer/Map 更可能成功 */
        if (g_dpy != EGL_NO_DISPLAY && g_surf != EGL_NO_SURFACE) {
            eglSwapBuffers(g_dpy, g_surf);
        }
        return nullptr;
    }
    OH_NativeBuffer_Config cfg;
    OH_NativeBuffer_GetConfig(nb, &cfg);

    const uint8_t cr = (uint8_t)(r * 255.0 + 0.5);
    const uint8_t cg = (uint8_t)(g * 255.0 + 0.5);
    const uint8_t cb = (uint8_t)(b * 255.0 + 0.5);
    static bool s_rawLogged = false;
    if (!s_rawLogged) {
        s_rawLogged = true;
        OH_LOG_ERROR(LOG_APP,
                     "p3surf raw cfg %{public}dx%{public}d fmt=%{public}d stride=%{public}d fill=%{public}d,%{public}d,%{public}d",
                     (int)cfg.width, (int)cfg.height, (int)cfg.format, (int)cfg.stride,
                     (int)cr, (int)cg, (int)cb);
    }
    for (int32_t y = 0; y < cfg.height; y++) {
        uint8_t* row = (uint8_t*)vir + (size_t)y * (size_t)cfg.stride;
        for (int32_t x = 0; x < cfg.width; x++) {
            row[x * 4] = cr;
            row[x * 4 + 1] = cg;
            row[x * 4 + 2] = cb;
            row[x * 4 + 3] = 255;
        }
    }
    OH_NativeBuffer_Unmap(nb);
    /* 注意：不要 OH_NativeBuffer_Unreference(nb)——FromNativeWindowBuffer 只是包装，
       不增加引用计数，buffer 归 surface 队列持有；Unreference 会 use-after-free
       （2026-08-12 真机实测：第 3 帧进程崩溃） */

    Region region;
    region.rects = nullptr;   /* 脏区域为空 = 整帧 */
    region.rectNumber = 0;
    ret = OH_NativeWindow_NativeWindowFlushBuffer(g_window, wbuf, fenceFd, region);
    if (ret != 0) {
        OH_LOG_ERROR(LOG_APP, "p3surf raw FlushBuffer failed: %{public}d", ret);
    }
    return nullptr;
}

napi_value p3surfSetTag(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    bool en = true;
    napi_get_value_bool(env, args[0], &en);
    g_tagEnabled = en;
    OH_LOG_ERROR(LOG_APP, "p3surf tag %{public}s", en ? "ON" : "OFF");
    return nullptr;
}

napi_value p3surfRelease(napi_env env, napi_callback_info info)
{
    (void)env;
    (void)info;
    releaseEgl();
    return nullptr;
}

} // namespace

void p3surf_register(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        {"p3surfInit", nullptr, p3surfInit, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"p3surfShow", nullptr, p3surfShow, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"p3surfRelease", nullptr, p3surfRelease, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"p3surfSetTag", nullptr, p3surfSetTag, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"p3surfShowRaw", nullptr, p3surfShowRaw, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);

    /* XComponent 以 libraryname='entry' 加载本 so 时，框架会在 exports 上挂
       __NATIVE_XCOMPONENT_OBJ__（包裹 OH_NativeXComponent*）；普通 napi 导入没有，跳过 */
    napi_value exportInstance = nullptr;
    if (napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance) != napi_ok ||
        exportInstance == nullptr) {
        return;
    }
    napi_valuetype vt = napi_undefined;
    napi_typeof(env, exportInstance, &vt);
    if (vt != napi_object && vt != napi_external) {
        return;
    }
    OH_NativeXComponent* comp = nullptr;
    if (napi_unwrap(env, exportInstance, reinterpret_cast<void**>(&comp)) != napi_ok || comp == nullptr) {
        OH_LOG_ERROR(LOG_APP, "p3surf unwrap native xcomponent failed");
        return;
    }
    static OH_NativeXComponent_Callback cb = {
        onSurfaceCreated, onSurfaceChanged, onSurfaceDestroyed, nullptr
    };
    int32_t r = OH_NativeXComponent_RegisterCallback(comp, &cb);
    OH_LOG_ERROR(LOG_APP, "p3surf RegisterCallback -> %{public}d", r);
}
