#pragma once

#include <cpprest/json.h>
#include <string>

namespace seeder::nmos_sync {

// Parse the primary RTP media format. DUP legs describe the same format.
// Throws on invalid SDP or invalid/missing required format parameters.
web::json::value parse_sdp_format(const std::string &sdp);

// Extract the SDP session name, destination endpoints and primary media format.
// Format errors are reported in the result while retaining valid endpoints.
web::json::value parse_sdp_sender(const std::string &sdp);

}
