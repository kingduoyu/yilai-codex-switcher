#include "../Sources/ConfigSources/runtime_probe.h"
#include "../Sources/HistorySync/vendor/json.hpp"
#include <filesystem>
#include <iostream>
int wmain(int argc,wchar_t **argv) {
  try {
    const auto runtime=argc>3 ? std::filesystem::path(argv[3]) : yilai_sources::locate_runtime();
    nlohmann::json result{{"runtime",runtime.u8string()}};
    if(argc>=3) {
      auto response=nlohmann::json::parse(yilai_sources::probe_config(runtime,std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[2])));
      result["layers"]=response.at("layers").size();
      result["provider"]=response.at("config").value("model_provider",nlohmann::json());
      result["reasoning"]=response.at("config").value("model_reasoning_effort",nlohmann::json());
    }
    std::cout<<result.dump()<<std::endl;
    return 0;
  } catch(const std::exception &e) {std::cerr<<e.what()<<std::endl;return 1;}
}
