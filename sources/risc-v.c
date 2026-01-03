unsigned char *ram, a = 0x00;
unsigned short pc = 0x0000;
typedef void opcode_func();
extern const opcode_func *opcodes[];

static void next_opcode() {
	opcodes[ram[pc++]]();
}

void opcode_nop() {
	next_opcode();
}

void opcode_lda() {
	a = ram[pc++];
	next_opcode();
}

const opcode_func *opcodes[256] = {
	&opcode_nop, &opcode_lda
};
