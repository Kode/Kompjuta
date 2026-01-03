#include <stdint.h>

uint8_t     *ram = 0x00;
uint64_t     registers[32];
uint64_t     pc = 0x0;
typedef void opcode_func(uint32_t instruction);
opcode_func *opcodes[];

static void next_opcode() {
	pc += 4;
	uint32_t instruction = *(uint32_t *)&ram[pc];
	uint8_t  opcode      = instruction & 0x7f;
	opcodes[opcode](instruction);
}

void opcode_nop(uint32_t instruction) {
	next_opcode();
}

void opcode_add_sub_sll_slt_sltu_xor_srl_sra_or_and(uint32_t instruction) {
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
	next_opcode();
}

void opcode_fence_fencei(uint32_t instruction) {
	next_opcode();
}

void opcode_not_implemented(uint32_t instruction) {}

opcode_func *opcodes[256] = {
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
    &opcode_fence_fencei,
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
    &opcode_add_sub_sll_slt_sltu_xor_srl_sra_or_and,
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

int kickstart(int argc, char **argv) {
	return 0;
}
