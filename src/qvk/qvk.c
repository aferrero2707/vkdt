/*
Copyright (C) 2018 Christoph Schied

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

// #define QVK_ENABLE_VALIDATION

#include "qvk.h"
#include "core/log.h"

#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <execinfo.h>


typedef enum {
  QVK_INIT_DEFAULT            = 0,
  QVK_INIT_SWAPCHAIN_RECREATE = (1 << 0),
  QVK_INIT_RELOAD_SHADER      = (1 << 1),
}
qvk_init_flags_t;

// dynamic reloading facility, put our stuff in here!
#if 0
typedef struct qvk_init_t
{
  const char *name;
  VkResult (*initialize)();
  VkResult (*destroy)();
  qvk_init_flags_t flags;
  int is_initialized;
}
qvk_init_t;
qvk_init_t qvk_initialization[] = {
  { "profiler", vkpt_profiler_initialize,            vkpt_profiler_destroy,                QVK_INIT_DEFAULT,            0 },
  { "shader",   vkpt_load_shader_modules,            vkpt_destroy_shader_modules,          QVK_INIT_RELOAD_SHADER,      0 },
  { "vbo",      vkpt_vertex_buffer_create,           vkpt_vertex_buffer_destroy,           QVK_INIT_DEFAULT,            0 },
  { "ubo",      vkpt_uniform_buffer_create,          vkpt_uniform_buffer_destroy,          QVK_INIT_DEFAULT,            0 },
  { "textures", vkpt_textures_initialize,            vkpt_textures_destroy,                QVK_INIT_DEFAULT,            0 },
  { "images",   vkpt_create_images,                  vkpt_destroy_images,                  QVK_INIT_SWAPCHAIN_RECREATE, 0 },
  { "draw",     vkpt_draw_initialize,                vkpt_draw_destroy,                    QVK_INIT_DEFAULT,            0 },
  { "lh",       vkpt_lh_initialize,                  vkpt_lh_destroy,                      QVK_INIT_DEFAULT,            0 },
  { "pt",       vkpt_pt_init,                        vkpt_pt_destroy,                      QVK_INIT_DEFAULT,            0 },
  { "pt|",      vkpt_pt_create_pipelines,            vkpt_pt_destroy_pipelines,            QVK_INIT_SWAPCHAIN_RECREATE
                                                                                         | QVK_INIT_RELOAD_SHADER,      0 },
  { "draw|",    vkpt_draw_create_pipelines,          vkpt_draw_destroy_pipelines,          QVK_INIT_SWAPCHAIN_RECREATE
                                                                                         | QVK_INIT_RELOAD_SHADER,      0 },
  { "vbo|",     vkpt_vertex_buffer_create_pipelines, vkpt_vertex_buffer_destroy_pipelines, QVK_INIT_RELOAD_SHADER,      0 },
  { "asvgf",    vkpt_asvgf_initialize,               vkpt_asvgf_destroy,                   QVK_INIT_DEFAULT,            0 },
  { "asvgf|",   vkpt_asvgf_create_pipelines,         vkpt_asvgf_destroy_pipelines,         QVK_INIT_RELOAD_SHADER,      0 },
};

static VkResult
qvk_initialize_all(qvk_init_flags_t init_flags)
{
  vkDeviceWaitIdle(qvk.device);
  for(int i = 0; i < LENGTH(qvk_initialization); i++)
  {
    qvk_init_t *init = qvk_initialization + i;
    if((init->flags & init_flags) != init_flags)
      continue;
    dt_log(s_log_qvk, "initializing %s", qvk_initialization[i].name);
    assert(!init->is_initialized);
    init->is_initialized = init->initialize
      ? (init->initialize() == VK_SUCCESS)
      : 1;
    assert(init->is_initialized);
  }
  return VK_SUCCESS;
}

static VkResult
qvk_destroy_all(qvk_init_flags_t destroy_flags)
{
  vkDeviceWaitIdle(qvk.device);
  for(int i = LENGTH(qvk_initialization) - 1; i >= 0; i--)
  {
    qvk_init_t *init = qvk_initialization + i;
    if((init->flags & destroy_flags) != destroy_flags)
      continue;
    dt_log(s_log_qvk, "destroying %s", qvk_initialization[i].name);
    assert(init->is_initialized);
    init->is_initialized = init->destroy
      ? !(init->destroy() == VK_SUCCESS)
      : 0;
    assert(!init->is_initialized);
  }
  return VK_SUCCESS;
}
#endif

// XXX currently unused, but we want this feature
#if 0
void
qvk_reload_shader()
{
  char buf[1024];
#ifdef _WIN32
  FILE *f = _popen("bash -c \"make -C/home/cschied/quake2-pt compile_shaders\"", "r");
#else
  FILE *f = popen("make -j compile_shaders", "r");
#endif
  if(f)
  {
    while(fgets(buf, sizeof buf, f))
      dt_log(s_log_qvk, "%s", buf);
    fclose(f);
  }
  qvk_destroy_all(QVK_INIT_RELOAD_SHADER);
  qvk_initialize_all(QVK_INIT_RELOAD_SHADER);
}
#endif

qvk_t qvk =
{
  .win_width          = 1920,
  .win_height         = 1080,
  .frame_counter      = 0,
};

#define _VK_EXTENSION_DO(a) PFN_##a q##a;
_VK_EXTENSION_LIST
#undef _VK_EXTENSION_DO

const char *vk_requested_layers[] = {
  "VK_LAYER_LUNARG_standard_validation"
};

const char *vk_requested_instance_extensions[] = {
  VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
  VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
};

static const VkApplicationInfo vk_app_info = {
  .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
  .pApplicationName   = "darktable ng",
  .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
  .pEngineName        = "vkdt",
  .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
  .apiVersion         = VK_API_VERSION_1_1,
};

/* use this to override file names */
static const char *shader_module_file_names[NUM_QVK_SHADER_MODULES];

static void
get_vk_extension_list(
    const char *layer,
    uint32_t *num_extensions,
    VkExtensionProperties **ext)
{
  QVK(vkEnumerateInstanceExtensionProperties(layer, num_extensions, NULL));
  *ext = malloc(sizeof(**ext) * *num_extensions);
  QVK(vkEnumerateInstanceExtensionProperties(layer, num_extensions, *ext));
}

static void
get_vk_layer_list(
    uint32_t *num_layers,
    VkLayerProperties **ext)
{
  QVK(vkEnumerateInstanceLayerProperties(num_layers, NULL));
  *ext = malloc(sizeof(**ext) * *num_layers);
  QVK(vkEnumerateInstanceLayerProperties(num_layers, *ext));
}

#if 0
static int
layer_supported(const char *name)
{
  assert(qvk.layers);
  for(int i = 0; i < qvk.num_layers; i++)
    if(!strcmp(name, qvk.layers[i].layerName))
      return 1;
  return 0;
}
#endif

static int
layer_requested(const char *name)
{
  for(int i = 0; i < LENGTH(vk_requested_layers); i++)
    if(!strcmp(name, vk_requested_layers[i]))
      return 1;
  return 0;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void *user_data)
{
  dt_log(s_log_qvk, "validation layer: %s", callback_data->pMessage);
#ifndef NDEBUG
  if(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
  {
    // void *const buf[100];
    // backtrace_symbols_fd(buf, 100, 2);
    // assert(0);
  }
#endif
  return VK_FALSE;
}

static VkResult
qvkCreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pCallback)
{
  PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)
    vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
  if(func)
    return func(instance, pCreateInfo, pAllocator, pCallback);
  return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static VkResult
qvkDestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT callback,
    const VkAllocationCallbacks* pAllocator)
{
  PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)
    vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
  if(func) {
    func(instance, callback, pAllocator);
    return VK_SUCCESS;
  }
  return VK_ERROR_EXTENSION_NOT_PRESENT;
}

VkResult
qvk_create_swapchain()
{
  /* create swapchain (query details and ignore them afterwards :-) )*/
  VkSurfaceCapabilitiesKHR surf_capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(qvk.physical_device, qvk.surface, &surf_capabilities);

  uint32_t num_formats = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(qvk.physical_device, qvk.surface, &num_formats, NULL);
  VkSurfaceFormatKHR *avail_surface_formats = alloca(sizeof(VkSurfaceFormatKHR) * num_formats);
  vkGetPhysicalDeviceSurfaceFormatsKHR(qvk.physical_device, qvk.surface, &num_formats, avail_surface_formats);
  dt_log(s_log_qvk, "num surface formats: %d", num_formats);

  dt_log(s_log_qvk, "available surface formats:");
  for(int i = 0; i < num_formats; i++)
    dt_log(s_log_qvk, vk_format_to_string(avail_surface_formats[i].format));


  VkFormat acceptable_formats[] = {
    // XXX when using srgb buffers, we don't need to apply the curve in f2srgb,
    // XXX but can probably let fixed function hardware do the job. faster?
    // XXX would need to double check that export does the right thing then.
    // VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
    VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM
  };

  for(int i = 0; i < LENGTH(acceptable_formats); i++) {
    for(int j = 0; j < num_formats; j++)
      if(acceptable_formats[i] == avail_surface_formats[j].format) {
        qvk.surf_format = avail_surface_formats[j];
        dt_log(s_log_qvk, "colour space: %u", qvk.surf_format.colorSpace);
        goto out;
      }
  }
out:;

  uint32_t num_present_modes = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(qvk.physical_device, qvk.surface, &num_present_modes, NULL);
  VkPresentModeKHR *avail_present_modes = alloca(sizeof(VkPresentModeKHR) * num_present_modes);
  vkGetPhysicalDeviceSurfacePresentModesKHR(qvk.physical_device, qvk.surface, &num_present_modes, avail_present_modes);
  //qvk.present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
  qvk.present_mode = VK_PRESENT_MODE_FIFO_KHR; // guaranteed to be there, but has vsync frame time jitter
  // qvk.present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
  //qvk.present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;

  if(surf_capabilities.currentExtent.width != ~0u)
  {
    qvk.extent = surf_capabilities.currentExtent;
  }
  else {
    qvk.extent.width  = MIN(surf_capabilities.maxImageExtent.width,  qvk.win_width);
    qvk.extent.height = MIN(surf_capabilities.maxImageExtent.height, qvk.win_height);

    qvk.extent.width  = MAX(surf_capabilities.minImageExtent.width,  qvk.extent.width);
    qvk.extent.height = MAX(surf_capabilities.minImageExtent.height, qvk.extent.height);
  }

  uint32_t num_images = 2;
  //uint32_t num_images = surf_capabilities.minImageCount + 1;
  if(surf_capabilities.maxImageCount > 0)
    num_images = MIN(num_images, surf_capabilities.maxImageCount);

  VkSwapchainCreateInfoKHR swpch_create_info = {
    .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface               = qvk.surface,
    .minImageCount         = num_images,
    .imageFormat           = qvk.surf_format.format,
    .imageColorSpace       = qvk.surf_format.colorSpace,
    .imageExtent           = qvk.extent,
    .imageArrayLayers      = 1, /* only needs to be changed for stereoscopic rendering */ 
    .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                           | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    .imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE, /* VK_SHARING_MODE_CONCURRENT if not using same queue */
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices   = NULL,
    .preTransform          = surf_capabilities.currentTransform,
    .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, /* no alpha for window transparency */
    .presentMode           = qvk.present_mode,
    .clipped               = VK_FALSE, /* do not render pixels that are occluded by other windows */
    //.clipped               = VK_TRUE, /* do not render pixels that are occluded by other windows */
    .oldSwapchain          = VK_NULL_HANDLE, /* need to provide previous swapchain in case of window resize */
  };

  if(vkCreateSwapchainKHR(qvk.device, &swpch_create_info, NULL, &qvk.swap_chain) != VK_SUCCESS)
  {
    dt_log(s_log_qvk, "error creating swapchain");
    return 1;
  }

  vkGetSwapchainImagesKHR(qvk.device, qvk.swap_chain, &qvk.num_swap_chain_images, NULL);
  //qvk.swap_chain_images = malloc(qvk.num_swap_chain_images * sizeof(*qvk.swap_chain_images));
  assert(qvk.num_swap_chain_images < QVK_MAX_SWAPCHAIN_IMAGES);
  vkGetSwapchainImagesKHR(qvk.device, qvk.swap_chain, &qvk.num_swap_chain_images, qvk.swap_chain_images);

  //qvk.swap_chain_image_views = malloc(qvk.num_swap_chain_images * sizeof(*qvk.swap_chain_image_views));
  for(int i = 0; i < qvk.num_swap_chain_images; i++)
  {
    VkImageViewCreateInfo img_create_info = {
      .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image      = qvk.swap_chain_images[i],
      .viewType   = VK_IMAGE_VIEW_TYPE_2D,
      .format     = qvk.surf_format.format,
#if 1
      .components = {
        VK_COMPONENT_SWIZZLE_R,
        VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B,
        VK_COMPONENT_SWIZZLE_A
      },
#endif
      .subresourceRange = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1
      }
    };

    if(vkCreateImageView(qvk.device, &img_create_info, NULL, qvk.swap_chain_image_views + i) != VK_SUCCESS)
    {
      dt_log(s_log_qvk|s_log_err, "error creating image view!");
      return 1;
    }
  }

  return VK_SUCCESS;
}

VkResult
qvk_destroy_command_pool_and_fences()
{
  // TODO
  return VK_SUCCESS;
}

#if 0
VkResult
qvk_create_command_pool_and_fences()
{
  VkCommandPoolCreateInfo cmd_pool_create_info = {
    .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .queueFamilyIndex = qvk.queue_idx_graphics,
    .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
  };

  /* command pool and buffers */
  QVK(vkCreateCommandPool(qvk.device, &cmd_pool_create_info, NULL, &qvk.command_pool));

  qvk.num_command_buffers = qvk.num_swap_chain_images;
  VkCommandBufferAllocateInfo cmd_buf_alloc_info = {
    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool        = qvk.command_pool,
    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = qvk.num_command_buffers
  };
  qvk.command_buffers = malloc(qvk.num_command_buffers * sizeof(*qvk.command_buffers));
  QVK(vkAllocateCommandBuffers(qvk.device, &cmd_buf_alloc_info, qvk.command_buffers));


  /* fences and semaphores */
  VkSemaphoreCreateInfo semaphore_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
  for(int i = 0; i < NUM_SEMAPHORES; i++)
    QVK(vkCreateSemaphore(qvk.device, &semaphore_info, NULL, &qvk.semaphores[i]));

  VkFenceCreateInfo fence_info = {
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .flags = VK_FENCE_CREATE_SIGNALED_BIT, /* fence's initial state set to be signaled
                          to make program not hang */
  };
  for(int i = 0; i < QVK_MAX_FRAMES_IN_FLIGHT; i++) {
    QVK(vkCreateFence(qvk.device, &fence_info, NULL, qvk.fences_frame_sync + i));
  }

  return VK_SUCCESS;
}
#endif

// this function works without gui and consequently does not init SDL
int
qvk_init()
{
  /* layers */
  get_vk_layer_list(&qvk.num_layers, &qvk.layers);
  dt_log(s_log_qvk, "available vulkan layers:");
  for(int i = 0; i < qvk.num_layers; i++) {
    int requested = layer_requested(qvk.layers[i].layerName);
    dt_log(s_log_qvk, "%s%s", qvk.layers[i].layerName, requested ? " (requested)" : "");
  }

  /* instance extensions */
  int num_inst_ext_combined = qvk.num_sdl2_extensions + LENGTH(vk_requested_instance_extensions);
  char **ext = alloca(sizeof(char *) * num_inst_ext_combined);
  memcpy(ext, qvk.sdl2_extensions, qvk.num_sdl2_extensions * sizeof(*qvk.sdl2_extensions));
  memcpy(ext + qvk.num_sdl2_extensions, vk_requested_instance_extensions, sizeof(vk_requested_instance_extensions));

  get_vk_extension_list(NULL, &qvk.num_extensions, &qvk.extensions); /* valid here? */
  dt_log(s_log_qvk, "supported vulkan instance extensions:");
  for(int i = 0; i < qvk.num_extensions; i++)
  {
    int requested = 0;
    for(int j = 0; j < num_inst_ext_combined; j++)
    {
      if(!strcmp(qvk.extensions[i].extensionName, ext[j]))
      {
        requested = 1;
        break;
      }
    }
    dt_log(s_log_qvk, "%s%s", qvk.extensions[i].extensionName, requested ? " (requested)" : "");
  }

  /* create instance */
  VkInstanceCreateInfo inst_create_info = {
    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo        = &vk_app_info,
#ifdef QVK_ENABLE_VALIDATION
    .enabledLayerCount       = LENGTH(vk_requested_layers),
    .ppEnabledLayerNames     = vk_requested_layers,
#endif
    .enabledExtensionCount   = num_inst_ext_combined,
    .ppEnabledExtensionNames = (const char * const*)ext,
  };

  QVK(vkCreateInstance(&inst_create_info, NULL, &qvk.instance));

  /* setup debug callback */
  VkDebugUtilsMessengerCreateInfoEXT dbg_create_info = {
    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
      | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
      | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
    .messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
      | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
      | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
    .pfnUserCallback = vk_debug_callback,
    .pUserData = NULL
  };

  QVK(qvkCreateDebugUtilsMessengerEXT(qvk.instance, &dbg_create_info, NULL, &qvk.dbg_messenger));

  /* pick physical device (iterate over all but pick device 0 anyways) */
  uint32_t num_devices = 0;
  QVK(vkEnumeratePhysicalDevices(qvk.instance, &num_devices, NULL));
  if(num_devices == 0)
    return 1;
  VkPhysicalDevice *devices = alloca(sizeof(VkPhysicalDevice) *num_devices);
  QVK(vkEnumeratePhysicalDevices(qvk.instance, &num_devices, devices));

  int picked_device = -1;
  for(int i = 0; i < num_devices; i++) {
    VkPhysicalDeviceProperties dev_properties;
    VkPhysicalDeviceFeatures   dev_features;
    vkGetPhysicalDeviceProperties(devices[i], &dev_properties);
    vkGetPhysicalDeviceFeatures  (devices[i], &dev_features);
    qvk.ticks_to_nanoseconds = dev_properties.limits.timestampPeriod;

    dt_log(s_log_qvk, "dev %d: %s", i, dev_properties.deviceName);
    dt_log(s_log_qvk, "max number of allocations %d", dev_properties.limits.maxMemoryAllocationCount);
    dt_log(s_log_qvk, "max image allocation size %u x %u",
        dev_properties.limits.maxImageDimension2D, dev_properties.limits.maxImageDimension2D);
    uint32_t num_ext;
    vkEnumerateDeviceExtensionProperties(devices[i], NULL, &num_ext, NULL);

    VkExtensionProperties *ext_properties = alloca(sizeof(VkExtensionProperties) * num_ext);
    vkEnumerateDeviceExtensionProperties(devices[i], NULL, &num_ext, ext_properties);

    dt_log(s_log_qvk, "supported extensions:");
    for(int j = 0; j < num_ext; j++) {
      dt_log(s_log_qvk, ext_properties[j].extensionName);
	  // XXX FIXME: no ray tracing needed!
      // if(!strcmp(ext_properties[j].extensionName, VK_NV_RAY_TRACING_EXTENSION_NAME)) {
        if(picked_device < 0)
          picked_device = i;
      // }
    }
  }

  if(picked_device < 0) {
    dt_log(s_log_qvk, "could not find any suitable device supporting " VK_NV_RAY_TRACING_EXTENSION_NAME"!");
    return 1;
  }

  dt_log(s_log_qvk, "picked device %d", picked_device);

  qvk.physical_device = devices[picked_device];


  vkGetPhysicalDeviceMemoryProperties(qvk.physical_device, &qvk.mem_properties);

  /* queue family and create physical device */
  uint32_t num_queue_families = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(qvk.physical_device, &num_queue_families, NULL);
  VkQueueFamilyProperties *queue_families = alloca(sizeof(VkQueueFamilyProperties) * num_queue_families);
  vkGetPhysicalDeviceQueueFamilyProperties(qvk.physical_device, &num_queue_families, queue_families);

  dt_log(s_log_qvk, "num queue families: %d", num_queue_families);

  qvk.queue_idx_graphics = -1;
  qvk.queue_idx_compute  = -1;
  qvk.queue_idx_transfer = -1;

  for(int i = 0; i < num_queue_families; i++) {
    if(!queue_families[i].queueCount)
      continue;
    // XXX don't do this without gui (don't have qvk.surface)
	// FIXME: put in different compilation unit and do check it there
#if 0 
    VkBool32 present_support = 0;
    vkGetPhysicalDeviceSurfaceSupportKHR(qvk.physical_device, i, qvk.surface, &present_support);
    if(!present_support)
      continue;
#endif
    if((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == VK_QUEUE_GRAPHICS_BIT && qvk.queue_idx_graphics < 0) {
      qvk.queue_idx_graphics = i;
    }
    if((queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == VK_QUEUE_COMPUTE_BIT && qvk.queue_idx_compute < 0) {
      qvk.queue_idx_compute = i;
    }
    if((queue_families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) == VK_QUEUE_TRANSFER_BIT && qvk.queue_idx_transfer < 0) {
      qvk.queue_idx_transfer = i;
    }
  }

  if(qvk.queue_idx_graphics < 0 || qvk.queue_idx_compute < 0 || qvk.queue_idx_transfer < 0)
  {
    dt_log(s_log_err|s_log_qvk, "could not find suitable queue family!");
    return 1;
  }

  float queue_priorities = 1.0f;
  int num_create_queues = 0;
  VkDeviceQueueCreateInfo queue_create_info[3];

  {
    VkDeviceQueueCreateInfo q = {
      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount       = 1,
      .pQueuePriorities = &queue_priorities,
      .queueFamilyIndex = qvk.queue_idx_graphics,
    };

    queue_create_info[num_create_queues++] = q;
  };
  if(qvk.queue_idx_compute != qvk.queue_idx_graphics) {
    VkDeviceQueueCreateInfo q = {
      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount       = 1,
      .pQueuePriorities = &queue_priorities,
      .queueFamilyIndex = qvk.queue_idx_compute,
    };
    queue_create_info[num_create_queues++] = q;
  };
  if(qvk.queue_idx_transfer != qvk.queue_idx_graphics && qvk.queue_idx_transfer != qvk.queue_idx_compute) {
    VkDeviceQueueCreateInfo q = {
      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount       = 1,
      .pQueuePriorities = &queue_priorities,
      .queueFamilyIndex = qvk.queue_idx_transfer,
    };
    queue_create_info[num_create_queues++] = q;
  };

  VkPhysicalDeviceDescriptorIndexingFeaturesEXT idx_features = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT,
    .runtimeDescriptorArray = 1,
    .shaderSampledImageArrayNonUniformIndexing = 1,
  };
  VkPhysicalDeviceFeatures2 device_features = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR,
    .pNext = &idx_features,

    .features = {
      .robustBufferAccess = 1,
      .fullDrawIndexUint32 = 1,
      .imageCubeArray = 1,
      .independentBlend = 1,
      .geometryShader = 1,
      .tessellationShader = 1,
      .sampleRateShading = 0,
      .dualSrcBlend = 1,
      .logicOp = 1,
      .multiDrawIndirect = 1,
      .drawIndirectFirstInstance = 1,
      .depthClamp = 1,
      .depthBiasClamp = 1,
      .fillModeNonSolid = 0,
      .depthBounds = 1,
      .wideLines = 0,
      .largePoints = 0,
      .alphaToOne = 1,
      .multiViewport = 0,
      .samplerAnisotropy = 1,
      .textureCompressionETC2 = 0,
      .textureCompressionASTC_LDR = 0,
      .textureCompressionBC = 0,
      .occlusionQueryPrecise = 0,
      .pipelineStatisticsQuery = 1,
      .vertexPipelineStoresAndAtomics = 1,
      .fragmentStoresAndAtomics = 1,
      .shaderTessellationAndGeometryPointSize = 1,
      .shaderImageGatherExtended = 1,
      .shaderStorageImageExtendedFormats = 1,
      .shaderStorageImageMultisample = 1,
      .shaderStorageImageReadWithoutFormat = 1,
      .shaderStorageImageWriteWithoutFormat = 1,
      .shaderUniformBufferArrayDynamicIndexing = 1,
      .shaderSampledImageArrayDynamicIndexing = 1,
      .shaderStorageBufferArrayDynamicIndexing = 1,
      .shaderStorageImageArrayDynamicIndexing = 1,
      .shaderClipDistance = 1,
      .shaderCullDistance = 1,
      .shaderFloat64 = 1,
      .shaderInt64 = 1,
      .shaderInt16 = 1,
      .shaderResourceResidency = 1,
      .shaderResourceMinLod = 1,
      .sparseBinding = 1,
      .sparseResidencyBuffer = 1,
      .sparseResidencyImage2D = 1,
      .sparseResidencyImage3D = 1,
      .sparseResidency2Samples = 1,
      .sparseResidency4Samples = 1,
      .sparseResidency8Samples = 1,
      .sparseResidency16Samples = 1,
      .sparseResidencyAliased = 1,
      .variableMultisampleRate = 0,
      .inheritedQueries = 1,
    }
  };

  const char *vk_requested_device_extensions[] = {
    // VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, // :( intel doesn't have it
#ifdef QVK_ENABLE_VALIDATION
    VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
#endif
    VK_KHR_SWAPCHAIN_EXTENSION_NAME, // goes last because we might not want it without gui
  };
  const int len = LENGTH(vk_requested_device_extensions) - (qvk.window ? 0 : 1);
  VkDeviceCreateInfo dev_create_info = {
    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext                   = &device_features,
    .pQueueCreateInfos       = queue_create_info,
    .queueCreateInfoCount    = num_create_queues,
    .enabledExtensionCount   = len,
    .ppEnabledExtensionNames = vk_requested_device_extensions,
  };

  /* create device and queue */
  QVK(vkCreateDevice(qvk.physical_device, &dev_create_info, NULL, &qvk.device));

  vkGetDeviceQueue(qvk.device, qvk.queue_idx_graphics, 0, &qvk.queue_graphics);
  vkGetDeviceQueue(qvk.device, qvk.queue_idx_compute,  0, &qvk.queue_compute);
  vkGetDeviceQueue(qvk.device, qvk.queue_idx_transfer, 0, &qvk.queue_transfer);

#define _VK_EXTENSION_DO(a) \
    q##a = (PFN_##a) vkGetDeviceProcAddr(qvk.device, #a); \
    if(!q##a) { dt_log(s_log_qvk, "warning: could not load function %s", #a); }
  _VK_EXTENSION_LIST
#undef _VK_EXTENSION_DO

  // create texture samplers
  VkSamplerCreateInfo sampler_info = {
    .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .magFilter               = VK_FILTER_LINEAR,
    .minFilter               = VK_FILTER_LINEAR,
    .addressModeU            = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    .addressModeV            = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    .addressModeW            = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    .anisotropyEnable        = VK_FALSE,
    .maxAnisotropy           = 16,
    .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
    .unnormalizedCoordinates = VK_FALSE,
    .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
    .minLod                  = 0.0f,
    .maxLod                  = 128.0f,
  };
  QVK(vkCreateSampler(qvk.device, &sampler_info, NULL, &qvk.tex_sampler));
  ATTACH_LABEL_VARIABLE(qvk.tex_sampler, SAMPLER);
  VkSamplerCreateInfo sampler_nearest_info = {
    .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .magFilter               = VK_FILTER_NEAREST,
    .minFilter               = VK_FILTER_NEAREST,
    .addressModeU            = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    .addressModeV            = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    .addressModeW            = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    .anisotropyEnable        = VK_FALSE,
    .maxAnisotropy           = 16,
    .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
    .unnormalizedCoordinates = VK_FALSE,
    .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST,
  };
  QVK(vkCreateSampler(qvk.device, &sampler_nearest_info, NULL, &qvk.tex_sampler_nearest));
  ATTACH_LABEL_VARIABLE(qvk.tex_sampler_nearest, SAMPLER);

  return 0;
}

static int
load_file(const char *path, char **data, size_t *s)
{
  *data = NULL;
  FILE *f = fopen(path, "rb");
  if(!f) {
    dt_log(s_log_qvk, "could not open %s", path);
    /* let's try not to crash everything */
    char *ret = malloc(1);
    *s = 1;
    ret[0] = 0;
    return 1;
  }
  fseek(f, 0, SEEK_END);
  *s = ftell(f);
  rewind(f);

  *data = malloc(*s + 1);
  //*data = aligned_alloc(4, *s + 1); // XXX lets hope malloc returns aligned memory
  if(fread(*data, 1, *s, f) != *s) {
    dt_log(s_log_qvk, "could not read file %s", path);
    fclose(f);
    *data[0] = 0;
    return 1;
  }
  fclose(f);
  return 0;
}

static VkShaderModule
create_shader_module_from_file(const char *name, const char *enum_name)
{
  char *data;
  size_t size;

  char path[1024];
  snprintf(path, sizeof path, QVK_SHADER_PATH_TEMPLATE, name ? name : (enum_name + 8));
  if(!name) {
    int len = 0;
    for(len = 0; path[len]; len++)
      path[len] = tolower(path[len]);
    while(--len >= 0) {
      if(path[len] == '_') {
        path[len] = '.';
        break;
      }
    }
  }

  if(load_file(path, &data, &size))
  {
    free(data);
    return VK_NULL_HANDLE;
  }

  VkShaderModule module;

  VkShaderModuleCreateInfo create_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = size,
    .pCode = (uint32_t *) data,
  };

  QVK(vkCreateShaderModule(qvk.device, &create_info, NULL, &module));

  free(data);

  return module;
}

VkResult
qvk_shader_modules_initialize()
{
  VkResult ret = VK_SUCCESS;
#define SHADER_MODULE_DO(a) do { \
  qvk.shader_modules[a] = create_shader_module_from_file(shader_module_file_names[a], #a); \
  ret = (ret == VK_SUCCESS && qvk.shader_modules[a]) ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED; \
  if(qvk.shader_modules[a]) { \
    ATTACH_LABEL_VARIABLE_NAME((uint64_t)qvk.shader_modules[a], SHADER_MODULE, #a); \
  }\
  } while(0);

  LIST_SHADER_MODULES

#undef SHADER_MODULE_DO
  return ret;
}

VkResult
qvk_shader_modules_destroy()
{
#define SHADER_MODULE_DO(a) \
  vkDestroyShaderModule(qvk.device, qvk.shader_modules[a], NULL); \
  qvk.shader_modules[a] = VK_NULL_HANDLE;

  LIST_SHADER_MODULES

#undef SHADER_MODULE_DO

  return VK_SUCCESS;
}

static VkResult
destroy_swapchain()
{
  for(int i = 0; i < qvk.num_swap_chain_images; i++) {
    vkDestroyImageView  (qvk.device, qvk.swap_chain_image_views[i], NULL);
  }

  vkDestroySwapchainKHR(qvk.device,   qvk.swap_chain, NULL);
  return VK_SUCCESS;
}

int
qvk_cleanup()
{
  vkDeviceWaitIdle(qvk.device);
  vkDestroySampler(qvk.device, qvk.tex_sampler, 0);
  vkDestroySampler(qvk.device, qvk.tex_sampler_nearest, 0);

  if(qvk.window)  destroy_swapchain();
  if(qvk.surface) vkDestroySurfaceKHR(qvk.instance, qvk.surface, NULL);

  for(int i = 0; i < NUM_SEMAPHORES; i++)
    vkDestroySemaphore(qvk.device, qvk.semaphores[i], NULL);

  for(int i = 0; i < QVK_MAX_FRAMES_IN_FLIGHT; i++)
    vkDestroyFence(qvk.device, qvk.fences_frame_sync[i], NULL);

  vkDestroyCommandPool (qvk.device, qvk.command_pool,     NULL);

  vkDestroyDevice      (qvk.device,   NULL);
  QVK(qvkDestroyDebugUtilsMessengerEXT(qvk.instance, qvk.dbg_messenger, NULL));
  vkDestroyInstance    (qvk.instance, NULL);

  free(qvk.extensions);
  qvk.extensions = NULL;
  qvk.num_extensions = 0;

  free(qvk.layers);
  qvk.layers = NULL;
  qvk.num_layers = 0;

  return 0;
}
 
// TODO: needs to go into gui mode, and parts to start the compute pipeline can be stolen, too:
#if 0
/* renders the map ingame */
void
R_RenderFrame(refdef_t *fd)
{
  vkpt_refdef.fd = fd;
  LOG_FUNC();
  if(!vkpt_refdef.bsp_mesh_world_loaded)
    return;

  //update_lights(); /* updates the light hierarchy, not present in this version */

  uint32_t num_vert_instanced;
  uint32_t num_instances;
  upload_entity_transforms(&num_instances, &num_vert_instanced);

  float P[16];
  float V[16];
  float VP[16];
  float inv_VP[16];

  QVKUniformBuffer_t *ubo = &vkpt_refdef.uniform_buffer;
  memcpy(ubo->VP_prev, ubo->VP, sizeof(float) * 16);
  create_projection_matrix(P, vkpt_refdef.z_near, vkpt_refdef.z_far, fd->fov_x, fd->fov_y);
  create_view_matrix(V, fd);
  mult_matrix_matrix(VP, P, V);
  memcpy(ubo->V, V, sizeof(float) * 16);
  memcpy(ubo->VP, VP, sizeof(float) * 16);
  inverse(VP, inv_VP);
  memcpy(ubo->invVP, inv_VP, sizeof(float) * 16);
  ubo->current_frame_idx = qvk.frame_counter;
  ubo->width  = qvk.extent.width;
  ubo->height = qvk.extent.height;
  ubo->under_water = !!(fd->rdflags & RDF_UNDERWATER);
  ubo->time = fd->time;
  memcpy(ubo->cam_pos, fd->vieworg, sizeof(float) * 3);

  _VK(vkpt_uniform_buffer_update());

  _VK(vkpt_profiler_query(PROFILER_INSTANCE_GEOMETRY, PROFILER_START));
  vkpt_vertex_buffer_create_instance(num_instances);
  _VK(vkpt_profiler_query(PROFILER_INSTANCE_GEOMETRY, PROFILER_STOP));

  _VK(vkpt_profiler_query(PROFILER_BVH_UPDATE, PROFILER_START));
  vkpt_pt_destroy_dynamic(qvk.current_image_index);

  assert(num_vert_instanced % 3 == 0);
  vkpt_pt_create_dynamic(qvk.current_image_index, qvk.buf_vertex.buffer,
    offsetof(VertexBuffer, positions_instanced), num_vert_instanced); 

  vkpt_pt_create_toplevel(qvk.current_image_index);
  vkpt_pt_update_descripter_set_bindings(qvk.current_image_index);
  _VK(vkpt_profiler_query(PROFILER_BVH_UPDATE, PROFILER_STOP));

  _VK(vkpt_profiler_query(PROFILER_ASVGF_GRADIENT_SAMPLES, PROFILER_START));
  vkpt_asvgf_create_gradient_samples(qvk.cmd_buf_current, qvk.frame_counter);
  _VK(vkpt_profiler_query(PROFILER_ASVGF_GRADIENT_SAMPLES, PROFILER_STOP));

  _VK(vkpt_profiler_query(PROFILER_PATH_TRACER, PROFILER_START));
  vkpt_pt_record_cmd_buffer(qvk.cmd_buf_current, qvk.frame_counter);
  _VK(vkpt_profiler_query(PROFILER_PATH_TRACER, PROFILER_STOP));

  _VK(vkpt_profiler_query(PROFILER_ASVGF_FULL, PROFILER_START));
  vkpt_asvgf_record_cmd_buffer(qvk.cmd_buf_current);
  _VK(vkpt_profiler_query(PROFILER_ASVGF_FULL, PROFILER_STOP));

  VkImageSubresourceRange subresource_range = {
    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel   = 0,
    .levelCount     = 1,
    .baseArrayLayer = 0,
    .layerCount     = 1
  };

  IMAGE_BARRIER(qvk.cmd_buf_current,
      .image            = qvk.swap_chain_images[qvk.current_image_index],
      .subresourceRange = subresource_range,
      .srcAccessMask    = 0,
      .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
      .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
  );

  int output_img = get_output_img();

  IMAGE_BARRIER(qvk.cmd_buf_current,
      .image            = qvk.images[output_img],
      .subresourceRange = subresource_range,
      .srcAccessMask    = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT,
      .oldLayout        = VK_IMAGE_LAYOUT_GENERAL,
      .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
  );

  VkOffset3D blit_size = {
    .x = qvk.extent.width, .y = qvk.extent.height, .z = 1
  };
  VkImageBlit img_blit = {
    .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
    .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
    .srcOffsets = { [1] = blit_size },
    .dstOffsets = { [1] = blit_size },
  };
  vkCmdBlitImage(qvk.cmd_buf_current,
      qvk.images[output_img],                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
      qvk.swap_chain_images[qvk.current_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1, &img_blit, VK_FILTER_NEAREST);

  IMAGE_BARRIER(qvk.cmd_buf_current,
      .image            = qvk.swap_chain_images[qvk.current_image_index],
      .subresourceRange = subresource_range,
      .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask    = 0,
      .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .newLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  );
}
#endif

// currently unused:
#if 0
static void
recreate_swapchain()
{
  vkDeviceWaitIdle(qvk.device);
  qvk_destroy_all(QVK_INIT_SWAPCHAIN_RECREATE);
  qvk_destroy_swapchain();
  SDL_GetWindowSize(qvk.window, &qvk.win_width, &qvk.win_height);
  qvk_create_swapchain();
  qvk_initialize_all(QVK_INIT_SWAPCHAIN_RECREATE);
}
#endif

// TODO: these need to go into render in gui mode
#if 0
void
R_BeginFrame()
{
  QVK_LOG_FUNC();
retry:;
  int sem_idx = qvk.frame_counter % MAX_FRAMES_IN_FLIGHT;

  vkWaitForFences(qvk.device, 1, qvk.fences_frame_sync + sem_idx, VK_TRUE, ~((uint64_t) 0));
  VkResult res_swapchain = vkAcquireNextImageKHR(qvk.device, qvk.swap_chain, ~((uint64_t) 0),
      qvk.semaphores[SEM_IMG_AVAILABLE + sem_idx], VK_NULL_HANDLE, &qvk.current_image_index);
  if(res_swapchain == VK_ERROR_OUT_OF_DATE_KHR || res_swapchain == VK_SUBOPTIMAL_KHR) {
    recreate_swapchain();
    goto retry;
  }
  else if(res_swapchain != VK_SUCCESS) {
    QVK(res_swapchain);
  }
  vkResetFences(qvk.device, 1, qvk.fences_frame_sync + sem_idx);

  QVK(vkpt_profiler_next_frame(qvk.current_image_index));

  /* cannot be called in R_EndRegistration as it would miss the initially textures (charset etc) */
  if(register_model_dirty) {
    QVK(vkpt_vertex_buffer_upload_models_to_staging());
    QVK(vkpt_vertex_buffer_upload_staging());
    register_model_dirty = 0;
  }
  vkpt_textures_end_registration();
  vkpt_draw_clear_stretch_pics();

  qvk.cmd_buf_current = qvk.command_buffers[qvk.current_image_index];

  VkCommandBufferBeginInfo begin_info = {
    .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags            = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
    .pInheritanceInfo = NULL,
  };

  QVK(vkResetCommandBuffer(qvk.cmd_buf_current, 0));
  QVK(vkBeginCommandBuffer(qvk.cmd_buf_current, &begin_info));
  QVK(vkpt_profiler_query(PROFILER_FRAME_TIME, PROFILER_START));
}

void
R_EndFrame()
{
  LOG_FUNC();

  if(vkpt_profiler->integer)
    draw_profiler();
  vkpt_draw_submit_stretch_pics(&qvk.cmd_buf_current);
  _VK(vkpt_profiler_query(PROFILER_FRAME_TIME, PROFILER_STOP));
  _VK(vkEndCommandBuffer(qvk.cmd_buf_current));

  int sem_idx = qvk.frame_counter % MAX_FRAMES_IN_FLIGHT;

  VkSemaphore          wait_semaphores[]   = { qvk.semaphores[SEM_IMG_AVAILABLE + sem_idx]    };
  VkPipelineStageFlags wait_stages[]       = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT  };
  VkSemaphore          signal_semaphores[] = { qvk.semaphores[SEM_RENDER_FINISHED + sem_idx]  };

  VkSubmitInfo submit_info = {
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount   = LENGTH(wait_semaphores),
    .pWaitSemaphores      = wait_semaphores,
    .signalSemaphoreCount = LENGTH(signal_semaphores),
    .pSignalSemaphores    = signal_semaphores,
    .pWaitDstStageMask    = wait_stages,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &qvk.cmd_buf_current,
  };

  _VK(vkQueueSubmit(qvk.queue_graphics, 1, &submit_info, qvk.fences_frame_sync[sem_idx]));

  VkPresentInfoKHR present_info = {
    .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = LENGTH(signal_semaphores),
    .pWaitSemaphores    = signal_semaphores,
    .swapchainCount     = 1,
    .pSwapchains        = &qvk.swap_chain,
    .pImageIndices      = &qvk.current_image_index,
    .pResults           = NULL,
  };

  VkResult res_present = vkQueuePresentKHR(qvk.queue_graphics, &present_info);
  if(res_present == VK_ERROR_OUT_OF_DATE_KHR || res_present == VK_SUBOPTIMAL_KHR) {
    recreate_swapchain();
  }
  qvk.frame_counter++;
}
#endif

// TODO: this needs to go into gui init
#if 0
/* called before the library is unloaded */
void
R_Shutdown(qboolean total)
{
  _VK(vkpt_destroy_all(VKPT_INIT_DEFAULT));

  if(destroy_vulkan()) {
    Com_EPrintf("[vkpt] destroy vulkan failed\n");
  }

  IMG_Shutdown();
  MOD_Shutdown(); // todo: currently leaks memory, need to clear submeshes
  VID_Shutdown();
}
#endif

