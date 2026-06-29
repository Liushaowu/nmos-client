#pragma once

#include "daemon/video_format.h"
#include "st_fmt.h"

#include <cpprest/basic_utils.h>
#include <nmos/capabilities.h>
#include <sdp/sdp.h>

#include <string>
#include <vector>

namespace seeder::nmos_node::internal::resource_factory_detail
{
  inline std::vector<utility::string_t>
  to_utility_string_vector(const std::vector<std::string> &values)
  {
    std::vector<utility::string_t> result;
    result.reserve(values.size());
    for (const auto &value : values)
    {
      result.push_back(utility::conversions::to_string_t(value));
    }
    return result;
  }

  inline sdp::sampling st_get_color_sampling(st20_fmt fmt)
  {
    switch (fmt)
    {
    case ST20_FMT_YUV_422_10BIT:
      return sdp::samplings::YCbCr_4_2_2;
    case ST20_FMT_YUV_422_8BIT:
      return sdp::samplings::YCbCr_4_2_2;
    case ST20_FMT_YUV_422_12BIT:
      return sdp::samplings::YCbCr_4_2_2;
    case ST20_FMT_YUV_422_16BIT:
      return sdp::samplings::YCbCr_4_2_2;
    case ST20_FMT_YUV_420_8BIT:
      return sdp::samplings::YCbCr_4_2_0;
    case ST20_FMT_YUV_420_10BIT:
      return sdp::samplings::YCbCr_4_2_0;
    case ST20_FMT_YUV_420_12BIT:
      return sdp::samplings::YCbCr_4_2_0;
    case ST20_FMT_YUV_420_16BIT:
      return sdp::samplings::YCbCr_4_2_0;
    case ST20_FMT_YUV_422_PLANAR10LE:
      return sdp::samplings::YCbCr_4_2_2;
    case ST20_FMT_V210:
      return sdp::samplings::YCbCr_4_2_2;
    case ST20_FMT_RGB_8BIT:
      return sdp::samplings::RGB;
    case ST20_FMT_RGB_10BIT:
      return sdp::samplings::RGB;
    case ST20_FMT_RGB_12BIT:
      return sdp::samplings::RGB;
    case ST20_FMT_RGB_16BIT:
      return sdp::samplings::RGB;
    case ST20_FMT_YUV_444_8BIT:
      return sdp::samplings::YCbCr_4_4_4;
    case ST20_FMT_YUV_444_10BIT:
      return sdp::samplings::YCbCr_4_4_4;
    case ST20_FMT_YUV_444_12BIT:
      return sdp::samplings::YCbCr_4_4_4;
    case ST20_FMT_YUV_444_16BIT:
      return sdp::samplings::YCbCr_4_4_4;
    case ST20_FMT_MAX:
      return sdp::samplings::YCbCr_4_4_4;
    default:
      return sdp::samplings::YCbCr_4_2_2;
    }
  }

  inline int st_get_component_depth(st20_fmt fmt)
  {
    switch (fmt)
    {
    case ST20_FMT_YUV_422_10BIT:
      return 10;
    case ST20_FMT_YUV_422_8BIT:
      return 8;
    case ST20_FMT_YUV_422_12BIT:
      return 12;
    case ST20_FMT_YUV_422_16BIT:
      return 16;
    case ST20_FMT_YUV_420_8BIT:
      return 8;
    case ST20_FMT_YUV_420_10BIT:
      return 10;
    case ST20_FMT_YUV_420_12BIT:
      return 12;
    case ST20_FMT_YUV_420_16BIT:
      return 16;
    case ST20_FMT_YUV_422_PLANAR10LE:
      return 10;
    case ST20_FMT_V210:
      return 10;
    case ST20_FMT_RGB_8BIT:
      return 8;
    case ST20_FMT_RGB_10BIT:
      return 10;
    case ST20_FMT_RGB_12BIT:
      return 12;
    case ST20_FMT_RGB_16BIT:
      return 16;
    case ST20_FMT_YUV_444_8BIT:
      return 8;
    case ST20_FMT_YUV_444_10BIT:
      return 10;
    case ST20_FMT_YUV_444_12BIT:
      return 12;
    case ST20_FMT_YUV_444_16BIT:
      return 16;
    case ST20_FMT_MAX:
      return 8;
    default:
      return 8;
    }
  }
}
