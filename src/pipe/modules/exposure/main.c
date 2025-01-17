#include "modules/api.h"
#include <math.h>
#include <stdlib.h>

void commit_params(dt_graph_t *graph, dt_node_t *node)
{
  float *f = (float *)node->module->committed_param;
  for(int k=0;k<4;k++)
    f[k] = node->module->img_param.black[k];
  for(int k=0;k<4;k++)
    f[4+k] = powf(2.0f, ((float*)node->module->param)[0]) *
       node->module->img_param.whitebalance[k] /
      (node->module->img_param.white[k]-node->module->img_param.black[k]);
}

int init(dt_module_t *mod)
{
  mod->committed_param_size = sizeof(float)*8;
  mod->committed_param = malloc(mod->committed_param_size);
  return 0;
}

void cleanup(dt_module_t *mod)
{
  free(mod->committed_param);
}
