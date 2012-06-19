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

#ifndef MAIDSAFE_PRIVATE_DOWNLOAD_MANAGER_H_
#define MAIDSAFE_PRIVATE_DOWNLOAD_MANAGER_H_
#include <fstream>
#include <iostream>
#include <istream>
#include <ostream>
#include <memory>
#include <string>
#include <vector>

#include "boost/asio.hpp"
#include "boost/filesystem/path.hpp"

namespace maidsafe {
// assumes NAME-VERSION-PATCH naming convention
// passing "" will mean system ignores these and always downloads
// the file
class DownloadManager {
 public:
  DownloadManager(std::string site,
                  std::string name, // eg maidsafe_vault / maidsafe_client
                  std::string platform, // linux / osx / windows
                  std::string cpu_size,  // 64/32
                  std::string current_version, // e.g. 123
                  std::string current_patchlevel); // e.g. 234
//  void SetSiteName(std::string site);
//  void SetProtocol(std::string protocol = "http");
//  void SetNameToDownload(std::string name);
//  void SetVersionToDownload(std::string version);
//  void SetPatchlevelToDownload(std::string patchlevel);
  bool Exists();
  std::ostream & ReadStream();
  std::string ReadString();
 private:
  std::string site_;
  std::string name_;
  std::string current_version_;
  std::string current_patchlevel_;
  std::string protocol_;
};



int RetrieveBootstrapContacts(const fs::path &download_dir,
                              std::vector<dht::Contact> *bootstrap_contacts);



}  // namespace maidsafe

#endif  // MAIDSAFE_PRIVATE_DOWNLOAD_MANAGER_H_
