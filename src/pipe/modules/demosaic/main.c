#include "modules/api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// XXX HACK! make scale dependent etc
#define HALF_SIZE

#if 0
// TODO: put in header!
static inline int
FC(const size_t row, const size_t col, const uint32_t filters)
{
  return filters >> (((row << 1 & 14) + (col & 1)) << 1) & 3;
}
#endif

void modify_roi_in(
    dt_graph_t *graph,
    dt_module_t *module)
{
  dt_roi_t *ri = &module->connector[0].roi;
  dt_roi_t *ro = &module->connector[1].roi;
#ifdef HALF_SIZE
  const int block = module->img_param.filters == 9u ? 3 : 2;
#else
  const int block = 1;
#endif
  ri->wd = block*ro->wd;
  ri->ht = block*ro->ht;
  ri->x  = block*ro->x;
  ri->y  = block*ro->y;
  ri->scale = 1.0f;

  assert(ro->x == 0 && "TODO: move to block boundary");
  assert(ro->y == 0 && "TODO: move to block boundary");
#if 0
  // TODO: fix for x trans once there are roi!
  // also move to beginning of demosaic rggb block boundary
  uint32_t f = module->img_param.filters;
  int ox = 0, oy = 0;
  if(FC(ry,rx,f) == 1)
  {
    if(FC(ry,rx+1,f) == 0) ox = 1;
    if(FC(ry,rx+1,f) == 2) oy = 1;
  }
  else if(FC(ry,rx,f) == 2)
  {
    ox = oy = 1;
  }
  ri->roi_ox += ox;
  ri->roi_oy += oy;
  ri->roi_wd -= ox;
  ri->roi_ht -= oy;
#endif
}

void modify_roi_out(
    dt_graph_t *graph,
    dt_module_t *module)
{
  dt_roi_t *ri = &module->connector[0].roi;
  dt_roi_t *ro = &module->connector[1].roi;
#ifdef HALF_SIZE
  const int block = module->img_param.filters == 9u ? 3 : 2;
#else
  const int block = 1;
#endif
  // this division is rounding down to full bayer block size, which is good:
  ro->full_wd = ri->full_wd/block;
  ro->full_ht = ri->full_ht/block;
}

// TODO: is this really needed?
void commit_params(dt_graph_t *graph, dt_node_t *node)
{
  uint32_t *i = (uint32_t *)node->module->committed_param;
  i[0] = node->module->img_param.filters;
}

int init(dt_module_t *mod)
{
  mod->committed_param_size = sizeof(uint32_t);
  mod->committed_param = malloc(mod->committed_param_size);
  return 0;
}

void cleanup(dt_module_t *mod)
{
  free(mod->committed_param);
}

void
create_nodes(
    dt_graph_t  *graph,
    dt_module_t *module)
{
#if 0 // experimental xtrans special case code. not any faster and quality not any better it seems now:
  {
  dt_connector_t ci = {
    .name   = dt_token("input"),
    .type   = dt_token("read"),
    .chan   = dt_token("rggb"),
    .format = dt_token("ui16"),
    .roi    = module->connector[0].roi,
    .connected_mi = -1,
  };
  dt_connector_t cg = {
    .name   = dt_token("output"),
    .type   = dt_token("write"),
    .chan   = dt_token("g"),
    .format = dt_token("f16"),
    .roi    = module->connector[1].roi,
  };
  dt_connector_t co = {
    .name   = dt_token("output"),
    .type   = dt_token("write"),
    .chan   = dt_token("rgb"),
    .format = dt_token("f16"),
    .roi    = module->connector[1].roi,
  };
  assert(graph->num_nodes < graph->max_nodes);
  const int id_xtrans = graph->num_nodes++;
  dt_node_t *node_xtrans = graph->node + id_xtrans;
  *node_xtrans = (dt_node_t) {
    .name   = dt_token("demosaic"),
    .kernel = dt_token("xtrans"),
    .module = module,
    .wd     = module->connector[1].roi.roi_wd/3,
    .ht     = module->connector[1].roi.roi_ht/3,
    .dp     = 1,
    .num_connectors = 2,
    .connector = {
      ci, cg,
    },
  };
  cg.name = dt_token("input");
  cg.type = dt_token("read");
  cg.connected_mi = -1;
  assert(graph->num_nodes < graph->max_nodes);
  const int id_col = graph->num_nodes++;
  dt_node_t *node_col = graph->node + id_col;
  *node_col = (dt_node_t) {
    .name   = dt_token("demosaic"),
    .kernel = dt_token("xtransc"),
    .module = module,
    .wd     = module->connector[1].roi.roi_wd/3,
    .ht     = module->connector[1].roi.roi_ht/3,
    .dp     = 1,
    .num_connectors = 3,
    .connector = {
      ci, cg, co,
    },
  };
  // TODO: check connector config before!
  dt_connector_copy(graph, module, 0, id_xtrans, 0);
  dt_connector_copy(graph, module, 0, id_col,    0);
  dt_connector_copy(graph, module, 1, id_col,    2);
  CONN(dt_node_connect(graph, id_xtrans, 1, id_col, 1));
  return;
  }
#endif
#ifdef HALF_SIZE
  {
  // we do whatever the default implementation would have done, too:
  dt_connector_t ci = {
    .name   = dt_token("input"),
    .type   = dt_token("read"),
    .chan   = dt_token("rggb"),
    .format = dt_token("ui16"),
    .roi    = module->connector[0].roi,
    .connected_mi = -1,
  };
  dt_connector_t co = {
    .name   = dt_token("output"),
    .type   = dt_token("write"),
    .chan   = dt_token("rgb"),
    .format = dt_token("f16"),
    .roi    = module->connector[1].roi,
  };
  assert(graph->num_nodes < graph->max_nodes);
  const int id_half = graph->num_nodes++;
  dt_node_t *node_half = graph->node + id_half;
  *node_half = (dt_node_t) {
    .name   = dt_token("demosaic"),
    .kernel = dt_token("halfsize"),
    .module = module,
    .wd     = module->connector[1].roi.wd,
    .ht     = module->connector[1].roi.ht,
    .dp     = 1,
    .num_connectors = 2,
    .connector = {
      ci, co,
    },
  };
  // TODO: check connector config before!
  dt_connector_copy(graph, module, 0, id_half, 0);
  dt_connector_copy(graph, module, 1, id_half, 1);
  return;
  }
#endif
  // need full size connectors and half size connectors:
  const int block = module->img_param.filters == 9u ? 3 : 2;
  const int wd = module->connector[1].roi.wd;
  const int ht = module->connector[1].roi.ht;
  const int dp = 1;
  dt_roi_t roi_full = module->connector[0].roi;
  dt_roi_t roi_half = module->connector[0].roi;
  roi_half.full_wd /= block;
  roi_half.full_ht /= block;
  roi_half.wd /= block;
  roi_half.ht /= block;
  roi_half.x  /= block;
  roi_half.y  /= block;
  // TODO roi_half.scale??
  dt_connector_t ci = {
    .name   = dt_token("input"),
    .type   = dt_token("read"),
    .chan   = dt_token("rggb"),
    .format = dt_token("ui16"),
    .roi    = roi_full,
    .connected_mi = -1,
  };
  dt_connector_t co = {
    .name   = dt_token("output"),
    .type   = dt_token("write"),
    .chan   = dt_token("y"),
    .format = dt_token("f16"),
    .roi    = roi_half,
  };
  dt_connector_t cg = {
    .name   = dt_token("gauss"),
    .type   = dt_token("read"),
    .chan   = dt_token("rgb"),
    .format = dt_token("f16"),
    .roi    = roi_half,
    .connected_mi = -1,
  };
  assert(graph->num_nodes < graph->max_nodes);
  const int id_down = graph->num_nodes++;
  dt_node_t *node_down = graph->node + id_down;
  *node_down = (dt_node_t) {
    .name   = dt_token("demosaic"),
    .kernel = dt_token("down"),
    .module = module,
    .wd     = wd/block,
    .ht     = ht/block,
    .dp     = dp,
    .num_connectors = 2,
    .connector = {
      ci, co,
    },
  };
  ci.chan   = dt_token("y");
  ci.format = dt_token("f16");
  ci.roi    = roi_half;
  co.chan   = dt_token("rgb");
  co.format = dt_token("f16");
  co.roi    = roi_half;
  assert(graph->num_nodes < graph->max_nodes);
  const int id_gauss = graph->num_nodes++;
  dt_node_t *node_gauss = graph->node + id_gauss;
  *node_gauss = (dt_node_t) {
    .name   = dt_token("demosaic"),
    .kernel = dt_token("gauss"),
    .module = module,
    .wd     = wd/block,
    .ht     = ht/block,
    .dp     = dp,
    .num_connectors = 2,
    .connector = {
      ci, co,
    },
  };
  CONN(dt_node_connect(graph, id_down, 1, id_gauss, 0));

  ci.chan   = dt_token("rggb");
  ci.format = dt_token("ui16");
  ci.roi    = roi_full;
  co.chan   = dt_token("rgb");
  co.format = dt_token("f16");
  co.roi    = roi_full;
  cg.chan   = dt_token("rgb");
  cg.format = dt_token("f16");
  cg.roi    = roi_half;
  assert(graph->num_nodes < graph->max_nodes);
  const int id_splat = graph->num_nodes++;
  dt_node_t *node_splat = graph->node + id_splat;
  *node_splat = (dt_node_t) {
    .name   = dt_token("demosaic"),
    .kernel = dt_token("splat"),
    .module = module,
    .wd     = wd,
    .ht     = ht,
    .dp     = dp,
    .num_connectors = 3,
    .connector = {
      ci, cg, co,
    },
  };
  dt_connector_copy(graph, module, 0, id_splat, 0);
  CONN(dt_node_connect(graph, id_gauss, 1, id_splat, 1));
  dt_connector_copy(graph, module, 0, id_down,  0);
  dt_connector_copy(graph, module, 1, id_splat, 2);
  // XXX DEBUG see output of gaussian params
  // dt_connector_copy(graph, module, 1 id_gauss, 1);
}
