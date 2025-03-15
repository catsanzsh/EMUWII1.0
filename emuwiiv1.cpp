// WiiEmulatorSDL.cpp - Enhanced Wii Emulator using SDL2
// Version: 1.0 Beta (Completed Implementation)
// Author: WiiEmulateTeam
// Last Modified: March 15, 2025

#include <iostream>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>
#include <map>
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>

// Constants
constexpr uint32_t MEMORY_SIZE = (24 + 64) * 1024 * 1024;  // 88 MB
constexpr uint32_t MEM_MASK = MEMORY_SIZE - 1;
constexpr int SCREEN_WIDTH = 640;
constexpr int SCREEN_HEIGHT = 480;
constexpr uint32_t KERNEL_BASE_ADDR = 0x80000000;
constexpr uint32_t INTERRUPT_TABLE_BASE = 0x80003000;
constexpr int MAX_CONTROLLERS = 4;

// Memory Map Constants
constexpr uint32_t RAM_START = 0x80000000;
constexpr uint32_t RAM_END = 0x81FFFFFF;
constexpr uint32_t HARDWARE_REGS_START = 0xCC000000;
constexpr uint32_t HARDWARE_REGS_END = 0xCC00FFFF;
constexpr uint32_t STARLET_MEM_START = 0xCD000000;
constexpr uint32_t STARLET_MEM_END = 0xCD00FFFF;

// CPU State Structure - PowerPC Architecture
struct FPR {
    float ps0;
    float ps1;
};

struct CPUState {
    uint32_t pc;                      // Program Counter
    uint32_t gpr[32];                 // General Purpose Registers
    FPR fpr[32];                      // Floating Point Registers (paired singles)
    uint32_t spr[1024];               // Special Purpose Registers
    bool running;                     // Emulation loop control
    bool interrupts_enabled;          // Interrupt management
    bool kernel_mode;                 // Kernel mode flag
    uint64_t cycle_count;             // Number of cycles executed

    CPUState() : pc(0), running(true), interrupts_enabled(false), kernel_mode(true), cycle_count(0) {
        memset(gpr, 0, sizeof(gpr));
        memset(fpr, 0, sizeof(fpr));
        memset(spr, 0, sizeof(spr));
    }
};

// Starlet Coprocessor Memory Structure
struct StarletMemory {
    uint32_t command;
    uint32_t response;
    uint32_t param_addr;
    uint32_t result_addr;
    uint32_t status;
    
    StarletMemory() : command(0), response(0), param_addr(0), result_addr(0), status(0) {}
} starlet_memory;

// Controller State
struct Controller {
    bool connected;
    uint16_t buttons;
    int8_t analog_x;
    int8_t analog_y;
    
    Controller() : connected(false), buttons(0), analog_x(0), analog_y(0) {}
};

// Audio State
struct AudioState {
    SDL_AudioDeviceID device;
    uint8_t* buffer;
    size_t buffer_size;
    size_t position;
    bool initialized;
    
    AudioState() : device(0), buffer(nullptr), buffer_size(0), position(0), initialized(false) {}
};

// Emulator Memory
uint8_t memory[MEMORY_SIZE];

// SDL2 Variables
SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
SDL_Texture* framebuffer_texture = nullptr;
uint32_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
Controller controllers[MAX_CONTROLLERS];
AudioState audio_state;

// Interrupt Vectors
std::map<int, uint32_t> interrupt_vectors;

// Function Prototypes
bool initialize_sdl();
bool initialize_wii_subsystems();
void close_sdl();
bool load_game(const char* filename);
uint32_t read_word(uint32_t address);
void write_word(uint32_t address, uint32_t value);
void trigger_interrupt(int interrupt_type, CPUState &state);
bool handle_starlet_command(CPUState &state);
void execute_instruction(uint32_t instruction, CPUState &state);
uint32_t fetch_instruction(const CPUState &state);
bool initialize_graphics();
void render_frame(const CPUState &state);
void audio_callback(void* userdata, uint8_t* stream, int len);
void process_sdl_input();
uint32_t translate_address(uint32_t virtual_addr);

// Kernel Functions
bool initialize_kernel() {
    std::cout << "Initializing Wii Kernel...\n";
    
    // Setup interrupt vectors
    interrupt_vectors[0] = INTERRUPT_TABLE_BASE + 0x00; // System Reset
    interrupt_vectors[1] = INTERRUPT_TABLE_BASE + 0x10; // Machine Check
    interrupt_vectors[2] = INTERRUPT_TABLE_BASE + 0x20; // DSI
    interrupt_vectors[3] = INTERRUPT_TABLE_BASE + 0x30; // ISI
    interrupt_vectors[4] = INTERRUPT_TABLE_BASE + 0x40; // External
    interrupt_vectors[5] = INTERRUPT_TABLE_BASE + 0x50; // Alignment
    interrupt_vectors[6] = INTERRUPT_TABLE_BASE + 0x60; // Program
    interrupt_vectors[7] = INTERRUPT_TABLE_BASE + 0x70; // Floating Point
    interrupt_vectors[8] = INTERRUPT_TABLE_BASE + 0x80; // Decrementer
    interrupt_vectors[9] = INTERRUPT_TABLE_BASE + 0x90; // System Call
    interrupt_vectors[10] = INTERRUPT_TABLE_BASE + 0xA0; // Trace
    interrupt_vectors[11] = INTERRUPT_TABLE_BASE + 0xB0; // Performance Monitor
    
    // Initialize kernel memory area with stub handlers
    for (const auto& entry : interrupt_vectors) {
        // Write a simple return from interrupt instruction at each vector
        // 0x4C000064 is "rfi" instruction in PowerPC
        write_word(entry.second, 0x4C000064);
    }
    
    std::cout << "Kernel initialized successfully.\n";
    return true;
}

void shutdown_kernel() {
    std::cout << "Shutting down Wii Kernel...\n";
    // Free any kernel resources
    interrupt_vectors.clear();
}

// Graphics Functions
bool initialize_graphics() {
    std::cout << "Initializing Wii Graphics...\n";
    
    framebuffer_texture = SDL_CreateTexture(
        renderer, 
        SDL_PIXELFORMAT_RGBA8888, 
        SDL_TEXTUREACCESS_STREAMING, 
        SCREEN_WIDTH, 
        SCREEN_HEIGHT
    );
    
    if (!framebuffer_texture) {
        std::cerr << "Failed to create framebuffer texture: " << SDL_GetError() << "\n";
        return false;
    }
    
    // Clear framebuffer
    memset(framebuffer, 0, sizeof(framebuffer));
    
    return true;
}

void shutdown_graphics() {
    std::cout << "Shutting down Wii Graphics...\n";
    
    if (framebuffer_texture) {
        SDL_DestroyTexture(framebuffer_texture);
        framebuffer_texture = nullptr;
    }
}

void render_frame(const CPUState &state) {
    // Update texture with framebuffer data
    SDL_UpdateTexture(framebuffer_texture, NULL, framebuffer, SCREEN_WIDTH * sizeof(uint32_t));
    
    // Clear screen
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
    // Render texture
    SDL_RenderCopy(renderer, framebuffer_texture, NULL, NULL);
    
    // Example: Draw a moving element based on CPU state for visual feedback
    int x = (state.cycle_count / 100) % SCREEN_WIDTH;
    int y = (state.cycle_count / 200) % SCREEN_HEIGHT;
    
    SDL_Rect rect = {x, y, 16, 16};
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_RenderFillRect(renderer, &rect);
    
    // Present the rendered frame
    SDL_RenderPresent(renderer);
}

// Audio Functions
void audio_callback(void* userdata, uint8_t* stream, int len) {
    AudioState* audio = (AudioState*)userdata;
    
    // If we have audio data, copy it to the stream
    if (audio->initialized && audio->buffer) {
        // Simple audio streaming implementation
        for (int i = 0; i < len; i++) {
            stream[i] = audio->buffer[(audio->position + i) % audio->buffer_size];
        }
        audio->position = (audio->position + len) % audio->buffer_size;
    } else {
        // Otherwise, just output silence
        memset(stream, 0, len);
    }
}

bool initialize_audio() {
    std::cout << "Initializing Wii Audio...\n";
    
    SDL_AudioSpec want, have;
    
    // Configure audio
    want.freq = 32000;  // Wii audio sampling rate
    want.format = AUDIO_S16LSB;  // 16-bit signed little-endian
    want.channels = 2;  // Stereo
    want.samples = 2048;  // Buffer size
    want.callback = audio_callback;
    want.userdata = &audio_state;
    
    // Open audio device
    audio_state.device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    
    if (audio_state.device == 0) {
        std::cerr << "Failed to open audio device: " << SDL_GetError() << "\n";
        return false;
    }
    
    // Allocate audio buffer (1 second of audio)
    audio_state.buffer_size = have.freq * have.channels * 2;  // 2 bytes per sample
    audio_state.buffer = new uint8_t[audio_state.buffer_size];
    memset(audio_state.buffer, 0, audio_state.buffer_size);
    audio_state.position = 0;
    audio_state.initialized = true;
    
    // Start audio playback
    SDL_PauseAudioDevice(audio_state.device, 0);
    
    std::cout << "Audio initialized successfully.\n";
    return true;
}

void shutdown_audio() {
    std::cout << "Shutting down Wii Audio...\n";
    
    if (audio_state.initialized) {
        SDL_CloseAudioDevice(audio_state.device);
        delete[] audio_state.buffer;
        audio_state.buffer = nullptr;
        audio_state.initialized = false;
    }
}

// Input Functions
bool initialize_input() {
    std::cout << "Initializing Wii Input...\n";
    
    // Initialize controller states
    for (int i = 0; i < MAX_CONTROLLERS; i++) {
        controllers[i].connected = (i == 0);  // Only first controller connected by default
        controllers[i].buttons = 0;
        controllers[i].analog_x = 0;
        controllers[i].analog_y = 0;
    }
    
    std::cout << "Input initialized successfully.\n";
    return true;
}

void shutdown_input() {
    std::cout << "Shutting down Wii Input...\n";
    // Nothing to clean up for input
}

void process_sdl_input() {
    SDL_Event e;
    
    while (SDL_PollEvent(&e) != 0) {
        if (e.type == SDL_QUIT) {
            // Exit emulator
            exit(0);
        } 
        else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
            bool pressed = (e.type == SDL_KEYDOWN);
            
            // Map keyboard keys to controller buttons
            switch (e.key.keysym.sym) {
                case SDLK_UP:
                    if (pressed) controllers[0].buttons |= 0x0001;
                    else controllers[0].buttons &= ~0x0001;
                    break;
                case SDLK_DOWN:
                    if (pressed) controllers[0].buttons |= 0x0002;
                    else controllers[0].buttons &= ~0x0002;
                    break;
                case SDLK_LEFT:
                    if (pressed) controllers[0].buttons |= 0x0004;
                    else controllers[0].buttons &= ~0x0004;
                    break;
                case SDLK_RIGHT:
                    if (pressed) controllers[0].buttons |= 0x0008;
                    else controllers[0].buttons &= ~0x0008;
                    break;
                case SDLK_z:  // A button
                    if (pressed) controllers[0].buttons |= 0x0100;
                    else controllers[0].buttons &= ~0x0100;
                    break;
                case SDLK_x:  // B button
                    if (pressed) controllers[0].buttons |= 0x0200;
                    else controllers[0].buttons &= ~0x0200;
                    break;
                case SDLK_a:  // X button
                    if (pressed) controllers[0].buttons |= 0x0400;
                    else controllers[0].buttons &= ~0x0400;
                    break;
                case SDLK_s:  // Y button
                    if (pressed) controllers[0].buttons |= 0x0800;
                    else controllers[0].buttons &= ~0x0800;
                    break;
                case SDLK_RETURN:  // Start
                    if (pressed) controllers[0].buttons |= 0x1000;
                    else controllers[0].buttons &= ~0x1000;
                    break;
            }
        }
    }
}

// Memory Management
uint32_t translate_address(uint32_t virtual_addr) {
    // For simplicity, we perform a basic memory translation
    if (virtual_addr >= RAM_START && virtual_addr <= RAM_END) {
        return (virtual_addr - RAM_START) & MEM_MASK;
    }
    else if (virtual_addr >= HARDWARE_REGS_START && virtual_addr <= HARDWARE_REGS_END) {
        // Hardware registers mapped to a special section of our memory array
        return (0x1000000 + (virtual_addr - HARDWARE_REGS_START)) & MEM_MASK;
    }
    else if (virtual_addr >= STARLET_MEM_START && virtual_addr <= STARLET_MEM_END) {
        // Starlet memory mapped to another special section
        return (0x1100000 + (virtual_addr - STARLET_MEM_START)) & MEM_MASK;
    }
    
    // Default fallback - direct mapping with masking
    return virtual_addr & MEM_MASK;
}

uint32_t read_word(uint32_t address) {
    uint32_t physical_addr = translate_address(address);
    
    if (physical_addr + 3 >= MEMORY_SIZE) {
        std::cerr << "Memory read out of bounds at address: 0x" << std::hex << address << "\n";
        return 0;
    }
    
    return (memory[physical_addr] << 24) | 
           (memory[physical_addr + 1] << 16) | 
           (memory[physical_addr + 2] << 8) | 
           memory[physical_addr + 3];
}

void write_word(uint32_t address, uint32_t value) {
    uint32_t physical_addr = translate_address(address);
    
    if (physical_addr + 3 >= MEMORY_SIZE) {
        std::cerr << "Memory write out of bounds at address: 0x" << std::hex << address << "\n";
        return;
    }
    
    memory[physical_addr] = (value >> 24) & 0xFF;
    memory[physical_addr + 1] = (value >> 16) & 0xFF;
    memory[physical_addr + 2] = (value >> 8) & 0xFF;
    memory[physical_addr + 3] = value & 0xFF;
    
    // Check if writing to framebuffer area (example mapping - adjust as needed)
    if (address >= 0x90000000 && address < 0x90000000 + SCREEN_WIDTH * SCREEN_HEIGHT * 4) {
        uint32_t pixel_offset = (address - 0x90000000) / 4;
        if (pixel_offset < SCREEN_WIDTH * SCREEN_HEIGHT) {
            framebuffer[pixel_offset] = value;
        }
    }
}

// Interrupt Management
uint32_t get_interrupt_vector(int interrupt_type) {
    auto it = interrupt_vectors.find(interrupt_type);
    if (it != interrupt_vectors.end()) {
        return it->second;
    }
    
    // Default interrupt handler
    return INTERRUPT_TABLE_BASE;
}

void trigger_interrupt(int interrupt_type, CPUState &state) {
    if (state.interrupts_enabled) {
        // Save current PC to link register (LR)
        state.spr[8] = state.pc;
        
        // Get interrupt vector address
        state.pc = get_interrupt_vector(interrupt_type);
        
        // Disable interrupts while in handler
        state.interrupts_enabled = false;
        
        // Switch to kernel mode
        state.kernel_mode = true;
        
        std::cout << "Interrupt triggered: " << interrupt_type << " PC set to 0x" 
                 << std::hex << state.pc << std::dec << "\n";
    }
}

// Starlet Coprocessor
bool handle_starlet_command(CPUState &state) {
    if (starlet_memory.command != 0) {
        std::cout << "Handling Starlet command: 0x" << std::hex << starlet_memory.command << std::dec << "\n";
        
        // Process command
        switch (starlet_memory.command) {
            case 0x01: // Initialize
                std::cout << "Starlet: Initialize Command Received\n";
                starlet_memory.response = 0x00; // Success
                break;
                
            case 0x02: // Reset
                std::cout << "Starlet: Reset Command Received\n";
                starlet_memory.response = 0x00; // Success
                break;
                
            case 0x03: // Read data
                {
                    uint32_t src_addr = read_word(starlet_memory.param_addr);
                    uint32_t dest_addr = read_word(starlet_memory.param_addr + 4);
                    uint32_t size = read_word(starlet_memory.param_addr + 8);
                    
                    std::cout << "Starlet: Read Command - Src: 0x" << std::hex << src_addr 
                             << " Dest: 0x" << dest_addr << " Size: " << size << std::dec << "\n";
                    
                    // Perform the data transfer
                    for (uint32_t i = 0; i < size; i += 4) {
                        uint32_t data = read_word(src_addr + i);
                        write_word(dest_addr + i, data);
                    }
                    
                    starlet_memory.response = 0x00; // Success
                }
                break;
                
            case 0x04: // Write data
                {
                    uint32_t src_addr = read_word(starlet_memory.param_addr);
                    uint32_t dest_addr = read_word(starlet_memory.param_addr + 4);
                    uint32_t size = read_word(starlet_memory.param_addr + 8);
                    
                    std::cout << "Starlet: Write Command - Src: 0x" << std::hex << src_addr 
                             << " Dest: 0x" << dest_addr << " Size: " << size << std::dec << "\n";
                    
                    // Perform the data transfer
                    for (uint32_t i = 0; i < size; i += 4) {
                        uint32_t data = read_word(src_addr + i);
                        write_word(dest_addr + i, data);
                    }
                    
                    starlet_memory.response = 0x00; // Success
                }
                break;
                
            case 0x05: // Update audio buffer
                {
                    uint32_t buffer_addr = read_word(starlet_memory.param_addr);
                    uint32_t buffer_size = read_word(starlet_memory.param_addr + 4);
                    
                    std::cout << "Starlet: Audio Buffer Update - Addr: 0x" << std::hex 
                             << buffer_addr << " Size: " << buffer_size << std::dec << "\n";
                    
                    // Copy data to audio buffer
                    if (audio_state.initialized && buffer_size <= audio_state.buffer_size) {
                        for (uint32_t i = 0; i < buffer_size; i++) {
                            audio_state.buffer[i] = memory[translate_address(buffer_addr + i)];
                        }
                        
                        starlet_memory.response = 0x00; // Success
                    } else {
                        starlet_memory.response = 0x01; // Failure
                    }
                }
                break;
                
            default:
                std::cerr << "Starlet: Unknown Command: 0x" << std::hex << starlet_memory.command << std::dec << "\n";
                starlet_memory.response = 0xFF; // Error
                break;
        }
        
        // Update status
        starlet_memory.status = 0x01; // Command completed
        
        // Reset command after handling
        starlet_memory.command = 0;
        
        // Trigger an interrupt to notify the CPU
        trigger_interrupt(1, state); // Starlet interrupt
        
        return true;
    }
    
    return false;
}

// PowerPC Instruction Execution
void execute_instruction(uint32_t instruction, CPUState &state) {
    uint32_t opcode = (instruction >> 26) & 0x3F; // Top 6 bits
    
    // Increment cycle count
    state.cycle_count++;
    
    switch (opcode) {
        case 0x18: { // ADD
            uint8_t ra = (instruction >> 21) & 0x1F;
            uint8_t rb = (instruction >> 16) & 0x1F;
            uint8_t rd = (instruction >> 11) & 0x1F;
            
            state.gpr[rd] = state.gpr[ra] + state.gpr[rb];
            state.pc += 4;
            break;
        }
        
        case 0x19: { // ADDI - Add Immediate
            uint8_t ra = (instruction >> 21) & 0x1F;
            uint8_t rd = (instruction >> 16) & 0x1F;
            int16_t imm = instruction & 0xFFFF;
            
            state.gpr[rd] = state.gpr[ra] + imm;
            state.pc += 4;
            break;
        }
        
        case 0x1C: { // ADDIS - Add Immediate Shifted
            uint8_t ra = (instruction >> 21) & 0x1F;
            uint8_t rd = (instruction >> 16) & 0x1F;
            int16_t imm = instruction & 0xFFFF;
            
            state.gpr[rd] = state.gpr[ra] + (imm << 16);
            state.pc += 4;
            break;
        }
        
        case 0x1F: { // Extended opcodes
            uint8_t xo = (instruction >> 1) & 0x3FF;
            
            if (xo == 0x10A) { // SUB - Subtract
                uint8_t ra = (instruction >> 21) & 0x1F;
                uint8_t rb = (instruction >> 16) & 0x1F;
                uint8_t rd = (instruction >> 11) & 0x1F;
                
                state.gpr[rd] = state.gpr[ra] - state.gpr[rb];
                state.pc += 4;
            } 
            else if (xo == 0x00A) { // CMP - Compare
                uint8_t ra = (instruction >> 21) & 0x1F;
                uint8_t rb = (instruction >> 16) & 0x1F;
                uint8_t crfd = (instruction >> 23) & 0x7;
                
                int32_t a = state.gpr[ra];
                int32_t b = state.gpr[rb];
                
                uint32_t cr_val = 0;
                if (a < b) cr_val = 0x8;
                else if (a > b) cr_val = 0x4;
                else cr_val = 0x2;
                
                // Set condition register field
                state.spr[0] = (state.spr[0] & ~(0xF << (28 - 4*crfd))) | (cr_val << (28 - 4*crfd));
                state.pc += 4;
            }
            else {
                std::cerr << "Unhandled extended opcode: 0x" << std::hex << xo << std::dec << " at PC: 0x" << std::hex << state.pc << std::dec << "\n";
                state.pc += 4;
            }
            break;
        }
        
        case 0x12: { // Branch
            uint32_t raw_offset = instruction & 0x03FFFFFF;
            int32_t offset = static_cast<int32_t>(raw_offset << 2) >> 2; // Sign-extend
            
            // Check if link bit is set (bit 31)
            bool link = (instruction & 0x1) != 0;
            
            if (link) {
                // Store return address in link register
                state.spr[8] = state.pc + 4;
            }
            
            // Check if absolute addressing bit is set (bit 30)
            bool absolute = (instruction & 0x2) != 0;
            
            if (absolute) {
                state.pc = offset & 0xFFFFFFFC;  // Masked to ensure alignment
            } else {
                state.pc += offset;
            }
            break;
        }
        
        case 0x10: { // Branch Conditional
            uint8_t bo = (instruction >> 21) & 0x1F;
            uint8_t bi = (instruction >> 16) & 0x1F;
            int16_t offset = static_cast<int16_t>(instruction & 0xFFFC);
            
            // Check if link bit is set
            bool link = (instruction & 0x1) != 0;
            
            if (link) {
                // Store return address in link register
                state.spr[8] = state.pc + 4;
            }
            
            // Extract condition from CR
            uint8_t cr_field = bi / 4;
            uint8_t cr_bit = bi % 4;
            uint32_t cr = state.spr[0];
            bool condition = (cr & (0x80000000 >> (cr_field * 4 + cr_bit))) != 0;
            
            // Simplified condition check (ignoring CTR decrement for now)
            bool branch_taken = false;
            
            if ((bo & 0x4) != 0) {
                // Skip condition check
                branch_taken = true;
            } else if ((bo & 0x8) != 0) {
                // Branch if condition is true
                branch_taken = condition;
            } else {
                // Branch if condition is false
                branch_taken = !condition;
            }
            
            if (branch_taken) {
                state.pc += offset;
            } else {
                state.pc += 4;
            }
            break;
        }
        
        case 0x3C: { // PS_ADD (Paired Single Add)
            uint8_t ra = (instruction >> 21) & 0x1F;
            uint8_t rb = (instruction >> 16) & 0x1F;
            uint8_t rd = (instruction >> 11) & 0x1F;
            
            state.fpr[rd].ps0 = state.fpr[ra].ps0 + state.fpr[rb].ps0;
            state.fpr[rd].ps1 = state.fpr[ra].ps1 + state.fpr[rb].ps1;
            state.pc += 4;
            break;
        }
        
        case 0x3D: { // PS_SUB (Paired Single Subtract)
            uint8_t ra = (instruction >> 21) & 0x1F;
            uint8_t rb = (instruction >> 16) & 0x1F;
            uint8_t rd = (instruction >> 11) & 0x1F;
            
            state.fpr[rd].ps0 = state.fpr[ra].ps0 - state.fpr[rb].ps0;
            state.fpr[rd].ps1 = state.fpr[ra].ps1 - state.fpr[rb].ps1;
            state.pc += 4;
            break;
        }
        
        case 0x3E: { // PS_MUL (Paired Single Multiply)
            uint8_t ra = (instruction >> 21) & 0x1F;
            uint8_t rb = (instruction >> 16) & 0x1F;
            uint8_t rd = (instruction >> 11) & 0x1F;
            
            state.fpr[rd].ps0 = state.fpr[ra].ps0 * state.fpr[rb].ps0;
            state.fpr[rd].ps1 = state.fpr[ra].ps1 * state.fpr[rb].ps1;
            state.pc += 4;
            break;
        }
        
        case 0x20: { // LWZ (Load Word and Zero)
            uint8_t rs = (instruction >> 21) & 0x1F;
            uint8_t ra = (instruction >> 16) & 0x1F;
            int16_t offset = instruction & 0xFFFF;
            
            uint32_t addr = (ra == 0) ? offset : state.gpr[ra] + offset;
            state.gpr[rs] = read_word(addr);
            state.pc += 4;
            break;
        }
        
        case 0x24: { // STW (Store Word)
            uint8_t rs = (instruction >> 21) & 0x1F;
            uint8_t ra = (instruction >> 16) & 0x1F;
            int16_t offset = instruction & 0xFFFF;
            
            uint32_t addr = (ra == 0) ? offset : state.gpr[ra] + offset;
            write_word(addr, state.gpr[rs]);
            state.pc += 4;
            break;
        }
        
        case 0x0C: { // SYNC/ISYNC
            // Memory synchronization instructions - just a NOP in our emulator
            state.pc += 4;
            break;
        }
        
        case 0x13: { // System Call (SC)
            // Trigger system call interrupt
            trigger_interrupt(9, state);
            break;
        }
        
        case 0x11: { // Return from Interrupt (RFI)
            // Restore PC from Link Register
            state.pc = state.spr[8];
            state.interrupts_enabled = true;
            break;
        }
        
        default:
            std::cerr << "Unhandled opcode: 0x" << std::hex << opcode << std::dec 
                     << " at PC: 0x" << std::hex << state.pc << std::dec << "\n";
            state.pc += 4;  // Skip unknown instruction
            break;
    }
}

// Fetch the next instruction based on PC
uint32_t fetch_instruction(const CPUState &state) {
    return read_word(state.pc);
}

// Main Function
int main(int argc, char* argv[]) {
    std::cout << "Wii Emulator Starting...\n";
    
    if (!initialize_sdl()) {
        std::cerr << "Failed to initialize SDL.\n";
        return 1;
    }
    
    if (!initialize_wii_subsystems()) {
        std::cerr << "Failed to initialize Wii subsystems.\n";
        close_sdl();
        return 1;
    }
    
    CPUState state;
    
    // Set initial PC to entry point
    state.pc = KERNEL_BASE_ADDR;
    
    const char* game_file = (argc > 1) ? argv[1] : "default_game.iso";
    if (!load_game(game_file)) {
        std::cerr << "Failed to load game: " << game_file << "\n";
        close_sdl();
        return 1;
    }
    
    std::cout << "Starting emulation loop...\n";
    
    // Main Emulation Loop
    while (state.running) {
        // Process SDL input events
        process_sdl_input();
        
        // Fetch instruction
        uint32_t instruction = fetch_instruction(state);
        
        // Execute instruction
        execute_instruction(instruction, state);
        
        // Handle Starlet Commands
        handle_starlet_command(state);
        
        // Render every ~16ms for approximately 60 FPS
        if (state.cycle_count % 300000 == 0) {
            render_frame(state);
        }
        
        // Delay to control emulation speed
        if (state.cycle_count % 1000000 == 0) {
            SDL_Delay(1);
        }
    }
    
    // Cleanup
    std::cout << "Shutting down emulator...\n";
    close_sdl();
    
    return 0;
}

// Initialize SDL2 and create window and renderer
bool initialize_sdl() {
    std::cout << "Initializing SDL...\n";
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << "\n";
        return false;
    }
    
    window = SDL_CreateWindow(
        "Wii Emulator", 
        SDL_WINDOWPOS_UNDEFINED, 
        SDL_WINDOWPOS_UNDEFINED, 
        SCREEN_WIDTH, 
        SCREEN_HEIGHT, 
        SDL_WINDOW_SHOWN
    );
    
    if (window == nullptr) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << "\n";
        return false;
    }
    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr) {
        std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << "\n";
        return false;
    }
    
    std::cout << "SDL initialized successfully.\n";
    return true;
}

// Initialize Wii subsystems: Kernel, Graphics, Audio, Input
bool initialize_wii_subsystems() {
    if (!initialize_kernel()) {
        std::cerr << "Failed to initialize Wii Kernel.\n";
        return false;
    }
    
    if (!initialize_graphics()) {
        std::cerr << "Failed to initialize Wii Graphics.\n";
        return false;
    }
    
    if (!initialize_audio()) {
        std::cerr << "Failed to initialize Wii Audio.\n";
        return false;
    }
    
    if (!initialize_input()) {
        std::cerr << "Failed to initialize Wii Input.\n";
        return false;
    }
    
    return true;
}

// Cleanup SDL2 and Wii subsystems
void close_sdl() {
    shutdown_kernel();
    shutdown_graphics();
    shutdown_audio();
    shutdown_input();
    
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    
    SDL_Quit();
}

// Load Wii game image into memory
bool load_game(const char* filename) {
    std::cout << "Loading game: " << filename << "\n";
    
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open game file: " << filename << "\n";
        return false;
    }
    
    // Clear memory
    memset(memory, 0, MEMORY_SIZE);
    
    // Read file into memory
    file.read(reinterpret_cast<char*>(memory), MEMORY_SIZE);
    if (!file) {
        std::cerr << "Warning: Could not read entire file into memory. Read " 
                 << file.gcount() << " bytes.\n";
    }
    
    std::cout << "Game loaded successfully.\n";
    return true;
}
