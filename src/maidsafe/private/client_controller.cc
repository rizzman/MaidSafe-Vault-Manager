/* Copyright (c) 2012 maidsafe.net limited
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
    * Neither the name of the maidsafe.net limited nor the names of its
    contributors may be used to endorse or promote products derived from this
    software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "maidsafe/private/client_controller.h"

#include <chrono>
#include <iostream>
#include <thread>

#include "boost/array.hpp"

#include "maidsafe/common/log.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/private/vault_identity_info_pb.h"
#include "maidsafe/private/vault_manager.h"

namespace bai = boost::asio::ip;

namespace maidsafe {

namespace priv {

ClientController::ClientController() : port_(0),
                                       asio_service_(new AsioService(2)),
                                       mutex_(),
                                       cond_var_(),
                                       state_(kInitialising) {
  asio_service_->Start();
  ConnectToManager();
}

ClientController::~ClientController() {}

void ClientController::ConnectToManager() {
  uint16_t port;
  {
    boost::mutex::scoped_lock lock(mutex_);
    if (state_ != kInitialising)
      return;
    if (port_ == 0)
      port_ = VaultManager::kMinPort;
    else
      ++port_;
    port = port_;
    if (port > VaultManager::kMaxPort) {
      state_ = kFailed;
      LOG(kError) << "ConnectToManager: Could not connect to any port in range "
                  << VaultManager::kMinPort << " to " << VaultManager::kMaxPort;
      cond_var_.notify_all();
      return;
    }
  }
  std::string hello_string;
  Endpoint endpoint(boost::asio::ip::address_v4::loopback(), port);
  int message_type(static_cast<int>(VaultManagerMessageType::kHelloFromClient));
  maidsafe::priv::ClientHello hello;
  hello.set_hello("hello");
  std::shared_ptr<TcpTransport> transport;
  std::shared_ptr<MessageHandler> message_handler;
  ResetTransport(transport, message_handler, nullptr);
  hello_string = message_handler->MakeSerialisedWrapperMessage(message_type,
                                                               hello.SerializeAsString());
  LOG(kInfo) << "ConnectToManager: trying port " << port;
  transport->Send(hello_string, endpoint, boost::posix_time::seconds(1));
}

void ClientController::ConnectToManagerCallback(const std::string& hello_response_string,
                                                const Info& sender_info) {
  ClientHelloResponse response;
  if (!response.ParseFromString(hello_response_string) ||
      response.hello_response() != "hello response") {
    LOG(kError) << "ConnectToManagerCallback: Invalid response, trying again.";
    ConnectToManager();
    return;
  }

  boost::mutex::scoped_lock lock(mutex_);
  port_ = sender_info.endpoint.port;
  state_ = kVerified;
  LOG(kSuccess) << "ConnectToManagerCallback: Successfully connected on port " << port_;
  cond_var_.notify_all();
}

void ClientController::OnSendError(const TransportCondition &transport_condition,
                                   const Endpoint& /*remote_endpoint*/,
                                   const std::function<void(bool)> &callback) {
  LOG(kError) << "OnSendError: Error sending/receiving connect message - " << transport_condition;
  ConnectToManager();
  if (callback)
    callback(false);
}

void ClientController::HandleIncomingMessage(const int &type,
                                             const std::string &payload,
                                             const Info &info,
                                             std::shared_ptr<TcpTransport> /*transport*/,
                                             std::shared_ptr<MessageHandler> /*message_handler*/,
                                             const std::function<void(bool)> &callback) {
  if (info.endpoint.ip.to_string() != "127.0.0.1") {
    LOG(kError) << "HandleIncomingMessage: message is not of local origin.";
    return;
  }
  VaultManagerMessageType message_type = boost::numeric_cast<VaultManagerMessageType>(type);
  switch (message_type) {
    case VaultManagerMessageType::kHelloResponseToClient:
      LOG(kInfo) << "kHelloResponseToClient";
      ConnectToManagerCallback(payload, info);
      if (callback)
        callback(true);
      break;
    case VaultManagerMessageType::kStartResponseToClient:
      LOG(kInfo) << "kStartResponseToClient";
      StartVaultRequestCallback(payload, info, callback);
      break;
    default:
      LOG(kWarning) << "Incorrect message type";
  }
}

void ClientController::StartVaultRequest(const maidsafe::asymm::Keys& keys,
                                         const std::string& account_name,
                                         const std::function<void(bool)> &callback) {
  int message_type(static_cast<int>(VaultManagerMessageType::kStartRequestFromClient));
  maidsafe::priv::ClientStartVaultRequest request;
  std::string keys_string;
  asymm::SerialiseKeys(keys, keys_string);
  request.set_keys(keys_string);
  request.set_account_name(account_name);
  std::string request_string;
  std::shared_ptr<TcpTransport> transport;
  std::shared_ptr<MessageHandler> message_handler;
  ResetTransport(transport, message_handler, callback);
  request_string = message_handler->MakeSerialisedWrapperMessage(message_type,
                                                                 request.SerializeAsString());
  uint16_t port;
  {
    boost::mutex::scoped_lock lock(mutex_);
    port = port_;
  }
  Endpoint endpoint(boost::asio::ip::address_v4::loopback(), port);
  LOG(kInfo) << "StartVaultRequest: Sending request to port " << port;
  transport->Send(request_string, endpoint, boost::posix_time::seconds(10));
}

void ClientController::StartVaultRequestCallback(const std::string& start_response_string,
                                                 const Info& /*sender_info*/,
                                                 const std::function<void(bool)> &callback) {
  ClientStartVaultResponse response;
  if (callback)
    callback(response.ParseFromString(start_response_string) && response.result());
}

void ClientController::ResetTransport(std::shared_ptr<TcpTransport> &transport,
                                      std::shared_ptr<MessageHandler> &message_handler,
                                      const std::function<void(bool)> &callback) {
  transport.reset(new TcpTransport(asio_service_->service()));
  message_handler.reset(new MessageHandler());
  transport->on_message_received()->connect(boost::bind(
      &MessageHandler::OnMessageReceived, message_handler.get(), _1, _2, _3, _4));
  transport->on_error()->connect(boost::bind(
      &MessageHandler::OnError, message_handler.get(), _1, _2));
  message_handler->on_error()->connect(boost::bind(
      &ClientController::OnSendError, this, _1, _2, callback));
  message_handler->SetCallback(boost::bind(
      &ClientController::HandleIncomingMessage, this, _1, _2, _3, transport, message_handler,
      callback));
}

bool ClientController::StartVault(const maidsafe::asymm::Keys& keys,
                                  const std::string& account_name) {
  {
    boost::mutex::scoped_lock lock(mutex_);
    if (!cond_var_.timed_wait(lock, boost::posix_time::seconds(3),
                              [&]() { return state_ != kInitialising; })) {
      LOG(kError) << "StartVault: Timed out waiting for initialisation.";
      return false;
    }
    if (state_ != kVerified) {
      LOG(kError) << "StartVault: Controller is uninitialised.";
      return false;
    }
  }

  boost::mutex local_mutex;
  boost::condition_variable local_cond_var;
  bool done(false), local_result(false);

  StartVaultRequest(keys, account_name,
                    [&local_mutex, &local_cond_var, &done, &local_result] (bool result) {
    boost::mutex::scoped_lock lock(local_mutex);
    local_result = result;
    done = true;
    local_cond_var.notify_one();
  });

  boost::mutex::scoped_lock lock(local_mutex);
  if (!local_cond_var.timed_wait(lock, boost::posix_time::seconds(10),
                                 [&]() { return done; })) {
    LOG(kError) << "StartVault: Timed out waiting for reply.";
    return false;
  }
  if (!local_result) {
    LOG(kError) << "StartVault: Failed starting vault.";
    return false;
  }

  return true;
}

bool ClientController::StopVault(const maidsafe::asymm::PlainText& /*data*/,
                                 const maidsafe::asymm::Signature& /*signature*/,
                                 const maidsafe::asymm::Identity& /*identity*/) {
  LOG(kError) << "StopVault: Not implemented.";
  return false;
}

}  // namespace priv

}  // namespace maidsafe
