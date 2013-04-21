#include "gba-bios.h"

#include "gba.h"
#include "gba-memory.h"

#include <math.h>

static void _unLz77(struct GBAMemory* memory, uint32_t source, uint8_t* dest);

static void _CpuSet(struct GBA* gba) {
	uint32_t source = gba->cpu.gprs[0];
	uint32_t dest = gba->cpu.gprs[1];
	uint32_t mode = gba->cpu.gprs[2];
	int count = mode & 0x000FFFFF;
	int fill = mode & 0x01000000;
	int wordsize = (mode & 0x04000000) ? 4 : 2;
	int i;
	if (fill) {
		if (wordsize == 4) {
			source &= 0xFFFFFFFC;
			dest &= 0xFFFFFFFC;
			int32_t word = GBALoad32(&gba->memory.d, source);
			for (i = 0; i < count; ++i) {
				GBAStore32(&gba->memory.d, dest + (i << 2), word);
			}
		} else {
			source &= 0xFFFFFFFE;
			dest &= 0xFFFFFFFE;
			uint16_t word = GBALoad16(&gba->memory.d, source);
			for (i = 0; i < count; ++i) {
				GBAStore16(&gba->memory.d, dest + (i << 1), word);
			}
		}
	} else {
		if (wordsize == 4) {
			source &= 0xFFFFFFFC;
			dest &= 0xFFFFFFFC;
			for (i = 0; i < count; ++i) {
				int32_t word = GBALoad32(&gba->memory.d, source + (i << 2));
				GBAStore32(&gba->memory.d, dest + (i << 2), word);
			}
		} else {
			source &= 0xFFFFFFFE;
			dest &= 0xFFFFFFFE;
			for (i = 0; i < count; ++i) {
				uint16_t word = GBALoad16(&gba->memory.d, source + (i << 1));
				GBAStore16(&gba->memory.d, dest + (i << 1), word);
			}
		}
	}
}

static void _FastCpuSet(struct GBA* gba) {
	uint32_t source = gba->cpu.gprs[0] & 0xFFFFFFFC;
	uint32_t dest = gba->cpu.gprs[1] & 0xFFFFFFFC;
	uint32_t mode = gba->cpu.gprs[2];
	int count = mode & 0x000FFFFF;
	count = ((count + 7) >> 3) << 3;
	int i;
	if (mode & 0x01000000) {
		int32_t word = GBALoad32(&gba->memory.d, source);
		for (i = 0; i < count; ++i) {
			GBAStore32(&gba->memory.d, dest + (i << 2), word);
		}
	} else {
		for (i = 0; i < count; ++i) {
			int32_t word = GBALoad32(&gba->memory.d, source + (i << 2));
			GBAStore32(&gba->memory.d, dest + (i << 2), word);
		}
	}
}

static void _MidiKey2Freq(struct GBA* gba) {
	uint32_t key = GBALoad32(&gba->memory.d, gba->cpu.gprs[0] + 4);
	gba->cpu.gprs[0] = key / powf(2, (180.f - gba->cpu.gprs[1] - gba->cpu.gprs[2] / 256.f) / 12.f);
}

void GBASwi16(struct ARMBoard* board, int immediate) {
	struct GBA* gba = ((struct GBABoard*) board)->p;
	switch (immediate) {
	case 0x2:
		GBAHalt(gba);
		break;
	case 0xB:
		_CpuSet(gba);
		break;
	case 0xC:
		_FastCpuSet(gba);
		break;
	case 0x11:
		_unLz77(&gba->memory, gba->cpu.gprs[0], &((uint8_t*) gba->memory.wram)[(gba->cpu.gprs[1] & (SIZE_WORKING_RAM - 1))]);
		break;
	case 0x12:
		_unLz77(&gba->memory, gba->cpu.gprs[0], &((uint8_t*) gba->video.vram)[(gba->cpu.gprs[1] & (SIZE_VRAM - 1))]);
		break;
	case 0x1F:
		_MidiKey2Freq(gba);
		break;
	default:
		GBALog(GBA_LOG_STUB, "Stub software interrupt: %02x", immediate);
	}
}

void GBASwi32(struct ARMBoard* board, int immediate) {
	GBASwi32(board, immediate >> 16);
}

static void _unLz77(struct GBAMemory* memory, uint32_t source, uint8_t* dest) {
	int remaining = (GBALoad32(&memory->d, source) & 0xFFFFFF00) >> 8;
	// We assume the signature byte (0x10) is correct
	int blockheader;
	uint32_t sPointer = source + 4;
	uint8_t* dPointer = dest;
	int blocksRemaining = 0;
	int block;
	uint8_t* disp;
	int bytes;
	while (remaining > 0) {
		if (blocksRemaining) {
			if (blockheader & 0x80) {
				// Compressed
				block = GBALoadU8(&memory->d, sPointer) | (GBALoadU8(&memory->d, sPointer + 1) << 8);
				sPointer += 2;
				disp = dPointer - (((block & 0x000F) << 8) | ((block & 0xFF00) >> 8)) - 1;
				bytes = ((block & 0x00F0) >> 4) + 3;
				while (bytes-- && remaining) {
					--remaining;
					*dPointer = *disp;
					++disp;
					++dPointer;
				}
			} else {
				// Uncompressed
				*dPointer = GBALoadU8(&memory->d, sPointer++);
				++dPointer;
				--remaining;
			}
			blockheader <<= 1;
			--blocksRemaining;
		} else {
			blockheader = GBALoadU8(&memory->d, sPointer++);
			blocksRemaining = 8;
		}
	}
}