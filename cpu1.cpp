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

    CPUState() : pc(0), running(true) {
        std::memset(gpr, 0, sizeof(gpr));
    }
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
        std::cerr << "Error: File size (" << fileSize << " bytes) exceeds memory size (" 
                  << MEMORY_SIZE << " bytes)." << std::endl;
        return false;
    }

    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(memory), fileSize)) {
        std::cerr << "Error: Failed to read the file." << std::endl;
        return false;
    }

    std::cout << "Loaded " << fileSize << " bytes into memory." << std::endl;
    return true;
}

// Function to read a 32-bit word from memory (little-endian)
inline uint32_t read_word(uint32_t address) {
    if (address > MEMORY_SIZE - 4) {
        std::cerr << "Error: Memory read out of bounds at address 0x" 
                  << std::hex << address << std::endl;
        return 0;
    }
    return static_cast<uint32_t>(memory[address]) |
           (static_cast<uint32_t>(memory[address + 1]) << 8) |
           (static_cast<uint32_t>(memory[address + 2]) << 16) |
           (static_cast<uint32_t>(memory[address + 3]) << 24);
}

// Function to write a 32-bit word to memory (little-endian)
inline void write_word(uint32_t address, uint32_t value) {
    if (address > MEMORY_SIZE - 4) {
        std::cerr << "Error: Memory write out of bounds at address 0x" 
                  << std::hex << address << std::endl;
        return;
    }
    memory[address]     = value & 0xFF;
    memory[address + 1] = (value >> 8) & 0xFF;
    memory[address + 2] = (value >> 16) & 0xFF;
    memory[address + 3] = (value >> 24) & 0xFF;
}

// Fetch the next instruction from memory
inline uint32_t fetch_instruction(CPUState &state) {
    if (state.pc > MEMORY_SIZE - 4) {
        std::cerr << "Error: Program counter out of bounds at address 0x"
                  << std::hex << state.pc << std::endl;
        state.running = false;
        return 0;
    }
    uint32_t instruction = read_word(state.pc);
    state.pc += 4;
    return instruction;
}

// Decode and execute the instruction
void execute_instruction(uint32_t instruction, CPUState &state) {
    uint32_t opcode = (instruction >> 26) & 0x3F;
    uint32_t rD, rA, rB, immediate;
    int16_t offset;

    switch (opcode) {
        case 0x00:  // NOP (No operation, useful for padding)
            break;

        case 0x14:  // ADDI
            rD = (instruction >> 21) & 0x1F;
            rA = (instruction >> 16) & 0x1F;
            immediate = static_cast<int16_t>(instruction & 0xFFFF);  // Sign-extend
            state.gpr[rD] = state.gpr[rA] + immediate;
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
            immediate = static_cast<int16_t>(instruction & 0xFFFF);
            state.gpr[rD] = state.gpr[rA] & static_cast<uint16_t>(immediate);
            break;

        case 0x0A:  // ORI
            rD = (instruction >> 21) & 0x1F;
            rA = (instruction >> 16) & 0x1F;
            immediate = static_cast<int16_t>(instruction & 0xFFFF);
            state.gpr[rD] = state.gpr[rA] | static_cast<uint16_t>(immediate);
            break;

        case 0x02:  // BEQ (Branch if Equal)
            rA = (instruction >> 21) & 0x1F;
            rB = (instruction >> 16) & 0x1F;
            immediate = static_cast<int16_t>(instruction & 0xFFFF);
            offset = immediate;
            if (state.gpr[rA] == state.gpr[rB]) {
                state.pc += static_cast<int32_t>(offset) << 2;
            }
            break;

        case 0x03:  // BNE (Branch if Not Equal)
            rA = (instruction >> 21) & 0x1F;
            rB = (instruction >> 16) & 0x1F;
            immediate = static_cast<int16_t>(instruction & 0xFFFF);
            offset = immediate;
            if (state.gpr[rA] != state.gpr[rB]) {
                state.pc += static_cast<int32_t>(offset) << 2;
            }
            break;

        // Load and Store Instructions (LW and SW)
        case 0x20:  // LW (Load Word)
            rA = (instruction >> 16) & 0x1F;  // Base register
            rD = (instruction >> 21) & 0x1F;  // Destination register
            immediate = instruction & 0xFFFF;
            {
                int16_t imm_signed = static_cast<int16_t>(immediate);
                int32_t address = state.gpr[rA] + imm_signed;
                if (address < 0 || address > MEMORY_SIZE - 4) {
                    std::cerr << "Error: Memory read out of bounds at address 0x"
                              << std::hex << address << std::endl;
                    state.running = false;
                    return;
                }
                state.gpr[rD] = read_word(address);
            }
            break;

        case 0x28:  // SW (Store Word)
            rA = (instruction >> 16) & 0x1F;  // Base register
            rD = (instruction >> 21) & 0x1F;  // Source register
            immediate = instruction & 0xFFFF;
            {
                int16_t imm_signed = static_cast<int16_t>(immediate);
                int32_t address = state.gpr[rA] + imm_signed;
                if (address < 0 || address > MEMORY_SIZE - 4) {
                    std::cerr << "Error: Memory write out of bounds at address 0x"
                              << std::hex << address << std::endl;
                    state.running = false;
                    return;
                }
                uint32_t value = state.gpr[rD];
                write_word(address, value);
            }
            break;

        case 0x3F:  // HALT
            state.running = false;
            std::cout << "HALT encountered. Stopping emulator." << std::endl;
            break;

        default:
            std::cerr << "Error: Unknown opcode 0x" << std::hex << opcode 
                      << " at PC=0x" << std::hex << state.pc - 4 << std::endl;
            state.running = false;
            break;
    }
}

// Function to display CPU state (for debugging purposes)
void display_cpu_state(const CPUState &state) {
    std::cout << "PC: 0x" << std::hex << state.pc << std::endl;
    std::cout << "Registers:" << std::endl;
    for (int i = 0; i < 32; ++i) {
        std::cout << "R" << std::dec << i << ": 0x" 
                  << std::hex << state.gpr[i] << "  ";
        if ((i + 1) % 4 == 0)
            std::cout << std::endl;
    }
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    // Initialize CPU state
    CPUState cpuState;

    // Load binary file
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <binary_file>" << std::endl;
        return 1;
    }

    if (!load_binary(argv[1])) {
        return 1;
    }

    // Optional: Display initial CPU state
    // display_cpu_state(cpuState);

    // Main emulation loop
    while (cpuState.running) {
        // Fetch
        uint32_t instruction = fetch_instruction(cpuState);

        // Optional: Display fetched instruction
        // std::cout << "Fetched Instruction: 0x" << std::hex << instruction << std::endl;

        // Execute
        execute_instruction(instruction, cpuState);

        // Optional: Display CPU state after execution
        // display_cpu_state(cpuState);
    }

    std::cout << "Emulation completed." << std::endl;
    return 0;
}
