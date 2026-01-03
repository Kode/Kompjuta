#include <stdbool.h>
#include <stdint.h>

#define MMIO_BASE 0xffff0000

#define FB_ADDR 0x0
#define FB_STRIDE 0x08
#define FB_WIDTH 0x0c
#define FB_HEIGHT 0x10
#define FB_FORMAT 0x14
#define PRESENT 0x18

void _start(void) {
	uint8_t *mmio = (uint8_t *)MMIO_BASE;
	
	uint32_t width = 256;
	uint32_t height = 256;
	uint32_t stride = width * 4;

	uint32_t *width_pointer = (uint32_t *)&mmio[FB_WIDTH];
	uint32_t *height_pointer = (uint32_t *)&mmio[FB_HEIGHT];
	uint32_t *stride_pointer = (uint32_t *)&mmio[FB_STRIDE];

	*width_pointer = width;
	*height_pointer = height;
	*stride_pointer = stride;

	uint64_t framebuffer_address = 0x20000;
	uint8_t *framebuffer = (uint8_t *)framebuffer_address;
	uint64_t *framebuffer_address_pointer = (uint64_t *)&mmio[FB_ADDR];
	*framebuffer_address_pointer = framebuffer_address;

	while (true) {
		for (uint32_t y = 0; y < height; ++y) {
			for (uint32_t x = 0; x < width; ++x) {
				uint8_t *pixel = &framebuffer[stride * y + x * 4];
				pixel[0] = 255;
				pixel[1] = 0;
				pixel[2] = 0;
				pixel[3] = 255;
			}
		}

		uint8_t *present_pointer = &mmio[PRESENT];
		*present_pointer = 1;
	}
}
