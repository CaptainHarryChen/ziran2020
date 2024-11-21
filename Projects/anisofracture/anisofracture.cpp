#include "AnisoFractureInit2D.h"
#include "AnisoFractureInit3D.h"
#include <Ziran/CS/Util/CommandLineFlags.h>
#include <Ziran/CS/Util/Debug.h>
#include <Ziran/CS/Util/Filesystem.h>
#include <Ziran/CS/Util/FloatingPointExceptions.h>
#include <Ziran/CS/Util/PluginManager.h>
#include <Ziran/CS/Util/SignalHandler.h>
#include <tbb/tbb.h>

using namespace ZIRAN;

#define T                                                                      \
  double // TODO: must also change a line in solvePhaseNaive in
         // AnisoFractureSimulation.h (VectorXcf vs VectorXcd)
#define dim 3

namespace Python {
double ax = 0, ay = 0, az = 0, bx = 0, by = 0, bz = 0, alpha1 = 0, alpha2 = 0,
       fiberScale = 0, residual = 0.001, percent = 0.1, eta = 1, E = 200,
       rho = 2, tau = 1;
bool useRadial = false, useLongitudinal = false, isotropic = false,
     orthotropic = false, inextensible = false, implicit = false;
} // namespace Python

namespace CmdHelper {
int helper = 0;
}

int main(int argc, char *argv[]) {
  {
    bool displayHelp = false;
    int test_number = -1;                    // Non-lua option.
    bool three_d = false;                    // Non-lua option.
    bool use_double = false;                 // Non-lua option
    int restart = 0;                         // Non-lua option
    bool run_diff_test = false;              // Non-lua option
    double diff_test_perturbation_scale = 1; // Non-lua option
    float phase_field_percentage;
    float phase_field_l0_ratio;

    // Not checking for nan, because when constitutive model returns that,
    // MpmForceBase is skipping them (treating as zeros) FPE::WatchedScope
    // w(FPE::Mask::Overflow | FPE::Mask::DivZero); Unconmment the following to
    // catch division by 0 FPE::WatchedScope w(FPE::Mask::Overflow |
    // FPE::Mask::Invalid | FPE::Mask::DivZero);
    FPE::WatchedScope w(FPE::Mask::Invalid);

    std::string script_file_name;
    StdVector<std::string> inline_strings;
    FLAGS::Register helpflag("--help", "Print help (this message) and exit",
                             displayHelp);

    // Lua command line options
    FLAGS::Register scriptflag("-script", "Lua script to read for initial data",
                               script_file_name);
    FLAGS::Register iflag("-i", "Append string to script", inline_strings);

    // Non-lua command line options
    FLAGS::Register test_number_flag("-test", "Test number (non-lua test)",
                                     test_number);
    FLAGS::Register three_d_flag("--3d", "Dimension is 3(non-lua test)",
                                 three_d);
    FLAGS::Register run_diff_test_flag(
        "--run_diff_test", "Run diff test (non-lua test)", run_diff_test);
    FLAGS::Register diff_test_perturbation_scale_flag(
        "-dtps", "diff_test_perturbation_scale (non-lua test)",
        diff_test_perturbation_scale);
    FLAGS::Register double_flag("--double", "Dimension (non-lua test)",
                                use_double);
    FLAGS::Register restart_flag("-restart", "Restart frame (non-lua test)",
                                 restart);

    FLAGS::Register phase_field_percentage_flag(
        "-p", "phase_field_percentage_flag", phase_field_percentage);
    FLAGS::Register phase_field_l0_ratio_flag(
        "-l0", "phase_field_l0_ratio_flag", phase_field_l0_ratio);

    FLAGS::Register ax_flag("-ax", "ax", Python::ax);
    FLAGS::Register ay_flag("-ay", "ay", Python::ay);
    FLAGS::Register az_flag("-az", "az", Python::az);
    FLAGS::Register bx_flag("-bx", "bx", Python::bx);
    FLAGS::Register by_flag("-by", "by", Python::by);
    FLAGS::Register bz_flag("-bz", "bz", Python::bz);
    FLAGS::Register alpha1_flag("-alpha1", "alpha1", Python::alpha1);
    FLAGS::Register alpha2_flag("-alpha2", "alpha2", Python::alpha2);
    FLAGS::Register fiber_flag("-fiberScale", "fiberScale", Python::fiberScale);
    FLAGS::Register residual_flag("-residual", "residual", Python::residual);
    FLAGS::Register percent_flag("-percent", "percent", Python::percent);
    FLAGS::Register eta_flag("-eta", "eta", Python::eta);
    FLAGS::Register E_flag("-E", "E", Python::E);
    FLAGS::Register rho_flag("-rho", "rho", Python::rho);
    FLAGS::Register tau_flag("-tau", "tau", Python::tau);
    FLAGS::Register useRadial_flag("--useRadial", "useRadial",
                                   Python::useRadial);
    FLAGS::Register useLongitudinal_flag("--useLongitudinal", "useLongitudinal",
                                         Python::useLongitudinal);
    FLAGS::Register isotropic_flag("--isotropic", "useIsotropic",
                                   Python::isotropic);
    FLAGS::Register orthotropic_flag("--orthotropic", "useOrthotropic",
                                     Python::orthotropic);
    FLAGS::Register inextensible_flag("--inextensible", "useInextensible",
                                      Python::inextensible);
    FLAGS::Register implicit_flag("--implicit_damage", "implicit",
                                  Python::implicit);

    FLAGS::Register helper_flag("-helper", "helper", CmdHelper::helper);

    int num_threads = tbb::this_task_arena::max_concurrency();

    FLAGS::Register thread_flag("-t", "Set number of threads", num_threads);

    std::stringstream script;
    PluginManager pm;
    try {
      FLAGS::ParseFlags(argc, argv);
      if (!script_file_name.empty()) {
        FILESYSTEM::readFile(script, script_file_name);
      }
      pm.loadAllPlugins();
    } catch (std::exception &e) {
      std::cerr << e.what() << std::endl;
      FLAGS::PrintUsage(std::cerr);
      return 1;
    }
    if (displayHelp) {
      std::cout << "Usage:\n";
      FLAGS::PrintUsage(std::cout);
      return 0;
    }
    installSignalHandler();
    for (const auto &s : inline_strings) {
      script << s << "\n";
    }

    tbb::global_control control(tbb::global_control::max_allowed_parallelism,
                                num_threads);

    if (script_file_name.empty()) {
      ZIRAN_ASSERT(
          test_number != -1,
          "No lua script loaded. Either load with --script or set --test");
      AnisoFractureSimulation<T, dim> e(Python::implicit);
      if (run_diff_test) {
        e.diff_test = true;
        e.diff_test_perturbation_scale = diff_test_perturbation_scale;
      }
      e.logger = LogWorker::initializeLogging();
#if dim == 2
      AnisoFractureInit2D<T> h(e, test_number);
#else
      AnisoFractureInit3D<T> h(e, test_number);
#endif
      h.percentage = phase_field_percentage;
      h.l0_ratio = phase_field_l0_ratio;
      if (!restart)
        h.start();
      else
        h.restart(restart);
    }
  }
  return 0;
}
