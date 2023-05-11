#pragma once

#include <cstdint>
#include <memory>

#include "envoy/http/codec.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/logger.h"
#include "source/common/config/well_known_names.h"
#include "source/common/network/utility.h"
#include "source/common/router/upstream_request.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Udp {

class UdpConnPool : public Router::GenericConnPool {
public:
  UdpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
              Upstream::LoadBalancerContext* ctx)
      : host_(thread_local_cluster.loadBalancer().chooseHost(ctx)) {}

  void newStream(Router::GenericConnectionPoolCallbacks* callbacks) override;

  bool cancelAnyPendingStream() override { return false; }

  Upstream::HostDescriptionConstSharedPtr host() const override { return host_; }

  Network::SocketPtr createSocket(const Upstream::HostConstSharedPtr& host) {
    return std::make_unique<Network::SocketImpl>(Network::Socket::Type::Datagram, host->address(),
                                                 nullptr, Network::SocketCreationOptions{});
  }

  bool valid() { return host_ != nullptr; }

private:
  Upstream::HostConstSharedPtr host_;
};

class UdpUpstream : public Router::GenericUpstream,
                    public Network::UdpPacketProcessor,
                    public quiche::CapsuleParser::Visitor {
public:
  UdpUpstream(Router::UpstreamToDownstream* upstream_request, Network::SocketPtr socket,
              Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher);

  // GenericUpstream
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeMetadata(const Envoy::Http::MetadataMapVector&) override {}
  Envoy::Http::Status encodeHeaders(const Envoy::Http::RequestHeaderMap&, bool end_stream) override;
  void encodeTrailers(const Envoy::Http::RequestTrailerMap&) override{};
  void readDisable(bool) override{};
  void resetStream() override;
  void setAccount(Buffer::BufferMemoryAccountSharedPtr) override {}
  const StreamInfo::BytesMeterSharedPtr& bytesMeter() override { return bytes_meter_; }

  // Network::UdpPacketProcessor
  void processPacket(Network::Address::InstanceConstSharedPtr local_address,
                     Network::Address::InstanceConstSharedPtr peer_address,
                     Buffer::InstancePtr buffer, MonotonicTime receive_time) override;
  uint64_t maxDatagramSize() const override {
    // TODO: Make it configurable
    return Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE;
  }
  void onDatagramsDropped(uint32_t) override {
    // TODO: Maintain stats.
  }
  size_t numPacketsExpectedPerEventLoop() const override {
    return Network::MAX_NUM_PACKETS_PER_EVENT_LOOP;
  }

  // quiche::CapsuleParser::Visitor
  // Converts a given Capsule to a HTTP/3 Datagrams and send it.
  bool OnCapsule(const quiche::Capsule& capsule) override;
  void OnCapsuleParseFailure(absl::string_view error_message) override;

private:
  void onSocketReadReady();

  Router::UpstreamToDownstream* upstream_request_;
  const Network::SocketPtr socket_;
  Upstream::HostConstSharedPtr host_;
  Event::Dispatcher& dispatcher_;
  StreamInfo::BytesMeterSharedPtr bytes_meter_{std::make_shared<StreamInfo::BytesMeter>()};
  quiche::CapsuleParser capsule_parser_{this};
  quiche::SimpleBufferAllocator capsule_buffer_allocator_;
};

} // namespace Udp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
