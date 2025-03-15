#include <SDL2/SDL.h>
#include <cstdint>
#include <vector>
#include <cstring>
#include <iostream>

// Constants for Wii memory sizes
const uint32_t MEM1_SIZE = 24 * 1024 * 1024;  // 24 MB
const uint32_t MEM2_SIZE = 64 * 1024 * 1024;  // 64 MB

// Memory-mapped I/O register addresses (for emulator integration)
const uint32_t REG_VIDEO_BG_COLOR = 0x0D000000;  // Background color register (example)
const uint32_t REG_INPUT_STATE    = 0x0D000004;  // Input state register (buttons)
const uint32_t REG_AUDIO_FREQ     = 0x0D000008;  // Audio frequency register (tone control)

class Video;  // forward declarations (defined later)
class Audio;
class Input;

class Memory {
public:
    Memory() {
        // Allocate and initialize MEM1 and MEM2
        mem1.resize(MEM1_SIZE);
        mem2.resize(MEM2_SIZE);
        std::memset(mem1.data(), 0, MEM1_SIZE);
        std::memset(mem2.data(), 0, MEM2_SIZE);
        // Initialize I/O register values
        videoBgColor = 0x00000000;  // default black background
        audioFreqValue = 0;
        video = nullptr;
        audio = nullptr;
        input = nullptr;
    }

    // Connect hardware components for I/O callbacks
    void connectVideo(Video* v)   { video = v; }
    void connectAudio(Audio* a)   { audio = a; }
    void connectInput(Input* i)   { input = i; }

    // Read 32-bit word from memory or I/O (PowerPC is big-endian)
    uint32_t read32(uint32_t address) {
        // Translate address to physical region (MEM1, MEM2 or I/O)
        if ((address >= 0x80000000 && address < 0x80000000 + MEM1_SIZE) || 
            (address >= 0xC0000000 && address < 0xC0000000 + MEM1_SIZE)) {
            // MEM1 (24MB)
            uint32_t offset = address & (MEM1_SIZE - 1);  // mask to 24MB
            if (offset + 3 < MEM1_SIZE) {
                // Combine 4 bytes in big-endian order
                uint32_t b0 = mem1[offset];
                uint32_t b1 = mem1[offset + 1];
                uint32_t b2 = mem1[offset + 2];
                uint32_t b3 = mem1[offset + 3];
                return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
            }
            SDL_Log("MEM1 read out of range: 0x%08X", address);
            return 0;
        } else if ((address >= 0x90000000 && address < 0x90000000 + MEM2_SIZE) || 
                   (address >= 0xD0000000 && address < 0xD0000000 + MEM2_SIZE)) {
            // MEM2 (64MB)
            uint32_t offset = address & (MEM2_SIZE - 1);
            if (offset + 3 < MEM2_SIZE) {
                uint32_t b0 = mem2[offset];
                uint32_t b1 = mem2[offset + 1];
                uint32_t b2 = mem2[offset + 2];
                uint32_t b3 = mem2[offset + 3];
                return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
            }
            SDL_Log("MEM2 read out of range: 0x%08X", address);
            return 0;
        } else {
            // I/O or other regions
            if (address == REG_VIDEO_BG_COLOR) {
                return videoBgColor;                  // last written background color
            } else if (address == REG_INPUT_STATE) {
                return input ? input->getButtonState() : 0;
            } else if (address == REG_AUDIO_FREQ) {
                return audioFreqValue;
            }
            SDL_Log("Unhandled read from address 0x%08X", address);
            return 0;
        }
    }

    // Write 32-bit word to memory or I/O (big-endian format)
    void write32(uint32_t address, uint32_t value) {
        if ((address >= 0x80000000 && address < 0x80000000 + MEM1_SIZE) || 
            (address >= 0xC0000000 && address < 0xC0000000 + MEM1_SIZE)) {
            // MEM1 write
            uint32_t offset = address & (MEM1_SIZE - 1);
            if (offset + 3 < MEM1_SIZE) {
                // Split value into bytes (big-endian)
                mem1[offset]     = (value >> 24) & 0xFF;
                mem1[offset + 1] = (value >> 16) & 0xFF;
                mem1[offset + 2] = (value >> 8) & 0xFF;
                mem1[offset + 3] = value & 0xFF;
            } else {
                SDL_Log("MEM1 write out of range: 0x%08X", address);
            }
        } else if ((address >= 0x90000000 && address < 0x90000000 + MEM2_SIZE) || 
                   (address >= 0xD0000000 && address < 0xD0000000 + MEM2_SIZE)) {
            // MEM2 write
            uint32_t offset = address & (MEM2_SIZE - 1);
            if (offset + 3 < MEM2_SIZE) {
                mem2[offset]     = (value >> 24) & 0xFF;
                mem2[offset + 1] = (value >> 16) & 0xFF;
                mem2[offset + 2] = (value >> 8) & 0xFF;
                mem2[offset + 3] = value & 0xFF;
            } else {
                SDL_Log("MEM2 write out of range: 0x%08X", address);
            }
        } else {
            // I/O register write
            if (address == REG_VIDEO_BG_COLOR) {
                videoBgColor = value;
                if (video) video->setBackgroundColor(value);
            } else if (address == REG_INPUT_STATE) {
                SDL_Log("Ignoring write to input state register");
            } else if (address == REG_AUDIO_FREQ) {
                audioFreqValue = value;
                if (audio) audio->setToneFrequency((double)value);
            } else {
                SDL_Log("Unhandled write to address 0x%08X: value 0x%08X", address, value);
            }
        }
    }

private:
    std::vector<uint8_t> mem1;
    std::vector<uint8_t> mem2;
    // Connected device pointers for I/O
    Video*  video;
    Audio*  audio;
    Input*  input;
    // Stored values for emulated hardware registers
    uint32_t videoBgColor;
    uint32_t audioFreqValue;
};
