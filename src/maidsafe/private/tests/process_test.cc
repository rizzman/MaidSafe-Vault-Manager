/* Copyright (c) 2009 maidsafe.net limited
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

#include <thread>
#include <string>
#include <vector>

#include "maidsafe/common/test.h"
#include "maidsafe/private/process_manager.h"

namespace maidsafe {

namespace test {

TEST(ProcessManagerTest, BEH_StartSingleProcess) {
  maidsafe::ProcessManager manager;
  maidsafe::Process test;
  ASSERT_TRUE(test.SetProcessName("DUMMYprocess"));
  test.AddArgument("DUMMYprocess");
  test.AddArgument("--runtime");
  test.AddArgument("1");
  test.AddArgument("--nocrash");

  EXPECT_EQ(0, manager.NumberOfProcesses());
  EXPECT_EQ(0, manager.NumberOfLiveProcesses());
  EXPECT_EQ(0, manager.NumberOfSleepingProcesses());
  uint32_t num = manager.AddProcess(test);
  manager.StartProcess(num);
  EXPECT_GT(num, 0);
  manager.LetProcessDie(num);
  EXPECT_EQ(1, manager.NumberOfProcesses());
  EXPECT_EQ(0, manager.NumberOfLiveProcesses());
  EXPECT_EQ(0, manager.NumberOfSleepingProcesses());
}

TEST(ProcessManagerTest, BEH_StartThenKillProcess) {
  maidsafe::ProcessManager manager;
  maidsafe::Process test;
  ASSERT_TRUE(test.SetProcessName("DUMMYprocess"));
  test.AddArgument("DUMMYprocess");
  test.AddArgument("--runtime");
  test.AddArgument("60");
  test.AddArgument("--nocrash");

  EXPECT_EQ(0, manager.NumberOfProcesses());
  EXPECT_EQ(0, manager.NumberOfLiveProcesses());
  EXPECT_EQ(0, manager.NumberOfSleepingProcesses());
  uint32_t num = manager.AddProcess(test);
  manager.StartProcess(num);
  EXPECT_GT(num, 0);
  manager.KillProcess(num);
  EXPECT_EQ(1, manager.NumberOfProcesses());
  EXPECT_EQ(0, manager.NumberOfLiveProcesses());
  EXPECT_EQ(0, manager.NumberOfSleepingProcesses());
}


/*TEST(ProcessManagerTest, BEH_StartThreeProcesses) {
  maidsafe::ProcessManager manager;
  maidsafe::Process test;
  ASSERT_TRUE(test.SetProcessName("DUMMYprocess"));
  test.AddArgument("DUMMYprocess");
  test.AddArgument("--runtime");
  test.AddArgument("1");
  test.AddArgument("--nocrash");

  maidsafe::Process test1;
  ASSERT_TRUE(test1.SetProcessName("DUMMYprocess"));
  test1.AddArgument("DUMMYprocess");
  test1.AddArgument("--runtime");
  test1.AddArgument("1");
  test1.AddArgument("--nocrash");

  maidsafe::Process test2;
  ASSERT_TRUE(test2.SetProcessName("DUMMYprocess"));
  test2.AddArgument("DUMMYprocess");
  test2.AddArgument("--runtime");
  test2.AddArgument("1");
  test2.AddArgument("--nocrash");

  EXPECT_EQ(0, manager.NumberOfProcesses());
  EXPECT_EQ(0, manager.NumberOfLiveProcesses());
  EXPECT_EQ(0, manager.NumberOfSleepingProcesses());
  uint32_t num = manager.AddProcess(test);
  uint32_t num1 = manager.AddProcess(test1);
  uint32_t num2 = manager.AddProcess(test2);
  manager.StartProcess(num);
  manager.StartProcess(num1);
  manager.StartProcess(num2);
  EXPECT_GT(num, 0);
  manager.LetProcessDie(num);
  manager.LetProcessDie(num1);
  manager.LetProcessDie(num2);
  EXPECT_EQ(3, manager.NumberOfProcesses());
  EXPECT_EQ(0, manager.NumberOfLiveProcesses());
  EXPECT_EQ(0, manager.NumberOfSleepingProcesses());
}*/

/*TEST(ProcessManagerTest, BEH_StartManyProcesses) {
  maidsafe::ProcessManager manager;
  std::vector<Process> processes_5, processes_10;
  for (int i(0); i < 2; ++i) {
    maidsafe::Process test_5;
    ASSERT_TRUE(test_5.SetProcessName("DUMMYprocess"));
    test_5.AddArgument("DUMMYprocess");
    test_5.AddArgument("--runtime");
    test_5.AddArgument("1");
    test_5.AddArgument("--nocrash");
    processes_5.push_back(test_5);
  }
  for (int i(0); i < 1; ++i) {
    maidsafe::Process test_10;
    ASSERT_TRUE(test_10.SetProcessName("DUMMYprocess"));
    test_10.AddArgument("DUMMYprocess");
    test_10.AddArgument("--runtime");
    test_10.AddArgument("10");
    test_10.AddArgument("--nocrash");
    processes_10.push_back(test_10);
  }
  EXPECT_EQ(0, manager.NumberOfProcesses());
  EXPECT_EQ(0, manager.NumberOfLiveProcesses());
  EXPECT_EQ(0, manager.NumberOfSleepingProcesses());
  std::vector<uint32_t> process_ids_5, process_ids_10;
  for (size_t i(0); i < processes_5.size(); ++i) {
    uint32_t num = manager.AddProcess(processes_5[i]);
    EXPECT_GT(num, 0);
    process_ids_5.push_back(num);
    manager.StartProcess(num);
  }
  for (size_t i(0); processes_10.size(); ++i) {
    uint32_t num = manager.AddProcess(processes_10[i]);
    EXPECT_GT(num, 0);
    manager.StartProcess(num);
    process_ids_10.push_back(num);
  }
  EXPECT_EQ(process_ids_5.size(), manager.NumberOfProcesses());
  EXPECT_EQ(process_ids_5.size(), manager.NumberOfLiveProcesses());

  std::this_thread::sleep_for(std::chrono::seconds(6));

  for (auto it(process_ids_5.begin()); it != process_ids_5.end(); ++it)
    manager.LetProcessDie(*it);

  EXPECT_EQ(process_ids_5.size(), manager.NumberOfProcesses());
  EXPECT_EQ(0, manager.NumberOfLiveProcesses());

  std::this_thread::sleep_for(std::chrono::seconds(6));

  EXPECT_EQ(2, manager.NumberOfProcesses());
  EXPECT_EQ(0, manager.NumberOfLiveProcesses());
  EXPECT_EQ(0, manager.NumberOfSleepingProcesses());
}*/




}  // namespace test

}  // namespace maidsafe

int main(int argc, char **argv) {
  maidsafe::log::FilterMap filter;
  filter["*"] = maidsafe::log::kInfo;
  return ExecuteMain(argc, argv, filter);
}
