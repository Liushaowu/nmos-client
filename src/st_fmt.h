#pragma once
/**
 * Format type of st2110-20(video) streaming
 */
enum st20_fmt {
  ST20_FMT_YUV_422_10BIT = 0, /**< 10-bit YUV 4:2:2 */
  ST20_FMT_YUV_422_8BIT,      /**< 8-bit YUV 4:2:2 */
  ST20_FMT_YUV_422_12BIT,     /**< 12-bit YUV 4:2:2 */
  ST20_FMT_YUV_422_16BIT,     /**< 16-bit YUV 4:2:2 */
  ST20_FMT_YUV_420_8BIT,      /**< 8-bit YUV 4:2:0 */
  ST20_FMT_YUV_420_10BIT,     /**< 10-bit YUV 4:2:0 */
  ST20_FMT_YUV_420_12BIT,     /**< 12-bit YUV 4:2:0 */
  ST20_FMT_YUV_420_16BIT,     /**< 16-bit YUV 4:2:0 */
  ST20_FMT_RGB_8BIT,          /**< 8-bit RGB */
  ST20_FMT_RGB_10BIT,         /**< 10-bit RGB */
  ST20_FMT_RGB_12BIT,         /**< 12-bit RGB */
  ST20_FMT_RGB_16BIT,         /**< 16-bit RGB */
  ST20_FMT_YUV_444_8BIT,      /**< 8-bit YUV 4:4:4 */
  ST20_FMT_YUV_444_10BIT,     /**< 10-bit YUV 4:4:4 */
  ST20_FMT_YUV_444_12BIT,     /**< 12-bit YUV 4:4:4 */
  ST20_FMT_YUV_444_16BIT,     /**< 16-bit YUV 4:4:4 */
  /*
   * Below are the formats which not compatible with st2110 rfc4175.
   * Ex, user want to transport ST_FRAME_FMT_YUV422PLANAR10LE directly with padding on the
   * network and no color convert required.
   */
  ST20_FMT_YUV_422_PLANAR10LE, /**< 10-bit YUV 4:2:2 planar little endian. Experimental
                                  now, how to support ext frame? */
  ST20_FMT_V210,               /**< 10-bit YUV 422 V210 */
  ST20_FMT_MAX,                /**< max value of this enum */
};
