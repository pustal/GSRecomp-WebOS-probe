/**
 * @file launcher.cpp
 * @brief webOS start-up for the GSRecomp probe: finds the player's ROM, prepares
 *        a writable data folder and starts the Golden Sun engine program.
 *
 * The IPK's `main` is this launcher (bin/gsr-launcher). It:
 *   1. makes `<app>/data` (or $GSR_DATA_DIR) the data folder and sends
 *      stdout/stderr to stdout.log / stderr.log there;
 *   2. looks for the ROM: `<data>/rom.gba`, then a folder named `GoldenSun` on
 *      a USB drive (any .gba in it), then RetroArch-style `roms/` folders
 *      (a .gba whose name contains "golden sun"); the engine checks the SHA-1;
 *   3. writes remote-friendly bindings (keybinds.ini, config.ini) on the
 *      first run: OK = A, Back = B, Blue = Start, Yellow = Select,
 *      CH+/CH- = L/R, Red = the engine's menu;
 *   4. raises the stack limit (generated code nests deeply where the compiler
 *      could not turn a guest jump into a host jump);
 *   5. on the first start (no benchmark.txt yet), runs the engine's headless
 *      timing pass twice, natively and with every instruction interpreted
 *      (`--frames 1800`: 30 s of game time, uncapped; the screen stays black),
 *      and writes the frame rates to benchmark.txt; delete it to measure again;
 *   6. execs bin/GoldenSunRecomp with --rom, --user-directory and --save, plus
 *      the engine's cost probe so frame timing reaches the log.
 */
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{

/** Stack for the engine's main thread (the Windows build links 16 MB; leave room). */
constexpr rlim_t kStackBytes = 256ull * 1024ull * 1024ull;
/** Frames of the benchmark pass: 30 s of game time at the GBA's 59.73 Hz. */
constexpr int kBenchmarkFrames = 1800;
/** Where webOS mounts USB drives: /tmp/usb/<device>/<partition>. */
constexpr const char *kUsbRoot = "/tmp/usb";

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool IsGba(const fs::path &path)
{
    std::error_code ec;
    return Lower(path.extension().string()) == ".gba" && fs::is_regular_file(path, ec);
}

/** Folder holding this executable. */
fs::path ExecutableFolder()
{
    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : self.parent_path();
}

/** The data folder, created; stdout/stderr go to log files inside it. */
fs::path PrepareDataFolder(const fs::path &appFolder)
{
    const char *overrideDir = std::getenv("GSR_DATA_DIR");
    const fs::path data = overrideDir && *overrideDir ? fs::path(overrideDir) : appFolder / "data";
    std::error_code ec;
    fs::create_directories(data / "saves", ec);
    if (!std::getenv("GSR_LOG_TO_CONSOLE")) {
        std::freopen((data / "stdout.log").c_str(), "w", stdout);
        std::freopen((data / "stderr.log").c_str(), "w", stderr);
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
    }
    return data;
}

/** Every directory directly below @p dir (no error if it is missing). */
std::vector<fs::path> Subfolders(const fs::path &dir)
{
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (it->is_directory(ec))
            out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

/** The first .gba below @p dir (up to @p depth levels) accepted by @p accept. */
template <typename Accept> fs::path FindGba(const fs::path &dir, int depth, Accept accept)
{
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it.depth() >= depth)
            it.disable_recursion_pending();
        if (IsGba(it->path()) && accept(it->path()))
            return it->path();
    }
    return fs::path();
}

/** The ROM to play, or empty. See the file comment for the order. */
fs::path FindRom(const fs::path &data)
{
    if (IsGba(data / "rom.gba"))
        return data / "rom.gba";
    for (const fs::path &device : Subfolders(kUsbRoot)) {
        for (const fs::path &drive : Subfolders(device)) {
            for (const fs::path &top : Subfolders(drive)) {
                const std::string name = Lower(top.filename().string());
                if (name == "goldensun" || name == "golden sun") {
                    const fs::path rom = FindGba(top, 1, [](const fs::path &) { return true; });
                    if (!rom.empty())
                        return rom;
                }
                if (name == "roms") {
                    const fs::path rom = FindGba(top, 3, [](const fs::path &p) {
                        const std::string file = Lower(p.filename().string());
                        return file.find("golden sun") != std::string::npos &&
                               file.find("lost age") == std::string::npos;
                    });
                    if (!rom.empty())
                        return rom;
                }
            }
        }
    }
    return fs::path();
}

/** Writes @p text to @p path unless the file already exists (the player may have changed it). */
void WriteIfMissing(const fs::path &path, const char *text)
{
    std::error_code ec;
    if (fs::exists(path, ec))
        return;
    std::ofstream(path) << text;
}

/** Remote-friendly bindings; key names are SDL-webOS's scancode names. */
void SeedBindings(const fs::path &data)
{
    WriteIfMissing(data / "keybinds.ini",
                   "; Written by the GSRecomp webOS probe on first start.\n"
                   "; Remote: OK = A, Back = B, Blue = Start, Yellow = Select, CH+/CH- = L/R.\n"
                   "[player1]\n"
                   "a=Return\n"
                   "b=Back\n"
                   "select=Yellow\n"
                   "start=Blue\n"
                   "right=Right\n"
                   "left=Left\n"
                   "up=Up\n"
                   "down=Down\n"
                   "r=ChannelDown\n"
                   "l=ChannelUp\n");
    WriteIfMissing(data / "config.ini",
                   "; Written by the GSRecomp webOS probe on first start.\n"
                   "[Video]\n"
                   "Fullscreen=true\n"
                   "ShowFPS=true\n"
                   "VSync=true\n"
                   "[KeyMap]\n"
                   "Menu=Red\n");
}

/** Seconds on a monotonic clock. */
double Now()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

/**
 * Runs @p args (argv[0] is the program) to completion, its output appended to
 * @p log, with @p interpret set GBARECOMP_FORCE_INTERP=1.
 * @return Wall-clock seconds, or a negative number if it failed.
 */
double RunTimed(const std::vector<std::string> &args, const fs::path &log, bool interpret)
{
    const double started = Now();
    const pid_t pid = fork();
    if (pid < 0)
        return -1.0;
    if (pid == 0) {
        const int fd = open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        if (interpret)
            setenv("GBARECOMP_FORCE_INTERP", "1", 1);
        std::vector<char *> argvOut;
        for (const std::string &a : args)
            argvOut.push_back(const_cast<char *>(a.c_str()));
        argvOut.push_back(nullptr);
        execv(argvOut[0], argvOut.data());
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1.0;
    return Now() - started;
}

/** The first-start benchmark (see the file comment). */
void Benchmark(const std::string &engine, const fs::path &rom, const fs::path &data)
{
    const fs::path result = data / "benchmark.txt";
    std::error_code ec;
    if (fs::exists(result, ec))
        return;
    const fs::path log = data / "benchmark.log";
    fs::remove(log, ec);
    const std::string frames = std::to_string(kBenchmarkFrames);
    std::ofstream out(result);
    out << "GSRecomp webOS probe benchmark: " << kBenchmarkFrames
        << " frames (30 s of game time) from power-on, uncapped, no display.\n"
        << "60 fps or more means the game can keep full speed.\n";
    for (const bool interpret : { false, true }) {
        const std::vector<std::string> args = { engine, "--rom", rom.string(), "--user-directory", data.string(),
                                                "--save", (data / "saves" / "benchmark.sav").string(),
                                                "--frames", frames, "--dump-png",
                                                (data / (interpret ? "bench-interpreted.png" : "bench-native.png")).string() };
        const double seconds = RunTimed(args, log, interpret);
        const char *mode = interpret ? "interpreted" : "native";
        if (seconds > 0) {
            out << mode << ": " << seconds << " s, " << (kBenchmarkFrames / seconds) << " fps\n";
            std::printf("[probe] benchmark %s: %.1f s, %.1f fps\n", mode, seconds, kBenchmarkFrames / seconds);
        } else {
            out << mode << ": failed (see benchmark.log)\n";
            std::printf("[probe] benchmark %s failed\n", mode);
        }
        out.flush();
    }
    fs::remove(data / "saves" / "benchmark.sav", ec);
}

} // namespace

int main(int argc, char **argv)
{
    const fs::path bin  = ExecutableFolder();
    const fs::path data = PrepareDataFolder(bin.parent_path());
    std::printf("[probe] data folder %s\n", data.c_str());

    const fs::path rom = FindRom(data);
    if (rom.empty()) {
        std::printf("[probe] no ROM: put it at %s/rom.gba or in a GoldenSun folder on a USB drive\n", data.c_str());
        return 1;
    }
    std::printf("[probe] ROM %s\n", rom.c_str());
    SeedBindings(data);

    rlimit stack{};
    if (getrlimit(RLIMIT_STACK, &stack) == 0) {
        const rlim_t wanted = stack.rlim_max == RLIM_INFINITY ? kStackBytes : std::min(kStackBytes, stack.rlim_max);
        stack.rlim_cur = std::max(stack.rlim_cur, wanted);
        if (setrlimit(RLIMIT_STACK, &stack) != 0)
            std::printf("[probe] could not raise the stack limit: %s\n", std::strerror(errno));
    }

    // Deliver Back to the game instead of webOS (SDL reads hints from the environment).
    setenv("SDL_WEBOS_ACCESS_POLICY_KEYS_BACK", "true", 1);
    setenv("GBARECOMP_CONTROLS_DIR", data.c_str(), 1);
    if (!std::getenv("GBARECOMP_COST_PROBE"))
        setenv("GBARECOMP_COST_PROBE", "1", 1);
    if (chdir(data.c_str()) != 0)
        std::printf("[probe] could not enter %s: %s\n", data.c_str(), std::strerror(errno));

    const std::string engine = (bin / "GoldenSunRecomp").string();
    Benchmark(engine, rom, data);
    const std::string userDir = data.string();
    const std::string save = (data / "saves" / "goldensun.sav").string();
    std::vector<std::string> args = { engine, "--rom", rom.string(), "--user-directory", userDir, "--save", save };
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]); // extra options pass through
    std::vector<char *> argvOut;
    for (std::string &a : args)
        argvOut.push_back(a.data());
    argvOut.push_back(nullptr);
    std::fflush(stdout);
    execv(engine.c_str(), argvOut.data());
    std::printf("[probe] could not start %s: %s\n", engine.c_str(), std::strerror(errno));
    return 1;
}
