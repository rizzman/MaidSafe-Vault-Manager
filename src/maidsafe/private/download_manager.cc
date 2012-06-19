/*
* ============================================================================
*
* Copyright [2011] maidsafe.net limited
*
* The following source code is property of maidsafe.net limited and is not
* meant for external use.  The use of this code is governed by the license
* file LICENSE.TXT found in the root of this directory and also on
* www.maidsafe.net.
*
* You are not free to copy, amend or otherwise use this source code without
* the explicit written permission of the board of directors of maidsafe.net.
*
* ============================================================================
*/

#include "maidsafe/private/download_manager.h"

#include <fstream>
#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "boost/archive/text_iarchive.hpp"
#include "boost/filesystem/fstream.hpp"
#include "boost/filesystem/operations.hpp"

#include "maidsafe/common/utils.h"
#include "maidsafe/common/log.h"

namespace bai = boost::asio::ip;

namespace maidsafe {

DownloadManager::DownloadManager(std::string site,
                                 std::string name,
                                 std::string platform,
                                 std::string cpu_size,
                                 std::string current_version,
                                 std::string current_patchlevel) :
  site_(site),
  name_(name),
  platform_(platform),
  cpu_size_(cpu_size),
  current_version_(current_version),
  current_patchlevel_(current_patchlevel),
  protocol_("http"),
  file_to_download_(),
  io_service_(),
  socket_(io_service_) {
  bai::tcp::resolver resolver(io_service_);
  bai::tcp::resolver::query query(site_, protocol_);
  bai::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
  // Try each endpoint until we successfully establish a connection.
  socket_ = bai::tcp::socket(io_service_);
  boost::asio::connect(socket_, endpoint_iterator);
}

bool DownloadManager::FileIsUseful(std::string file) {
  boost::char_separator<char> sep("_");
  boost::tokenizer<boost::char_separator<char>> tok(file, sep);
  auto it(tok.begin());
  std::string name(*it);
  if (name_ != name)
    return false;
  std::string platform(*(++it));
  if (platform_ != platform)
    return false;
  std::string cpu_size(*(++it));
  if (cpu_size_ != cpu_size)
    return false;
  uint32_t version(boost::lexical_cast<uint32_t>(*(++it)));
  if (current_version_ == "") {
    LOG(kInfo) << "Empty version, getting any version from server";
    return true;
  }
  uint32_t current_version(boost::lexical_cast<uint32_t>(current_version_));
  if (version < current_version)
    return false;
  uint32_t patchlevel(boost::lexical_cast<uint32_t>(*(++it)));
  if (current_patchlevel_ == "") {
    LOG(kInfo) << "Empty patchlevel, getting any patchlevel with current version from server";
    return true;
  }
  uint32_t current_patchlevel(boost::lexical_cast<uint32_t>(current_patchlevel_));
  if (patchlevel <= current_patchlevel)
    return false;
  return true;
}

bool DownloadManager::Exists() {
  std::vector<std::string> files;
  std::string current_file;
  file_to_download_ = "";
  boost::asio::streambuf file_list_buffer;
  std::istream file_list_stream(&file_list_buffer);
  if (!GetFileBuffer("/file_list", &file_list_buffer, &file_list_stream)) {
    return false;
  }
  // Read until EOF, puts whole file list in memory but this should be of manageable size
  boost::system::error_code error;
  while (boost::asio::read(socket_, file_list_buffer, boost::asio::transfer_at_least(1), error));
  if (error != boost::asio::error::eof) {
    LOG(kError) << "Error downloading list of latest file versions: " << error.message();
    return false;
  }
  while (std::getline(file_list_stream, current_file))
    files.push_back(current_file);
  auto it(files.begin());
  for (; it != files.end(); ++it)
    if (FileIsUseful(*it))
      break;
  if (it == files.end()) {
    LOG(kWarning) << "No more recent version of requested file " << name_
                  << " exists in latest file versions list";
    return false;
  }
  file_to_download_ = *it;
    LOG(kInfo) << "Found more recent version of file " << name_ << "on updates server";
  return true;
}

bool DownloadManager::UpdateCurrentFile(boost::filesystem::path directory) {
  boost::asio::streambuf current_file_buffer(1024);
    std::istream current_file_stream(&current_file_buffer);
  if (!GetFileBuffer("/" +/*add location on site here*/ file_to_download_, &current_file_buffer,
      &current_file_stream)) {
    return false;
  }
  try {
    boost::filesystem::ofstream file_out(directory / file_to_download_,
                                        std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file_out.good()) {
      LOG(kError) << "Can't get ofstream created for " << directory / file_to_download_;
      return false;
    }
    boost::system::error_code error;
    std::string current_block;
    std::ostringstream current_block_stream;
    // Read until EOF, puts whole file list in memory but this should be of manageable size
    while (boost::asio::read(socket_, current_file_buffer, error)) {
      if (error && error != boost::asio::error::eof) {
        LOG(kError) << "Error downloading file " << file_to_download_ << ": " << error.message();
        return false;
      }
      current_block_stream << current_file_stream.rdbuf();
      current_block = current_block_stream.str();
      file_out.write(current_block.c_str(), current_block.size());
    }
    LOG(kInfo) << "Finished downloading file " << file_to_download_ << ", closing file.";
    file_out.close();
  } catch(const std::exception &e) {
    LOG(kError) << "Failed to write file " << directory / file_to_download_ << ": " << e.what();
    return false;
  }
  return true;
}

bool DownloadManager::GetFileBuffer(const std::string& file_path,
                                    boost::asio::streambuf* response,
                                    std::istream* response_stream) {
  boost::asio::streambuf request;
  std::ostream request_stream(&request);
  try {
    // Form the request. We specify the "Connection: close" header so that the
    // server will close the socket after transmitting the response. This will
    // allow us to treat all data up until the EOF as the content.
    request_stream << "GET " << file_path <<" HTTP/1.0\r\n";
    request_stream << "Host: " << site_ << "\r\n";
    request_stream << "Accept: */*\r\n";  // TODO(dirvine) check files here
    request_stream << "Connection: close\r\n\r\n";
    // Send the request.
    boost::asio::write(socket_, request);
    // Read the response status line. The response streambuf will automatically
    // grow to accommodate the entire line. The growth may be limited by passing
    // a maximum size to the streambuf constructor.
    boost::asio::read_until(socket_, *response, "\r\n");
    // Check that response is OK.
    std::string http_version;
    *response_stream >> http_version;
    unsigned int status_code;
    *response_stream >> status_code;
    std::string status_message;
    std::getline(*response_stream, status_message);
    if (!(*response_stream) || http_version.substr(0, 5) != "HTTP/") {
      LOG(kError) << "Error downloading file list: Invalid response";
      return false;
    }
    if (status_code != 200) {
      LOG(kError) << "Error downloading file list: Response returned "
                  << "with status code " << status_code;
      return false;
    }

    // Read the response headers, which are terminated by a blank line.
    boost::asio::read_until(socket_, *response, "\r\n\r\n");

    // Process the response headers.
    std::string header;
    while (std::getline(*response_stream, header)) {
      if (header == "\r")
        break;
    }
  }
  catch(const std::exception &e) {
    LOG(kError) << "Exception: " << e.what();
    return false;
  }
  return true;
}

}  // namespace maidsafe