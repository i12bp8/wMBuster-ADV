// wM-Buster ADV — Web Server Interface
// GPL-3.0
#pragma once
#include <stddef.h>

namespace wmb {

typedef bool (*AnalyzeCallbackFn)(const char* hex, char* json_out, size_t json_max);
void set_analyze_callback(AnalyzeCallbackFn cb);

void init_web_server();
void update_web_server();
void toggle_webui_ap();

} // namespace wmb
