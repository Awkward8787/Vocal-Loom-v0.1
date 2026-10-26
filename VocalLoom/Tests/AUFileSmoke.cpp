#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// Exercise the public AU plug-in interface in-process. This does not need the
// system registry, and intentionally does not claim host discovery or auval.
struct DirectUnit {
    AudioComponentPlugInInterface* api = nullptr;
    template<class... Args> OSStatus call(SInt16 selector, Args... args) {
        const auto method = api->Lookup(selector);
        if (!method) return kAudio_UnimplementedError;
        return reinterpret_cast<OSStatus(*)(void*, Args...)>(method)(api, args...);
    }
};
OSStatus directGet(DirectUnit* u, AudioUnitPropertyID p, AudioUnitScope s, AudioUnitElement e, void* data, UInt32* size) {
    return u->call(kAudioUnitGetPropertySelect, p, s, e, data, size);
}
OSStatus directInfo(DirectUnit* u, AudioUnitPropertyID p, AudioUnitScope s, AudioUnitElement e, UInt32* size, Boolean* writable) {
    return u->call(kAudioUnitGetPropertyInfoSelect, p, s, e, size, writable);
}
OSStatus directSet(DirectUnit* u, AudioUnitPropertyID p, AudioUnitScope s, AudioUnitElement e, const void* data, UInt32 size) {
    return u->call(kAudioUnitSetPropertySelect, p, s, e, data, size);
}
OSStatus directParameter(DirectUnit* u, AudioUnitParameterID p, AudioUnitScope s, AudioUnitElement e, AudioUnitParameterValue value, UInt32 offset) {
    return u->call(kAudioUnitSetParameterSelect, p, s, e, value, offset);
}
OSStatus directMidi(DirectUnit* u, UInt32 status, UInt32 data1, UInt32 data2, UInt32 offset) {
    return u->call(kMusicDeviceMIDIEventSelect, status, data1, data2, offset);
}
OSStatus directRender(DirectUnit* u, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* time, UInt32 bus, UInt32 n, AudioBufferList* data) {
    return u->call(kAudioUnitRenderSelect, flags, time, bus, n, data);
}
OSStatus directInit(DirectUnit* u) { return u->call(kAudioUnitInitializeSelect); }
OSStatus directUninit(DirectUnit* u) { return u->call(kAudioUnitUninitializeSelect); }
OSStatus directDispose(DirectUnit* u) { return u->api->Close(u->api); }
void ok(OSStatus status, const char* operation) {
    if (status != noErr) throw std::runtime_error(std::string(operation) + ": " + std::to_string(status));
}
void require(bool result, const char* operation) { if (!result) throw std::runtime_error(operation); }
struct Input { double phase = 0, rate = 48000; bool silent = false; };
OSStatus input(void* context, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames, AudioBufferList* buffers) {
    auto& source = *static_cast<Input*>(context);
    for (UInt32 sample = 0; sample < frames; ++sample) {
        source.phase += 2 * 3.14159265358979323846 * 223.0 / source.rate;
        const float value = source.silent ? 0.f : static_cast<float>(.2 * std::sin(source.phase) + .1 * std::sin(2 * source.phase) + .05 * std::sin(3 * source.phase));
        for (UInt32 c = 0; c < buffers->mNumberBuffers; ++c) {
            auto& buffer = buffers->mBuffers[c];
            if (buffer.mData) for (UInt32 channel = 0; channel < buffer.mNumberChannels; ++channel)
                static_cast<float*>(buffer.mData)[sample * buffer.mNumberChannels + channel] = value;
        }
    }
    return noErr;
}
AudioUnitParameterID parameter(DirectUnit* unit, const char* name) {
    UInt32 bytes = 0;
    ok(directInfo(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &bytes, nullptr), "Parameter list size");
    std::vector<AudioUnitParameterID> ids(bytes / sizeof(AudioUnitParameterID));
    ok(directGet(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &bytes), "Parameter list");
    for (auto id : ids) {
        AudioUnitParameterInfo info {}; UInt32 size = sizeof(info);
        ok(directGet(unit, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &info, &size), "Parameter info");
        char text[256] {};
        if (info.cfNameString) CFStringGetCString(info.cfNameString, text, sizeof(text), kCFStringEncodingUTF8);
        else std::snprintf(text, sizeof(text), "%s", info.name);
        const bool match = std::string(text) == name;
        if ((info.flags & kAudioUnitParameterFlag_CFNameRelease) && info.cfNameString) CFRelease(info.cfNameString);
        if (match) return id;
    }
    throw std::runtime_error(std::string("Missing AU parameter: ") + name);
}
} // namespace

int main(int argc, char** argv) {
    DirectUnit* unit = nullptr;
    try {
        require(argc > 1, "Pass AU bundle path");
        const std::string path(argv[1]);
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.data()), static_cast<CFIndex>(path.size()), true);
        CFBundleRef bundle = CFBundleCreate(nullptr, url); CFRelease(url);
        require(bundle != nullptr, "Create AU bundle reference");
        CFErrorRef error = nullptr;
        require(CFBundleLoadExecutableAndReturnError(bundle, &error), "Load AU binary from disk");
        auto factory = reinterpret_cast<AudioComponentFactoryFunction>(CFBundleGetFunctionPointerForName(bundle, CFSTR("Vocal_LoomAUFactory")));
        require(factory != nullptr, "Find exported AU factory");
        AudioComponentDescription description { kAudioUnitType_MusicEffect, 'VcLm', 'AnJo', 0, 0 };
        DirectUnit direct { factory(&description) };
        require(direct.api != nullptr, "Create AU plug-in interface from binary");
        ok(direct.api->Open(direct.api, nullptr), "Open AU wrapper directly");
        unit = &direct;
        const auto tune = parameter(unit, "Pitch correction"), vocoder = parameter(unit, "Vocoder"), source = parameter(unit, "Carrier note source");
        Input sourceData;
        AURenderCallbackStruct callback { input, &sourceData };
        ok(directSet(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback)), "Set AU audio input callback");
        constexpr UInt32 frames = 128;
        ok(directSet(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &frames, sizeof(frames)), "Set maximum render block");
        for (double rate : { 44100., 48000., 96000. }) {
            sourceData.rate = rate; sourceData.phase = 0; sourceData.silent = false;
            AudioStreamBasicDescription format {};
            format.mSampleRate = rate; format.mFormatID = kAudioFormatLinearPCM;
            format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
            format.mBytesPerPacket = format.mBytesPerFrame = sizeof(float); format.mFramesPerPacket = 1;
            format.mChannelsPerFrame = 2; format.mBitsPerChannel = 32;
            ok(directSet(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format)), "Set input format");
            ok(directSet(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &format, sizeof(format)), "Set output format");
            ok(directInit(unit), "Initialize AU");
            Float64 latency = 0; UInt32 latencySize = sizeof(latency);
            ok(directGet(unit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &latency, &latencySize), "Read AU latency");
            require(std::abs(latency - .012) < .00003, "AU reports nominal delay");
            ok(directParameter(unit, tune, kAudioUnitScope_Global, 0, 0, 0), "Disable tuning via AU automation");
            ok(directParameter(unit, vocoder, kAudioUnitScope_Global, 0, 1, 0), "Enable vocoder via AU automation");
            ok(directParameter(unit, source, kAudioUnitScope_Global, 0, 1, 0), "Select MIDI via AU automation");
            ok(directMidi(unit, 0x90, 57, 100, 17), "Send AU MIDI note with sample offset");
            struct StereoBuffers { UInt32 count = 2; AudioBuffer buffers[2] {}; } output;
            std::array<float, frames> left {}, right {};
            output.buffers[0] = { 1, frames * sizeof(float), left.data() }; output.buffers[1] = { 1, frames * sizeof(float), right.data() };
            double energy = 0;
            for (int block = 0; block < 160; ++block) {
                AudioTimeStamp time {}; time.mFlags = kAudioTimeStampSampleTimeValid; time.mSampleTime = block * frames;
                AudioUnitRenderActionFlags flags = 0;
                ok(directRender(unit, &flags, &time, 0, frames, reinterpret_cast<AudioBufferList*>(&output)), "Render AU audio");
                for (float x : left) { require(std::isfinite(x), "Finite AU output"); energy += x * x; }
            }
            require(energy > .01, "AU MIDI vocoder produces audio with pitch correction off");
            ok(directMidi(unit, 0x80, 57, 0, 0), "Send AU MIDI note off");
            CFPropertyListRef state = nullptr; UInt32 stateSize = sizeof(state);
            ok(directGet(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &stateSize), "Save AU state");
            require(state != nullptr, "AU supplies state");
            ok(directSet(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof(state)), "Restore AU state");
            CFRelease(state);
            ok(directUninit(unit), "Uninitialize AU");
            std::cout << "PASS: AU binary MIDI/render/state at " << rate << " Hz; latency " << latency * 1000 << " ms; energy " << energy << ".\n";
        }
        ok(directDispose(unit), "Dispose AU"); unit = nullptr;
        // Keep the dynamic bundle loaded until process exit.
        std::cout << "PASS: direct AU interface render smoke test. This does not test system discovery and is not Apple's full auval validation.\n";
        return 0;
    } catch (const std::exception& error) {
        if (unit) directDispose(unit);
        std::cerr << "FAIL: " << error.what() << "\n"; return 1;
    }
}
