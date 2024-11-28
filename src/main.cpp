#include <iostream>
#include <cmath>
#include <algorithm>
#include <sys/signal.h>
#include <wayland-client.h>
#include <vector>
#include "protocols/hyprland-ctm-control-v1.hpp"
#include "protocols/wayland.hpp"

#include "helpers/Log.hpp"

#include <hyprutils/math/Mat3x3.hpp>
#include <hyprutils/memory/WeakPtr.hpp>
using namespace Hyprutils::Math;
using namespace Hyprutils::Memory;
#define SP CSharedPointer
#define WP CWeakPointer




struct SOutput {
    SP<CCWlOutput> output;
    uint32_t       id = 0;
    void           applyCTM();
};

struct {
    SP<CCWlRegistry>                  pRegistry;
    SP<CCHyprlandCtmControlManagerV1> pCTMMgr;
    wl_display*                       wlDisplay = nullptr;
    std::vector<SP<SOutput>>          outputs;
    bool                              initialized = false;
    Mat3x3                            ctm;
} state;

void sigHandler(int sig) {
    if (state.pCTMMgr) // reset the CTM state...
        state.pCTMMgr.reset();

    Debug::log(NONE, "┣ Exiting on user interrupt\n╹");

    exit(0);
}

void SOutput::applyCTM() {
    auto arr = state.ctm.getMatrix();
    state.pCTMMgr->sendSetCtmForOutput(output->resource(), wl_fixed_from_double(arr[0]), wl_fixed_from_double(arr[1]), wl_fixed_from_double(arr[2]), wl_fixed_from_double(arr[3]),
                                       wl_fixed_from_double(arr[4]), wl_fixed_from_double(arr[5]), wl_fixed_from_double(arr[6]), wl_fixed_from_double(arr[7]),
                                       wl_fixed_from_double(arr[8]));
}

static void commitCTMs() {
    state.pCTMMgr->sendCommit();
}

static void printHelp() {
    Debug::log(NONE, "┣ --help              -h  →  Print this info");
    Debug::log(NONE, "╹");
}

int main(int argc, char** argv, char** envp) {
    Debug::log(NONE, "┏ hyprsunset v{} ━━╸\n┃", HYPRSUNSET_VERSION);

    for (int i = 1; i < argc; ++i) {
        if (argv[i] == std::string{"-h"} || argv[i] == std::string{"--help"}) {
            printHelp();
            return 0;
        } else {
            Debug::log(NONE, "✖ Argument not recognized: {}", argv[i]);
            printHelp();
            return 1;
        }
    }



    // calculate the matrix
    state.ctm = std::array<float, 9>{0.6, 0, 0, 0.6, 0, 0, 0, 0, 0.6};

    Debug::log(NONE, "┣ Calculated the CTM to be {}\n┃", state.ctm.toString());

    // connect to the wayland server
    if (const auto SERVER = getenv("XDG_CURRENT_DESKTOP"); SERVER)
        Debug::log(NONE, "┣ Running on {}", SERVER);

    state.wlDisplay = wl_display_connect(nullptr);

    if (!state.wlDisplay) {
        Debug::log(NONE, "✖ Couldn't connect to a wayland compositor");
        return 1;
    }

    signal(SIGTERM, sigHandler);

    state.pRegistry = makeShared<CCWlRegistry>((wl_proxy*)wl_display_get_registry(state.wlDisplay));
    state.pRegistry->setGlobal([](CCWlRegistry* r, uint32_t name, const char* interface, uint32_t version) {
        const std::string IFACE = interface;

        if (IFACE == hyprland_ctm_control_manager_v1_interface.name) {
            Debug::log(NONE, "┣ Found hyprland-ctm-control-v1 supported with version {}, binding to v1", version);
            state.pCTMMgr = makeShared<CCHyprlandCtmControlManagerV1>(
                (wl_proxy*)wl_registry_bind((wl_registry*)state.pRegistry->resource(), name, &hyprland_ctm_control_manager_v1_interface, 1));
        } else if (IFACE == wl_output_interface.name) {

            if (std::find_if(state.outputs.begin(), state.outputs.end(), [name](const auto& el) { return el->id == name; }) != state.outputs.end())
                return;

            Debug::log(NONE, "┣ Found new output with ID {}, binding", name);
            auto o = state.outputs.emplace_back(
                makeShared<SOutput>(makeShared<CCWlOutput>((wl_proxy*)wl_registry_bind((wl_registry*)state.pRegistry->resource(), name, &wl_output_interface, 1)), name));

            if (state.initialized) {
                Debug::log(NONE, "┣ already initialized, applying CTM instantly", name);
                o->applyCTM();
                commitCTMs();
            }
        }
    });

    wl_display_roundtrip(state.wlDisplay);

    if (!state.pCTMMgr) {
        Debug::log(NONE, "✖ Compositor doesn't support hyprland-ctm-control-v1, are you running on Hyprland?");
        return 1;
    }

    Debug::log(NONE, "┣ Found {} outputs, applying CTMs", state.outputs.size());

    for (auto& o : state.outputs) {
        o->applyCTM();
    }

    commitCTMs();

    state.initialized = true;

    while (wl_display_dispatch(state.wlDisplay) != -1) {
        ;
    }

    return 0;
}
