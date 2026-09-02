#include "gpu_burn.h"

#include <algorithm>
#include <vector>

#include <command.h>
#include <gpu.h>
#include <mat.h>
#include <pipeline.h>

// GPU 烤机 shader：大量线程循环执行 FP32 FMA（乘加），把 GPU 打满
// afp 由 ncnn 的 compile_spirv_module 根据 Option 展开为 float

#define REPEAT_1(...) #__VA_ARGS__
#define REPEAT_2(...) REPEAT_1(__VA_ARGS__) REPEAT_1(__VA_ARGS__)
#define REPEAT_4(...) REPEAT_2(__VA_ARGS__) REPEAT_2(__VA_ARGS__)
#define REPEAT_8(...) REPEAT_4(__VA_ARGS__) REPEAT_4(__VA_ARGS__)
#define REPEAT_16(...) REPEAT_8(__VA_ARGS__) REPEAT_8(__VA_ARGS__)

static const char glsl_stress_data[] = R"(
#version 450

layout (constant_id = 0) const int loop = 1;

layout (binding = 0) writeonly buffer c_blob { float c_blob_data[]; };

void main()
{
    const uint gx = gl_GlobalInvocationID.x;
    const uint lx = gl_LocalInvocationID.x;

    afp c = afp(gx);

    afp a = c;
    afp b = afp(lx);

    for (int i = 0; i < loop; i++)
    {)"
        REPEAT_16(c = a * c + b;)
    R"(}

    c_blob_data[gx] = float(c);
}
)";

void gpu_burn(std::atomic<int>* stop_flag,
              std::atomic<unsigned long long>* dispatches,
              std::atomic<int>* error)
{
    *error = 0;

    ncnn::create_gpu_instance();

    ncnn::VulkanDevice* vkdev = ncnn::get_gpu_device(0);
    if (!vkdev)
    {
        *error = 1; // Vulkan 设备获取失败
        ncnn::destroy_gpu_instance();
        return;
    }

    {
        ncnn::Option opt;
        opt.use_vulkan_compute = true;

        ncnn::VkAllocator* allocator = vkdev->acquire_blob_allocator();

        // Vulkan 对象放在内层作用域，确保先于 allocator / gpu instance 销毁
        {
            // 64MB fp32 buffer，1600 万元素
            const int buffer_size = 64 * 1024 * 1024;
            ncnn::VkMat c(buffer_size, (size_t)1u, 1, allocator);

            const int local_size_x = std::min(128, std::max(1, (int)vkdev->info.subgroup_size()));
            int invocation_count = buffer_size / 4;
            invocation_count = std::max(invocation_count / local_size_x, 1) * local_size_x;

            // 每个线程 1024 x 16 次 FMA
            const int loop = 1024;

            ncnn::Pipeline pipeline(vkdev);
            pipeline.set_local_size_xyz(local_size_x, 1, 1);

            std::vector<ncnn::vk_specialization_type> specializations(1);
            specializations[0].i = loop;

            std::vector<uint32_t> spirv;
            ncnn::compile_spirv_module(glsl_stress_data, sizeof(glsl_stress_data) - 1, opt, spirv);

            if (pipeline.create(spirv.data(), spirv.size() * 4, specializations) != 0)
            {
                *error = 2; // pipeline 创建失败
            }
            else
            {
                while (!stop_flag->load(std::memory_order_relaxed))
                {
                    ncnn::VkCompute cmd(vkdev);

                    std::vector<ncnn::VkMat> bindings(1);
                    bindings[0] = c;

                    std::vector<ncnn::vk_constant_type> constants(0);

                    ncnn::VkMat dispatcher;
                    dispatcher.w = invocation_count;
                    dispatcher.h = 1;
                    dispatcher.c = 1;
                    cmd.record_pipeline(&pipeline, bindings, constants, dispatcher);

                    if (cmd.submit_and_wait() != 0)
                    {
                        *error = 3; // compute 提交失败
                        break;
                    }

                    dispatches->fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        vkdev->reclaim_blob_allocator(allocator);
    }

    ncnn::destroy_gpu_instance();
}
