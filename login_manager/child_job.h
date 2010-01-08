// Copyright (c) 2009-2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_CHILD_JOB_H_
#define LOGIN_MANAGER_CHILD_JOB_H_

#include <gtest/gtest.h>
#include <unistd.h>

#include <base/basictypes.h>

class CommandLine;

namespace login_manager {
// SessionManager takes a ChildJob object, forks and then calls the ChildJob's
// Run() method.  I've created this interface so that I can create mocks for
// unittesting SessionManager.
class ChildJob {
 public:
  ChildJob() {}
  virtual ~ChildJob() {}

  // Wraps up all the logic of what the job is meant to do.
  virtual void Run() = 0;

  // If the ChildJob contains a toggleable piece of state, toggle it.
  virtual void Toggle() = 0;
};

class SetUidExecJob : public ChildJob {
 public:
  SetUidExecJob(const CommandLine* command_line,
                const bool add_flag,
                const std::string pipe_name)
      : argv_(NULL),
        num_args_passed_in_(0),
        pipe_arg_(kSessionManagerPipe),
        desired_uid_(0),
        include_login_flag_(add_flag),
        set_uid_(false) {
    PopulateArgv(command_line);
    pipe_arg_.append(pipe_name);
  }
  virtual ~SetUidExecJob();

  // The flag to pass to chrome to tell it to behave as the login manager.
  static const char kLoginManagerFlag[];
  // The flag to pass to chrome to tell it how to talk to the session manager.
  static const char kSessionManagerPipe[];

  // Potential exit codes for Run().
  static const int kCantSetuid;
  static const int kCantSetgid;
  static const int kCantSetgroups;
  static const int kCantExec;

  void Run();
  void Toggle() { include_login_flag_ = !include_login_flag_; }

  void set_uid(uid_t uid) {
    desired_uid_ = uid;
    set_uid_ = true;
  }

 protected:
  std::vector<std::string> ExtractArgvForTest();

  // Pulls all loose args from |command_line|, converts them to ASCII, and
  // puts them into an array that's ready to be used by exec().
  void PopulateArgv(const CommandLine* command_line);
  void UseLoginManagerFlagIfNeeded();

  // If the caller has provided a UID with SetUid(), this method will:
  // 1) try to setgid to that uid
  // 2) try to setgroups to that uid
  // 3) try to setuid to that uid
  //
  // Returns 0 on success, the appropriate exit code (defined above) if a
  // call fails.
  int SetIDs();

 private:
  char const* *argv_;
  uint32 num_args_passed_in_;

  std::string pipe_arg_;

  uid_t desired_uid_;
  bool include_login_flag_;  // This class' piece of toggleable state.
  bool set_uid_;

  FRIEND_TEST(SetUidExecJobTest, FlagAppendTest);
  FRIEND_TEST(SetUidExecJobTest, NoFlagAppendTest);
  FRIEND_TEST(SetUidExecJobTest, PopulateArgvTest);
  DISALLOW_COPY_AND_ASSIGN(SetUidExecJob);

};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_CHILD_JOB_H_
