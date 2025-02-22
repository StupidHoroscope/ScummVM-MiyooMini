/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 * The PSG_HuC6280 class is based on the HuC6280 sound chip emulator
 * by Charles MacDonald (E-mail: cgfm2@hotmail.com, WWW: http://cgfm2.emuviews.com)
 * The implementation used here was taken from MESS (http://www.mess.org/)
 * the Multiple Emulator Super System (sound/c6280.c).
 * LFO and noise channel support have been removed (not used by Loom PCE).
 */

#include "scumm/players/player_pce.h"
#include "common/endian.h"

// PCE sound engine is only used by Loom, which requires 16bit color support
#ifdef USE_RGB_COLOR

namespace Scumm {

// CPU and PSG use the same base clock but with a different divider
const double MASTER_CLOCK = 21477270.0;      // ~21.48 MHz
const double CPU_CLOCK = MASTER_CLOCK / 3;   // ~7.16 MHz
const double PSG_CLOCK = MASTER_CLOCK / 6;   // ~3.58 MHz
const double TIMER_CLOCK = CPU_CLOCK / 1024; // ~6.9 kHz

// The PSG update routine is originally triggered by the timer IRQ (not by VSYNC)
// approx. 120 times per second (TIML=0x39). But as just every second call is used
// to update the PSG we will call the update routine approx. 60 times per second.
const double UPDATE_FREQ = TIMER_CLOCK / (57 + 1) / 2; // ~60 Hz

// $AFA5
static const byte wave_table[7][32] = {
	{ // sine
	0x10, 0x19, 0x1C, 0x1D, 0x1E, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,	0x1E, 0x1D, 0x1C, 0x19,
	0x10, 0x05, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00,	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x05,
	}, { // mw-shaped
	0x10, 0x1C, 0x1D, 0x1E,	0x1E, 0x1F, 0x1F, 0x1E, 0x1C, 0x1E, 0x1F, 0x1F, 0x1E, 0x1E, 0x1D, 0x1C,
	0x10, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0x01, 0x03, 0x01, 0x00, 0x00,	0x01, 0x01, 0x02, 0x03,
	}, { // square
	0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,	0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
	0x01, 0x01, 0x01, 0x01,	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	}, { // triangle
	0x10, 0x0C, 0x08, 0x04, 0x01, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C,	0x1F, 0x1C, 0x18, 0x14,
	0x10, 0x0C, 0x08, 0x04, 0x01, 0x04, 0x08, 0x0C,	0x10, 0x14, 0x18, 0x1C, 0x1F, 0x1C, 0x18, 0x14,
	}, { // saw-tooth
	0x00, 0x01, 0x02, 0x03,	0x04, 0x06, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,	0x1C, 0x1D, 0x1E, 0x1F,
	}, { // sigmoid
	0x07, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,	0x1F, 0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x19,
	0x08, 0x06, 0x05, 0x03,	0x02, 0x01, 0x00, 0x00, 0x0F, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x16,
	}, { // MW-shaped
	0x1F, 0x1E, 0x1D, 0x1D, 0x1C, 0x1A, 0x17, 0x0F, 0x0F, 0x17, 0x1A, 0x1C,	0x1D, 0x1D, 0x1E, 0x1F,
	0x00, 0x01, 0x02, 0x02, 0x03, 0x05, 0x08, 0x0F,	0x0F, 0x08, 0x05, 0x03, 0x02, 0x02, 0x01, 0x00
	}
};

// AEBC
static const int control_offsets[14] = {
	0, 7, 20, 33, 46, 56, 75, 88, 116, 126, 136, 152, 165, 181
};

// AED8
static const byte control_data[205] = {
	/*  0*/ 0xF0, 0x00, 0xF8, 0x01, 0x00, 0x00, 0xFF,
	/*  7*/ 0xF0, 0x00, 0xF8, 0x01, 0x00, 0x00, 0x01, 0x00, 0xF0, 0x3A, 0x00, 0xFD, 0xFF,
	/* 20*/ 0xF0, 0x00, 0xF8, 0x01,	0x00, 0x00, 0x01, 0x00, 0xF0, 0x1D, 0x00, 0xF8, 0xFF,
	/* 33*/ 0xF0, 0x00, 0xF8, 0x01, 0x00, 0x00, 0x02, 0x00, 0xF0, 0x1E, 0x00, 0xFC, 0xFF,
	/* 46*/ 0xF0, 0x00, 0xF8, 0x01, 0x00, 0x00, 0x07, 0x00, 0xE0, 0xFF,
	/* 56*/ 0xF0, 0x00, 0xF8, 0x01,	0x00, 0x00, 0x01, 0x00, 0xD8, 0xF0, 0x00, 0xD8, 0x01, 0x00, 0x00, 0x04,	0x00, 0xF0, 0xFF,
	/* 75*/ 0xF0, 0x00, 0xF8, 0x01, 0x00, 0x00, 0x03, 0x00, 0xF8, 0x6E, 0x00, 0xFF, 0xFF,
	/* 88*/ 0xF0, 0x00, 0xF8, 0x01, 0x00, 0x00, 0x08, 0x00,	0xF0, 0xF0, 0x00, 0xD0, 0x01, 0x00, 0x00, 0x05, 0x00, 0xF0, 0xF0, 0x00,	0xB8, 0xE6, 0x80, 0xFF, 0xE6, 0x80, 0xFF, 0xFF,
	/*116*/ 0xF0, 0x00, 0xF8, 0x01,	0x00, 0x00, 0x05, 0x00, 0xD0, 0xFF,
	/*126*/ 0xF0, 0x00, 0xF8, 0x01, 0x00, 0x00, 0x04, 0x00, 0xF8, 0xFF,
	/*136*/ 0xF0, 0x00, 0xF8, 0x01, 0x00, 0x00, 0x03, 0x00,	0xF4, 0xE6, 0xC0, 0xFF, 0xE6, 0xC0, 0xFF, 0xFF,
	/*152*/ 0xF0, 0x00, 0xD0, 0x01,	0x00, 0x00, 0x02, 0x00, 0x10, 0x0E, 0x00, 0xFE, 0xFF,
	/*165*/ 0xF0, 0x00, 0xA8, 0x01, 0x00, 0x00, 0x18, 0x00, 0x02, 0xE6, 0x80, 0xFE, 0xE6, 0xC0, 0xFF, 0xFF,
	/*181*/ 0xF0, 0x00, 0xF8, 0x01, 0x00, 0x00, 0x02, 0x00, 0xF8, 0x02, 0x00, 0x00, 0x02, 0x00, 0xF0, 0x01, 0x00, 0xF8, 0x02, 0x00, 0x08, 0xF1, 0x99, 0xAF
};

static const uint16 lookup_table[87] = {
	0x0D40, 0x0C80, 0x0BC0, 0x0B20, 0x0A80, 0x09E0, 0x0940, 0x08C0,
	0x0840, 0x07E0, 0x0760, 0x0700, 0x06A0, 0x0640, 0x05E0, 0x0590,
	0x0540, 0x04F0, 0x04A0, 0x0460, 0x0420, 0x03F0, 0x03B0, 0x0380,
	0x0350, 0x0320, 0x02F0, 0x02C8, 0x02A0, 0x0278, 0x0250, 0x0230,
	0x0210, 0x01F8, 0x01D8, 0x01C0, 0x01A8, 0x0190, 0x0178, 0x0164,
	0x0150, 0x013C, 0x0128, 0x0118, 0x0108, 0x00FC, 0x00EC, 0x00E0,
	0x00D4, 0x00C8, 0x00BC, 0x00B2, 0x00A8, 0x009E, 0x0094, 0x008C,
	0x0084, 0x007E, 0x0076, 0x0070, 0x006A, 0x0064, 0x005E, 0x0059,
	0x0054, 0x004F, 0x004A, 0x0046, 0x0042, 0x003F, 0x003B, 0x0038,
	0x0035, 0x0032, 0x0030, 0x002D, 0x002A, 0x0028, 0x0026, 0x0024,
	0x0022, 0x0020, 0x001E, 0x001C, 0x001B, 0x8E82, 0xB500
};

// B27B
static const uint16 freq_offset[3] = {
	0, 2, 9
};

static const uint16 freq_table[] = {
	0x0000, 0x0800,
	0xFFB0, 0xFFD1, 0xFFE8, 0xFFF1, 0x0005, 0x0000, 0x0800,
	0xFF9C, 0xFFD8, 0x0000, 0x000F, 0x0005, 0x0000, 0x0800
};

static const int sound_table[13] = {
	0, 2, 3, 4, 5, 6, 7, 8, 9, 11, 1, 10, 11
};

// 0xAE12
// Note:
// - offsets relative to data_table
// - byte one of each sound was always 0x3F (= use all channels) -> removed from table
static const uint16 sounds[13][6] = {
	{ 481, 481, 481, 481, 481, 481 },
	{ 395, 408, 467, 480, 480, 480 },
	{  85,  96, 109, 109, 109, 109 },
	{ 110, 121, 134, 134, 134, 134 },
	{ 135, 146, 159, 159, 159, 159 },
	{ 160, 171, 184, 184, 184, 184 },
	{ 185, 196, 209, 209, 209, 209 },
	{ 210, 221, 234, 234, 234, 234 },
	{ 235, 246, 259, 259, 259, 259 },
	{ 260, 271, 284, 284, 284, 284 },
	{ 285, 298, 311, 324, 335, 348 },
	{ 349, 360, 361, 362, 373, 384 },
	{   0,  84,  84,  84,  84,  84 } // unused
};

// 0xB2A1
static const byte data_table[482] = {
	/*  0*/ 0xE2, 0x0A, 0xE1, 0x0D, 0xE6, 0xED, 0xE0, 0x0F, 0xE2, 0x00, 0xE1, 0x00,
			0xF2, 0xF2, 0xB2, 0xE1, 0x01, 0xF2, 0xF2, 0xB2, 0xE1, 0x02, 0xF2, 0xF2,
			0xB2, 0xE1, 0x03, 0xF2, 0xF2, 0xB2, 0xE1, 0x04, 0xF2, 0xF2, 0xB2, 0xE1,
			0x05, 0xF2, 0xF2, 0xB2, 0xE1, 0x06, 0xF2, 0xF2, 0xB2, 0xE1, 0x07, 0xF2,
			0xF2, 0xB2, 0xE1, 0x08, 0xF2, 0xF2, 0xB2, 0xE1, 0x09, 0xF2, 0xF2, 0xB2,
			0xE1, 0x0A, 0xF2, 0xF2, 0xB2, 0xE1, 0x0B, 0xF2, 0xF2, 0xB2, 0xE1, 0x0C,
			0xF2, 0xF2, 0xB2, 0xE1, 0x0D, 0xF2, 0xF2, 0xB2, 0xFF, 0xD1, 0x03, 0xF3,
	/* 84*/ 0xFF,

	/* 85*/	0xE2, 0x0C, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD3, 0x07, 0xFF,
	/* 96*/ 0xE2, 0x06, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD4, 0xF0, 0x0C, 0x07, 0xFF,
	/*109*/ 0xFF,

	/*110*/	0xE2, 0x0C, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD3, 0x27, 0xFF,
	/*121*/ 0xE2, 0x06, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD4, 0xF0, 0x0C, 0x27, 0xFF,
	/*134*/ 0xFF,

	/*135*/	0xE2, 0x0C, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD3, 0x47, 0xFF,
	/*146*/	0xE2, 0x06, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD4, 0xF0, 0x0C, 0x47, 0xFF,
	/*159*/ 0xFF,

	/*160*/	0xE2, 0x0C, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD3, 0x57, 0xFF,
	/*171*/ 0xE2, 0x06, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD4, 0xF0, 0x0C, 0x57, 0xFF,
	/*184*/ 0xFF,

	/*185*/	0xE2, 0x0C, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD3, 0x77, 0xFF,
	/*196*/ 0xE2, 0x06, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD4, 0xF0, 0x0C, 0x77, 0xFF,
	/*209*/ 0xFF,

	/*210*/	0xE2, 0x0C, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD3, 0x97, 0xFF,
	/*221*/ 0xE2, 0x06, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD4, 0xF0, 0x0C, 0x97, 0xFF,
	/*234*/ 0xFF,

	/*235*/	0xE2, 0x0C, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD3, 0xB7, 0xFF,
	/*246*/ 0xE2, 0x06, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD4, 0xF0, 0x0C, 0xB7, 0xFF,
	/*259*/ 0xFF,

	/*260*/ 0xE2, 0x0C, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD4, 0x07, 0xFF,
	/*271*/ 0xE2, 0x06, 0xE1, 0x02, 0xE0, 0x0F, 0xE6, 0xED, 0xD5, 0xF0, 0x0C, 0x07, 0xFF,
	/*284*/ 0xFF,

	/*285*/ 0xE2, 0x0B, 0xE1, 0x04, 0xE6, 0xED, 0xE0, 0x0A, 0xD0, 0xDB, 0x14, 0x0E, 0xFF,
	/*298*/ 0xE2, 0x0B, 0xE1, 0x04, 0xE6, 0xED, 0xE0, 0x0A, 0xD0, 0xDB, 0x32, 0x1E, 0xFF,
	/*311*/ 0xE2, 0x0B, 0xE1, 0x0B, 0xE6, 0xED, 0xE0, 0x0A, 0xD0, 0xDB, 0x50, 0x1E, 0xFF,
	/*324*/ 0xE2, 0x0B, 0xE1, 0x0B, 0xE6, 0xED, 0xE0, 0x0A, 0xD0, 0x0E, 0xFF,
	/*335*/ 0xE2, 0x0B, 0xE1, 0x02, 0xE6, 0xED, 0xE0, 0x0A, 0xD0, 0xDB, 0x0A, 0x0E, 0xFF,
	/*348*/ 0xFF,

	/*349*/	0xE2, 0x03, 0xE1, 0x01, 0xE6, 0xED, 0xE0, 0x06, 0xD6, 0x17, 0xFF,
	/*360*/ 0xFF,
	/*361*/ 0xFF,
	/*362*/ 0xE2, 0x04, 0xE1, 0x04, 0xE6, 0xED, 0xE0, 0x06, 0xD5, 0xA7, 0xFF,
	/*373*/ 0xE2, 0x03, 0xE1, 0x06, 0xE6, 0xED, 0xE0, 0x06, 0xD6, 0x37, 0xFF,
	/*384*/ 0xE2, 0x04, 0xE1, 0x06, 0xE6, 0xED, 0xE0, 0x06, 0xD3, 0x87, 0xFF,

	/*395*/	0xE2, 0x0C, 0xE1, 0x00, 0xE0, 0x04, 0xE6, 0xED, 0xD4, 0x0B, 0xE8, 0x0B, 0xFF,
	/*408*/ 0xE2, 0x0C, 0xE1, 0x03, 0xE0, 0x04, 0xE6, 0xED, 0xD4, 0xF0, 0x0C, 0x00,
			0xE8, 0x10, 0xE8, 0x00, 0xE8, 0x10, 0xE8, 0x00, 0xE8, 0x10, 0xE8, 0x00,
			0xE8, 0x10, 0xE8, 0x00, 0xE8, 0x10, 0xE8, 0x00, 0xE8, 0x10, 0xE8, 0x00,
			0xE8, 0x10, 0xE8, 0x00, 0xE8, 0x10, 0xE8, 0x00, 0xE8, 0x10, 0xE8, 0x00,
			0xE8, 0x10, 0xE8, 0x00, 0xE8, 0x10, 0xE8, 0x00, 0xE8, 0x10, 0xFF,
	/*467*/ 0xE2, 0x0C, 0xE1, 0x00, 0xE0, 0x04, 0xE6, 0xED, 0xD4, 0x1B, 0xE8, 0x1B, 0xFF,
	/*480*/ 0xFF,

	/*481*/	0xFF
};


/*
 * PSG_HuC6280
 */

class PSG_HuC6280 {
private:
	typedef struct {
		uint16 frequency;
		uint8 control;
		uint8 balance;
		uint8 waveform[32];
		uint8 index;
		int16 dda;
		uint32 counter;
	} channel_t;

	double _clock;
	double _rate;
	uint8 _select;
	uint8 _balance;
	channel_t _channel[8];
	int16 _volumeTable[32];
	uint32 _noiseFreqTable[32];
	uint32 _waveFreqTable[4096];

public:
	void init();
	void reset();
	void write(int offset, byte data);
	void update(int16* samples, int sampleCnt);

	PSG_HuC6280(double clock, double samplerate);
};

PSG_HuC6280::PSG_HuC6280(double clock, double samplerate) {
	_clock = clock;
	_rate = samplerate;

	// Initialize PSG_HuC6280 emulator
	init();
}

void PSG_HuC6280::init() {
	int i;
	double step;

	// Loudest volume level for table
	double level = 65535.0 / 6.0 / 32.0;

	// Clear context
	reset();

	// Make waveform frequency table
	for (i = 0; i < 4096; i++) {
		step = ((_clock / _rate) * 4096) / (i+1);
		_waveFreqTable[(1 + i) & 0xFFF] = (uint32)step;
	}

	// Make noise frequency table
	for (i = 0; i < 32; i++) {
		step = ((_clock / _rate) * 32) / (i+1);
		_noiseFreqTable[i] = (uint32)step;
	}

	// Make volume table
	// PSG_HuC6280 has 48dB volume range spread over 32 steps
	step = 48.0 / 32.0;
	for (i = 0; i < 31; i++) {
		_volumeTable[i] = (uint16)level;
		level /= pow(10.0, step / 20.0);
	}
	_volumeTable[31] = 0;
}

void PSG_HuC6280::reset() {
	_select = 0;
	_balance = 0xFF;
	memset(_channel, 0, sizeof(_channel));
	memset(_volumeTable, 0, sizeof(_volumeTable));
	memset(_noiseFreqTable, 0, sizeof(_noiseFreqTable));
	memset(_waveFreqTable, 0, sizeof(_waveFreqTable));
}

void PSG_HuC6280::write(int offset, byte data) {
	channel_t *chan = &_channel[_select];

	switch(offset & 0x0F) {
	case 0x00: // Channel select
		_select = data & 0x07;
		break;

	case 0x01: // Global balance
		_balance  = data;
		break;

	case 0x02: // Channel frequency (LSB)
		chan->frequency = (chan->frequency & 0x0F00) | data;
		chan->frequency &= 0x0FFF;
		break;

	case 0x03: // Channel frequency (MSB)
		chan->frequency = (chan->frequency & 0x00FF) | (data << 8);
		chan->frequency &= 0x0FFF;
		break;

	case 0x04: // Channel control (key-on, DDA mode, volume)
		// 1-to-0 transition of DDA bit resets waveform index
		if ((chan->control & 0x40) && ((data & 0x40) == 0)) {
			chan->index = 0;
		}
		chan->control = data;
		break;

	case 0x05: // Channel balance
		chan->balance = data;
		break;

	case 0x06: // Channel waveform data
		switch(chan->control & 0xC0) {
		case 0x00:
			chan->waveform[chan->index & 0x1F] = data & 0x1F;
			chan->index = (chan->index + 1) & 0x1F;
			break;

		case 0x40:
		default:
			break;

		case 0x80:
			chan->waveform[chan->index & 0x1F] = data & 0x1F;
			chan->index = (chan->index + 1) & 0x1F;
			break;

		case 0xC0:
			chan->dda = data & 0x1F;
			break;
		}

		break;

	case 0x07: // Noise control (enable, frequency)
	case 0x08: // LFO frequency
	case 0x09: // LFO control (enable, mode)
		break;

	default:
		break;
	}
}

void PSG_HuC6280::update(int16* samples, int sampleCnt) {
	static const int scale_tab[] = {
		0x00, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F,
		0x10, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x1F
	};
	int ch;
	int i;

	int lmal = (_balance >> 4) & 0x0F;
	int rmal = (_balance >> 0) & 0x0F;
	int vll, vlr;

	lmal = scale_tab[lmal];
	rmal = scale_tab[rmal];

	// Clear buffer
	memset(samples, 0, 2 * sampleCnt * sizeof(int16));

	for (ch = 0; ch < 6; ch++) {
		// Only look at enabled channels
		if (_channel[ch].control & 0x80) {
			int lal = (_channel[ch].balance >> 4) & 0x0F;
			int ral = (_channel[ch].balance >> 0) & 0x0F;
			int al  = _channel[ch].control & 0x1F;

			lal = scale_tab[lal];
			ral = scale_tab[ral];

			// Calculate volume just as the patent says
			vll = (0x1F - lal) + (0x1F - al) + (0x1F - lmal);
			if (vll > 0x1F) vll = 0x1F;

			vlr = (0x1F - ral) + (0x1F - al) + (0x1F - rmal);
			if (vlr > 0x1F) vlr = 0x1F;

			vll = _volumeTable[vll];
			vlr = _volumeTable[vlr];

			// Check channel mode
			if (_channel[ch].control & 0x40) {
				/* DDA mode */
				for (i = 0; i < sampleCnt; i++) {
					samples[2*i]     += (int16)(vll * (_channel[ch].dda - 16));
					samples[2*i + 1] += (int16)(vlr * (_channel[ch].dda - 16));
				}
			} else {
				/* Waveform mode */
				uint32 step = _waveFreqTable[_channel[ch].frequency];
				for (i = 0; i < sampleCnt; i += 1) {
					int offset;
					int16 data;
					offset = (_channel[ch].counter >> 12) & 0x1F;
					_channel[ch].counter += step;
					_channel[ch].counter &= 0x1FFFF;
					data = _channel[ch].waveform[offset];
					samples[2*i]     += (int16)(vll * (data - 16));
					samples[2*i + 1] += (int16)(vlr * (data - 16));
				}
			}
		}
	}
}


/*
 * Player_PCE
 */

void Player_PCE::PSG_Write(int reg, byte data) {
	_psg->write(reg, data);
}

void Player_PCE::setupWaveform(byte bank) {
	const byte *ptr = wave_table[bank];
	PSG_Write(4, 0x40);
	PSG_Write(4, 0x00);
	for (int i = 0; i < 32; ++i) {
		PSG_Write(6, ptr[i]);
	}
}

// A541
void Player_PCE::procA541(channel_t *channel) {
	channel->soundDataPtr = nullptr;
	channel->controlVecShort10 = 0;

	channel->controlVecShort03 = 0;
	channel->controlVecShort06 = 0;
	channel->controlVec8 = 0;
	channel->controlVec9 = 0;
	channel->controlVec10 = 0;
	channel->soundUpdateCounter = 0;
	channel->controlVec18 = 0;
	channel->controlVec19 = 0;
	channel->controlVec23 = false;
	channel->controlVec24 = false;
	channel->controlVec21 = 0;

	channel->waveformCtrl = 0x80;
}

// A592
void Player_PCE::startSound(int sound) {
	channel_t *channel;
	const uint16 *ptr = sounds[sound_table[sound]];

	for (int i = 0; i < 6; ++i) {
		channel = &channels[i];
		procA541(channel);

		channel->controlVec24 = true;
		channel->waveformCtrl = 0;
		channel->controlVec0 = 0;
		channel->controlVec19 = 0;
		channel->controlVec18 = 0;
		channel->soundDataPtr = &data_table[*ptr++];
	}
}

// A64B
void Player_PCE::updateSound() {
	for (int i = 0; i < 12; i++) {
		channel_t *channel = &channels[i];
		bool cond = true;
		if (i < 6) {
			channel->controlVec21 ^= 0xFF;
			if (!channel->controlVec21)
				cond = false;
		}
		if (cond) {
			processSoundData(channel);
			procAB7F(channel);
			procAC24(channel);
			channel->controlVec11 = (channel->controlVecShort10 >> 11) | 0x80;
			channel->balance = channel->balance2;
		}
	}

	for (int i = 0; i < 6; ++i) {
		procA731(&channels[i]);
	}
}

int Player_PCE::readBuffer(int16 *buffer, const int numSamples) {
	int sampleCopyCnt;
	int samplesLeft = numSamples;
	int16 *sampleBufferPtr = _sampleBuffer;

	Common::StackLock lock(_mutex);

	while (true) {
		// copy samples to output buffer
		sampleCopyCnt = (samplesLeft < _sampleBufferCnt) ? samplesLeft : _sampleBufferCnt;
		if (sampleCopyCnt > 0) {
			memcpy(buffer, sampleBufferPtr, sampleCopyCnt * sizeof(int16));
			buffer += sampleCopyCnt;
			samplesLeft -= sampleCopyCnt;
			_sampleBufferCnt -= sampleCopyCnt;
			sampleBufferPtr += sampleCopyCnt;
		}

		if (samplesLeft == 0)
			break;

		// retrieve samples for one timer period
		updateSound();
		_psg->update(_sampleBuffer, _samplesPerPeriod / 2);
		_sampleBufferCnt = _samplesPerPeriod;
		sampleBufferPtr = _sampleBuffer;
	}

	// copy remaining samples to the front of the buffer
	if (_sampleBufferCnt > 0) {
		memmove(_sampleBuffer,
			sampleBufferPtr,
			_sampleBufferCnt * sizeof(int16));
	}

	return numSamples;
}

void Player_PCE::procA731(channel_t *channel) {
	PSG_Write(0, channel->id);
	PSG_Write(2, channel->freq & 0xFF);
	PSG_Write(3, (channel->freq >> 8) & 0xFF);

	int tmp = channel->controlVec11;
	if ((channel->controlVec11 & 0xC0) == 0x80) {
		tmp = channel->controlVec11 & 0x1F;
		if (tmp != 0) {
			tmp -= channel->controlVec0;
			if (tmp >= 0) {
				tmp |= 0x80;
			} else {
				tmp = 0;
			}
		}
	}

	PSG_Write(5, channel->balance);
	if ((channel->waveformCtrl & 0x80) == 0) {
		channel->waveformCtrl |= 0x80;
		PSG_Write(0, channel->id);
		setupWaveform(channel->waveformCtrl & 0x7F);
	}

	PSG_Write(4, tmp);
}

// A793
void Player_PCE::processSoundData(channel_t *channel) {
	channel->soundUpdateCounter--;
	if (channel->soundUpdateCounter > 0) {
		return;
	}

	while (true) {
		const byte *ptr = channel->soundDataPtr;
		byte value = (ptr ? *ptr++ : 0xFF);
		if (value < 0xD0) {
			int mult = (value & 0x0F) + 1;
			channel->soundUpdateCounter = mult * channel->controlVec1;
			value >>= 4;
			procAA62(channel, value);
			channel->soundDataPtr = ptr;
			return;
		}

		// jump_table (A7F7)
		switch (value - 0xD0) {
		case 0: /*A85A*/
		case 1: /*A85D*/
		case 2: /*A861*/
		case 3: /*A865*/
		case 4: /*A869*/
		case 5: /*A86D*/
		case 6: /*A871*/
			channel->controlVec2 = (value - 0xD0) * 12;
			break;
		case 11: /*A8A8*/
			channel->controlVecShort06 = (int8)*ptr++;
			break;
		case 16: /*A8C2*/
			channel->controlVec1 = *ptr++;
			break;
		case 17: /*A8CA*/
			channel->waveformCtrl = *ptr++;
			break;
		case 18: /*A8D2*/
			channel->controlVec10 = *ptr++;
			break;
		case 22: /*A8F2*/
			value = *ptr;
			channel->balance = value;
			channel->balance2 = value;
			ptr++;
			break;
		case 24: /*A905*/
			channel->controlVec23 = true;
			break;
		case 32: /*A921*/
			ptr++;
			break;
		case 47:
			channel->controlVec24 = false;
			channel->controlVec10 &= 0x7F;
			channel->controlVecShort10 &= 0x00FF;
			return;
		default:
			// unused -> ignore
			break;
		}

		channel->soundDataPtr = ptr;
	}
}

void Player_PCE::procAA62(channel_t *channel, int a) {
	procACEA(channel, a);
	if (channel->controlVec23) {
		channel->controlVec23 = false;
		return;
	}

	channel->controlVec18 = 0;

	channel->controlVec10 |= 0x80;
	int y = channel->controlVec10 & 0x7F;
	channel->controlBufferPos = &control_data[control_offsets[y]];
	channel->controlVec5 = 0;
}

void Player_PCE::procAB7F(channel_t *channel) {
	uint16 freqValue = channel->controlVecShort02;
	channel->controlVecShort02 += channel->controlVecShort03;

	int pos = freq_offset[channel->controlVec19] + channel->controlVec18;
	freqValue += freq_table[pos];
	if (freq_table[pos + 1] != 0x0800) {
		channel->controlVec18++;
	}
	freqValue += channel->controlVecShort06;

	channel->freq = freqValue;
}

void Player_PCE::procAC24(channel_t *channel) {
	if ((channel->controlVec10 & 0x80) == 0)
		return;

	if (channel->controlVec5 == 0) {
		const byte *ctrlPtr = channel->controlBufferPos;
		byte value = *ctrlPtr++;
		while (value >= 0xF0) {
			if (value == 0xF0) {
				channel->controlVecShort10 = READ_LE_UINT16(ctrlPtr);
				ctrlPtr += 2;
			} else if (value == 0xFF) {
				channel->controlVec10 &= 0x7F;
				return;
			} else {
				// unused
			}
			value = *ctrlPtr++;
		}
		channel->controlVec5 = value;
		channel->controlVecShort09 = READ_LE_UINT16(ctrlPtr);
		ctrlPtr += 2;
		channel->controlBufferPos = ctrlPtr;
	}

	channel->controlVecShort10 += channel->controlVecShort09;
	channel->controlVec5--;
}

void Player_PCE::procACEA(channel_t *channel, int a) {
	int x = a +
		channel->controlVec2 +
		channel->controlVec8 +
		channel->controlVec9;
	channel->controlVecShort02 = lookup_table[x];
}

Player_PCE::Player_PCE(ScummEngine *scumm, Audio::Mixer *mixer) {
	for (int i = 0; i < 12; ++i) {
		memset(&channels[i], 0, sizeof(channel_t));
		channels[i].id = i;
	}

	_mixer = mixer;
	_sampleRate = _mixer->getOutputRate();
	_vm = scumm;

	_samplesPerPeriod = 2 * (int)(_sampleRate / UPDATE_FREQ);
	_sampleBuffer = new int16[_samplesPerPeriod];
	_sampleBufferCnt = 0;

	_psg = new PSG_HuC6280(PSG_CLOCK, _sampleRate);

	_mixer->playStream(Audio::Mixer::kPlainSoundType, &_soundHandle, this, -1, Audio::Mixer::kMaxChannelVolume, 0, DisposeAfterUse::NO, true);
}

Player_PCE::~Player_PCE() {
	_mixer->stopHandle(_soundHandle);
	delete[] _sampleBuffer;
	delete _psg;
}

void Player_PCE::stopSound(int nr) {
	// TODO: implement
}

void Player_PCE::stopAllSounds() {
	// TODO: implement
}

int Player_PCE::getSoundStatus(int nr) const {
	// TODO: status for each sound
	for (int i = 0; i < 6; ++i) {
		if (channels[i].controlVec24)
			return 1;
	}
	return 0;
}

int Player_PCE::getMusicTimer() {
	return 0;
}

} // End of namespace Scumm

#endif // USE_RGB_COLOR
