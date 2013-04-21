#include "gba-memory.h"

#include "gba-io.h"
#include "hle-bios.h"

#include <limits.h>
#include <string.h>
#include <sys/mman.h>

static const char* GBA_CANNOT_MMAP = "Could not map memory";

static void GBASetActiveRegion(struct ARMMemory* memory, uint32_t region);

static const char GBA_BASE_WAITSTATES[16] = { 0, 0, 2, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4 };
static const char GBA_BASE_WAITSTATES_SEQ[16] = { 0, 0, 2, 0, 0, 0, 0, 0, 2, 2, 4, 4, 8, 8, 4 };
static const char GBA_ROM_WAITSTATES[] = { 4, 3, 2, 8 };
static const char GBA_ROM_WAITSTATES_SEQ[] = { 2, 1, 4, 1, 8, 1 };
static const int DMA_OFFSET[] = { 1, -1, 0, 1 };

void GBAMemoryInit(struct GBAMemory* memory) {
	memory->d.load32 = GBALoad32;
	memory->d.load16 = GBALoad16;
	memory->d.loadU16 = GBALoadU16;
	memory->d.load8 = GBALoad8;
	memory->d.loadU8 = GBALoadU8;
	memory->d.store32 = GBAStore32;
	memory->d.store16 = GBAStore16;
	memory->d.store8 = GBAStore8;

	memory->bios = (uint32_t*) hleBios;
	memory->wram = mmap(0, SIZE_WORKING_RAM, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	memory->iwram = mmap(0, SIZE_WORKING_IRAM, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	memory->rom = 0;
	memset(memory->io, 0, sizeof(memory->io));
	memset(memory->dma, 0, sizeof(memory->dma));

	if (!memory->wram || !memory->iwram) {
		GBAMemoryDeinit(memory);
		memory->p->errno = GBA_OUT_OF_MEMORY;
		memory->p->errstr = GBA_CANNOT_MMAP;
	}

	int i;
	for (i = 0; i < 16; ++i) {
		memory->waitstates16[i] = GBA_BASE_WAITSTATES[i];
		memory->waitstatesSeq16[i] = GBA_BASE_WAITSTATES_SEQ[i];
		memory->waitstates32[i] = GBA_BASE_WAITSTATES[i] + GBA_BASE_WAITSTATES_SEQ[i] + 1;
		memory->waitstatesSeq32[i] = GBA_BASE_WAITSTATES_SEQ[i] + GBA_BASE_WAITSTATES_SEQ[i] + 1;
	}
	for (; i < 256; ++i) {
		memory->waitstates16[i] = 0;
		memory->waitstatesSeq16[i] = 0;
		memory->waitstates32[i] = 0;
		memory->waitstatesSeq32[i] = 0;
	}

	memory->activeRegion = 0;
	memory->d.activeRegion = 0;
	memory->d.activeMask = 0;
	memory->d.setActiveRegion = GBASetActiveRegion;
	memory->d.activePrefetchCycles32 = 0;
	memory->d.activePrefetchCycles16 = 0;
}

void GBAMemoryDeinit(struct GBAMemory* memory) {
	munmap(memory->wram, SIZE_WORKING_RAM);
	munmap(memory->iwram, SIZE_WORKING_IRAM);
}

static void GBASetActiveRegion(struct ARMMemory* memory, uint32_t address) {
	struct GBAMemory* gbaMemory = (struct GBAMemory*) memory;

	memory->activePrefetchCycles32 = gbaMemory->waitstates32[address >> BASE_OFFSET];
	memory->activePrefetchCycles16 = gbaMemory->waitstates16[address >> BASE_OFFSET];
	gbaMemory->activeRegion = address >> BASE_OFFSET;
	switch (address & ~OFFSET_MASK) {
	case BASE_BIOS:
		memory->activeRegion = gbaMemory->bios;
		memory->activeMask = SIZE_BIOS - 1;
		break;
	case BASE_WORKING_RAM:
		memory->activeRegion = gbaMemory->wram;
		memory->activeMask = SIZE_WORKING_RAM - 1;
		break;
	case BASE_WORKING_IRAM:
		memory->activeRegion = gbaMemory->iwram;
		memory->activeMask = SIZE_WORKING_IRAM - 1;
		break;
	case BASE_CART0:
	case BASE_CART0_EX:
	case BASE_CART1:
	case BASE_CART1_EX:
	case BASE_CART2:
	case BASE_CART2_EX:
		memory->activeRegion = gbaMemory->rom;
		memory->activeMask = SIZE_CART0 - 1;
		break;
	default:
		memory->activeRegion = 0;
		memory->activeMask = 0;
		break;
	}
}

int32_t GBALoad32(struct ARMMemory* memory, uint32_t address) {
	struct GBAMemory* gbaMemory = (struct GBAMemory*) memory;

	switch (address & ~OFFSET_MASK) {
	case BASE_BIOS:
		break;
	case BASE_WORKING_RAM:
		return gbaMemory->wram[(address & (SIZE_WORKING_RAM - 1)) >> 2];
	case BASE_WORKING_IRAM:
		return gbaMemory->iwram[(address & (SIZE_WORKING_IRAM - 1)) >> 2];
	case BASE_IO:
		return GBAIORead(gbaMemory->p, address & (SIZE_IO - 1)) | (GBAIORead(gbaMemory->p, (address & (SIZE_IO - 1)) | 2) << 16);
	case BASE_PALETTE_RAM:
		break;
	case BASE_VRAM:
		break;
	case BASE_OAM:
		break;
	case BASE_CART0:
	case BASE_CART0_EX:
	case BASE_CART1:
	case BASE_CART1_EX:
	case BASE_CART2:
	case BASE_CART2_EX:
		return gbaMemory->rom[(address & (SIZE_CART0 - 1)) >> 2];
	case BASE_CART_SRAM:
		break;
	default:
		break;
	}

	return 0;
}

int16_t GBALoad16(struct ARMMemory* memory, uint32_t address) {
	struct GBAMemory* gbaMemory = (struct GBAMemory*) memory;

	switch (address & ~OFFSET_MASK) {
	case BASE_BIOS:
		break;
	case BASE_WORKING_RAM:
		return ((int16_t*) gbaMemory->wram)[(address & (SIZE_WORKING_RAM - 1)) >> 1];
	case BASE_WORKING_IRAM:
		return ((int16_t*) gbaMemory->iwram)[(address & (SIZE_WORKING_IRAM - 1)) >> 1];
	case BASE_IO:
		return GBAIORead(gbaMemory->p, address & (SIZE_IO - 1));
	case BASE_PALETTE_RAM:
		break;
	case BASE_VRAM:
		break;
	case BASE_OAM:
		break;
	case BASE_CART0:
	case BASE_CART0_EX:
	case BASE_CART1:
	case BASE_CART1_EX:
	case BASE_CART2:
	case BASE_CART2_EX:
		return ((int16_t*) gbaMemory->rom)[(address & (SIZE_CART0 - 1)) >> 1];
	case BASE_CART_SRAM:
		break;
	default:
		break;
	}

	return 0;
}

uint16_t GBALoadU16(struct ARMMemory* memory, uint32_t address) {
	struct GBAMemory* gbaMemory = (struct GBAMemory*) memory;

	switch (address & ~OFFSET_MASK) {
	case BASE_BIOS:
		break;
	case BASE_WORKING_RAM:
		return ((uint16_t*) gbaMemory->wram)[(address & (SIZE_WORKING_RAM - 1)) >> 1];
	case BASE_WORKING_IRAM:
		return ((uint16_t*) gbaMemory->iwram)[(address & (SIZE_WORKING_IRAM - 1)) >> 1];
	case BASE_IO:
		return GBAIORead(gbaMemory->p, address & (SIZE_IO - 1));
	case BASE_PALETTE_RAM:
		break;
	case BASE_VRAM:
		break;
	case BASE_OAM:
		break;
	case BASE_CART0:
	case BASE_CART0_EX:
	case BASE_CART1:
	case BASE_CART1_EX:
	case BASE_CART2:
	case BASE_CART2_EX:
		return ((uint16_t*) gbaMemory->rom)[(address & (SIZE_CART0 - 1)) >> 1];
	case BASE_CART_SRAM:
		break;
	default:
		break;
	}

	return 0;
}

int8_t GBALoad8(struct ARMMemory* memory, uint32_t address) {
	struct GBAMemory* gbaMemory = (struct GBAMemory*) memory;

	switch (address & ~OFFSET_MASK) {
	case BASE_BIOS:
		break;
	case BASE_WORKING_RAM:
		return ((int8_t*) gbaMemory->wram)[address & (SIZE_WORKING_RAM - 1)];
	case BASE_WORKING_IRAM:
		return ((int8_t*) gbaMemory->iwram)[address & (SIZE_WORKING_IRAM - 1)];
	case BASE_IO:
		break;
	case BASE_PALETTE_RAM:
		break;
	case BASE_VRAM:
		break;
	case BASE_OAM:
		break;
	case BASE_CART0:
	case BASE_CART0_EX:
	case BASE_CART1:
	case BASE_CART1_EX:
	case BASE_CART2:
	case BASE_CART2_EX:
		return ((int8_t*) gbaMemory->rom)[address & (SIZE_CART0 - 1)];
	case BASE_CART_SRAM:
		break;
	default:
		break;
	}

	return 0;
}

uint8_t GBALoadU8(struct ARMMemory* memory, uint32_t address) {
	struct GBAMemory* gbaMemory = (struct GBAMemory*) memory;

	switch (address & ~OFFSET_MASK) {
	case BASE_BIOS:
		break;
	case BASE_WORKING_RAM:
		return ((uint8_t*) gbaMemory->wram)[address & (SIZE_WORKING_RAM - 1)];
		break;
	case BASE_WORKING_IRAM:
		return ((uint8_t*) gbaMemory->iwram)[address & (SIZE_WORKING_IRAM - 1)];
		break;
	case BASE_IO:
		return (GBAIORead(gbaMemory->p, address & 0xFFFE) >> ((address & 0x0001) << 3)) & 0xFF;
	case BASE_PALETTE_RAM:
		break;
	case BASE_VRAM:
		break;
	case BASE_OAM:
		break;
	case BASE_CART0:
	case BASE_CART0_EX:
	case BASE_CART1:
	case BASE_CART1_EX:
	case BASE_CART2:
	case BASE_CART2_EX:
		return ((uint8_t*) gbaMemory->rom)[address & (SIZE_CART0 - 1)];
	case BASE_CART_SRAM:
		break;
	default:
		break;
	}

	return 0;
}

void GBAStore32(struct ARMMemory* memory, uint32_t address, int32_t value) {
	struct GBAMemory* gbaMemory = (struct GBAMemory*) memory;

	switch (address & ~OFFSET_MASK) {
	case BASE_WORKING_RAM:
		gbaMemory->wram[(address & (SIZE_WORKING_RAM - 1)) >> 2] = value;
		break;
	case BASE_WORKING_IRAM:
		gbaMemory->iwram[(address & (SIZE_WORKING_IRAM - 1)) >> 2] = value;
		break;
	case BASE_IO:
		GBAIOWrite32(gbaMemory->p, address & (SIZE_IO - 1), value);
		break;
	case BASE_PALETTE_RAM:
		break;
	case BASE_VRAM:
		break;
	case BASE_OAM:
		break;
	case BASE_CART0:
		break;
	case BASE_CART2_EX:
		break;
	case BASE_CART_SRAM:
		break;
	default:
		break;
	}
}

void GBAStore16(struct ARMMemory* memory, uint32_t address, int16_t value) {
	struct GBAMemory* gbaMemory = (struct GBAMemory*) memory;

	switch (address & ~OFFSET_MASK) {
	case BASE_WORKING_RAM:
		((int16_t*) gbaMemory->wram)[(address & (SIZE_WORKING_RAM - 1)) >> 1] = value;
		break;
	case BASE_WORKING_IRAM:
		((int16_t*) gbaMemory->iwram)[(address & (SIZE_WORKING_IRAM - 1)) >> 1] = value;
		break;
	case BASE_IO:
		GBAIOWrite(gbaMemory->p, address & (SIZE_IO - 1), value);
		break;
	case BASE_PALETTE_RAM:
		break;
	case BASE_VRAM:
		break;
	case BASE_OAM:
		break;
	case BASE_CART0:
		break;
	case BASE_CART2_EX:
		break;
	case BASE_CART_SRAM:
		break;
	default:
		break;
	}
}

void GBAStore8(struct ARMMemory* memory, uint32_t address, int8_t value) {
	struct GBAMemory* gbaMemory = (struct GBAMemory*) memory;

	switch (address & ~OFFSET_MASK) {
	case BASE_WORKING_RAM:
		((int8_t*) gbaMemory->wram)[address & (SIZE_WORKING_RAM - 1)] = value;
		break;
	case BASE_WORKING_IRAM:
		((int8_t*) gbaMemory->iwram)[address & (SIZE_WORKING_IRAM - 1)] = value;
		break;
	case BASE_IO:
		break;
	case BASE_PALETTE_RAM:
		break;
	case BASE_VRAM:
		break;
	case BASE_OAM:
		break;
	case BASE_CART0:
		break;
	case BASE_CART2_EX:
		break;
	case BASE_CART_SRAM:
		break;
	default:
		break;
	}
}

void GBAAdjustWaitstates(struct GBAMemory* memory, uint16_t parameters) {
	int sram = parameters & 0x0003;
	int ws0 = (parameters & 0x000C) >> 2;
	int ws0seq = (parameters & 0x0010) >> 4;
	int ws1 = (parameters & 0x0060) >> 5;
	int ws1seq = (parameters & 0x0080) >> 7;
	int ws2 = (parameters & 0x0300) >> 8;
	int ws2seq = (parameters & 0x0400) >> 10;
	int prefetch = parameters & 0x4000;

	memory->waitstates16[REGION_CART_SRAM] =  GBA_ROM_WAITSTATES[sram];
	memory->waitstatesSeq16[REGION_CART_SRAM] = GBA_ROM_WAITSTATES[sram];
	memory->waitstates32[REGION_CART_SRAM] = 2 * GBA_ROM_WAITSTATES[sram] + 1;
	memory->waitstatesSeq32[REGION_CART_SRAM] = 2 * GBA_ROM_WAITSTATES[sram] + 1;

	memory->waitstates16[REGION_CART0] = memory->waitstates16[REGION_CART0_EX] = GBA_ROM_WAITSTATES[ws0];
	memory->waitstates16[REGION_CART1] = memory->waitstates16[REGION_CART1_EX] = GBA_ROM_WAITSTATES[ws1];
	memory->waitstates16[REGION_CART2] = memory->waitstates16[REGION_CART2_EX] = GBA_ROM_WAITSTATES[ws2];

	memory->waitstatesSeq16[REGION_CART0] = memory->waitstatesSeq16[REGION_CART0_EX] = GBA_ROM_WAITSTATES_SEQ[ws0seq];
	memory->waitstatesSeq16[REGION_CART1] = memory->waitstatesSeq16[REGION_CART1_EX] = GBA_ROM_WAITSTATES_SEQ[ws1seq + 2];
	memory->waitstatesSeq16[REGION_CART2] = memory->waitstatesSeq16[REGION_CART2_EX] = GBA_ROM_WAITSTATES_SEQ[ws2seq + 4];

	memory->waitstates32[REGION_CART0] = memory->waitstates32[REGION_CART0_EX] = memory->waitstates16[REGION_CART0] + 1 + memory->waitstatesSeq16[REGION_CART0];
	memory->waitstates32[REGION_CART1] = memory->waitstates32[REGION_CART1_EX] = memory->waitstates16[REGION_CART1] + 1 + memory->waitstatesSeq16[REGION_CART1];
	memory->waitstates32[REGION_CART2] = memory->waitstates32[REGION_CART2_EX] = memory->waitstates16[REGION_CART2] + 1 + memory->waitstatesSeq16[REGION_CART2];

	memory->waitstatesSeq32[REGION_CART0] = memory->waitstatesSeq32[REGION_CART0 + 1] = 2 * memory->waitstatesSeq16[REGION_CART0] + 1;
	memory->waitstatesSeq32[REGION_CART1] = memory->waitstatesSeq32[REGION_CART1 + 1] = 2 * memory->waitstatesSeq16[REGION_CART1] + 1;
	memory->waitstatesSeq32[REGION_CART2] = memory->waitstatesSeq32[REGION_CART2 + 1] = 2 * memory->waitstatesSeq16[REGION_CART2] + 1;

	memory->d.activePrefetchCycles32 = memory->waitstates32[memory->activeRegion];
	memory->d.activePrefetchCycles16 = memory->waitstates16[memory->activeRegion];
}

int32_t GBAMemoryProcessEvents(struct GBAMemory* memory, int32_t cycles) {
	struct GBADMA* dma;
	int32_t test = INT_MAX;

	dma = &memory->dma[0];
	dma->nextIRQ -= cycles;
	if (dma->enable && dma->doIrq && dma->nextIRQ) {
		if (dma->nextIRQ <= 0) {
			dma->nextIRQ = INT_MAX;
			GBARaiseIRQ(memory->p, IRQ_DMA0);
		} else if (dma->nextIRQ < test) {
			test = dma->nextIRQ;
		}
	}

	dma = &memory->dma[1];
	dma->nextIRQ -= cycles;
	if (dma->enable && dma->doIrq && dma->nextIRQ) {
		if (dma->nextIRQ <= 0) {
			dma->nextIRQ = INT_MAX;
			GBARaiseIRQ(memory->p, IRQ_DMA1);
		} else if (dma->nextIRQ < test) {
			test = dma->nextIRQ;
		}
	}

	dma = &memory->dma[2];
	dma->nextIRQ -= cycles;
	if (dma->enable && dma->doIrq && dma->nextIRQ) {
		if (dma->nextIRQ <= 0) {
			dma->nextIRQ = INT_MAX;
			GBARaiseIRQ(memory->p, IRQ_DMA2);
		} else if (dma->nextIRQ < test) {
			test = dma->nextIRQ;
		}
	}

	dma = &memory->dma[3];
	dma->nextIRQ -= cycles;
	if (dma->enable && dma->doIrq && dma->nextIRQ) {
		if (dma->nextIRQ <= 0) {
			dma->nextIRQ = INT_MAX;
			GBARaiseIRQ(memory->p, IRQ_DMA3);
		} else if (dma->nextIRQ < test) {
			test = dma->nextIRQ;
		}
	}

	return test;
}

void GBAMemoryWriteDMASAD(struct GBAMemory* memory, int dma, uint32_t address) {
	memory->dma[dma].source = address & 0xFFFFFFFE;
}

void GBAMemoryWriteDMADAD(struct GBAMemory* memory, int dma, uint32_t address) {
	memory->dma[dma].dest = address & 0xFFFFFFFE;
}

void GBAMemoryWriteDMACNT_LO(struct GBAMemory* memory, int dma, uint16_t count) {
	memory->dma[dma].count = count ? count : (dma == 3 ? 0x10000 : 0x4000);
}

uint16_t GBAMemoryWriteDMACNT_HI(struct GBAMemory* memory, int dma, uint16_t control) {
	struct GBADMA* currentDma = &memory->dma[dma];
	int wasEnabled = currentDma->enable;
	currentDma->packed = control;
	currentDma->nextIRQ = 0;

	if (currentDma->drq) {
		GBALog(GBA_LOG_STUB, "DRQ not implemented");
	}

	if (!wasEnabled && currentDma->enable) {
		currentDma->nextSource = currentDma->source;
		currentDma->nextDest = currentDma->dest;
		currentDma->nextCount = currentDma->count;
		GBAMemoryScheduleDMA(memory, dma, currentDma);
	}
	// If the DMA has already occurred, this value might have changed since the function started
	return currentDma->packed;
};

void GBAMemoryScheduleDMA(struct GBAMemory* memory, int number, struct GBADMA* info) {
	switch (info->timing) {
	case DMA_TIMING_NOW:
		GBAMemoryServiceDMA(memory, number, info);
		break;
	case DMA_TIMING_HBLANK:
		// Handled implicitly
		break;
	case DMA_TIMING_VBLANK:
		// Handled implicitly
		break;
	case DMA_TIMING_CUSTOM:
		switch (number) {
		case 0:
			GBALog(GBA_LOG_WARN, "Discarding invalid DMA0 scheduling");
			break;
		case 1:
		case 2:
			//this.cpu.irq.audio.scheduleFIFODma(number, info);
			break;
		case 3:
			//this.cpu.irq.video.scheduleVCaptureDma(dma, info);
			break;
		}
	}
}

void GBAMemoryRunHblankDMAs(struct GBAMemory* memory) {
	struct GBADMA* dma;
	int i;
	for (i = 0; i < 4; ++i) {
		dma = &memory->dma[i];
		if (dma->enable && dma->timing == DMA_TIMING_HBLANK) {
			GBAMemoryServiceDMA(memory, i, dma);
		}
	}
}

void GBAMemoryRunVblankDMAs(struct GBAMemory* memory) {
	struct GBADMA* dma;
	int i;
	for (i = 0; i < 4; ++i) {
		dma = &memory->dma[i];
		if (dma->enable && dma->timing == DMA_TIMING_VBLANK) {
			GBAMemoryServiceDMA(memory, i, dma);
		}
	}
}

void GBAMemoryServiceDMA(struct GBAMemory* memory, int number, struct GBADMA* info) {
	if (!info->enable) {
		// There was a DMA scheduled that got canceled
		return;
	}

	uint32_t width = info->width ? 4 : 2;
	int sourceOffset = DMA_OFFSET[info->srcControl] * width;
	int destOffset = DMA_OFFSET[info->dstControl] * width;
	int32_t wordsRemaining = info->nextCount;
	uint32_t source = info->nextSource;
	uint32_t dest = info->nextDest;
	uint32_t sourceRegion = source >> BASE_OFFSET;
	uint32_t destRegion = dest >> BASE_OFFSET;

	if (width == 4) {
		int32_t word;
		source &= 0xFFFFFFFC;
		dest &= 0xFFFFFFFC;
		while (wordsRemaining--) {
			word = GBALoad32(&memory->d, source);
			GBAStore32(&memory->d, dest, word);
			source += sourceOffset;
			dest += destOffset;
		}
	} else {
		uint16_t word;
		while (wordsRemaining--) {
			word = GBALoadU16(&memory->d, source);
			GBAStore16(&memory->d, dest, word);
			source += sourceOffset;
			dest += destOffset;
		}
	}

	if (info->doIrq) {
		info->nextIRQ = memory->p->cpu.cycles + 2;
		info->nextIRQ += (width == 4 ? memory->waitstates32[sourceRegion] + memory->waitstates32[destRegion]
		                            : memory->waitstates16[sourceRegion] + memory->waitstates16[destRegion]);
		info->nextIRQ += (info->count - 1) * (width == 4 ? memory->waitstatesSeq32[sourceRegion] + memory->waitstatesSeq32[destRegion]
		                                               : memory->waitstatesSeq16[sourceRegion] + memory->waitstatesSeq16[destRegion]);
	}

	info->nextSource = source;
	info->nextDest = dest;
	info->nextCount = wordsRemaining;

	if (!info->repeat) {
		info->enable = 0;

		// Clear the enable bit in memory
		memory->io[(REG_DMA0CNT_HI + number * (REG_DMA1CNT_HI - REG_DMA0CNT_HI)) >> 1] &= 0x7FE0;
	} else {
		info->nextCount = info->count;
		if (info->dstControl == DMA_INCREMENT_RELOAD) {
			info->nextDest = info->dest;
		}
		GBAMemoryScheduleDMA(memory, number, info);
	}
}