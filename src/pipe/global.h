#pragma once

#include "token.h"
#include "params.h"
#include "connector.h"

// static global structs to keep around for all instances of pipelines.
// this queries the modules on startup, does the dlopen and expensive
// parsing once, and holds a list for modules to quickly access run time.

typedef int (*dt_module_create_nodes_t)(void *data);

// this is all the "class" info that is not bound to an instance and can be
// read once on startup
typedef struct dt_module_so_t
{
  dt_token_t name;

  void *dlhandle;
  dt_module_create_nodes_t create_nodes;

  dt_connector_t connector[10]; // enough for everybody, right?
  int num_connectors;

  // pointer to variably-sized parameters
  dt_ui_param_t *param[10];
  int num_params;
}
dt_module_so_t;

typedef struct dt_pipe_global_t
{
  char module_dir[2048];
  dt_module_so_t *module;
  uint32_t num_modules;
}
dt_pipe_global_t;

extern dt_pipe_global_t dt_pipe;

// returns non-zero on failure:
int dt_pipe_global_init();

// global cleanup:
void dt_pipe_global_cleanup();