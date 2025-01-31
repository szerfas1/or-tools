// Copyright 2010-2018 Google LLC
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This is the skeleton for the official flatzinc interpreter.  Much
// of the funcionalities are fixed (name of parameters, format of the
// input): see http://www.minizinc.org/downloads/doc-1.6/flatzinc-spec.pdf

#if defined(__GNUC__)  // Linux or Mac OS X.
#include <signal.h>
#endif  // __GNUC__

#include <csignal>
#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "ortools/base/commandlineflags.h"
#include "ortools/base/integral_types.h"
#include "ortools/base/logging.h"
#include "ortools/base/threadpool.h"
#include "ortools/base/timer.h"
#include "ortools/flatzinc/cp_model_fz_solver.h"
#include "ortools/flatzinc/logging.h"
#include "ortools/flatzinc/model.h"
#include "ortools/flatzinc/parser.h"
#include "ortools/flatzinc/presolve.h"

ABSL_FLAG(double, time_limit, 0, "time limit in seconds.");
ABSL_FLAG(bool, all_solutions, false, "Search for all solutions.");
ABSL_FLAG(int, num_solutions, 0,
          "Maximum number of solution to search for, 0 means unspecified.");
ABSL_FLAG(bool, free_search, false,
          "If false, the solver must follow the defined search."
          "If true, other search are allowed.");
ABSL_FLAG(int, threads, 0, "Number of threads the solver will use.");
ABSL_FLAG(bool, presolve, true, "Presolve the model to simplify it.");
ABSL_FLAG(bool, statistics, false, "Print solver statistics after search.");
ABSL_FLAG(bool, read_from_stdin, false,
          "Read the FlatZinc from stdin, not from a file.");
ABSL_FLAG(int, fz_seed, 0, "Random seed");
ABSL_FLAG(std::string, fz_model_name, "stdin",
          "Define problem name when reading from stdin.");
ABSL_FLAG(std::string, params, "", "SatParameters as a text proto.");

ABSL_DECLARE_FLAG(bool, log_prefix);

namespace operations_research {
namespace fz {

std::vector<char*> FixAndParseParameters(int* argc, char*** argv) {
  absl::SetFlag(&FLAGS_log_prefix, false);

  char all_param[] = "--all_solutions";
  char free_param[] = "--free_search";
  char threads_param[] = "--threads";
  char solutions_param[] = "--num_solutions";
  char logging_param[] = "--fz_logging";
  char statistics_param[] = "--statistics";
  char seed_param[] = "--fz_seed";
  char verbose_param[] = "--fz_verbose";
  char debug_param[] = "--fz_debug";
  char time_param[] = "--time_limit";
  bool use_time_param = false;
  for (int i = 1; i < *argc; ++i) {
    if (strcmp((*argv)[i], "-a") == 0) {
      (*argv)[i] = all_param;
    }
    if (strcmp((*argv)[i], "-f") == 0) {
      (*argv)[i] = free_param;
    }
    if (strcmp((*argv)[i], "-p") == 0) {
      (*argv)[i] = threads_param;
    }
    if (strcmp((*argv)[i], "-n") == 0) {
      (*argv)[i] = solutions_param;
    }
    if (strcmp((*argv)[i], "-l") == 0) {
      (*argv)[i] = logging_param;
    }
    if (strcmp((*argv)[i], "-s") == 0) {
      (*argv)[i] = statistics_param;
    }
    if (strcmp((*argv)[i], "-r") == 0) {
      (*argv)[i] = seed_param;
    }
    if (strcmp((*argv)[i], "-v") == 0) {
      (*argv)[i] = verbose_param;
    }
    if (strcmp((*argv)[i], "-d") == 0) {
      (*argv)[i] = debug_param;
    }
    if (strcmp((*argv)[i], "-t") == 0) {
      (*argv)[i] = time_param;
      use_time_param = true;
    }
  }
  const char kUsage[] =
      "Usage: see flags.\nThis program parses and solve a flatzinc problem.";

  absl::SetProgramUsageMessage(kUsage);
  const std::vector<char*> residual_flags =
      absl::ParseCommandLine(*argc, *argv);
  google::InitGoogleLogging((*argv)[0]);

  // Fix time limit if -t was used.
  if (use_time_param) {
    absl::SetFlag(&FLAGS_time_limit, absl::GetFlag(FLAGS_time_limit) / 1000.0);
  }
  return residual_flags;
}

Model ParseFlatzincModel(const std::string& input, bool input_is_filename) {
  WallTimer timer;
  timer.Start();
  // Read model.
  std::string problem_name =
      input_is_filename ? input : absl::GetFlag(FLAGS_fz_model_name);
  if (input_is_filename || absl::EndsWith(problem_name, ".fzn")) {
    CHECK(absl::EndsWith(problem_name, ".fzn"))
        << "Unrecognized flatzinc file: `" << problem_name << "'";
    problem_name.resize(problem_name.size() - 4);
    const size_t found = problem_name.find_last_of("/\\");
    if (found != std::string::npos) {
      problem_name = problem_name.substr(found + 1);
    }
  }
  Model model(problem_name);
  if (input_is_filename) {
    CHECK(ParseFlatzincFile(input, &model));
  } else {
    CHECK(ParseFlatzincString(input, &model));
  }

  FZLOG << "File " << (input_is_filename ? input : "stdin") << " parsed in "
        << timer.GetInMs() << " ms" << FZENDL;

  // Presolve the model.
  Presolver presolve;
  FZLOG << "Presolve model" << FZENDL;
  timer.Reset();
  timer.Start();
  presolve.Run(&model);
  FZLOG << "  - done in " << timer.GetInMs() << " ms" << FZENDL;

  // Print statistics.
  ModelStatistics stats(model);
  stats.BuildStatistics();
  stats.PrintStatistics();
  return model;
}

}  // namespace fz
}  // namespace operations_research

int main(int argc, char** argv) {
  // Flatzinc specifications require single dash parameters (-a, -f, -p).
  // We need to fix parameters before parsing them.
  const std::vector<char*> residual_flags =
      operations_research::fz::FixAndParseParameters(&argc, &argv);
  // We allow piping model through stdin.
  std::string input;
  if (absl::GetFlag(FLAGS_read_from_stdin)) {
    std::string currentLine;
    while (std::getline(std::cin, currentLine)) {
      input.append(currentLine);
    }
  } else {
    if (residual_flags.empty()) {
      LOG(ERROR) << "Usage: " << argv[0] << " <file>";
      return EXIT_FAILURE;
    }
    input = residual_flags.back();
  }

  operations_research::fz::Model model =
      operations_research::fz::ParseFlatzincModel(
          input, !absl::GetFlag(FLAGS_read_from_stdin));
  operations_research::fz::FlatzincSatParameters parameters;
  parameters.display_all_solutions = absl::GetFlag(FLAGS_all_solutions);
  parameters.use_free_search = absl::GetFlag(FLAGS_free_search);
  parameters.verbose_logging = absl::GetFlag(FLAGS_fz_logging);
  if (absl::GetFlag(FLAGS_num_solutions) == 0) {
    absl::SetFlag(&FLAGS_num_solutions,
                  absl::GetFlag(FLAGS_all_solutions) ? kint32max : 1);
  }
  parameters.max_number_of_solutions = absl::GetFlag(FLAGS_num_solutions);
  parameters.random_seed = absl::GetFlag(FLAGS_fz_seed);
  parameters.display_statistics = absl::GetFlag(FLAGS_statistics);
  parameters.number_of_threads = absl::GetFlag(FLAGS_threads);
  parameters.max_time_in_seconds = absl::GetFlag(FLAGS_time_limit);

  operations_research::sat::SolveFzWithCpModelProto(
      model, parameters, absl::GetFlag(FLAGS_params));
  return EXIT_SUCCESS;
}
