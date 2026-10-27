# 注册中心 Sender 查询接口

`GET /api/nmos/senders` 查询当前已连接注册中心中的 Sender，包含其他节点注册的 Sender。接口由 daemon 的 `debug_http_url` 提供，示例地址为 `http://127.0.0.1:18081`。

## 请求

| 参数 | 说明 |
| --- | --- |
| `page` | 从 1 开始。提供后启用分页，`page_size` 默认 50。 |
| `page_size` | 每页 1–1000 条。单独提供时默认 `page=1`。 |
| `keyword` | 可选，子串匹配 Sender 和 Flow 的 `id`、`label`、`description`、`device_id`、`flow_id`、`source_id` 及 tags。ASCII 英文忽略大小写，支持 UTF-8 中文。 |
| `type` | 可选，`video`、`audio`、`data`，通过关联 Flow 的 `format` 判断。 |

不提供 `page` 和 `page_size` 时返回筛选后的全量结果。各参数可以组合。关键字应进行 URL 编码。

```bash
# 全量 Sender（包含 SDP）
curl 'http://127.0.0.1:18081/api/nmos/senders'

# 第 2 页，每页 20 条
curl 'http://127.0.0.1:18081/api/nmos/senders?page=2&page_size=20'

# 视频 Sender 中搜索 Camera
curl -G 'http://127.0.0.1:18081/api/nmos/senders' \
  --data-urlencode 'keyword=Camera' \
  --data-urlencode 'type=video' \
  --data-urlencode 'page=1' \
  --data-urlencode 'page_size=20'
```

## 响应

```json
{
  "items": [
    {
      "sdp": "v=0\r\n...",
      "name": "Camera 1",
      "ip": "239.1.1.1",
      "port": 5004,
      "format": {
        "type": "video",
        "name": "1080p5000",
        "display_name": "1080p50"
      },
      "transports": [{"ip": "239.1.1.1", "port": 5004}]
    }
  ],
  "total": 21,
  "page": 1,
  "page_size": 20,
  "has_more": true
}
```

`items` 仅返回 Sender 名称、SDP 原文及从 SDP 解析出的内容。每条成功记录包含 `sdp`、`name`、`ip`、`port`、`format`、`transports`，不再返回注册中心的 `id`、`flow`、`manifest_href` 等原始字段。上例中的 `format` 为简化展示，完整格式字段见下文。`total` 为筛选后、分页前的总数。全量查询时 `page` 和 `page_size` 为 `null`。超出末页时 `items` 为空，总数仍然有效。

`name` 取注册中心 Sender 的 `label`，缺少 label 时为 `null`；SDP 的会话名称 `s=` 不覆盖它。`ip` 取连接地址 `c=`，`port` 取媒体行 `m=` 的 RTP 端口。媒体级 `c=` 优先于会话级 `c=`，IPv4 的 TTL 和 IPv6 地址计数后缀会去掉。`o=` 中的地址不是目标 IP，不用于回填。未提供 `c=` 时 `ip=null`。

`transports` 按 SDP 媒体段顺序包含所有目标地址和端口，支持 ST 2022-7 DUP 主备路径；顶层 `ip`、`port` 对应第一项。结果仍按内部 Sender ID 排序。缺少 Flow 的 Sender 在不按类型筛选时也会被处理。搜索和类型筛选继续使用注册中心元数据，在分页及 SDP 获取之前完成。

SDP 从内部 `manifest_href` 获取并原样返回。地址缺失、HTTP 错误或超时时，仍保留来自 label 的 `name`，该条记录的 `sdp`、地址、端口和格式为 `null`，`transports` 为空，额外的 `error` 字段说明原因，其余记录正常返回。成功记录不包含 `error`。SDP 请求最多并发 8 个，仅请求本页记录，每个 HTTP 请求超时为 3 秒。

## SDP 格式解析

每条 Sender 的 `format` 为从 SDP 解析出的格式对象。SDP 下载成功但解析失败时，保留 `sdp` 原文，额外的 `error` 字段说明原因。仅格式参数解析失败时，`format=null`，已解析的名称、地址和端口仍保留。

视频示例（`video/raw`）：

```json
{
  "format": {
    "type": "video",
    "media_type": "video/raw",
    "encoding_name": "raw",
    "payload_type": 96,
    "clock_rate": 90000,
    "name": "1080p5000",
    "display_name": "1080p50",
    "width": 1920,
    "height": 1080,
    "frame_rate": {"numerator": 50, "denominator": 1},
    "fps": 50,
    "field_rate": null,
    "scan": "progressive",
    "interlace": false,
    "segmented": false,
    "sampling": "YCbCr-4:2:2",
    "bit_depth": 10,
    "colorspace": "BT709",
    "transfer_characteristics": "SDR",
    "fmtp": {}
  }
}
```

`name` 优先采用项目已有格式名称，`display_name` 用于显示。未匹配预设的尺寸使用如 `2048x1080p25` 的名称。`frame_rate` 保留准确分数（例如 60000/1001），`fps` 为实际每秒帧数。隔行 `1080i50` 对应 `fps=25`、`field_rate=50`，PsF 用 `scan=psf` 区分。

音频示例：

```json
{
  "format": {
    "type": "audio",
    "media_type": "audio/L24",
    "encoding_name": "L24",
    "payload_type": 97,
    "clock_rate": 48000,
    "name": "L24/48000/2",
    "display_name": "L24 48000 Hz 2 ch",
    "sample_rate": 48000,
    "channel_count": 2,
    "bit_depth": 24,
    "packet_time_ms": 1,
    "channel_order": "SMPTE2110.(ST)",
    "fmtp": {"channel-order": "SMPTE2110.(ST)"}
  }
}
```

音频支持 L8/L16/L24 的位深解析，`rtpmap` 省略通道数时按 SDP 默认值返回单声道，未提供 `ptime` 时 `packet_time_ms=null`。其他音频编码返回编码、采样时钟、通道数等元数据，`bit_depth=null`，不会推测 PCM 位深。

JPEG XS (`video/jxsv`) 同样解析视频参数，并提供 `profile`、`level`、`sublevel`；若 SDP 省略只在码流中携带的尺寸、位深或帧率，相应字段为 `null`。其他编码及 data SDP 返回基础 RTP 元数据和 `fmtp`。`fmtp` 保留 SDP 中的参数键值，示例中的空对象仅为简化展示。视频未提供 TCS 时使用 SDR 默认值。

格式取 SDP 的首个 media description；ST 2022-7 DUP 主备路径按同一种媒体格式处理。`format.type` 反映 SDP 内容，列表的 `type` 筛选仍依据注册中心 Flow，不因 SDP 解析改变。

## Query 服务地址

默认使用 DNS-SD 发现与当前 Registration 服务同一主机上的 Query API，支持 Query 与 Registration 使用不同端口。未发现时尝试当前注册地址相同 origin 下的 `/x-nmos/query/{version}`。

若 Query 服务位于其他主机或未发布 DNS-SD，可在节点配置（`node_config_path` 指向的 JSON）中设置：

```json
{
  "registry_query_url": "http://192.168.123.195:3211/x-nmos/query/v1.3"
}
```

这是显式 Query 地址覆盖，需由部署方确保它对应当前注册中心；切换注册中心时应同步更新或移除此配置。URL 必须包含版本，不接受查询参数或 fragment。当前客户端不附带注册中心或 SDP 的认证凭据。

## 错误与一致性

| HTTP 状态 | 含义 |
| --- | --- |
| 400 | 分页、类型或未知查询参数无效。 |
| 503 | 节点未连接注册中心，或无法确定 Query API 地址。 |
| 502 | 注册中心请求失败、配置 URL 无效、响应格式异常或上游分页异常。 |

错误体为 `{"error":"原因"}`。SDP 的单条失败仍返回 HTTP 200。

客户端从 `paging.since=0:0` 开始沿上游 `Link: rel="next"` 获取完整 Sender 和 Flow 列表，去重、筛选后再做本接口分页。列表不缓存，因此分页查询仍需要读取上游全量元数据，适合需要准确筛选总数的场景；全量 SDP 查询可能耗时较长。单次请求最多接受每种资源 10000 个上游分页，超限报错而非返回截断结果。

注册中心在查询期间可能发生变化，Sender 和 Flow 的读取也不构成事务快照；连续翻页之间的记录可能随注册变化而移动。接口返回注册信息，不验证媒体包是否实际发送。
