#include "plugin-manager.h"

#include "../config.h"
#include <boost/dll/import.hpp> // for import_alias
#include <boost/foreach.hpp>
#include <boost/function.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <stdlib.h>
#include <vector>

std::vector<Plugin *> plugins;

Plugin *setup_plugin(std::string plugin_lib, std::string plugin_name) {
  BOOST_LOG_TRIVIAL(info) << "Setting up plugin -  Name: " << plugin_name << "\t Library file: " << plugin_lib;
  //Plugin *plugin = plugin_new(plugin_lib == "" ? NULL : plugin_lib.c_str(), plugin_name.c_str());

  // Based on factory plugin method from Boost: https://www.boost.org/doc/libs/1_64_0/doc/html/boost_dll/tutorial.html#boost_dll.tutorial.factory_method_in_plugin
  boost::filesystem::path lib_path("./");
  Plugin *plugin = new Plugin();
  plugin->creator = boost::dll::import_alias<pluginapi_create_t>( // type of imported symbol must be explicitly specified
      plugin_lib,                                                 // path to library
      "create_plugin",                                            // symbol to import
      boost::dll::load_mode::append_decorations                   // do append extensions and prefixes
  );

  plugin->api = plugin->creator();
  /*
  assert(plugin_name != NULL);
  char *fname = NULL;
  assert(asprintf(&fname, "%s_plugin_new", plugin_name) > 0);
  plugin_new_func_t fptr = NULL;
  std::cout << "Load file: " << plugin_file << std::endl;

  void *dlhandle = dlopen(plugin_file, RTLD_NOW);
  assert(dlhandle != NULL);
  fptr = (plugin_new_func_t)dlsym(dlhandle, fname);

  free(fname);
  if (fptr == NULL) {
    return NULL;
  }

  plugin_t *plugin = (*fptr)();
  assert(plugin->init != NULL);
  return plugin;*/

  plugins.push_back(plugin);

  return plugin;
}

void initialize_plugins(boost::property_tree::ptree &cfg, Config *config, std::vector<Source *> sources, std::vector<System *> systems) {

  BOOST_FOREACH (boost::property_tree::ptree::value_type &node, cfg.get_child("plugins")) {
    std::string plugin_lib = node.second.get<std::string>("library", "");
    std::string plugin_name = node.second.get<std::string>("name", "");

    Plugin *plugin = setup_plugin(plugin_lib, plugin_name);
    plugin->api->parse_config(cfg);
  }

  if (plugins.size() == 1) {
    BOOST_LOG_TRIVIAL(info) << "Loaded " << plugins.size() << " Plugin";
  }

  if (plugins.size() > 1) {
    BOOST_LOG_TRIVIAL(info) << "Loaded " << plugins.size() << " Plugins";
  }

  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;
    int ret = plugin->api->init(config, sources, systems);
    if (ret < 0) {
      plugin->state = PLUGIN_FAILED;
    } else {
      plugin->state = PLUGIN_INITIALIZED;
      ret = 0;
    }
  }
}

void initialize_internal_plugin(std::string name) {
  //std::string lib = "plugins/" + name
  setup_plugin("", name);
}

void start_plugins(std::vector<Source *> sources, std::vector<System *> systems) {
  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;

    /* ----- Plugin Start ----- */
    if (plugin->state == PLUGIN_INITIALIZED) {
      int err = plugin->api->start();
      if (err != 0) {
        plugin->state = PLUGIN_FAILED;
        continue;
      }
    }
    plugin->state = PLUGIN_RUNNING;

    /* ----- Plugin Setup Sources ----- */
    if (plugin->state == PLUGIN_RUNNING) {
      plugin->api->setup_sources(sources);
    }

    /* ----- Plugin Setup Systems ----- */
    if (plugin->state == PLUGIN_RUNNING) {
      plugin->api->setup_systems(systems);
    }
  }
}

void stop_plugins() {
  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;
    if (plugin->state == PLUGIN_RUNNING) {
      int err = plugin->api->stop();
      if (err != 0) {
        plugin->state = PLUGIN_FAILED;
        continue;
      }
    }
    plugin->state = PLUGIN_STOPPED;
  }
}

void plugman_poll_one() {
  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;
    if (plugin->state == PLUGIN_RUNNING) {
      plugin->api->poll_one();
    }
  }
}

void plugman_audio_callback(Recorder *recorder, float *samples, int sampleCount) {
  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;
    if (plugin->state == PLUGIN_RUNNING) {
      plugin->api->audio_stream(recorder, samples, sampleCount);
    }
  }
}

int plugman_signal(long unitId, const char *signaling_type, gr::blocks::SignalType sig_type, Call *call, System *system, Recorder *recorder) {
  int error = 0;
  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;
    if (plugin->state == PLUGIN_RUNNING) {
      plugin->api->signal(unitId, signaling_type, sig_type, call, system, recorder);
    }
  }
  return error;
}

int plugman_call_start(Call *call) {
  int error = 0;
  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;
    if (plugin->state == PLUGIN_RUNNING) {
      plugin->api->call_start(call);
    }
  }
  return error;
}

int plugman_call_end(Call_Data_t call_info) {
  int error = 0;
  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;
    if (plugin->state == PLUGIN_RUNNING) {
      error = error + plugin->api->call_end(call_info);
    }
  }
  return error;
}

int plugman_calls_active(std::vector<Call *> calls) {
  int error = 0;
  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;
    if (plugin->state == PLUGIN_RUNNING) {
      plugin->api->calls_active(calls);
    }
  }
  return error;
}

void plugman_setup_recorder(Recorder *recorder) {
  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;
    if (plugin->state == PLUGIN_RUNNING) {
      plugin->api->setup_recorder(recorder);
    }
  }
}

void plugman_setup_system(System *system) {
  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;
    if (plugin->state == PLUGIN_RUNNING) {
      plugin->api->setup_system(system);
    }
  }
}

void plugman_setup_systems(std::vector<System *> systems) {
  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;
    if (plugin->state == PLUGIN_RUNNING) {
      plugin->api->setup_systems(systems);
    }
  }
}

void plugman_setup_sources(std::vector<Source *> sources) {
  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;
    if (plugin->state == PLUGIN_RUNNING) {
      plugin->api->setup_sources(sources);
    }
  }
}

void plugman_setup_config(std::vector<Source *> sources, std::vector<System *> systems) {
  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;
    if (plugin->state == PLUGIN_RUNNING) {
      plugin->api->setup_config(sources, systems);
    }
  }
}

void plugman_system_rates(std::vector<System *> systems, float timeDiff) {
  for (std::vector<Plugin *>::iterator it = plugins.begin(); it != plugins.end(); it++) {
    Plugin *plugin = *it;
    if (plugin->state == PLUGIN_RUNNING) {
      plugin->api->system_rates(systems, timeDiff);
    }
  }
}