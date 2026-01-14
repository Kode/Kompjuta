#include <kore3/gpu/buffer.h>
#include <kore3/gpu/device.h>
#include <kore3/log.h>
#include <kore3/system.h>

#include <kong.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mmio.h"

typedef struct vector {
	union {
		uint8_t  u8[128];
		uint16_t u16[64];
		uint32_t u32[32];
		uint64_t u64[16];
		float    f32[32];
		double   f64[16];
	} values;
} vector;

uint8_t *ram = NULL;

static uint64_t x[32] = {0};
static double   f[32] = {0};
static vector   v[32] = {0};

static uint64_t pc      = 0x0;
static uint8_t  sew     = 0x0;
static uint8_t  lmul    = 0x0;
static uint8_t  lmuldiv = 0x0;
static uint16_t vl      = 0x0;

typedef void opcode_func(uint32_t instruction);
opcode_func *opcodes[];

static bool     framebuffer_present = false;
static uint32_t framebuffer_width   = 0;
static uint32_t framebuffer_height  = 0;
static uint32_t framebuffer_stride  = 0;
static uint64_t framebuffer_address = 0;

static bool     command_list_present = false;
static uint32_t command_list_size    = 0;
static uint64_t command_list_address = 0;

#define MEMORY_SIZE 1024 * 1024 * 1024

bool v0_bit(uint16_t lane) {
	uint32_t byte = lane >> 3;
	uint32_t bit  = lane & 7;
	return (v[0].values.u8[byte] >> bit) & 1;
}

uint32_t read_memory8(uint64_t address) {
	if (address >= MMIO_BASE) {
		return 0;
	}
	return ram[address];
}

uint16_t read_memory16(uint64_t address) {
	if (address >= MMIO_BASE) {
		return 0;
	}
	return *(uint16_t *)&ram[address];
}

uint32_t read_memory32(uint64_t address) {
	if (address >= MMIO_BASE) {
		uint64_t offset = address - MMIO_BASE;
		switch (offset) {
		case FB_STRIDE:
			return framebuffer_stride;
		case FB_WIDTH:
			return framebuffer_width;
		case FB_HEIGHT:
			return framebuffer_height;
		}
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

static void execute_command_list(void);

void store_memory8(uint64_t address, uint8_t value) {
	if (address >= MMIO_BASE) {
		uint64_t offset = address - MMIO_BASE;
		switch (offset) {
		case PRESENT:
			framebuffer_present = true;
			break;
		case EXECUTE_COMMAND_LIST:
			execute_command_list();
			break;
		}
	}
	else {
		ram[address] = value;
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
		uint64_t offset = address - MMIO_BASE;
		switch (offset) {
		case COMMAND_LIST_SIZE:
			command_list_size = value;
			break;
		}
	}
	else {
		uint32_t *target = (uint32_t *)&ram[address];
		*target          = value;
	}
}

void store_memory64(uint64_t address, uint64_t value) {
	if (address >= MMIO_BASE) {
		uint64_t offset = address - MMIO_BASE;
		switch (offset) {
		case FB_ADDR:
			framebuffer_address = value;
			break;
		case COMMAND_LIST_ADDR:
			command_list_address = value;
			break;
		}
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
}

static void opcode_lui(uint32_t instruction) {
	uint32_t immediate = instruction >> 12u;
	uint8_t  rd        = (instruction >> 7) & 0x1f;

	x[rd] = sign_extend64(immediate << 12u, 32);

	increment_pc();
}

static void opcode_auipc(uint32_t instruction) {
	uint32_t immediate = instruction >> 12u;
	uint8_t  rd        = (instruction >> 7) & 0x1f;

	x[rd] = pc + sign_extend64(immediate << 12u, 32);

	increment_pc();
}

static void opcode_addi_slti_sltiu_xori_ori_andi_slli_srli_srai(uint32_t instruction) {
	uint8_t  rs1       = (instruction >> 15) & 0x1f;
	uint8_t  rd        = (instruction >> 7) & 0x1f;
	uint16_t immediate = instruction >> 20;
	uint32_t shamt     = (instruction >> 20) & 0x3f;

	uint8_t command = (instruction >> 12) & 0x7;
	switch (command) {
	case 0x0: // addi
		x[rd] = x[rs1] + sign_extend64(immediate, 12);
		break;
	case 0x2: { // slti
		uint64_t immediate_value = sign_extend64(immediate, 12);

		int64_t rs1_value = *(int64_t *)&x[rs1];
		int64_t rs2_value = *(int64_t *)&immediate_value;

		x[rd] = (rs1_value < rs2_value) ? 1 : 0;
		break;
	}
	case 0x3: // sltiu
		x[rd] = (x[rs1] < sign_extend64(immediate, 12)) ? 1 : 0;
		break;
	case 0x4: // xori
		x[rd] = x[rs1] ^ sign_extend64(immediate, 12);
		break;
	case 0x6: // ori
		x[rd] = x[rs1] | sign_extend64(immediate, 12);
		break;
	case 0x7: // andi
		x[rd] = x[rs1] & sign_extend64(immediate, 12);
		break;
	case 0x1: // slli
		x[rd] = x[rs1] << shamt;
		break;
	case 0x5: { // srli_srai
		uint8_t upper = instruction >> 26;
		switch (upper) {
		case 0x00: // srli
			x[rd] = x[rs1] >> shamt;
			break;
		case 0x10: { // srai
			int64_t rs1_value = *(int64_t *)&x[rs1];
			x[rd]             = rs1_value >> shamt;
			break;
		}
		default:
			assert(false);
			break;
		}
		break;
	}
	default:
		assert(false);
		break;
	}

	increment_pc();
}

static void opcode_lb_lh_lw_lbu_lhu_lwu_ld(uint32_t instruction) {
	uint8_t  rs1    = (instruction >> 15) & 0x1f;
	uint8_t  rd     = (instruction >> 7) & 0x1f;
	uint16_t offset = instruction >> 20;

	uint8_t command = (instruction >> 12) & 0x7;

	switch (command) {
	case 0x0: { // lb
		uint8_t value = read_memory8(x[rs1] + sign_extend64(offset, 12));
		x[rd]         = sign_extend64(value, 8);
		break;
	}
	case 0x1: { // lh
		uint16_t value = read_memory16(x[rs1] + sign_extend64(offset, 12));
		x[rd]          = sign_extend64(value, 16);
		break;
	}
	case 0x2: { // lw
		uint32_t value = read_memory32(x[rs1] + sign_extend64(offset, 12));
		x[rd]          = sign_extend64(value, 32);
		break;
	}
	case 0x4: { // lbu
		uint64_t value = read_memory8(x[rs1] + sign_extend64(offset, 12));
		x[rd]          = value & 0xff;
		break;
	}
	case 0x5: { // lhu
		uint64_t value = read_memory16(x[rs1] + sign_extend64(offset, 12));
		x[rd]          = value & 0xffff;
		break;
	}
	case 0x6: { // lwu
		uint64_t value = read_memory32(x[rs1] + sign_extend64(offset, 12));
		x[rd]          = value & 0xffffffff;
		break;
	}
	case 0x3: { // ld
		uint64_t value = read_memory64(x[rs1] + sign_extend64(offset, 12));
		x[rd]          = value;
		break;
	}
	default:
		assert(false);
		break;
	}

	increment_pc();
}

static void opcode_addiw(uint32_t instruction) {
	uint8_t  rs1       = (instruction >> 15) & 0x1f;
	uint8_t  rd        = (instruction >> 7) & 0x1f;
	uint16_t immediate = instruction >> 20u;

	uint32_t rs1_value = (uint32_t)x[rs1];
	uint32_t result    = rs1_value + sign_extend32(immediate, 12);
	x[rd]              = sign_extend64(result, 32);

	increment_pc();
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
		store_memory8(x[rs1] + sign_extend64(immediate, 12), *(uint8_t *)&x[rs2]);
		break;
	case 0x1: // sh
		store_memory16(x[rs1] + sign_extend64(immediate, 12), *(uint16_t *)&x[rs2]);
		break;
	case 0x2: // sw
		store_memory32(x[rs1] + sign_extend64(immediate, 12), *(uint32_t *)&x[rs2]);
		break;
	case 0x3: // sd
		store_memory64(x[rs1] + sign_extend64(immediate, 12), x[rs2]);
		break;
	default:
		assert(false);
		break;
	}

	increment_pc();
}

static void opcode_jal(uint32_t instruction) {
	uint8_t rd = (instruction >> 7) & 0x1f;

	if (rd != 0) {
		x[rd] = pc + 4;
	}

	uint32_t immediate =
	    ((instruction >> 31) & 0x1) << 20 | ((instruction >> 21) & 0x3ff) << 1 | ((instruction >> 20) & 0x1) << 11 | ((instruction >> 12) & 0xff) << 12;

	pc += sign_extend64(immediate, 21);
	assert(pc != 0);
}

static void opcode_jalr(uint32_t instruction) {
	uint8_t  rs1       = (instruction >> 15) & 0x1f;
	uint8_t  rd        = (instruction >> 7) & 0x1f;
	uint16_t immediate = (instruction >> 20) & 0xfff;

	uint64_t t      = pc + 4;
	uint64_t nextpc = (x[rs1] + sign_extend64(immediate, 12)) & ~1;
	assert(nextpc != 0);
	pc = nextpc;

	if (rd != 0) {
		x[rd] = t;
	}
}

static void opcode_beq_bne_blt_bge_bltu_bgeu(uint32_t instruction) {
	uint8_t rs1 = (instruction >> 15) & 0x1f;
	uint8_t rs2 = (instruction >> 20) & 0x1f;

	uint8_t command = (instruction >> 12) & 0x7;

	bool branch = false;
	switch (command) {
	case 0x0: // beq
		branch = x[rs1] == x[rs2];
		break;
	case 0x1: // bne
		branch = x[rs1] != x[rs2];
		break;
	case 0x4: { // blt
		int64_t rs1_value = *(int64_t *)&x[rs1];
		int64_t rs2_value = *(int64_t *)&x[rs2];
		branch            = rs1_value < rs2_value;
		break;
	}
	case 0x5: { // bge
		int64_t rs1_value = *(int64_t *)&x[rs1];
		int64_t rs2_value = *(int64_t *)&x[rs2];
		branch            = rs1_value >= rs2_value;
		break;
	}
	case 0x6: // bltu
		branch = x[rs1] < x[rs2];
		break;
	case 0x7: // bgeu
		branch = x[rs1] >= x[rs2];
		break;
	default:
		assert(false);
		break;
	}

	if (branch) {
		uint32_t immediate =
		    (((instruction >> 31) & 0x1) << 12) | (((instruction >> 25) & 0x3f) << 5) | (((instruction >> 8) & 0xf) << 1) | (((instruction >> 7) & 0x1) << 11);
		pc += sign_extend64(immediate, 13);
		assert(pc != 0);
	}
	else {
		increment_pc();
	}
}

static void opcode_add_sub_sll_slt_sltu_xor_srl_sra_or_and_mul_mulh_mulhsu_mulhu_div_divu_rem_remu(uint32_t instruction) {
	uint8_t upper  = (instruction >> 25) & 0x7f;
	uint8_t middle = (instruction >> 12) & 0x7;

	uint8_t rs1 = (instruction >> 15) & 0x1f;
	uint8_t rs2 = (instruction >> 20) & 0x1f;
	uint8_t rd  = (instruction >> 7) & 0x1f;

	if (rd != 0) {
		switch (middle) {
		case 0x0: { // add_sub
			switch (upper) {
			case 0x00: // add
				x[rd] = x[rs1] + x[rs2];
				break;
			case 0x20: // sub
				x[rd] = x[rs1] - x[rs2];
				break;
			case 0x01: { // mul
				int64_t rs1_value = *(int64_t *)&x[rs1];
				int64_t rs2_value = *(int64_t *)&x[rs2];
				x[rd]             = rs1_value * rs2_value;
				break;
			}
			default:
				assert(false);
				break;
			}
			break;
		}
		case 0x1: // sll_mulh
			switch (upper) {
			case 0x00: // sll
				x[rd] = x[rs1] << x[rs2];
				break;
			case 0x01: // mulh
				assert(false);
				break;
			default:
				assert(false);
				break;
			}
			break;
		case 0x2: { // slt_mulhsu
			int64_t rs1_value = *(int64_t *)&x[rs1];
			int64_t rs2_value = *(int64_t *)&x[rs2];
			switch (upper) {
			case 0x00: // slt
				x[rd] = (rs1_value < rs2_value) ? 1u : 0u;
				break;
			case 0x01: // mulhsu
				assert(false);
				break;
			}
			break;
		}
		case 0x3: // sltu_mulhu
			switch (upper) {
			case 0x00: // sltu
				x[rd] = (x[rs1] < x[rs2]) ? 1u : 0u;
				break;
			case 0x01: // mulhu
				assert(false);
				break;
			}
			break;
		case 0x4: // xor_div
			switch (upper) {
			case 0x00: // xor
				x[rd] = x[rs1] ^ x[rs2];
				break;
			case 0x01: // div
				assert(false);
				break;
			}
			break;
		case 0x5: { // srl_sra_divu
			uint8_t upper = (instruction >> 25) & 0x7f;

			uint8_t rs2_value = x[rs2] & 0x1f;

			switch (upper) {
			case 0x00: // srl
				x[rd] = x[rs1] >> rs2_value;
				break;
			case 0x01: // divu
				x[rd] = x[rs1] / x[rs2];
				break;
			case 0x20: { // sra
				int64_t rs1_value = *(int64_t *)&x[rs1];
				int64_t result    = rs1_value >> rs2_value;
				x[rd]             = *(uint64_t *)&result;
				break;
			default:
				assert(false);
				break;
			}
			}
			break;
		}
		case 0x6: // or_rem
			switch (upper) {
			case 0x00: // or
				x[rd] = x[rs1] | x[rs2];
				break;
			case 0x01: // rem
				assert(false);
				break;
			}
			break;
		case 0x7: // and_remu
			switch (upper) {
			case 0x00: // and
				x[rd] = x[rs1] & x[rs2];
				break;
			case 0x01: // remu
				assert(false);
				break;
			}
			break;
		default:
			assert(false);
			break;
		}
	}

	increment_pc();
}

static void opcode_addw_subw_sllw_srlw_sraw(uint32_t instruction) {
	uint8_t middle = (instruction >> 12) & 0x7;

	uint8_t rs1 = (instruction >> 15) & 0x1f;
	uint8_t rs2 = (instruction >> 20) & 0x1f;
	uint8_t rd  = (instruction >> 7) & 0x1f;

	if (rd != 0) {
		switch (middle) {
		case 0x0: { // addw_subw
			uint8_t upper = (instruction >> 25) & 0x7f;

			switch (upper) {
			case 0x00: // addw
				x[rd] = sign_extend64((x[rs1] + x[rs2]) & 0xffffffff, 32);
				break;
			case 0x30: // subw
				x[rd] = sign_extend64((x[rs1] - x[rs2]) & 0xffffffff, 32);
				break;
			}
			break;
		}
		case 0x1: // sllw
			x[rd] = sign_extend64((x[rs1] << (x[rs2] & 0x1f)) & 0xffffffff, 32);
			break;
		case 0x5: { // srlw_sraw
			uint8_t upper = (instruction >> 25) & 0x7f;

			uint8_t rs2_value = x[rs2] & 0x1f;

			switch (upper) {
			case 0x0: // srlw
				x[rd] = sign_extend64((x[rs1] & 0xffffffff) >> rs2_value, 32);
				break;
			case 0x20: { // sraw
				uint64_t reg1      = x[rs1] & 0xffffffff;
				int32_t  rs1_value = *(int32_t *)&reg1;
				int32_t  result    = rs1_value >> rs2_value;
				x[rd]              = sign_extend64(result, 32);
				break;
			}
			}
			break;
		}
		default:
			assert(false);
			break;
		}
	}

	increment_pc();
}

static void opcode_flw(uint32_t instruction) {
	uint8_t  rs1    = (instruction >> 15) & 0x1f;
	uint8_t  rd     = (instruction >> 7) & 0x1f;
	uint16_t offset = instruction >> 20;
	uint8_t  middle = (instruction >> 12) & 0x7;

	switch (middle) {
	case 0x2: { // flw
		uint32_t memory_value = read_memory32(x[rs1] + sign_extend64(offset, 12));
		float    float_value;
		memcpy(&float_value, &memory_value, sizeof(uint32_t));
		f[rd] = (double)float_value;
		break;
	}
	default:
		assert(false);
		break;
	}

	increment_pc();
}

static void opcode_fsw(uint32_t instruction) {
	uint8_t middle = (instruction >> 12) & 0x7;

	uint8_t  offset0 = (instruction >> 7) & 0x1f;
	uint8_t  offset1 = instruction >> 25;
	uint16_t offset  = (offset1 << 5) | offset0;

	uint8_t rs1 = (instruction >> 15) & 0x1f;
	uint8_t rs2 = (instruction >> 20) & 0x1f;

	switch (middle) {
	case 0x0:
	case 0x5:
	case 0x6:
	case 0x7: { // vs<eew>_vs<nf>r
		uint8_t funct = (instruction >> 20) & 0x1f;
		switch (funct) {
		case 0x0: { // vs<eew>
			uint8_t vs3 = offset0;

			uint8_t mask = (instruction >> 25) & 0x1;
			assert(mask == 0x1);

			uint8_t width = (instruction >> 12) & 0x7;
			switch (width) {
			case 0x0: // 8 bit
				assert(false);
				break;
			case 0x5: // 16 bit
				assert(false);
				break;
			case 0x6: { // 32 bit
				assert(sew == 32);

				uint64_t base = x[rs1];

				for (uint16_t i = 0; i < vl; ++i) {
					*(uint32_t *)(&ram[base + 4 * i]) = v[vs3].values.u32[i];
				}

				break;
			}
			case 0x7: // 64 bit
				assert(sew == 64);

				uint64_t base = x[rs1];

				for (uint16_t i = 0; i < vl; ++i) {
					*(uint64_t *)(&ram[base + 8 * i]) = v[vs3].values.u64[i];
				}

				break;
			}

			break;
		}
		case 0x8: { // vs<nf>r
			assert(middle == 0x0);

			uint8_t nf;
			switch (instruction >> 29) {
			case 0x0:
				nf = 1;
				break;
			case 0x1:
				nf = 2;
				break;
			case 0x3:
				nf = 4;
				break;
			case 0x7:
				nf = 8;
				break;
			}

			uint8_t vs3 = (instruction >> 7) & 0x17;

			for (uint8_t reg = vs3; reg < vs3 + lmul; ++reg) {
				memcpy(&ram[rs1 + (reg - vs3)], &v[reg].values.u8[0], 128);
			}

			break;
		}
		default:
			assert(false);
			break;
		}

		break;
	}
	case 0x2: { // fsw
		float    float_value = (float)f[rs2];
		uint32_t value;
		memcpy(&value, &float_value, sizeof(uint32_t));
		store_memory32(x[rs1] + sign_extend64(offset, 12), value);
		break;
	}
	default:
		assert(false);
		break;
	}

	increment_pc();
}

static void opcode_fence_fencei(uint32_t instruction) {
	increment_pc();
}

static void opcode_csrrw_csrrs_csrrc_csrrwi_csrrsi_csrrci_ecall_ebreak_sret_mret_wfi_sfencevma(uint32_t instruction) {
	uint8_t middle = (instruction >> 12) & 0x7;

	switch (middle) {
	case 0x00: // ecall_ebreak_sret_mret_wfi_sfencevma
		assert(false);
		break;
	case 0x01: // csrrw
		assert(false);
		break;
	case 0x02: { // csrrs
		uint8_t  rs1 = (instruction >> 15) & 0x1f;
		uint8_t  rd  = (instruction >> 7) & 0x1f;
		uint16_t csr = instruction >> 20;

		assert(csr == 0xc22); // CSR_VLENB

		const uint64_t csr_vlenb = 128;

		x[rd] = csr_vlenb;

		break;
	}
	case 0x03: // csrrc
		assert(false);
		break;
	case 0x05: // csrrwi
		assert(false);
		break;
	case 0x06: // csrrsi
		assert(false);
		break;
	case 0x07: // csrrci
		assert(false);
		break;
	default:
		assert(false);
	}

	increment_pc();
}

static void opcode_vector(uint32_t instruction) {
	uint8_t funct3 = (instruction >> 12) & 0x7;

	switch (funct3) {
	case 0x2: // vmv
		assert(false);
		break;
	case 0x3: { // OPIVI
		uint8_t funct6 = instruction >> 26;
		switch (funct6) {
		case 0x17: { // vmerge_vmv
			uint8_t vs2  = (instruction >> 20) & 0x1f;
			uint8_t vd   = (instruction >> 7) & 0x1f;
			uint8_t imm  = (instruction >> 15) & 0x1f;
			uint8_t mask = (instruction >> 25) & 0x1;

			switch (sew) {
			case 8: {
				uint8_t value = (uint8_t)imm;

				for (uint16_t element = 0; element < vl; ++element) {
					if (mask == 1 || v0_bit(element)) {
						v[vd].values.u8[element] = value;
					}
					else {
						v[vd].values.u8[element] = v[vs2].values.u8[element];
					}
				}
				break;
			}
			case 32: {
				uint32_t value = (uint32_t)imm;

				for (uint16_t element = 0; element < vl; ++element) {
					if (mask == 1 || v0_bit(element)) {
						v[vd].values.u32[element] = value;
					}
					else {
						v[vd].values.u32[element] = v[vs2].values.u32[element];
					}
				}
				break;
			}
			case 64: {
				uint64_t value = (uint64_t)imm;

				for (uint16_t element = 0; element < vl; ++element) {
					if (mask == 1 || v0_bit(element)) {
						v[vd].values.u64[element] = value;
					}
					else {
						v[vd].values.u64[element] = v[vs2].values.u64[element];
					}
				}
				break;
			}
			default:
				assert(false);
				break;
			}

			break;
		}
		default:
			assert(false);
			break;
		}
		break;
	}
	case 0x4: { // vslideup_vslidedown_vmerge_vmv
		uint8_t funct6 = instruction >> 26;
		switch (funct6) {
		case 0x17: { // vmerge_vmv
			uint8_t rs1  = (instruction >> 15) & 0x1f;
			uint8_t vd   = (instruction >> 7) & 0x1f;
			uint8_t mask = (instruction >> 25) & 0x1;

			switch (sew) {
			case 8: {
				uint8_t value = (uint8_t)x[rs1];
				for (uint16_t element = 0; element < vl; ++element) {
					if (mask == 1 || v0_bit(element)) {
						v[vd].values.u8[element] = value;
					}
				}
				break;
			}
			case 32: {
				uint32_t value = (uint32_t)x[rs1];
				for (uint16_t element = 0; element < vl; ++element) {
					if (mask == 1 || v0_bit(element)) {
						v[vd].values.u32[element] = value;
					}
				}
				break;
			}
			default:
				assert(false);
				break;
			}

			break;
		}
		default:
			assert(false);
			break;
		}
		break;
	}
	case 0x6: { // OPMVX
		uint8_t funct6 = instruction >> 26;
		switch (funct6) {
		case 0x10: { // VRXUNARY0
			uint8_t vs2 = (instruction >> 20) & 0x1f;
			assert(vs2 == 0); // vmv.s.x

			uint8_t rs1 = (instruction >> 15) & 0x1f;
			uint8_t vd  = (instruction >> 7) & 0x1f;

			switch (sew) {
			case 32: {
				v[vd].values.u32[0] = (uint32_t)x[rs1];
				break;
			}
			default:
				assert(false);
				break;
			}
			break;
		}
		default:
			assert(false);
			break;
		}
		break;
	}
	case 0x7: { // vsetvli_vsetivli_vsetvl
		uint8_t upper = instruction >> 31;
		if (upper == 0) { // vsetvli
			uint16_t zimm = (instruction >> 20) & 0x7ff;
			uint8_t  rs1  = (instruction >> 15) & 0x1f;
			uint8_t  rd   = (instruction >> 7) & 0x1f;

			uint8_t vlmul = zimm & 0x7;
			uint8_t vsew  = (zimm >> 3) & 0x7;
			uint8_t vta   = (zimm >> 6) & 0x1;
			uint8_t vma   = (zimm >> 7) & 0x1;

			switch (vsew) {
			case 0x0:
				sew = 8;
				break;
			case 0x1:
				sew = 16;
				break;
			case 0x2:
				sew = 32;
				break;
			case 0x3:
				sew = 64;
				break;
			default:
				assert(false);
				break;
			}

			switch (vlmul) {
			case 0x0:
				lmul    = 1;
				lmuldiv = 1;
				break;
			case 0x1:
				lmul    = 2;
				lmuldiv = 1;
				break;
			case 0x2:
				lmul    = 4;
				lmuldiv = 1;
				break;
			case 0x3:
				lmul    = 8;
				lmuldiv = 1;
				break;
			case 0x5:
				lmul    = 1;
				lmuldiv = 8;
				break;
			case 0x6:
				lmul    = 1;
				lmuldiv = 4;
				break;
			case 0x7:
				lmul    = 1;
				lmuldiv = 2;
				break;
			default:
				assert(false);
				break;
			}

			uint16_t avl = 0;

			if (rs1 != 0) {
				avl = (uint16_t)x[rs1];
			}
			else if (rd != 0) {
				avl = ~0;
			}
			else {
				avl = vl;
			}

			uint16_t vlen  = 1024;
			uint16_t vlmax = (vlen / sew) * lmul / lmuldiv;
			vl             = min(avl, vlmax);

			if (rd != 0) {
				x[rd] = vl;
			}
		}
		else {
			if (((instruction >> 30) & 0x1) == 1) { // vsetivli
				uint16_t zimm = (instruction >> 20) & 0x3ff;
				uint8_t  uimm = (instruction >> 15) & 0x1f;
				uint8_t  rd   = (instruction >> 7) & 0x1f;

				uint8_t vlmul = zimm & 0x7;
				uint8_t vsew  = (zimm >> 3) & 0x7;
				uint8_t vta   = (zimm >> 6) & 0x1;
				uint8_t vma   = (zimm >> 7) & 0x1;

				switch (vsew) {
				case 0x0:
					sew = 8;
					break;
				case 0x1:
					sew = 16;
					break;
				case 0x2:
					sew = 32;
					break;
				case 0x3:
					sew = 64;
					break;
				default:
					assert(false);
					break;
				}

				switch (vlmul) {
				case 0x0:
					lmul    = 1;
					lmuldiv = 1;
					break;
				case 0x1:
					lmul    = 2;
					lmuldiv = 1;
					break;
				case 0x2:
					lmul    = 4;
					lmuldiv = 1;
					break;
				case 0x3:
					lmul    = 8;
					lmuldiv = 1;
					break;
				case 0x5:
					lmul    = 1;
					lmuldiv = 8;
					break;
				case 0x6:
					lmul    = 1;
					lmuldiv = 4;
					break;
				case 0x7:
					lmul    = 1;
					lmuldiv = 2;
					break;
				default:
					assert(false);
					break;
				}

				uint16_t avl = uimm;

				uint16_t vlen  = 1024;
				uint16_t vlmax = (vlen / sew) * lmul / lmuldiv;
				vl             = min(avl, vlmax);

				if (rd != 0) {
					x[rd] = vl;
				}
			}
			else { // vsetvl
				assert(false);
			}
		}

		break;
	}
	default:
		assert(false);
		break;
	}

	increment_pc();
}

static void opcode_not_implemented(uint32_t instruction) {
	assert(false);
	increment_pc();
}

opcode_func *opcodes[256] = {
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_lb_lh_lw_lbu_lhu_lwu_ld,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_flw,
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
    &opcode_auipc,
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
    &opcode_fsw,
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
    &opcode_add_sub_sll_slt_sltu_xor_srl_sra_or_and_mul_mulh_mulhsu_mulhu_div_divu_rem_remu,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_lui,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_addw_subw_sllw_srlw_sraw,
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
    &opcode_vector,
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
    &opcode_jalr,
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
    &opcode_csrrw_csrrs_csrrc_csrrwi_csrrsi_csrrci_ecall_ebreak_sret_mret_wfi_sfencevma,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
    &opcode_not_implemented,
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

			kore_log(KORE_LOG_LEVEL_INFO, "Setting up a memory area from 0x%x to 0x%x.", virtual_address, virtual_address + memory_size);

			memcpy(&ram[virtual_address], &binary[file_offset], file_size);
			memset(&ram[virtual_address + file_size], 0, memory_size - file_size);
		}
	}
}

static kore_gpu_device       device;
static kore_gpu_command_list list;
static kore_gpu_buffer       framebuffer_buffer;

static const int width  = 800;
static const int height = 600;

static void execute_command_list(void) {
	kompjuta_gpu_command *commands = (kompjuta_gpu_command *)&ram[command_list_address];

	for (uint32_t command_index = 0; command_index < command_list_size; ++command_index) {
		kompjuta_gpu_command *command = &commands[command_index];
		switch (command->kind) {
		case KOMPJUTA_GPU_COMMAND_CLEAR: {
			kore_gpu_texture *gpu_framebuffer = kore_gpu_device_get_framebuffer(&device);

			kore_gpu_color clear_color = {
			    .r = command->data.clear.r,
			    .g = command->data.clear.g,
			    .b = command->data.clear.b,
			    .a = command->data.clear.a,
			};

			kore_gpu_render_pass_parameters parameters = {
			    .color_attachments_count = 1,
			    .color_attachments =
			        {
			            {
			                .load_op     = KORE_GPU_LOAD_OP_CLEAR,
			                .clear_value = clear_color,
			                .texture =
			                    {
			                        .texture           = gpu_framebuffer,
			                        .array_layer_count = 1,
			                        .mip_level_count   = 1,
			                        .format            = kore_gpu_device_framebuffer_format(&device),
			                        .dimension         = KORE_GPU_TEXTURE_VIEW_DIMENSION_2D,
			                    },
			            },
			        },
			};
			kore_gpu_command_list_begin_render_pass(&list, &parameters);

			kore_gpu_command_list_end_render_pass(&list);
			break;
		}
		case KOMPJUTA_GPU_COMMAND_PRESENT:
			kore_gpu_command_list_present(&list);
			command_list_present = true;
			break;
		}
	}

	kore_gpu_device_execute_command_list(&device, &list);
}

static void update(void *data) {
	while (!framebuffer_present && !command_list_present) {
		execute_opcode();
	}

	if (framebuffer_present) {
		uint8_t *pixels        = (uint8_t *)kore_gpu_buffer_lock_all(&framebuffer_buffer);
		uint32_t buffer_stride = kore_gpu_device_align_texture_row_bytes(&device, framebuffer_width * 4);
		for (uint32_t y = 0; y < framebuffer_height; ++y) {
			memcpy(&pixels[buffer_stride * y], &ram[framebuffer_address + framebuffer_stride * y], framebuffer_width * 4);
		}
		kore_gpu_buffer_unlock(&framebuffer_buffer);

		kore_gpu_texture *gpu_framebuffer = kore_gpu_device_get_framebuffer(&device);

		kore_gpu_color clear_color = {
		    .r = 0.0f,
		    .g = 0.0f,
		    .b = 0.0f,
		    .a = 1.0f,
		};

		kore_gpu_render_pass_parameters parameters = {
		    .color_attachments_count = 1,
		    .color_attachments =
		        {
		            {
		                .load_op     = KORE_GPU_LOAD_OP_CLEAR,
		                .clear_value = clear_color,
		                .texture =
		                    {
		                        .texture           = gpu_framebuffer,
		                        .array_layer_count = 1,
		                        .mip_level_count   = 1,
		                        .format            = kore_gpu_device_framebuffer_format(&device),
		                        .dimension         = KORE_GPU_TEXTURE_VIEW_DIMENSION_2D,
		                    },
		            },
		        },
		};
		kore_gpu_command_list_begin_render_pass(&list, &parameters);

		kore_gpu_command_list_end_render_pass(&list);

		kore_gpu_image_copy_buffer copy_buffer = {
		    .buffer         = &framebuffer_buffer,
		    .bytes_per_row  = buffer_stride,
		    .offset         = 0,
		    .rows_per_image = framebuffer_height,
		};

		kore_gpu_image_copy_texture copy_texture = {
		    .texture   = gpu_framebuffer,
		    .origin_x  = 0,
		    .origin_y  = 0,
		    .origin_z  = 0,
		    .mip_level = 0,
		    .aspect    = KORE_GPU_IMAGE_COPY_ASPECT_ALL,
		};

		kore_gpu_command_list_copy_buffer_to_texture(&list, &copy_buffer, &copy_texture, framebuffer_width, framebuffer_height, 1);

		kore_gpu_command_list_present(&list);

		kore_gpu_device_execute_command_list(&device, &list);

		framebuffer_present = false;
	}

	command_list_present = false;
}

int kickstart(int argc, char **argv) {
	assert(argc == 2);
	FILE *file = fopen(argv[1], "rb");
	fseek(file, 0, SEEK_END);
	uint32_t size = ftell(file);
	fseek(file, 0, 0);

	uint8_t *binary = (uint8_t *)malloc(size);
	assert(binary != NULL);
	fread(binary, 1, size, file);
	fclose(file);

	read_header(binary);

	ram = malloc(MEMORY_SIZE);
	assert(ram != NULL);
	memset(ram, 0, MEMORY_SIZE);

	read_loadable_segments(binary);

	framebuffer_width   = width;
	framebuffer_height  = height;
	framebuffer_stride  = framebuffer_width * 4u;
	framebuffer_address = MEMORY_SIZE - framebuffer_stride * framebuffer_height;

	pc = entry;

	kore_init("Kompjuta", width, height, NULL, NULL);
	kore_set_update_callback(update, NULL);

	kore_gpu_device_wishlist wishlist = {0};
	kore_gpu_device_create(&device, &wishlist);

	kong_init(&device);

	kore_gpu_device_create_command_list(&device, KORE_GPU_COMMAND_LIST_TYPE_GRAPHICS, &list);

	while (!framebuffer_present && !command_list_present) {
		execute_opcode();
	}

	if (framebuffer_present) {
		kore_gpu_buffer_parameters parameters = {
		    .size        = kore_gpu_device_align_texture_row_bytes(&device, framebuffer_width * 4) * framebuffer_height,
		    .usage_flags = KORE_GPU_BUFFER_USAGE_CPU_WRITE | KORE_GPU_BUFFER_USAGE_COPY_SRC,
		};
		kore_gpu_device_create_buffer(&device, &parameters, &framebuffer_buffer);
	}

	kore_start();

	kore_gpu_command_list_destroy(&list);

	kore_gpu_device_destroy(&device);

	return 0;
}
