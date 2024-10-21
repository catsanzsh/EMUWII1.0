// emulator.cpp

#include <iostream>
#include <cstdint>
#include <cstring>
#include <fstream>

// Define CPU state structure
struct CPUState {
    uint32_t pc;          // Program counter
    uint32_t gpr[32];     // General-purpose registers
    bool running;         // Emulator running state
};

// Define memory size (16 MB)
constexpr uint32_t MEMORY_SIZE = 16 * 1024 * 1024;  // 16 MB
uint8_t memory[MEMORY_SIZE];

// Function to load a binary file into memory starting at address 0x00000000
bool load_binary(const char* filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Error: Unable to open file " << filename << std::endl;
        return false;
    }

    std::streamsize fileSize = file.tellg();
    if (fileSize > MEMORY_SIZE) {
        std::cerr << "Error: File size exceeds memory size." << std::endl;
        return false;
    }

    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(memory), fileSize)) {
        std::cerr << "Error: Failed to read the file." << std::endl;
        return false;
    }

    return true;
}

// Function to read a 32-bit word from memory (handles little-endian)
inline uint32_t read_word(uint32_t address) {
    return memory[address] |
           (memory[address + 1] << 8) |
           (memory[address + 2] << 16) |
           (memory[address + 3] << 24);
}

// Fetch the next instruction from memory
inline uint32_t fetch_instruction(CPUState &state) {
    uint32_t instruction = read_word(state.pc);
    state.pc += 4;
    return instruction;
}

// Decode and execute the instruction
void execute_instruction(uint32_t instruction, CPUState &state) {
    uint32_t opcode = (instruction >> 26) & 0x3F;
    uint32_t rD, rA, rB, immediate;

    switch (opcode) {
        case 0x00:  // NOP
            break;

        case 0x14:  // ADDI
            rD = (instruction >> 21) & 0x1F;
            rA = (instruction >> 16) & 0x1F;
            immediate = instruction & 0xFFFF;
            state.gpr[rD] = state.gpr[rA] + static_cast<int16_t>(immediate);
            break;

        case 0x10:  // ADD
            rD = (instruction >> 21) & 0x1F;
            rA = (instruction >> 16) & 0x1F;
            rB = (instruction >> 11) & 0x1F;
            state.gpr[rD] = state.gpr[rA] + state.gpr[rB];
            break;

        case 0x08:  // SUB
            rD = (instruction >> 21) & 0x1F;
            rA = (instruction >> 16) & 0x1F;
            rB = (instruction >> 11) & 0x1F;
            state.gpr[rD] = state.gpr[rA] - state.gpr[rB];
            break;

        case 0x0C:  // MUL
            rD = (instruction >> 21) & 0x1F;
            rA = (instruction >> 16) & 0x1F;
            rB = (instruction >> 11) & 0x1F;
            state.gpr[rD] = state.gpr[rA] * state.gpr[rB];
            break;

        case 0x04:  // ANDI
            rD = (instruction >> 21) & 0x1F;
            rA = (instruction >> 16) & 0x1F;
            immediate = instruction & 0xFFFF;
            state.gpr[rD] = state.gpr[rA] & immediate;
            break;

        case 0x0A:  // ORI
            rD = (instruction >> 21) & 0x1F;
            rA = (instruction >> 16) & 0x1F;
            immediate = instruction & 0xFFFF;
            state.gpr[rD] = state.gpr[rA] | immediate;
            break;

        case 0x02:  // BEQ (Branch if Equal)
            rA = (instruction >> 21) & 0x1F;
            rB = (instruction >> 16) & 0x1F;
            immediate = instruction & 0xFFFF;
            if (state.gpr[rA] == state.gpr[rB]) {
                state.pc += (static_cast<int16_t>(immediate) << 2) - 4;
            }
            break;

        case 0x03:  // BNE (Branch if Not Equal)
            rA = (instruction >> 21) & 0x1F;
            rB = (instruction >> 16) & 0x1F;
            immediate = instruction & 0xFFFF;
            if (state.gpr[rA] != state.gpr[rB]) {
                state.pc += (static_cast<int16_t>(immediate) << 2) - 4;
            }
            break;

        // Load and Store Instructions (LW and SW)
        case 0x20:  // LW (Load Word)
            rD = (instruction >> 21) & 0x1F;
            immediate = instruction & 0xFFFF;
            {
                uint32_t address = state.gpr[rD] + static_cast<int16_t>(immediate);
                if (address > MEMORY_SIZE - 4) {
                    std::cerr << "Error: Memory read out of bounds at address 0x"
                              << std::hex << address << std::endl;
                    state.running = false;
                    return;
                }
                state.gpr[rD] = read_word(address);
            }
            break;

        case 0x28:  // SW (Store Word)
            rD = (instruction >> 21) & 0x1F;
            immediate = instruction & 0xFFFF;
            {
                uint32_t address = state.gpr[rD] + static_cast<int16_t>(immediate);
                if (address > MEMORY_SIZE - 4) {
                    std::cerr << "Error: Memory write out of bounds at address 0x"
                              << std::hex << address << std::endl;
                    state.running = false;
                    return;
                }
                uint32_t value = state.gpr[rD];
                memory[address]     = value & 0xFF;
                memory[address + 1] = (value >> 8) & 0xFF;
                memory[address + 2] = (value >> 16) & 0xFF;
                memory[address + 3] = (value >> 24) & 0xFF;
            }
            break;

        case 0x3F:  // HALT
            state.running = false;
            break;

        default:
            std::cerr << "Error: Unknown opcode 0x" << std::hex << opcode << std::endl;
            state.running = false;
            break;
    }
}

int main(int argc, char* argv[]) {
    // Initialize CPU state
    CPUState cpuState = {};
    cpuState.pc = 0;
    cpuState.running = true;

    // Load binary file
    if (argc < 2) {
        std::cerr << "Usage: emulator <binary_file>" << std::endl;
        return 1;
    }

    if (!load_binary(argv[1])) {
        return 1;
    }

    // Main emulation loop
    while (cpuState.running) {
        uint32_t instruction = fetch_instruction(cpuState);
        execute_instruction(instruction, cpuState);
    }

    return 0;
}
