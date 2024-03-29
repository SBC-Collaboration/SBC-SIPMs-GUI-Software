// C++ STD includes
#include <chrono>
#include <thread>
#include <stack>

// C++ 3rd party includes
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <concurrentqueue.h>

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/rotating_file_sink.h"

// My includes
// Only have 1 include at a time!
#ifdef USE_VULKAN
#include "sbcqueens-gui/rendering_wrappers/glfw_vulkan_wrapper.h"
#else
#include "sbcqueens-gui/rendering_wrappers/glfw_opengl3_wrapper.h"
#endif

// my includes
#include "sbcqueens-gui/imgui_helpers.hpp"

#include "sbcqueens-gui/multithreading_helpers/Pipe.hpp"

#include "sbcqueens-gui/hardware_helpers/TeensyControllerManager.hpp"
#include "sbcqueens-gui/hardware_helpers/SiPMAcquisitionManager.hpp"
#include "sbcqueens-gui/hardware_helpers/SlowDAQManager.hpp"
#include "sbcqueens-gui/hardware_helpers/GUIManager.hpp"

// PipeInterface -> The type of Pipe to use and Traits are an internal
// memory requirement of the Pipe.
template <template <typename, typename> class PipeInterface,
          typename Traits,
          typename TokenType>
struct Pipes {
    using SiPMPipe_type = SBCQueens::SiPMAcquisitionPipe<PipeInterface, Traits, TokenType>;
    using TeensyPipe_type = SBCQueens::TeensyControllerPipe<PipeInterface, Traits, TokenType>;
    using SlowDAQ_type = SBCQueens::SlowDAQPipe<PipeInterface, Traits, TokenType>;

    SiPMPipe_type SiPMPipe;
    TeensyPipe_type TeensyPipe;
    SlowDAQ_type SlowDAQPipe;
};

using SiPMCharacterizationPipes = Pipes<moodycamel::ConcurrentQueue,
    moodycamel::ConcurrentQueueDefaultTraits, moodycamel::ProducerToken>;

int main() {
    try{
        spdlog::init_thread_pool(8192, 1);
        auto stdout_sink
            = std::make_shared<spdlog::sinks::stdout_color_sink_mt >();
        auto rotating_sink
            = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                "log.txt", 1024*1024*10, 3);
        std::vector<spdlog::sink_ptr> sinks {stdout_sink, rotating_sink};
        auto logger = std::make_shared<spdlog::async_logger>("log",
            sinks.begin(), sinks.end(), spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);
        spdlog::register_logger(logger);

    } catch(const spdlog::spdlog_ex& ex) {
      std::cout << "Log initialization failed: " << ex.what() << std::endl;
      throw "Log initialization failed";
    }

    auto logger = spdlog::get("log");
    logger->info("Created logger. Let's go!");

    SiPMCharacterizationPipes pipes;
    moodycamel::ConcurrentQueue<int> q;
    std::vector<std::unique_ptr<SBCQueens::ThreadManager<SiPMCharacterizationPipes>>> _threads;
    logger->info("Created pipes that communicate between threads.");

    logger->info("Creating GUIMangaer using wrapper: ");
#ifdef USE_VULKAN
    _threads.push_back(SBCQueens::make_gui_manager(pipes,
        ImGUIWrappers::main_glfw_vulkan_wrapper));
    logger->info("VULKAN + GLFW backend.");
#else
    _threads.push_back(SBCQueens::make_gui_manager(pipes,
                                                   ImGUIWrappers::main_glfw_open3gl_wrapper));
    logger->info("OpenGL + GLFW backend.");
#endif
    logger->info("Creating Teensy Controller Manager.");
    _threads.push_back(SBCQueens::make_teensy_controller_manager(pipes));
    logger->info("Creating Slow DAQ Controller Manager.");
    _threads.push_back(SBCQueens::make_slow_daq_manager(pipes));

    // The lambdas we are passing are the functions
    // to read and write to the queue
    logger->info("Starting all manager threads.");
    for(auto& thread : _threads) {
        std::jthread t([thread = std::move(thread)]() {
            (*thread)();
        });

        t.detach();
    }

    logger->info("Creating SiPM Controller Manager.");
    auto sipm_thread = SBCQueens::make_sipmacquisition_manager(pipes);
    std::jthread t([thread = std::move(sipm_thread)]() {
        (*thread)();
    });

    t.join();
    // This function just holds the rendering framework we are using
    // all of them found under rendering_wrappers
    // We wrapping it under a lambda so we can pass it to a thread
    // because we do not care about its returning vallue
    // This is our GUI function which actually holds all of our buttons
    // labels, inputs, graphs and ect


    // logger->info("Closing spawning thread. See you on the other side!");

    return 0;
}
