#pragma once

#include "token.h"
#include "alloc.h"

#include <stdint.h>
#include <vulkan/vulkan.h>

// info about a region of interest.
// stores full buffer dimensions, context and roi.
typedef struct dt_roi_t
{
  uint32_t full_wd, full_ht; // full input size
  uint32_t wd, ht;           // dimensions of region of interest
  uint32_t x, y;             // offset in full image
  float scale;               // scale: wd * scale is on input scale
}
dt_roi_t;

typedef enum dt_connector_flags_t
{
  s_conn_none   = 0,
  s_conn_smooth = 1,  // access this with a bilinear sampler during read
  s_conn_clear  = 2,  // clear this to zero before writing
  s_conn_drawn  = 4,  // this image is created via rasterisation pipeline, not a compute shader
}
dt_connector_flags_t;


// shared property of nodes and modules: how many connectors do we allocate at
// max for each one of them:
#define DT_MAX_CONNECTORS 30
// connectors are used for modules as well as for nodes.
// modules:
// these have to be setup very quickly using tokens from a file
// these are only meta connections, the finest layer DAG will connect nodes
// with memory allocations for vulkan etc.
typedef struct dt_connector_t
{
  dt_token_t name;   // connector name
  dt_token_t type;   // read write source sink
  dt_token_t chan;   // rgb yuv..
  dt_token_t format; // f32 ui16

  dt_connector_flags_t flags;

  // outputs (write buffers) can be connected to multiple inputs
  // inputs (read buffers) can only be connected to exactly one output
  // we only keep track of where inputs come from. this is also
  // how we'll access it in the DAG during DFS from sinks.
  int connected_mi;  // pointing to connected module or node (or -1). is a reference count for write buffers.
  int connected_mc;  // index of the connector on the module

  // module only:
  int connected_ni;  // pointing to connected node after create_nodes has been called
  int connected_nc;  // index of the connector on the node

  // information about buffer dimensions transported here:
  dt_roi_t roi;

  // buffer associated with this in case it connects nodes:
  uint64_t offset, size;
  uint64_t offset_staging, size_staging;
  // mem object for allocator:
  // while this may seem duplicate with offset/size, it may be freed already
  // and the offset and size are still valid for successive runs through the
  // pipeline once it has been setup.
  dt_vkmem_t *mem;
  dt_vkmem_t *mem_staging;

  VkImage     image;
  VkImageView image_view;
  VkBuffer    staging;    // for sources and sinks

  VkFramebuffer framebuffer; // for draw kernels
}
dt_connector_t;

// "templatised" connection functions for both modules and nodes
typedef struct dt_graph_t dt_graph_t; // fwd declare
// connect source|write (m0,c0) -> sink|read (m1,c1)
int dt_module_connect(dt_graph_t *graph, int m0, int c0, int m1, int c1);
int dt_node_connect(dt_graph_t *graph, int m0, int c0, int m1, int c1);

static inline const char*
dt_connector_error_str(const int err)
{
  switch(err)
  {
    case  1: return "no such destination node";
    case  2: return "no such destination connector";
    case  3: return "destination does not read";
    case  4: return "destination inconsistent";
    case  5: return "destination inconsistent";
    case  6: return "destination inconsistent";
    case  7: return "no such source node";
    case  8: return "no such source connector";
    case  9: return "source does not write";
    case 10: return "channels do not match";
    case 11: return "format does not match";
    default: return "";
  }
}

#ifdef NDEBUG
#define CONN(A) (A)
#else
#define CONN(A) \
{ \
  int err = (A); \
  if(err) fprintf(stderr, "%s:%d connection failed: %s\n", __FILE__, __LINE__, dt_connector_error_str(err)); \
}
#endif

static inline size_t
dt_connector_bytes_per_pixel(const dt_connector_t *c)
{
  switch(c->format)
  {
    case dt_token("ui32"):
    case dt_token("f32") :
      return 4;
    case dt_token("ui16"):
    case dt_token("f16") :
      return 2;
    case dt_token("ui8") :
      return 1;
  }
  return 0;
}

static inline int
dt_connector_channels(const dt_connector_t *c)
{
  // bayer or x-trans?
  if(c->chan == dt_token("rggb") || c->chan == dt_token("rgbx")) return 1;
  return c->chan <=     0xff ? 1 :
        (c->chan <=   0xffff ? 2 :
         4);
        // (c->chan <= 0xffffff ? 3 : 4));
}

static inline VkFormat
dt_connector_vkformat(const dt_connector_t *c)
{
  const int len = dt_connector_channels(c);
  switch(c->format)
  {
    case dt_token("ui32"): switch(len)
    {
      case 1: return VK_FORMAT_R32_UINT;
      case 2: return VK_FORMAT_R32G32_UINT;
      case 3: // return VK_FORMAT_R32G32B32_UINT;
      case 4: return VK_FORMAT_R32G32B32A32_UINT;
    }
    case dt_token("f32") : switch(len)
    {
      case 1: return VK_FORMAT_R32_SFLOAT;          // r32f
      case 2: return VK_FORMAT_R32G32_SFLOAT;       // rg32f
      case 3: // return VK_FORMAT_R32G32B32_SFLOAT; // glsl does not support this
      case 4: return VK_FORMAT_R32G32B32A32_SFLOAT; // rgba32f
    }
    case dt_token("f16") : switch(len)
    {
      case 1: return VK_FORMAT_R16_SFLOAT;          // r16f
      case 2: return VK_FORMAT_R16G16_SFLOAT;       // rg16f
      case 3: // return VK_FORMAT_R16G16B16_SFLOAT; // glsl does not support this
      case 4: return VK_FORMAT_R16G16B16A16_SFLOAT; // rgba16f
    }
    case dt_token("ui16"): switch(len)
    {
      case 1: return VK_FORMAT_R16_UINT;
      case 2: return VK_FORMAT_R16G16_UINT;
      case 3: // return VK_FORMAT_R16G16B16_UINT;
      case 4: return VK_FORMAT_R16G16B16A16_UINT;
    }
    case dt_token("ui8") : switch(len)
    {
      case 1: return VK_FORMAT_R8_UINT;
      case 2: return VK_FORMAT_R8G8_UINT;
      case 3: // return VK_FORMAT_R8G8B8_UINT;
      case 4: return VK_FORMAT_R8G8B8A8_UINT;
    }
  }
  return VK_FORMAT_UNDEFINED;
}

static inline size_t
dt_connector_bufsize(const dt_connector_t *c)
{
  const int numc = dt_connector_channels(c);
  const size_t bpp = dt_connector_bytes_per_pixel(c);
  return numc * bpp * c->roi.wd * c->roi.ht;
}
