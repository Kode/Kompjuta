#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t        *ram = NULL;
static uint64_t registers[32];
static uint64_t pc = 0x0;
typedef void    opcode_func(uint32_t instruction);
opcode_func    *opcodes[];

#define MMIO_BASE 0xffff0000

#define FB_ADDR   0x0
#define FB_STRIDE 0x08
#define FB_WIDTH  0x0c
#define FB_HEIGHT 0x10
#define FB_FORMAT 0x14
#define PRESENT   0x18

uint32_t read_memory32(uint64_t address) {
	if (address >= MMIO_BASE) {
		return 0;
	}
	return *(uint32_t *)&ram[address];
}

uint64_t read_memory64(uint64_t address) {
	if (address >= MMIO_BASE) {
		return 0;
	}
	return *(uint64_t *)&ram[address];
}

void store_memory8(uint64_t address, uint8_t value) {
	if (address >= MMIO_BASE) {
	}
	else {
		uint8_t *target = (uint8_t *)&ram[address];
		*target         = value;
	}
}

void store_memory16(uint64_t address, uint16_t value) {
	if (address >= MMIO_BASE) {
	}
	else {
		uint16_t *target = (uint16_t *)&ram[address];
		*target          = value;
	}
}

void store_memory32(uint64_t address, uint32_t value) {
	if (address >= MMIO_BASE) {
	}
	else {
		uint32_t *target = (uint32_t *)&ram[address];
		*target          = value;
	}
}

void store_memory64(uint64_t address, uint64_t value) {
	if (address >= MMIO_BASE) {
	}
	else {
		uint64_t *target = (uint64_t *)&ram[address];
		*target          = value;
	}
}

static void increment_pc(void) {
	pc += 4;
}

static void execute_opcode(void) {
	uint32_t instruction = *(uint32_t *)&ram[pc];
	uint8_t  opcode      = instruction & 0x7f;
	opcodes[opcode](instruction);
}

static uint32_t sign_extend32(uint32_t value, int bits) {
	uint32_t mask = 1ull << (bits - 1);
	return (value ^ mask) - mask;
}

static uint64_t sign_extend64(uint64_t value, int bits) {
	uint64_t mask = 1ull << (bits - 1);
	return (value ^ mask) - mask;
}

static void opcode_nop(uint32_t instruction) {
	increment_pc();
	execute_opcode();
}

static void opcode_lui(uint32_t instruction) {
	uint32_t immediate = instruction >> 12u;
	uint8_t  rd        = (instruction >> 7) & 0x1f;

	registers[rd] = immediate << 12u;

	increment_pc();
	execute_opcode();
}

static void opcode_addi_slti_sltiu_xori_ori_andi_slli_srli_srai(uint32_t instruction) {
	uint8_t  rs1       = (instruction >> 15) & 0x1f;
	uint8_t  rd        = (instruction >> 7) & 0x1f;
	uint16_t immediate = instruction >> 20;
	uint32_t shamt     = (instruction >> 20) & 0x3f;

	uint8_t command = (instruction >> 12) & 0x7;
	switch (command) {
	case 0x0: // addi
		registers[rd] = registers[rs1] + sign_extend64(immediate, 12);
		break;
	case 0x2: { // slti
		uint64_t immediate_value = sign_extend64(immediate, 12);

		int64_t x = *(int64_t *)&registers[rs1];
		int64_t y = *(int64_t *)&immediate_value;

		registers[rd] = (x < y) ? 1 : 0;
		break;
	}
	case 0x3: // sltiu
		registers[rd] = (registers[rs1] < sign_extend64(immediate, 12)) ? 1 : 0;
		break;
	case 0x4: // xori
		registers[rd] = registers[rs1] ^ sign_extend64(immediate, 12);
		break;
	case 0x6: // ori
		registers[rd] = registers[rs1] | sign_extend64(immediate, 12);
		break;
	case 0x7: // andi
		registers[rd] = registers[rs1] & sign_extend64(immediate, 12);
		break;
	case 0x1: // slli
		registers[rd] = registers[rs1] << shamt;
		break;
	case 0x5: { // srli_srai
		uint8_t signed_shift = instruction >> 25;
		if (signed_shift != 0) {
			int64_t x     = *(int64_t *)&registers[rs1];
			registers[rd] = x >> shamt;
		}
		else {
			registers[rd] = registers[rs1] >> shamt;
		}
		break;
	}
	}

	increment_pc();
	execute_opcode();
}

static void opcode_lw(uint32_t instruction) {
	uint8_t  rs1    = (instruction >> 15) & 0x1f;
	uint8_t  rd     = (instruction >> 7) & 0x1f;
	uint16_t offset = instruction >> 20;

	uint32_t value = read_memory32(registers[rs1] + sign_extend64(offset, 12));
	registers[rd]  = sign_extend64(value, 32);

	increment_pc();
	execute_opcode();
}

static void opcode_addiw(uint32_t instruction) {
	uint8_t  rs1       = (instruction >> 15) & 0x1f;
	uint8_t  rd        = (instruction >> 7) & 0x1f;
	uint16_t immediate = instruction >> 20u;

	uint32_t x      = (uint32_t)registers[rs1];
	uint32_t result = x + sign_extend32(immediate, 12);
	registers[rd]   = sign_extend64(result, 32);

	increment_pc();
	execute_opcode();
}

static void opcode_sb_sh_sw_sd(uint32_t instruction) {
	uint8_t rs1 = (instruction >> 15) & 0x1f;
	uint8_t rs2 = (instruction >> 20) & 0x1f;

	uint16_t imm1      = (instruction >> 25) & 0x7f;
	uint16_t imm2      = (instruction >> 7) & 0x1f;
	uint16_t immediate = (imm1 << 5) | imm2;

	uint8_t command = (instruction >> 12) & 0x7;

	switch (command) {
	case 0x0: // sb
		store_memory8(registers[rs1] + immediate, *(uint8_t *)&registers[rs2]);
		break;
	case 0x1: // sh
		store_memory16(registers[rs1] + immediate, *(uint16_t *)&registers[rs2]);
		break;
	case 0x2: // sw
		store_memory32(registers[rs1] + immediate, *(uint32_t *)&registers[rs2]);
		break;
	case 0x3: // sd
		store_memory64(registers[rs1] + immediate, registers[rs2]);
		break;
	}

	increment_pc();
	execute_opcode();
}

static void opcode_jal(uint32_t instruction) {
	uint8_t rd = (instruction >> 7) & 0x1f;

	if (rd != 0) {
		registers[rd] = pc + 4;
	}

	uint32_t immediate =
	    ((instruction >> 31) & 0x1) << 20 | ((instruction >> 21) & 0x3ff) << 1 | ((instruction >> 20) & 0x1) << 11 | ((instruction >> 12) & 0xff) << 12;

	pc += sign_extend64(immediate, 21);

	execute_opcode();
}

static void opcode_beq_bne_blt_bge_bltu_bgeu(uint32_t instruction) {
	uint8_t rs1 = (instruction >> 15) & 0x1f;
	uint8_t rs2 = (instruction >> 20) & 0x1f;

	uint8_t command = (instruction >> 12) & 0x7;

	bool branch = false;
	switch (command) {
	case 0x0: // beq
		branch = registers[rs1] == registers[rs2];
		break;
	case 0x1: // bne
		branch = registers[rs1] != registers[rs2];
		break;
	case 0x4: { // blt
		int64_t x = *(int64_t *)&registers[rs1];
		int64_t y = *(int64_t *)&registers[rs2];
		branch    = x < y;
		break;
	}
	case 0x5: { // bge
		int64_t x = *(int64_t *)&registers[rs1];
		int64_t y = *(int64_t *)&registers[rs2];
		branch    = x >= y;
		break;
	}
	case 0x6: // bltu
		branch = registers[rs1] < registers[rs2];
		break;
	case 0x7: // bgeu
		branch = registers[rs1] >= registers[rs2];
		break;
	}

	if (branch) {
		uint32_t immediate =
		    (((instruction >> 31) & 0x1) << 12) | (((instruction >> 25) & 0x3f) << 5) | (((instruction >> 8) & 0xf) << 1) | (((instruction >> 7) & 0x1) << 11);
		pc += sign_extend64(immediate, 13);
	}
	else {
		increment_pc();
	}

	execute_opcode();
}

static void opcode_add_sub_sll_slt_sltu_xor_srl_sra_or_and(uint32_t instruction) {
	uint8_t middle = (instruction >> 12) & 0x7;

	uint8_t rs1 = (instruction >> 15) & 0x1f;
	uint8_t rs2 = (instruction >> 20) & 0x1f;
	uint8_t rd  = (instruction >> 7) & 0x1f;

	if (rd != 0) {
		switch (middle) {
		case 0x0: { // add_sub
			uint8_t upper = (instruction >> 25) & 0x7f;

			switch (upper) {
			case 0x00: // add
				registers[rd] = registers[rs1] + registers[rs2];
				break;
			case 0x30: // sub
				registers[rd] = registers[rs1] - registers[rs2];
				break;
			}
			break;
		}
		case 0x1: // sll
			registers[rd] = registers[rs1] << registers[rs2];
			break;
		case 0x2: { // slt
			int64_t x     = *(int64_t *)&registers[rs1];
			int64_t y     = *(int64_t *)&registers[rs2];
			registers[rd] = (x < y) ? 1u : 0u;
			break;
		}
		case 0x3: // sltu
			registers[rd] = (registers[rs1] < registers[rs2]) ? 1u : 0u;
			break;
		case 0x4: // xor
			registers[rd] = registers[rs1] ^ registers[rs2];
			break;
		case 0x5: { // srl_sra
			uint8_t upper = (instruction >> 25) & 0x7f;

			uint8_t y = registers[rs2] & 0x1f;

			switch (upper) {
			case 0x0: // srl
				registers[rd] = registers[rs1] >> y;
				break;
			case 0x20: { // src
				int64_t x      = *(int64_t *)&registers[rs1];
				int64_t result = x >> y;
				registers[rd]  = *(uint64_t *)&result;
				break;
			}
			}
			break;
		}
		case 0x6: // or
			registers[rd] = registers[rs1] | registers[rs2];
			break;
		case 0x7: // and
			registers[rd] = registers[rs1] & registers[rs2];
			break;
		}
	}

	increment_pc();
	execute_opcode();
}

static void opcode_fence_fencei(uint32_t instruction) {
	execute_opcode();
}

static void opcode_not_implemented(uint32_t instruction) {
	assert(false);
	increment_pc();
	execute_opcode();
}

opcode_func *opcodes[256] = {
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_lw,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented, // 10
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_fence_fencei,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_addi_slti_sltiu_xori_ori_andi_slli_srli_srai,
    &opcode_not_implemented, // 20
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_addiw,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented, // 30
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_sb_sh_sw_sd,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented, // 40
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented, // 50
    &opcode_add_sub_sll_slt_sltu_xor_srl_sra_or_and,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_lui,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented, // 60
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented, // 70
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented, // 80
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented, // 90
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_beq_bne_blt_bge_bltu_bgeu,
    &opcode_not_implemented, // 100
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented, // 110
    &opcode_jal,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_nop,
    &opcode_not_implemented,
};

bool read_magic_number(uint8_t *binary, uint64_t *offset) {
	bool value = binary[*offset + 0] == 0x7f && binary[*offset + 1] == 0x45 && binary[*offset + 2] == 0x4c && binary[*offset + 3] == 0x46;
	*offset += 4;
	return value;
}

uint8_t read_uint8(uint8_t *binary, uint64_t *offset) {
	uint8_t value = binary[*offset];
	*offset += 1;
	return value;
}

uint16_t read_uint16(uint8_t *binary, uint64_t *offset) {
	uint16_t value = *(uint16_t *)&binary[*offset];
	*offset += 2;
	return value;
}

uint32_t read_uint32(uint8_t *binary, uint64_t *offset) {
	uint32_t value = *(uint32_t *)&binary[*offset];
	*offset += 4;
	return value;
}

uint64_t read_uint64(uint8_t *binary, uint64_t *offset) {
	uint64_t value = *(uint64_t *)&binary[*offset];
	*offset += 8;
	return value;
}

static uint64_t program_header_offset;
static uint16_t program_header_entry_size;
static uint16_t program_header_entry_count;

uint64_t entry;

static void read_header(uint8_t *binary) {
	uint64_t offset = 0;

	read_magic_number(binary, &offset);

	uint8_t width = read_uint8(binary, &offset);
	assert(width == 2);

	uint8_t endianess = read_uint8(binary, &offset);
	assert(endianess == 1);

	uint8_t ident_version = read_uint8(binary, &offset);
	assert(ident_version == 1);

	uint8_t abi = read_uint8(binary, &offset);
	assert(abi == 0);

	uint8_t abi_version = read_uint8(binary, &offset);
	assert(abi_version == 0);

	offset += 7;

	uint16_t object_type = read_uint16(binary, &offset);
	assert(object_type == 0x2);

	uint16_t machine = read_uint16(binary, &offset);
	assert(machine == 0xf3);

	uint32_t elf_version = read_uint32(binary, &offset);
	assert(elf_version == 1);

	entry = read_uint64(binary, &offset);

	program_header_offset = read_uint64(binary, &offset);

	uint64_t section_header_offset = read_uint64(binary, &offset);

	uint32_t flags = read_uint32(binary, &offset);

	uint32_t header_size = read_uint16(binary, &offset);

	program_header_entry_size = read_uint16(binary, &offset);

	program_header_entry_count = read_uint16(binary, &offset);

	uint16_t section_header_entry_size = read_uint16(binary, &offset);

	uint16_t section_header_entry_count = read_uint16(binary, &offset);

	uint16_t section_header_names_entry_index = read_uint16(binary, &offset);
}

static void read_loadable_segments(uint8_t *binary) {
	uint8_t *program_header = &binary[program_header_offset];
	for (uint16_t program_header_index = 0; program_header_index < program_header_entry_count; ++program_header_index) {
		uint8_t *program_header_entry = &program_header[program_header_index * program_header_entry_size];

		uint64_t offset       = 0;
		uint32_t program_type = read_uint32(program_header_entry, &offset);
		if (program_type == 0x1) {
			uint32_t flags            = read_uint32(program_header_entry, &offset);
			uint64_t file_offset      = read_uint64(program_header_entry, &offset);
			uint64_t virtual_address  = read_uint64(program_header_entry, &offset);
			uint64_t physical_address = read_uint64(program_header_entry, &offset);
			uint64_t file_size        = read_uint64(program_header_entry, &offset);
			uint64_t memory_size      = read_uint64(program_header_entry, &offset);
			uint64_t alignment        = read_uint64(program_header_entry, &offset);

			memcpy(&ram[virtual_address], &binary[file_offset], file_size);
			memset(&ram[virtual_address + file_size], 0, memory_size - file_size);
		}
	}
}

int kickstart(int argc, char **argv) {
	FILE *file = fopen("prog.elf", "rb");
	fseek(file, 0, SEEK_END);
	uint32_t size = ftell(file);
	fseek(file, 0, 0);

	uint8_t *binary = (uint8_t *)malloc(size);
	assert(binary != NULL);
	fread(binary, 1, size, file);
	fclose(file);

	read_header(binary);

	const uint64_t memory_size = 64 * 1024 * 1024;

	ram = malloc(memory_size);
	assert(ram != NULL);
	memset(ram, 0, memory_size);

	read_loadable_segments(binary);

	pc = entry;
	execute_opcode();

	return 0;
}
