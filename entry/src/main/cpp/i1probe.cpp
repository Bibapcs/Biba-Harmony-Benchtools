// i1probe.cpp — i1Pro3 USB DDK 探针（在 DriverExtensionAbility 生命周期内使用）
//
// 验证链路：OH_Usb_Init → GetDeviceDescriptor → GetConfigDescriptor →
// ClaimInterface → 控制读产品字符串 → 释放。全部结果以文本日志返回给 ArkTS。

#include "napi/native_api.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <usb/usb_ddk_api.h>
#include <usb/usb_ddk_types.h>

static uint64_t g_i1_device_id = 0;

static void logf(std::string& s, const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    s += buf;
    s += '\n';
}

// ArkTS: i1SetDeviceId(deviceId: number)
static napi_value i1SetDeviceId(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int64_t id = 0;
    napi_get_value_int64(env, args[0], &id);
    g_i1_device_id = (uint64_t)id;
    napi_value ret;
    napi_get_undefined(env, &ret);
    return ret;
}

// ArkTS: i1Probe() => string  （必须在 DriverExtensionAbility 进程内调用）
static napi_value i1Probe(napi_env env, napi_callback_info info)
{
    std::string log;
    int32_t ret;
    uint64_t target = 0;

    logf(log, "onInit 传入 deviceId = %llu", (unsigned long long)g_i1_device_id);

    ret = OH_Usb_Init();
    logf(log, "OH_Usb_Init -> %d", ret);
    if (ret != USB_DDK_SUCCESS) goto out;

    // 用 GetDevices 拿本驱动可见的设备列表（按驱动配置的 vid 过滤）
    {
        struct Usb_DeviceArray arr;
        uint64_t ids[128];
        memset(ids, 0, sizeof(ids));
        arr.deviceIds = ids;
        arr.num = 128;
        ret = OH_Usb_GetDevices(&arr);
        logf(log, "OH_Usb_GetDevices -> %d, num=%u", ret, arr.num);
        uint32_t count = arr.num;
        if (count > 128) count = 128;
        for (uint32_t i = 0; i < count; i++) {
            struct UsbDeviceDescriptor d;
            memset(&d, 0, sizeof(d));
            int32_t r2 = OH_Usb_GetDeviceDescriptor(ids[i], &d);
            logf(log, "  dev[%u] id=%llu desc->%d VID=%04X PID=%04X",
                 i, (unsigned long long)ids[i], r2, d.idVendor, d.idProduct);
            if (r2 == USB_DDK_SUCCESS && d.idVendor == 0x0765 && d.idProduct == 0x6009) {
                target = ids[i];
            }
        }
    }

    if (target == 0 && g_i1_device_id != 0) {
        // GetDevices 为空或没有 i1Pro3：退回用 onInit 的 deviceId 直接读描述符
        struct UsbDeviceDescriptor d;
        memset(&d, 0, sizeof(d));
        ret = OH_Usb_GetDeviceDescriptor(g_i1_device_id, &d);
        logf(log, "GetDeviceDescriptor(onInit id) -> %d VID=%04X PID=%04X", ret, d.idVendor, d.idProduct);
        if (ret == USB_DDK_SUCCESS && d.idVendor == 0x0765 && d.idProduct == 0x6009) {
            target = g_i1_device_id;
        }
    }

    if (target == 0) {
        log += "!! 未找到 i1Pro3（VID=0765 PID=6009）";
        OH_Usb_Release();
        goto out;
    }

    logf(log, ">> 锁定 i1Pro3, deviceId=%llu", (unsigned long long)target);

    {
        struct UsbDeviceDescriptor desc;
        memset(&desc, 0, sizeof(desc));
        ret = OH_Usb_GetDeviceDescriptor(target, &desc);
        logf(log, "GetDeviceDescriptor -> %d", ret);
        if (ret == USB_DDK_SUCCESS) {
            logf(log, "  VID=%04X PID=%04X bcdUSB=%04X class=%02X/%02X/%02X",
                 desc.idVendor, desc.idProduct, desc.bcdUSB,
                 desc.bDeviceClass, desc.bDeviceSubClass, desc.bDeviceProtocol);
            logf(log, "  iManufacturer=%u iProduct=%u iSerial=%u",
                 desc.iManufacturer, desc.iProduct, desc.iSerialNumber);

            struct UsbDdkConfigDescriptor* config = nullptr;
            ret = OH_Usb_GetConfigDescriptor(target, 1, &config);
            logf(log, "GetConfigDescriptor -> %d", ret);
            if (ret == USB_DDK_SUCCESS && config != nullptr) {
                logf(log, "  bNumInterfaces=%u", config->configDescriptor.bNumInterfaces);
                for (uint32_t i = 0; i < config->configDescriptor.bNumInterfaces; i++) {
                    struct UsbDdkInterfaceDescriptor* alt = config->interface[i].altsetting;
                    if (alt == nullptr) continue;
                    uint8_t ifnum = alt->interfaceDescriptor.bInterfaceNumber;
                    uint8_t nep = alt->interfaceDescriptor.bNumEndpoints;
                    logf(log, "  interface %u: %u endpoints", ifnum, nep);
                    for (uint32_t e = 0; e < nep; e++) {
                        auto& ep = alt->endPoint[e].endpointDescriptor;
                        logf(log, "    ep 0x%02X attr=0x%02X maxPkt=%u",
                             ep.bEndpointAddress, ep.bmAttributes, ep.wMaxPacketSize);
                    }
                }
                OH_Usb_FreeConfigDescriptor(config);
            }

            uint64_t ifhandle = 0;
            ret = OH_Usb_ClaimInterface(target, 0, &ifhandle);
            logf(log, "ClaimInterface(0) -> %d (handle=%llu)", ret, (unsigned long long)ifhandle);
            if (ret == USB_DDK_SUCCESS) {
                uint8_t strbuf[128] = {0};
                uint32_t slen = sizeof(strbuf);
                struct UsbControlRequestSetup setup;
                setup.bmRequestType = 0x80;
                setup.bRequest = 0x06;
                setup.wValue = (uint16_t)((0x03 << 8) | desc.iProduct);
                setup.wIndex = 0x409;
                setup.wLength = (uint16_t)slen;
                ret = OH_Usb_SendControlReadRequest(ifhandle, &setup, 5000, strbuf, &slen);
                logf(log, "ControlRead(iProduct) -> %d len=%u", ret, slen);
                if (ret == USB_DDK_SUCCESS && slen > 2) {
                    std::string prod;
                    for (uint32_t i = 2; i + 1 < slen; i += 2) {
                        if (strbuf[i] >= 0x20 && strbuf[i] < 0x7F) prod += (char)strbuf[i];
                    }
                    logf(log, "  产品字符串: \"%s\"", prod.c_str());
                }
                ret = OH_Usb_ReleaseInterface(ifhandle);
                logf(log, "ReleaseInterface -> %d", ret);
            }
        }
    }

    OH_Usb_Release();
    log += "OH_Usb_Release done";

out:
    napi_value retv;
    napi_create_string_utf8(env, log.c_str(), log.size(), &retv);
    return retv;
}

void i1probe_register(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        {"i1SetDeviceId", nullptr, i1SetDeviceId, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"i1Probe", nullptr, i1Probe, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
}
