#ifdef __linux__

#include <deque>
#include <utility>

#include "absl/strings/match.h"
#include "src/common/base/base.h"
#include "src/common/base/utils.h"
#include "src/stirling/event_parser.h"
#include "src/stirling/socket_trace_connector.h"

// TODO(yzhao): Consider simplify the semantic by filtering entirely on content type.
DEFINE_string(http_response_header_filters, "Content-Type:json",
              "Comma-separated strings to specify the substrings should be included for a header. "
              "The format looks like <header-1>:<substr-1>,...,<header-n>:<substr-n>. "
              "The substrings cannot include comma(s). The filters are conjunctive, "
              "therefore the headers can be duplicate. For example, "
              "'Content-Type:json,Content-Type:text' will select a HTTP response "
              "with a Content-Type header whose value contains 'json' *or* 'text'.");

namespace pl {
namespace stirling {

Status SocketTraceConnector::InitImpl() {
  if (!IsRoot()) {
    return error::PermissionDenied("BCC currently only supported as the root user.");
  }
  auto init_res = bpf_.init(std::string(kBCCScript));
  if (init_res.code() != 0) {
    return error::Internal(
        absl::StrCat("Failed to initialize BCC script, error message: ", init_res.msg()));
  }
  // TODO(yzhao): We need to clean the already attached probes after encountering a failure.
  for (const ProbeSpec& p : kProbeSpecs) {
    ebpf::StatusTuple attach_status =
        bpf_.attach_kprobe(bpf_.get_syscall_fnname(p.kernel_fn_short_name), p.trace_fn_name,
                           p.kernel_fn_offset, p.attach_type);
    if (attach_status.code() != 0) {
      return error::Internal(
          absl::StrCat("Failed to attach kprobe to kernel function: ", p.kernel_fn_short_name,
                       ", error message: ", attach_status.msg()));
    }
  }
  for (auto& perf_buffer_spec : kPerfBufferSpecs) {
    ebpf::StatusTuple open_status = bpf_.open_perf_buffer(
        perf_buffer_spec.name, perf_buffer_spec.probe_output_fn, perf_buffer_spec.probe_loss_fn,
        // TODO(yzhao): We sort of are not unified around how record_batch and
        // cb_cookie is passed to the callback. Consider unifying them.
        /*cb_cookie*/ this, perf_buffer_spec.num_pages);
    if (open_status.code() != 0) {
      return error::Internal(absl::StrCat("Failed to open perf buffer: ", perf_buffer_spec.name,
                                          ", error message: ", open_status.msg()));
    }
  }

  PL_RETURN_IF_ERROR(Configure(kProtocolHTTP, kSocketTraceSendReq | kSocketTraceRecvResp));
  PL_RETURN_IF_ERROR(Configure(kProtocolMySQL, kSocketTraceSendReq));
  PL_RETURN_IF_ERROR(Configure(kProtocolHTTP2, kSocketTraceSendReq | kSocketTraceRecvResp));

  // TODO(oazizi): if machine is ever suspended, this would have to be called again.
  InitClockRealTimeOffset();

  return Status::OK();
}

Status SocketTraceConnector::StopImpl() {
  // TODO(yzhao): We should continue to detach after encountering a failure.
  for (const ProbeSpec& p : kProbeSpecs) {
    ebpf::StatusTuple detach_status =
        bpf_.detach_kprobe(bpf_.get_syscall_fnname(p.kernel_fn_short_name), p.attach_type);
    if (detach_status.code() != 0) {
      return error::Internal(
          absl::StrCat("Failed to detach kprobe to kernel function: ", p.kernel_fn_short_name,
                       ", error message: ", detach_status.msg()));
    }
  }

  for (auto& perf_buffer_spec : kPerfBufferSpecs) {
    ebpf::StatusTuple close_status = bpf_.close_perf_buffer(perf_buffer_spec.name);
    if (close_status.code() != 0) {
      return error::Internal(absl::StrCat("Failed to close perf buffer: ", perf_buffer_spec.name,
                                          ", error message: ", close_status.msg()));
    }
  }

  return Status::OK();
}

void SocketTraceConnector::TransferDataImpl(uint32_t table_num,
                                            types::ColumnWrapperRecordBatch* record_batch) {
  CHECK_LT(table_num, kTables.size())
      << absl::StrFormat("Trying to access unexpected table: table_num=%d", table_num);
  CHECK(record_batch != nullptr) << "record_batch cannot be nullptr";

  // TODO(oazizi): Should this run more frequently than TransferDataImpl?
  // This drains the relevant perf buffer, and causes Handle() callback functions to get called.
  record_batch_ = record_batch;
  ReadPerfBuffer(table_num);
  record_batch_ = nullptr;

  // ReadPerfBuffer copies data into a reorder buffer called the write_stream_map_.
  // This call transfers the data from the
  TransferStreamData(table_num, record_batch);
}

Status SocketTraceConnector::Configure(uint32_t protocol, uint64_t config_mask) {
  auto control_map_handle = bpf_.get_array_table<uint64_t>("control_map");

  auto update_res = control_map_handle.update_value(protocol, config_mask);
  if (update_res.code() != 0) {
    return error::Internal("Failed to set control map");
  }

  config_mask_[protocol] = config_mask;

  return Status::OK();
}

//-----------------------------------------------------------------------------
// Perf Buffer Polling and Callback functions.
//-----------------------------------------------------------------------------

void SocketTraceConnector::ReadPerfBuffer(uint32_t table_num) {
  DCHECK_LT(table_num, kTablePerfBufferMap.size())
      << "Index out of bound. Trying to read from perf buffer that doesn't exist.";
  auto buffer_names = kTablePerfBufferMap[table_num];
  for (auto& buffer_name : buffer_names) {
    auto perf_buffer = bpf_.get_perf_buffer(buffer_name.get());
    if (perf_buffer != nullptr) {
      perf_buffer->poll(1);
    }
  }
}

void SocketTraceConnector::HandleHTTPProbeOutput(void* cb_cookie, void* data, int /*data_size*/) {
  DCHECK(cb_cookie != nullptr) << "Perf buffer callback not set-up properly. Missing cb_cookie.";
  auto* connector = static_cast<SocketTraceConnector*>(cb_cookie);
  connector->AcceptDataEvent(SocketDataEvent(data));
}

void SocketTraceConnector::HandleMySQLProbeOutput(void* cb_cookie, void* data, int /*data_size*/) {
  DCHECK(cb_cookie != nullptr) << "Perf buffer callback not set-up properly. Missing cb_cookie.";
  auto* connector = static_cast<SocketTraceConnector*>(cb_cookie);
  // TODO(oazizi): Use AcceptDataEvent() to handle reorderings.
  connector->TransferMySQLEvent(SocketDataEvent(data), connector->record_batch_);
}

// This function is invoked by BCC runtime when a item in the perf buffer is not read and lost.
// For now we do nothing.
void SocketTraceConnector::HandleProbeLoss(void* /*cb_cookie*/, uint64_t lost) {
  VLOG(1) << "Possibly lost " << lost << " samples";
  // TODO(oazizi): Can we figure out which perf buffer lost the event?
}

void SocketTraceConnector::HandleOpenProbeOutput(void* cb_cookie, void* data, int /*data_size*/) {
  DCHECK(cb_cookie != nullptr) << "Perf buffer callback not set-up properly. Missing cb_cookie.";
  auto* connector = static_cast<SocketTraceConnector*>(cb_cookie);
  const auto conn = CopyFromBPF<conn_info_t>(data);
  connector->AcceptOpenConnEvent(conn);
}

void SocketTraceConnector::HandleCloseProbeOutput(void* cb_cookie, void* data, int /*data_size*/) {
  DCHECK(cb_cookie != nullptr) << "Perf buffer callback not set-up properly. Missing cb_cookie.";
  auto* connector = static_cast<SocketTraceConnector*>(cb_cookie);
  const auto conn = CopyFromBPF<conn_info_t>(data);
  connector->AcceptCloseConnEvent(conn);
}

//-----------------------------------------------------------------------------
// Stream Functions
//-----------------------------------------------------------------------------

namespace {

uint64_t GetStreamId(uint32_t tgid, uint32_t conn_id) {
  return (static_cast<uint64_t>(tgid) << 32) | conn_id;
}

}  // namespace

void SocketTraceConnector::AcceptDataEvent(SocketDataEvent event) {
  const uint64_t stream_id = GetStreamId(event.attr.tgid, event.attr.conn_id);

  // Need to adjust the clocks to convert to real time.
  event.attr.timestamp_ns += ClockRealTimeOffset();

  switch (event.attr.protocol) {
    case kProtocolHTTP:
    case kProtocolHTTP2:
      // TODO(oazizi/yzhao): Add MySQL when it goes through streams.
      break;
    default:
      LOG(WARNING) << "AcceptDataEvent ignored due to unknown protocol: " << event.attr.protocol;
      return;
  }

  ConnectionTracker& tracker = connection_trackers_[stream_id];
  tracker.AddDataEvent(event);
}

void SocketTraceConnector::AcceptOpenConnEvent(conn_info_t conn_info) {
  const uint64_t stream_id = GetStreamId(conn_info.tgid, conn_info.conn_id);

  // Need to adjust the clocks to convert to real time.
  conn_info.timestamp_ns += ClockRealTimeOffset();

  ConnectionTracker& tracker = connection_trackers_[stream_id];
  tracker.AddConnOpenEvent(conn_info);
}

void SocketTraceConnector::AcceptCloseConnEvent(conn_info_t conn_info) {
  const uint64_t stream_id = GetStreamId(conn_info.tgid, conn_info.conn_id);
  conn_info.timestamp_ns += ClockRealTimeOffset();

  // Need to adjust the clocks to convert to real time.
  conn_info.timestamp_ns += ClockRealTimeOffset();

  ConnectionTracker& tracker = connection_trackers_[stream_id];
  tracker.AddConnCloseEvent();
}

//-----------------------------------------------------------------------------
// HTTP Specific TransferImpl Helpers
//-----------------------------------------------------------------------------

void SocketTraceConnector::TransferStreamData(uint32_t table_num,
                                              types::ColumnWrapperRecordBatch* record_batch) {
  switch (table_num) {
    case kHTTPTableNum:
      TransferStreams<HTTPMessage>(kProtocolHTTP, record_batch);
      TransferStreams<HTTPMessage>(kProtocolHTTP2, record_batch);
      break;
    case kMySQLTableNum:
      // TODO(oazizi): Convert MySQL protocol to use streams.
      // TransferStreams<MySQLMessage>(kProtocolMySQL, record_batch);
      break;
    default:
      CHECK(false) << absl::StrFormat("Unknown table number: %d", table_num);
  }
}

template <class TMessageType>
void SocketTraceConnector::TransferStreams(TrafficProtocol protocol,
                                           types::ColumnWrapperRecordBatch* record_batch) {
  // TODO(oazizi): The single connection trackers model makes TransferStreams() inefficient,
  //               because it will get called multiple times, looping through all connection
  //               trackers, but selecting a mutually exclusive subset each time.
  //               Possible solutions: 1) different pools, 2) auxiliary pool of pointers.
  for (auto& [id, stream] : connection_trackers_) {
    PL_UNUSED(id);

    if (stream.protocol() != protocol) {
      continue;
    }

    // TODO(oazizi): I don't like this way of detecting requestor vs responder. But works for now.
    bool is_requestor_side = (config_mask_[protocol] & kSocketTraceSendReq) ||
                             (config_mask_[protocol] & kSocketTraceRecvResp);
    bool is_responder_side = (config_mask_[protocol] & kSocketTraceSendResp) ||
                             (config_mask_[protocol] & kSocketTraceRecvReq);
    CHECK(is_requestor_side ^ is_responder_side)
        << absl::StrFormat("Must be either requestor or responder (and not both)");

    auto& resp_data = is_requestor_side ? stream.recv_data() : stream.send_data();
    resp_data.template ExtractMessages<TMessageType>(MessageType::kResponses);
    auto& resp_messages = std::get<std::deque<TMessageType>>(resp_data.messages);

    auto& req_data = is_requestor_side ? stream.send_data() : stream.recv_data();
    req_data.template ExtractMessages<TMessageType>(MessageType::kRequests);
    auto& req_messages = std::get<std::deque<TMessageType>>(req_data.messages);

    // TODO(oazizi): Section defined below may need to be split out
    // by message type (e.g. HTTP1, GRPC, MySQL) to give flexibility.
    //------- BEGIN SECTION --------

    // TODO(oazizi): If we stick with this approach, resp_data could be converted back to vector.
    for (TMessageType& msg : resp_messages) {
      if (!req_messages.empty()) {
        TraceRecord<TMessageType> record{stream.conn(), std::move(req_messages.front()),
                                         std::move(msg)};
        req_messages.pop_front();
        ConsumeMessage(std::move(record), record_batch);
      } else {
        TraceRecord<TMessageType> record{stream.conn(), HTTPMessage(), std::move(msg)};
        ConsumeMessage(std::move(record), record_batch);
      }
    }
    resp_messages.clear();

    //------- END SECTION --------
  }

  // TODO(yzhao): Add the capability to remove events that are too old.
  // TODO(yzhao): Consider change the data structure to a vector, and use sorting to order events
  // before stitching. That might be faster (verify with benchmark).
}

template <class TMessageType>
void SocketTraceConnector::ConsumeMessage(TraceRecord<TMessageType> record,
                                          types::ColumnWrapperRecordBatch* record_batch) {
  // Only allow certain records to be transferred upstream.
  if (SelectMessage(record)) {
    // Currently decompresses gzip content, but could handle other transformations too.
    // Note that we do this after filtering to avoid burning CPU cycles unnecessarily.
    PreProcessMessage(&record.resp_message);

    // Push data to the TableStore.
    AppendMessage(std::move(record), record_batch);
  }
}

template <>
bool SocketTraceConnector::SelectMessage(const TraceRecord<HTTPMessage>& record) {
  // Some of this function is currently a placeholder for the demo.
  // TODO(oazizi/yzhao): update this function further.

  const HTTPMessage& message = record.resp_message;

  // Rule: Exclude any HTTP requests.
  // TODO(oazizi): Think about how requests should be handled by this function.
  if (message.type == HTTPEventType::kHTTPRequest) {
    return false;
  }

  // Rule: Exclude anything that doesn't specify its Content-Type.
  auto content_type_iter = message.http_headers.find(http_headers::kContentType);
  if (content_type_iter == message.http_headers.end()) {
    return false;
  }

  // Rule: Exclude anything that doesn't match the filter, if filter is active.
  if (message.type == HTTPEventType::kHTTPResponse &&
      (!http_response_header_filter_.inclusions.empty() ||
       !http_response_header_filter_.exclusions.empty())) {
    if (!MatchesHTTPTHeaders(message.http_headers, http_response_header_filter_)) {
      return false;
    }
  }

  return true;
}

namespace {

HTTPContentType DetectContentType(const HTTPMessage& message) {
  auto content_type_iter = message.http_headers.find(http_headers::kContentType);
  if (content_type_iter == message.http_headers.end()) {
    return HTTPContentType::kUnknown;
  }
  if (absl::StrContains(content_type_iter->second, "json")) {
    return HTTPContentType::kJSON;
  }
  if (absl::StrContains(content_type_iter->second, "grpc")) {
    return HTTPContentType::kGRPC;
  }
  return HTTPContentType::kUnknown;
}

}  // namespace

template <>
void SocketTraceConnector::AppendMessage(TraceRecord<HTTPMessage> record,
                                         types::ColumnWrapperRecordBatch* record_batch) {
  CHECK_EQ(kHTTPTable.elements().size(), record_batch->size());

  const SocketConnection& conn = record.conn;
  HTTPMessage& req_message = record.req_message;
  HTTPMessage& resp_message = record.resp_message;

  // Check for positive latencies.
  DCHECK_GE(resp_message.timestamp_ns, conn.timestamp_ns);

  RecordBuilder<&kHTTPTable> r(record_batch);
  r.Append<r.ColIndex("time_")>(resp_message.timestamp_ns);
  r.Append<r.ColIndex("tgid")>(conn.tgid);
  r.Append<r.ColIndex("fd")>(conn.fd);
  // TODO(oazizi): Kill this?
  r.Append<r.ColIndex("event_type")>(HTTPEventTypeToString(resp_message.type));
  // Note that there is a string copy here,
  // But std::move is not allowed because we re-use conn object.
  // TODO(oazizi): Long-term need to make remote_addr a uint128.
  r.Append<r.ColIndex("remote_addr")>(std::string(conn.remote_addr));
  r.Append<r.ColIndex("remote_port")>(conn.remote_port);
  r.Append<r.ColIndex("http_major_version")>(1);
  r.Append<r.ColIndex("http_minor_version")>(resp_message.http_minor_version);
  r.Append<r.ColIndex("http_headers")>(
      absl::StrJoin(resp_message.http_headers, "\n", absl::PairFormatter(": ")));
  r.Append<r.ColIndex("http_content_type")>(static_cast<uint64_t>(DetectContentType(resp_message)));
  r.Append<r.ColIndex("http_req_method")>(std::move(req_message.http_req_method));
  r.Append<r.ColIndex("http_req_path")>(std::move(req_message.http_req_path));
  r.Append<r.ColIndex("http_resp_status")>(resp_message.http_resp_status);
  r.Append<r.ColIndex("http_resp_message")>(std::move(resp_message.http_resp_message));
  r.Append<r.ColIndex("http_resp_body")>(std::move(resp_message.http_msg_body));
  r.Append<r.ColIndex("http_resp_latency_ns")>(resp_message.timestamp_ns - conn.timestamp_ns);
  // TODO(oazizi): Change to req timestamp when it exists.
}

//-----------------------------------------------------------------------------
// MySQL Specific TransferImpl Helpers
//-----------------------------------------------------------------------------

void SocketTraceConnector::TransferMySQLEvent(SocketDataEvent event,
                                              types::ColumnWrapperRecordBatch* record_batch) {
  // TODO(oazizi): Enable the below to only capture requestor-side messages.
  //  if (event.attr.event_type != kEventTypeSyscallWriteEvent &&
  //      event.attr.event_type != kEventTypeSyscallSendEvent) {
  //    return;
  //  }

  // TODO(chengruizhe/oazizi): Add connection info back, once MySQL uses a ConnectionTracker.
  int fd = -1;
  std::string ip = "-";
  int port = -1;

  RecordBuilder<&kMySQLTable> r(record_batch);
  r.Append<r.ColIndex("time_")>(event.attr.timestamp_ns + ClockRealTimeOffset());
  r.Append<r.ColIndex("tgid")>(event.attr.tgid);
  r.Append<r.ColIndex("fd")>(fd);
  r.Append<r.ColIndex("bpf_event")>(event.attr.event_type);
  r.Append<r.ColIndex("remote_addr")>(std::move(ip));
  r.Append<r.ColIndex("remote_port")>(port);
  r.Append<r.ColIndex("body")>(std::move(event.msg));
}

}  // namespace stirling
}  // namespace pl

#endif
