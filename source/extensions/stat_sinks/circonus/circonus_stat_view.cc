#include "extensions/stat_sinks/circonus/circonus_stat_view.h"

#include <google/protobuf/util/internal/json_objectwriter.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <iostream>

#include "common/buffer/buffer_impl.h"
#include "common/config/well_known_names.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/stats/utility.h"

#include "circllhist.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Circonus {

namespace {
const char* METRIC_HISTOGRAM_CUMULATIVE = "H";
const char* METRIC_HISTOGRAM = "h";

const char* chooseHistogramsType(absl::string_view path_and_query) {
  const auto qp = Envoy::Http::Utility::parseQueryString(path_and_query);
  auto found = qp.find("histogram_type");

  if (found != qp.end() && found->second == "cumulative") {
    return METRIC_HISTOGRAM_CUMULATIVE;
  }

  return METRIC_HISTOGRAM;
}
} // namespace

CirconusStatView::CirconusStatView(Server::Instance& server) : server_(server) {
  server.admin().addHandler("/stats/circonus", "Display Circonus metrics on the admin console",
                            MAKE_ADMIN_HANDLER(handlerAdminStats), false, false);
}

Http::Code CirconusStatView::handlerAdminStats(absl::string_view path_and_query,
                                               Http::ResponseHeaderMap& response_headers,
                                               Buffer::Instance& response,
                                               Server::AdminStream& /* admin_stream */) {
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
  response.add(makeJsonBody(chooseHistogramsType(path_and_query)));
  return Http::Code::OK;
}

std::string CirconusStatView::makeJsonBody(const char* histogram_type) {
  const auto& stats = server_.stats();
  const auto histograms = stats.histograms();
  auto& bb = bb_;
  std::string json;
  google::protobuf::io::StringOutputStream sos{&json};
  google::protobuf::io::CodedOutputStream cos{&sos};
  google::protobuf::util::converter::JsonObjectWriter w{" ", &cos};

  w.StartObject("");

  for (const auto& counter : stats.counters()) {
    w.StartObject(counter->name());
    w.RenderString("_type", "L");
    w.RenderUint64("_value", counter->value());
    w.EndObject();
  }

  for (const auto& gauge : stats.gauges()) {
    w.StartObject(gauge->name());
    w.RenderString("_type", "L");
    w.RenderUint64("_value", gauge->value());
    w.EndObject();
  }

  for (const auto& parent_histogram : stats.histograms()) {
    const auto& stat_name = parent_histogram->statName();

    if (!stat_name.empty()) {
      const auto stat_name_as_string =
          std::string(reinterpret_cast<const char*>(stat_name.data()), stat_name.dataSize());

      if (histogram_type == METRIC_HISTOGRAM) {
        parent_histogram->intervalHistogram(bb);
      } else {
        parent_histogram->cumulativeHistogram(bb);
      }

      w.StartObject(stat_name_as_string);
      w.RenderString("_type", histogram_type);
      w.RenderString("_value", reinterpret_cast<const char*>(&bb[0]));
      w.EndObject();
    }
  }

  w.EndObject();
  return json;
}

} // namespace Circonus
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
