// i1meas.cpp — i1Pro3 测量编排层（基于 ArgyllCMS 仪器栈子集）
//
// 在 DriverExtensionAbility 进程内被调用（此时 OHOS USB DDK 可用）。
// 导出 4 个 NAPI 函数：
//   i1mInit()      => string  "OK <仪器信息>" 或 "ERR <描述>"
//   i1mCalibrate() => string  "OK" 或 "ERR <描述>"（调用前用户须已把仪器放到白色校准底座）
//   i1mReadPatch() => string  "OK X Y Z"（发射模式绝对 XYZ，cd/m^2）或 "ERR <描述>"
//   i1mClose()     => void    释放仪器，允许再次 i1mInit
//
// 状态用文件内 static 保存（单仪器单会话）。
// 驱动进程没有控制台：stdout 无人看，错误一律通过返回值字符串传递。

#include "napi/native_api.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>

#include <hilog/log.h>   /* OHOS hilog - diagnostic logging (LOG_APP) */

extern "C" {
#include "aconfig.h"
#include "numsup.h"
#include "cgats.h"
#include "xspect.h"
#include "conv.h"
#include "insttypes.h"
#include "icoms.h"
#include "inst.h"
#include "i1pro3.h"
#include "i1pro3_imp.h"
}

static inst *g_inst = nullptr;      /* Open instrument */
static icompaths *g_paths = nullptr;/* Path list owning the path g_inst was made from */

/* OHOS changes: log every error return to hilog. The driver process has */
/* no console, and the returned string only reaches the ArkTS caller, so */
/* mirror it to hilog for on-device diagnosis. */
static std::string i1m_fail(const std::string& msg)
{
    OH_LOG_ERROR(LOG_APP, "i1meas: %{public}s", msg.c_str());
    return msg;
}

static void i1m_loginfo(const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OH_LOG_ERROR(LOG_APP, "i1meas: %{public}s", buf);
}

/* ------------------------------------------------------------------ */
/* Argyll global (one time) initialisation, mirroring what a command */
/* line tool does in main() before anything else. */
static void i1m_ensure_globals(void)
{
    static bool done = false;
    if (done)
        return;
    done = true;

    set_exe_path((char *)"i1meas");         /* No real argv[0] in a driver process */
    setenv("ARGYLL_NOT_INTERACTIVE", "1", 1); /* Never touch stdin/stdout interactively */
    check_if_not_interactive();
}

/* Format an inst_code error using the instrument's own interpreters */
static std::string inst_errstr(inst *it, inst_code rv)
{
    char buf[64];
    if (it != nullptr) {
        const char *a = it->inst_interp_error(it, rv);
        const char *b = it->interp_error(it, rv);
        return std::string(a != nullptr ? a : "?") + " ("
             + (b != nullptr ? b : "?") + ")";
    }
    snprintf(buf, sizeof(buf), "inst_code 0x%x", (unsigned)rv);
    return buf;
}

static void cleanup_paths(void)
{
    if (g_paths != nullptr) {
        g_paths->del(g_paths);
        g_paths = nullptr;
    }
}

/* ------------------------------------------------------------------ */
/* i1mInit(): find the i1Pro3, open and init it, set emission spot mode */
static std::string do_init(void)
{
    i1m_ensure_globals();

    if (g_inst != nullptr)
        return i1m_fail("ERR already initialized (call i1mClose first)");

    /* Enumerate the USB instruments */
    if ((g_paths = new_icompaths(NULL)) == nullptr)
        return i1m_fail("ERR new_icompaths failed");

    /* Find the first i1Pro3 (USB VID 0765 PID 6009, matched by the driver) */
    icompath *path = nullptr;
    for (int i = 0; i < g_paths->ndpaths[dtix_inst]; i++) {
        if (g_paths->paths[i]->dtype == instI1Pro3) {
            path = g_paths->paths[i];
            break;
        }
    }
    if (path == nullptr) {
        cleanup_paths();
        return i1m_fail("ERR no i1Pro3 found (USB VID 0765 PID 6009)");
    }

    /* Create the instrument. No UI callback: all triggers are programmatic. */
    inst *it = new_inst(path, 0, g_log, NULL, NULL);
    if (it == nullptr) {
        cleanup_paths();
        return i1m_fail("ERR new_inst failed");
    }

    inst_code rv;
    if ((rv = it->init_coms(it, baud_nc, fc_nc, 15.0)) != inst_ok) {
        std::string e = inst_errstr(it, rv);
        it->del(it);
        cleanup_paths();
        return i1m_fail("ERR init_coms: " + e);
    }
    if ((rv = it->init_inst(it)) != inst_ok) {
        std::string e = inst_errstr(it, rv);
        it->del(it);
        cleanup_paths();
        return i1m_fail("ERR init_inst: " + e);
    }

    /* Emission spot measurement mode (absolute XYZ, cd/m^2) */
    if ((rv = it->set_mode(it, inst_mode_emis_spot)) != inst_ok) {
        std::string e = inst_errstr(it, rv);
        it->del(it);
        cleanup_paths();
        return i1m_fail("ERR set_mode(emis_spot): " + e);
    }

    /* Programmatic trigger: read_sample() measures immediately and */
    /* returns without waiting for the instrument switch or a UI callback. */
    if ((rv = it->get_set_opt(it, inst_opt_trig_prog)) != inst_ok) {
        std::string e = inst_errstr(it, rv);
        it->del(it);
        cleanup_paths();
        return i1m_fail("ERR set trigger(prog): " + e);
    }

    g_inst = it;

    /* Instrument info: type, serial number, firmware */
    const char *name = inst_name(it->dtype);
    const char *sn = it->get_serial_no(it);
    char fw[64] = "?";
    i1pro3imp *m = (i1pro3imp *)((i1pro3 *)it)->m;
    if (m != nullptr && m->fwvstr[0] != '\0')
        snprintf(fw, sizeof(fw), "%s", m->fwvstr);

    std::string info = "OK ";
    info += name != nullptr ? name : "i1Pro3";
    info += " S/N ";
    info += sn != nullptr ? sn : "?";
    info += " FW ";
    info += fw;
    i1m_loginfo("init OK: %s", info.c_str() + 3);
    return info;
}

/* i1mCalibrate(): white (and dark) calibration. The caller (ArkTS) is */
/* responsible for telling the user to place the instrument on the white */
/* calibration cradle before calling. Uses calibrate() directly rather */
/* than inst_handle_calibrate() because that helper waits on the console. */
static std::string do_calibrate(void)
{
    if (g_inst == nullptr)
        return i1m_fail("ERR not initialized (call i1mInit first)");
    inst *it = g_inst;

    /* Nothing needed? Then we're done. */
    inst_cal_type nc = it->needs_calibration(it);
    if (nc == inst_calt_none)
        return "OK";

    /* User requested calibration: let the driver pick the available types */
    inst_cal_type calt = inst_calt_available;
    inst_cal_cond calc = inst_calc_none;
    inst_calc_id_type idtype;
    char id[CALIDLEN];

    for (int tries = 0; tries < 4; tries++) {
        inst_code ev = it->calibrate(it, &calt, &calc, &idtype, id);
        if ((ev & inst_mask) == inst_ok)
            return "OK";
        if ((ev & inst_mask) == inst_cal_setup) {
            /* Needs the user to set something up (e.g. white tile in */
            /* place). The ArkTS layer already prompted, so go around */
            /* again - the next call performs the actual calibration. */
            continue;
        }
        if ((ev & inst_mask) == inst_user_abort)
            return i1m_fail("ERR user abort");
        return i1m_fail("ERR calibrate: " + inst_errstr(it, ev));
    }
    return i1m_fail("ERR calibrate did not complete");
}

/* i1mReadPatch(): one emission spot measurement, absolute XYZ in cd/m^2 */
static std::string do_read_patch(void)
{
    if (g_inst == nullptr)
        return i1m_fail("ERR not initialized (call i1mInit first)");
    inst *it = g_inst;

    ipatch val;
    memset(&val, 0, sizeof(val));

    inst_code rv = it->read_sample(it, (char *)"SPOT", &val, instNoClamp);
    if (rv != inst_ok) {
        if ((rv & inst_mask) == inst_needs_cal)
            return i1m_fail("ERR needs calibration (call i1mCalibrate)");
        std::string e = inst_errstr(it, rv);
        /* OHOS changes: after a communications failure the port/session */
        /* state is uncertain (e.g. a permanently stalled endpoint that */
        /* the DDK cannot clear). The usbio_ohos backend has already */
        /* attempted interface re-claim / DDK session restart; on top of */
        /* that, re-establish the coms here (close + re-open + re-claim), */
        /* so that the caller's retry (ArkTS retries each block up to 3 */
        /* times) starts from a clean instrument state. */
        if ((rv & inst_mask) == inst_coms_fail) {
            inst_code crv = it->init_coms(it, baud_nc, fc_nc, 15.0);
            i1m_loginfo("read_sample coms fail, init_coms reconnect %s",
                crv == inst_ok ? "OK" : "FAILED");
        }
        return i1m_fail("ERR read_sample: " + e);
    }
    if (val.XYZ_v == 0)
        return i1m_fail("ERR instrument did not return XYZ");

    char buf[128];
    snprintf(buf, sizeof(buf), "OK %.2f %.2f %.2f", val.XYZ[0], val.XYZ[1], val.XYZ[2]);
    return buf;
}

/* i1mClose(): release the instrument and path list */
static void do_close(void)
{
    if (g_inst != nullptr) {
        g_inst->del(g_inst);
        g_inst = nullptr;
    }
    cleanup_paths();
}

/* ------------------------------------------------------------------ */
/* NAPI wrappers */

static napi_value i1mInit(napi_env env, napi_callback_info info)
{
    std::string r = do_init();
    napi_value ret;
    napi_create_string_utf8(env, r.c_str(), r.size(), &ret);
    return ret;
}

static napi_value i1mCalibrate(napi_env env, napi_callback_info info)
{
    std::string r = do_calibrate();
    napi_value ret;
    napi_create_string_utf8(env, r.c_str(), r.size(), &ret);
    return ret;
}

static napi_value i1mReadPatch(napi_env env, napi_callback_info info)
{
    std::string r = do_read_patch();
    napi_value ret;
    napi_create_string_utf8(env, r.c_str(), r.size(), &ret);
    return ret;
}

static napi_value i1mClose(napi_env env, napi_callback_info info)
{
    do_close();
    napi_value ret;
    napi_get_undefined(env, &ret);
    return ret;
}

void i1meas_register(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        {"i1mInit", nullptr, i1mInit, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"i1mCalibrate", nullptr, i1mCalibrate, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"i1mReadPatch", nullptr, i1mReadPatch, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"i1mClose", nullptr, i1mClose, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
}
