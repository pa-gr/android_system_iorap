// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "common/debug.h"
#include "compiler/compiler.h"
#include "maintenance/controller.h"
#include "inode2filename/inode_resolver.h"

#include <android-base/parseint.h>
#include <android-base/logging.h>

#include <iostream>
#include <optional>

#if defined(IORAP_MAINTENANCE_MAIN)

namespace iorap::maintenance {

void Usage(char** argv) {
  std::cerr << "Usage: " << argv[0] << " <path of sqlite db>" << std::endl;
  std::cerr << "" << std::endl;
  std::cerr << "  Compile the perfetto trace for an package and activity." << std::endl;
  std::cerr << "  The info of perfetto trace is stored in the sqlite db." << std::endl;
  std::cerr << "" << std::endl;
  std::cerr << "  Optional flags:" << std::endl;
  std::cerr << "    --package $,-p $           Package name." << std::endl;
  std::cerr << "    --activity $,-a $          Activity name." << std::endl;
  std::cerr << "    --inode-textcache $,-it $  Resolve inode->filename from textcache." << std::endl;
  std::cerr << "    --help,-h                  Print this Usage." << std::endl;
  std::cerr << "    --force,-f                 Force compilation, which replace the existing compiled trace ." << std::endl;
  std::cerr << "    --verbose,-v               Set verbosity (default off)." << std::endl;
  std::cerr << "    --output-text,-ot          Output ascii text instead of protobuf (default off)." << std::endl;
  exit(1);
}

int Main(int argc, char** argv){
  android::base::InitLogging(argv);
  android::base::SetLogger(android::base::StderrLogger);

  if (argc == 1) {
    // Need at least 1 input file to do anything.
    Usage(argv);
  }

  std::vector<std::string> arg_input_filenames;
  std::optional<std::string> arg_package;
  std::optional<std::string> arg_activity;
  std::optional<std::string> arg_inode_textcache;
  bool enable_force = false;
  bool enable_verbose = false;
  bool arg_output_text = false;

  for (int arg = 1; arg < argc; ++arg) {
    std::string argstr = argv[arg];
    bool has_arg_next = (arg+1)<argc;
    std::string arg_next = has_arg_next ? argv[arg+1] : "";

    if (argstr == "--help" || argstr == "-h") {
      Usage(argv);
    } else if (argstr == "--package" || argstr == "-p") {
      if (!has_arg_next) {
        std::cerr << "Missing --package <value>" << std::endl;
        return 1;
      }
      arg_package = arg_next;
      ++arg;
    } else if (argstr == "--activity" || argstr == "-a") {
      if (!has_arg_next) {
        std::cerr << "Missing --activity <value>" << std::endl;
        return 1;
      }
      arg_activity = arg_next;
      ++arg;
    } else if (argstr == "--inode-textcache" || argstr == "-it") {
      if (!has_arg_next) {
        std::cerr << "Missing --inode-textcache <value>" << std::endl;
        return 1;
      }
      arg_inode_textcache = arg_next;
      ++arg;
    } else if (argstr == "--verbose" || argstr == "-v") {
      enable_verbose = true;
    } else if (argstr == "--force" || argstr == "-f") {
      enable_force = true;
    } else if (argstr == "--output-text" || argstr == "-ot") {
      arg_output_text = true;
    }else {
      arg_input_filenames.push_back(argstr);
    }
  }

  if (arg_input_filenames.empty()) {
    LOG(ERROR) << "Missing filename to a sqlite database.";
    Usage(argv);
  } else if (arg_input_filenames.size() > 1) {
    LOG(ERROR) << "More than one filename to a sqlite database.";
    Usage(argv);
  }

  std::string db = arg_input_filenames[0];

  if (enable_verbose) {
    android::base::SetMinimumLogSeverity(android::base::VERBOSE);

    LOG(VERBOSE) << "Verbose check";
    LOG(VERBOSE) << "Debug check: " << ::iorap::kIsDebugBuild;
  } else {
    android::base::SetMinimumLogSeverity(android::base::DEBUG);
  }

  auto system_call = std::make_unique<SystemCallImpl>();

  inode2filename::InodeResolverDependencies ir_dependencies;

  // Passed from command-line.
  if (arg_inode_textcache) {
    ir_dependencies.data_source = inode2filename::DataSourceKind::kTextCache;
    ir_dependencies.text_cache_filename = arg_inode_textcache;
    ir_dependencies.verify = inode2filename::VerifyKind::kNone;  // required for determinism.
  } else {
    ir_dependencies.data_source = inode2filename::DataSourceKind::kDiskScan;
    LOG(WARNING) << "--inode-textcache unspecified. "
                 << "Inodes will be resolved by scanning the disk, which makes compilation "
                 << "non-deterministic.";
  }
  // TODO: add command line.
  ir_dependencies.root_directories.push_back("/system");
  ir_dependencies.root_directories.push_back("/apex");
  ir_dependencies.root_directories.push_back("/data");
  ir_dependencies.root_directories.push_back("/vendor");
  ir_dependencies.root_directories.push_back("/product");
  ir_dependencies.root_directories.push_back("/metadata");
  // Hardcoded.
  ir_dependencies.process_mode = inode2filename::ProcessMode::kInProcessDirect;  // TODO: others.
  ir_dependencies.system_call = /*borrowed*/system_call.get();

  int ret_code = 0;
  if (arg_package && arg_activity) {
    ret_code = !Compile(std::move(db),
                        std::move(*arg_package),
                        std::move(*arg_activity),
                        !arg_output_text,
                        std::move(ir_dependencies),
                        enable_force);
  } else if (arg_package) {
    ret_code = !Compile(std::move(db),
                        std::move(*arg_package),
                        !arg_output_text,
                        std::move(ir_dependencies),
                        enable_force);
  } else {
    ret_code = !Compile(std::move(db),
                        !arg_output_text,
                        std::move(ir_dependencies),
                        enable_force);
  }
  return ret_code;
}

} // iorap::maintenance

int main(int argc, char** argv) {
  return ::iorap::maintenance::Main(argc, argv);
}


#endif  // IORAP_MAINTENANCE_MAIN
