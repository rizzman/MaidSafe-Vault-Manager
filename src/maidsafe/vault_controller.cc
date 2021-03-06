/*  Copyright 2012 MaidSafe.net limited

    This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,
    version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which
    licence you accepted on initial access to the Software (the "Licences").

    By contributing code to the MaidSafe Software, or to this project generally, you agree to be
    bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root
    directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also
    available at: http://www.maidsafe.net/licenses

    Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed
    under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS
    OF ANY KIND, either express or implied.

    See the Licences for the specific language governing permissions and limitations relating to
    use of the MaidSafe Software.                                                                 */

#include "maidsafe/client_manager/vault_controller.h"

#include <chrono>
#include <iterator>

#include "boost/algorithm/string.hpp"
#include "boost/filesystem/operations.hpp"

#include "maidsafe/common/config.h"
#include "maidsafe/common/error.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/passport/types.h"
#include "maidsafe/passport/passport.h"
#include "maidsafe/client_manager/controller_messages.pb.h"
#include "maidsafe/client_manager/local_tcp_transport.h"
#include "maidsafe/client_manager/return_codes.h"
#include "maidsafe/client_manager/utils.h"
#include "maidsafe/client_manager/client_manager.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace client_manager {

namespace {

boost::asio::ip::udp::endpoint GetEndpoint(const std::string& ip, Port port) {
  boost::asio::ip::udp::endpoint ep;
  ep.port(port);
  ep.address(boost::asio::ip::address::from_string(ip));
  return ep;
}

}  // namespace

typedef std::function<void()> VoidFunction;
typedef std::function<void(bool)> VoidFunctionBoolParam;  // NOLINT (Philip)

namespace bai = boost::asio::ip;

VaultController::VaultController(const std::string& client_manager_identifier,
                                 VoidFunction stop_callback)
    : process_index_(std::numeric_limits<uint32_t>::max()),
      client_manager_port_(0),
      local_port_(0),
      pmid_(),
      bootstrap_endpoints_(),
      stop_callback_(std::move(stop_callback)),
      asio_service_(3),
      receiving_transport_(std::make_shared<LocalTcpTransport>(asio_service_.service())) {
  if (!stop_callback_)
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::invalid_parameter));
  if (client_manager_identifier != "test") {
    detail::ParseVmidParameter(client_manager_identifier, process_index_,
                               client_manager_port_);
    OnMessageReceived::slot_type on_message_slot([this](
        const std::string & message,
        Port client_manager_port) { HandleReceivedRequest(message, client_manager_port); });
    detail::StartControllerListeningPort(receiving_transport_, on_message_slot, local_port_);
    RequestVaultIdentity(local_port_);
  }
}

VaultController::~VaultController() { receiving_transport_->StopListening(); }

bool VaultController::GetIdentity(
    std::unique_ptr<passport::Pmid>& pmid,
    std::vector<boost::asio::ip::udp::endpoint>& bootstrap_endpoints) {
  if (client_manager_port_ == 0) {
    std::cout << "Invalid ClientManager port." << std::endl;
    LOG(kError) << "Invalid ClientManager port.";
    return false;
  }
  pmid.reset(new passport::Pmid(*pmid_));
  bootstrap_endpoints = bootstrap_endpoints_;
  return true;
}

void VaultController::ConfirmJoin() {
  std::mutex local_mutex;
  std::condition_variable local_cond_var;
  bool done(false);
  protobuf::VaultJoinedNetwork vault_joined_network;
  vault_joined_network.set_process_index(process_index_);
  vault_joined_network.set_joined(true);

  VoidFunction callback = [&] {
    {
      std::lock_guard<std::mutex> lock(local_mutex);
      done = true;
    }
    local_cond_var.notify_one();
  };

  TransportPtr request_transport(std::make_shared<LocalTcpTransport>(asio_service_.service()));
  int result(0);
  request_transport->Connect(client_manager_port_, result);
  if (result != kSuccess) {
    LOG(kError) << "Failed to connect request transport to ClientManager.";
    return;
  }
  request_transport->on_message_received().connect([this, callback](
      const std::string & message,
      Port /*client_manager_port*/) { HandleVaultJoinedAck(message, callback); });
  request_transport->on_error().connect([callback](const int & error) {
    LOG(kError) << "Transport reported error code " << error;
    callback();
  });

  std::unique_lock<std::mutex> lock(local_mutex);
  LOG(kVerbose) << "Sending joined notification to port " << client_manager_port_;
  request_transport->Send(detail::WrapMessage(MessageType::kVaultJoinedNetwork,
                                              vault_joined_network.SerializeAsString()),
                          client_manager_port_);

  if (!local_cond_var.wait_for(lock, std::chrono::seconds(3),
                               [&] { return done; }))  // NOLINT (Fraser)
    LOG(kError) << "Timed out waiting for reply.";
}

void VaultController::HandleVaultJoinedAck(const std::string& message, VoidFunction callback) {
  MessageType type;
  std::string payload;
  if (!detail::UnwrapMessage(message, type, payload)) {
    LOG(kError) << "Failed to handle incoming message.";
    return;
  }

  protobuf::VaultJoinedNetworkAck vault_joined_network_ack;
  if (!vault_joined_network_ack.ParseFromString(payload)) {
    LOG(kError) << "Failed to parse VaultJoinedNetworkAck.";
    return;
  }
  callback();
}

bool VaultController::GetBootstrapNodes(
    std::vector<boost::asio::ip::udp::endpoint>& bootstrap_endpoints) {
  std::mutex local_mutex;
  std::condition_variable local_cond_var;
  bool done(false), success(false);
  protobuf::BootstrapRequest request;
  uint32_t message_id(maidsafe::RandomUint32());
  request.set_message_id(message_id);

  VoidFunctionBoolParam callback = [&](bool result) {
    {
      std::lock_guard<std::mutex> lock(local_mutex);
      done = true;
      success = result;
    }
    local_cond_var.notify_one();
  };

  TransportPtr request_transport(new LocalTcpTransport(asio_service_.service()));
  int result(0);
  request_transport->Connect(client_manager_port_, result);
  if (result != kSuccess) {
    LOG(kError) << "Failed to connect request transport to ClientManager.";
    return false;
  }
  request_transport->on_message_received().connect([this, callback, &bootstrap_endpoints](
      const std::string & message, Port /*client_manager_port*/) {
    HandleBootstrapResponse(message, bootstrap_endpoints, callback);
  });
  request_transport->on_error().connect([callback](const int & error) {
    LOG(kError) << "Transport reported error code " << error;
    callback(false);
  });
  std::unique_lock<std::mutex> lock(local_mutex);
  LOG(kVerbose) << "Requesting bootstrap nodes from port " << client_manager_port_;
  request_transport->Send(
      detail::WrapMessage(MessageType::kBootstrapRequest, request.SerializeAsString()),
      client_manager_port_);
  if (!local_cond_var.wait_for(lock, std::chrono::seconds(3),
                               [&] { return done; })) {  // NOLINT (Philip)
    LOG(kError) << "Timed out waiting for reply.";
    return false;
  }
  return success;
}

void VaultController::HandleBootstrapResponse(
    const std::string& message, std::vector<boost::asio::ip::udp::endpoint>& bootstrap_endpoints,
    VoidFunctionBoolParam callback) {
  MessageType type;
  std::string payload;
  if (!detail::UnwrapMessage(message, type, payload)) {
    LOG(kError) << "Failed to handle incoming message.";
    callback(false);
    return;
  }

  protobuf::BootstrapResponse bootstrap_response;
  if (!bootstrap_response.ParseFromString(payload)) {
    LOG(kError) << "Failed to parse BootstrapResponse.";
    callback(false);
    return;
  }

  if (bootstrap_response.bootstrap_endpoint_ip_size() !=
      bootstrap_response.bootstrap_endpoint_port_size()) {
    LOG(kWarning) << "Number of ports in endpoints does not equal number of addresses";
  }
  int size(std::min(bootstrap_response.bootstrap_endpoint_ip_size(),
                    bootstrap_response.bootstrap_endpoint_port_size()));
  for (int i(0); i < size; ++i) {
    try {
      bootstrap_endpoints.push_back(
          GetEndpoint(bootstrap_response.bootstrap_endpoint_ip(i),
                      static_cast<Port>(bootstrap_response.bootstrap_endpoint_port(i))));
    }
    catch (...) {
      continue;
    }
  }
  bootstrap_endpoints_ = bootstrap_endpoints;
  callback(true);
}

bool VaultController::SendEndpointToClientManager(
    const boost::asio::ip::udp::endpoint& endpoint) {
  std::mutex local_mutex;
  std::condition_variable local_cond_var;
  bool done(false), success(false);
  protobuf::SendEndpointToClientManagerRequest request;
  request.set_bootstrap_endpoint_ip(endpoint.address().to_string());
  request.set_bootstrap_endpoint_port(endpoint.port());

  VoidFunctionBoolParam callback = [&](bool result) {
    {
      std::lock_guard<std::mutex> lock(local_mutex);
      done = true;
      success = result;
    }
    local_cond_var.notify_one();
  };

  TransportPtr request_transport(new LocalTcpTransport(asio_service_.service()));
  int result(0);
  request_transport->Connect(client_manager_port_, result);
  if (result != kSuccess) {
    LOG(kError) << "Failed to connect request transport to ClientManager.";
    return false;
  }
  request_transport->on_message_received().connect([this, callback](
      const std::string & message, Port /*client_manager_port*/) {
    HandleSendEndpointToClientManagerResponse(message, callback);
  });
  request_transport->on_error().connect([callback](const int & error) {
    LOG(kError) << "Transport reported error code " << error;
    callback(false);
  });

  std::unique_lock<std::mutex> lock(local_mutex);
  LOG(kVerbose) << "Sending bootstrap endpoint to port " << client_manager_port_;
  request_transport->Send(detail::WrapMessage(MessageType::kSendEndpointToClientManagerRequest,
                                              request.SerializeAsString()),
                          client_manager_port_);
  if (!local_cond_var.wait_for(lock, std::chrono::seconds(3),
                               [&] { return done; })) {  // NOLINT (Philip)
    LOG(kError) << "Timed out waiting for reply.";
    return false;
  }
  return success;
}

void VaultController::HandleSendEndpointToClientManagerResponse(const std::string& message,
                                                                   VoidFunctionBoolParam callback) {
  MessageType type;
  std::string payload;
  if (!detail::UnwrapMessage(message, type, payload)) {
    LOG(kError) << "Failed to handle incoming message.";
    callback(false);
    return;
  }

  protobuf::SendEndpointToClientManagerResponse send_endpoint_response;
  if (!send_endpoint_response.ParseFromString(payload)) {
    LOG(kError) << "Failed to parse SendEndpointToClientManagerResponse.";
    callback(false);
    return;
  }
  callback(send_endpoint_response.result());
}

void VaultController::RequestVaultIdentity(uint16_t listening_port) {
  std::mutex local_mutex;
  std::condition_variable local_cond_var;

  protobuf::VaultIdentityRequest vault_identity_request;
  vault_identity_request.set_process_index(process_index_);
  vault_identity_request.set_listening_port(listening_port);
  vault_identity_request.set_version(VersionToInt(kApplicationVersion()));

  TransportPtr request_transport(std::make_shared<LocalTcpTransport>(asio_service_.service()));
  int connect_result(0);
  request_transport->Connect(client_manager_port_, connect_result);
  if (connect_result != kSuccess) {
    LOG(kError) << "Failed to connect request transport to ClientManager.";
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::uninitialised));
  }

  auto connection(
      request_transport->on_message_received().connect([this, &local_mutex, &local_cond_var](
          const std::string & message, Port /*client_manager_port*/) {
        HandleVaultIdentityResponse(message, local_mutex);
        local_cond_var.notify_one();
      }));
  auto error_connection(request_transport->on_error().connect([](const int & error) {
    LOG(kError) << "Transport reported error code " << error;
  }));

  std::unique_lock<std::mutex> lock(local_mutex);
  LOG(kVerbose) << "Sending request for vault identity to port " << client_manager_port_;
  request_transport->Send(detail::WrapMessage(MessageType::kVaultIdentityRequest,
                                              vault_identity_request.SerializeAsString()),
                          client_manager_port_);

  if (!local_cond_var.wait_for(lock, std::chrono::seconds(3),
                               [this]() { return static_cast<bool>(pmid_); })) {
    connection.disconnect();
    error_connection.disconnect();
    LOG(kError) << "Timed out waiting for reply.";
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::uninitialised));
  }
}

void VaultController::HandleVaultIdentityResponse(const std::string& message, std::mutex& mutex) {
  MessageType type;
  std::string payload;
  std::lock_guard<std::mutex> lock(mutex);
  if (!detail::UnwrapMessage(message, type, payload)) {
    LOG(kError) << "Failed to handle incoming message.";
    return;
  }

  protobuf::VaultIdentityResponse vault_identity_response;
  if (!vault_identity_response.ParseFromString(payload)) {
    LOG(kError) << "Failed to parse VaultIdentityResponse.";
    return;
  }

  pmid_.reset(
      new passport::Pmid(passport::ParsePmid(NonEmptyString(vault_identity_response.pmid()))));

  if (vault_identity_response.bootstrap_endpoint_ip_size() !=
      vault_identity_response.bootstrap_endpoint_port_size()) {
    LOG(kWarning) << "Number of ports in endpoints does not equal number of addresses";
  }
  int size(std::min(vault_identity_response.bootstrap_endpoint_ip_size(),
                    vault_identity_response.bootstrap_endpoint_port_size()));
  for (int i(0); i < size; ++i) {
    try {
      bootstrap_endpoints_.push_back(
          GetEndpoint(vault_identity_response.bootstrap_endpoint_ip(i),
                      static_cast<Port>(vault_identity_response.bootstrap_endpoint_port(i))));
    }
    catch (...) {
      continue;
    }
  }

  LOG(kInfo) << "Received VaultIdentityResponse.";
}

void VaultController::HandleReceivedRequest(const std::string& message, Port /*peer_port*/) {
  MessageType type;
  std::string payload;
  if (!detail::UnwrapMessage(message, type, payload)) {
    LOG(kError) << "Failed to handle incoming message.";
    return;
  }
  LOG(kVerbose) << "HandleReceivedRequest: message type " << static_cast<int>(type) << " received.";
  std::string response;
  switch (type) {
    case MessageType::kVaultShutdownRequest:
      HandleVaultShutdownRequest(payload, response);
      break;
    default:
      return;
  }
}

void VaultController::HandleVaultShutdownRequest(const std::string& request,
                                                 std::string& /*response*/) {
  LOG(kInfo) << "Received shutdown request.";
  protobuf::VaultShutdownRequest vault_shutdown_request;
  protobuf::VaultShutdownResponse vault_shutdown_response;
  if (!vault_shutdown_request.ParseFromString(request)) {
    LOG(kError) << "Failed to parse VaultShutdownRequest.";
    vault_shutdown_response.set_shutdown(false);
  } else if (vault_shutdown_request.process_index() != process_index_) {
    LOG(kError) << "This shutdown request is not for this process.";
    vault_shutdown_response.set_shutdown(false);
  } else {
    vault_shutdown_response.set_shutdown(true);
  }
  vault_shutdown_response.set_process_index(process_index_);
  stop_callback_();
}

}  // namespace client_manager

}  // namespace maidsafe
