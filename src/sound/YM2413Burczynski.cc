// $Id$

/*
 *
 * File: ym2413.c - software implementation of YM2413
 *                  FM sound generator type OPLL
 *
 * Copyright (C) 2002 Jarek Burczynski
 *
 * Version 1.0
 *
 *
 * TODO:
 *  - make sure of the sinus amplitude bits
 *  - make sure of the EG resolution bits (looks like the biggest
 *    modulation index generated by the modulator is 123, 124 = no modulation)
 *  - find proper algorithm for attack phase of EG
 *  - tune up instruments ROM
 *  - support sample replay in test mode (it is NOT as simple as setting bit 0
 *    in register 0x0f and using register 0x10 for sample data).
 *    Which games use this feature ?
 */

#include "YM2413Burczynski.hh"
#include "serialize.hh"
#include <cmath>
#include <cstring>

namespace openmsx {
namespace YM2413Burczynski {

// envelope output entries
static const int ENV_BITS = 10;
static const double ENV_STEP = 128.0 / (1 << ENV_BITS);

static const int MAX_ATT_INDEX = (1 << (ENV_BITS - 2)) - 1; // 255
static const int MIN_ATT_INDEX = 0;

// sinwave entries
static const int SIN_BITS = 10;
static const int SIN_LEN  = 1 << SIN_BITS;
static const int SIN_MASK = SIN_LEN - 1;

static const int TL_RES_LEN = 256; // 8 bits addressing (real chip)

// key scale level
// table is 3dB/octave, DV converts this into 6dB/octave
// 0.1875 is bit 0 weight of the envelope counter (volume) expressed
// in the 'decibel' scale
#define DV(x) int(x / 0.1875)
static const int ksl_tab[8 * 16] =
{
	// OCT 0
	DV( 0.000),DV( 0.000),DV( 0.000),DV( 0.000),
	DV( 0.000),DV( 0.000),DV( 0.000),DV( 0.000),
	DV( 0.000),DV( 0.000),DV( 0.000),DV( 0.000),
	DV( 0.000),DV( 0.000),DV( 0.000),DV( 0.000),
	// OCT 1
	DV( 0.000),DV( 0.000),DV( 0.000),DV( 0.000),
	DV( 0.000),DV( 0.000),DV( 0.000),DV( 0.000),
	DV( 0.000),DV( 0.750),DV( 1.125),DV( 1.500),
	DV( 1.875),DV( 2.250),DV( 2.625),DV( 3.000),
	// OCT 2
	DV( 0.000),DV( 0.000),DV( 0.000),DV( 0.000),
	DV( 0.000),DV( 1.125),DV( 1.875),DV( 2.625),
	DV( 3.000),DV( 3.750),DV( 4.125),DV( 4.500),
	DV( 4.875),DV( 5.250),DV( 5.625),DV( 6.000),
	// OCT 3
	DV( 0.000),DV( 0.000),DV( 0.000),DV( 1.875),
	DV( 3.000),DV( 4.125),DV( 4.875),DV( 5.625),
	DV( 6.000),DV( 6.750),DV( 7.125),DV( 7.500),
	DV( 7.875),DV( 8.250),DV( 8.625),DV( 9.000),
	// OCT 4
	DV( 0.000),DV( 0.000),DV( 3.000),DV( 4.875),
	DV( 6.000),DV( 7.125),DV( 7.875),DV( 8.625),
	DV( 9.000),DV( 9.750),DV(10.125),DV(10.500),
	DV(10.875),DV(11.250),DV(11.625),DV(12.000),
	// OCT 5
	DV( 0.000),DV( 3.000),DV( 6.000),DV( 7.875),
	DV( 9.000),DV(10.125),DV(10.875),DV(11.625),
	DV(12.000),DV(12.750),DV(13.125),DV(13.500),
	DV(13.875),DV(14.250),DV(14.625),DV(15.000),
	// OCT 6
	DV( 0.000),DV( 6.000),DV( 9.000),DV(10.875),
	DV(12.000),DV(13.125),DV(13.875),DV(14.625),
	DV(15.000),DV(15.750),DV(16.125),DV(16.500),
	DV(16.875),DV(17.250),DV(17.625),DV(18.000),
	// OCT 7
	DV( 0.000),DV( 9.000),DV(12.000),DV(13.875),
	DV(15.000),DV(16.125),DV(16.875),DV(17.625),
	DV(18.000),DV(18.750),DV(19.125),DV(19.500),
	DV(19.875),DV(20.250),DV(20.625),DV(21.000)
};
#undef DV

// sustain level table (3dB per step)
// 0 - 15: 0, 3, 6, 9,12,15,18,21,24,27,30,33,36,39,42,45 (dB)
#define SC(db) int((double(db)) / ENV_STEP)
static const int sl_tab[16] = {
	SC( 0),SC( 1),SC( 2),SC(3 ),SC(4 ),SC(5 ),SC(6 ),SC( 7),
	SC( 8),SC( 9),SC(10),SC(11),SC(12),SC(13),SC(14),SC(15)
};
#undef SC

static const byte eg_inc[15][8] =
{
	// cycle: 0 1  2 3  4 5  6 7

	/* 0 */ { 0,1, 0,1, 0,1, 0,1, }, // rates 00..12 0 (increment by 0 or 1)
	/* 1 */ { 0,1, 0,1, 1,1, 0,1, }, // rates 00..12 1
	/* 2 */ { 0,1, 1,1, 0,1, 1,1, }, // rates 00..12 2
	/* 3 */ { 0,1, 1,1, 1,1, 1,1, }, // rates 00..12 3

	/* 4 */ { 1,1, 1,1, 1,1, 1,1, }, // rate 13 0 (increment by 1)
	/* 5 */ { 1,1, 1,2, 1,1, 1,2, }, // rate 13 1
	/* 6 */ { 1,2, 1,2, 1,2, 1,2, }, // rate 13 2
	/* 7 */ { 1,2, 2,2, 1,2, 2,2, }, // rate 13 3

	/* 8 */ { 2,2, 2,2, 2,2, 2,2, }, // rate 14 0 (increment by 2)
	/* 9 */ { 2,2, 2,4, 2,2, 2,4, }, // rate 14 1
	/*10 */ { 2,4, 2,4, 2,4, 2,4, }, // rate 14 2
	/*11 */ { 2,4, 4,4, 2,4, 4,4, }, // rate 14 3

	/*12 */ { 4,4, 4,4, 4,4, 4,4, }, // rates 15 0, 15 1, 15 2, 15 3 (incr by 4)
	/*13 */ { 8,8, 8,8, 8,8, 8,8, }, // rates 15 2, 15 3 for attack
	/*14 */ { 0,0, 0,0, 0,0, 0,0, }, // infinity rates for attack and decay(s)
};

// note that there is no value 13 in this table - it's directly in the code
static const byte eg_rate_select[16 + 64 + 16] =
{
	// Envelope Generator rates (16 + 64 rates + 16 RKS)
	// 16 infinite time rates
	14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,

	// rates 00-12
	 0, 1, 2, 3,
	 0, 1, 2, 3,
	 0, 1, 2, 3,
	 0, 1, 2, 3,
	 0, 1, 2, 3,
	 0, 1, 2, 3,
	 0, 1, 2, 3,
	 0, 1, 2, 3,
	 0, 1, 2, 3,
	 0, 1, 2, 3,
	 0, 1, 2, 3,
	 0, 1, 2, 3,
	 0, 1, 2, 3,

	// rate 13
	 4, 5, 6, 7,

	// rate 14
	 8, 9,10,11,

	// rate 15
	12,12,12,12,

	// 16 dummy rates (same as 15 3)
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,
};

// rate  0,    1,    2,    3,    4,   5,   6,   7,  8,  9, 10, 11, 12, 13, 14, 15
// shift 13,   12,   11,   10,   9,   8,   7,   6,  5,  4,  3,  2,  1,  0,  0,  0
// mask  8191, 4095, 2047, 1023, 511, 255, 127, 63, 31, 15, 7,  3,  1,  0,  0,  0

static const byte eg_rate_shift[16 + 64 + 16] =
{
	// Envelope Generator counter shifts (16 + 64 rates + 16 RKS)
	// 16 infinite time rates
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	// rates 00-12
	13,13,13,13,
	12,12,12,12,
	11,11,11,11,
	10,10,10,10,
	 9, 9, 9, 9,
	 8, 8, 8, 8,
	 7, 7, 7, 7,
	 6, 6, 6, 6,
	 5, 5, 5, 5,
	 4, 4, 4, 4,
	 3, 3, 3, 3,
	 2, 2, 2, 2,
	 1, 1, 1, 1,

	// rate 13
	 0, 0, 0, 0,

	// rate 14
	 0, 0, 0, 0,

	// rate 15
	 0, 0, 0, 0,

	// 16 dummy rates (same as 15 3)
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// multiple table
#define ML(x) byte(2 * x)
static const byte mul_tab[16] =
{
	ML( 0.50), ML( 1.00), ML( 2.00), ML( 3.00),
	ML( 4.00), ML( 5.00), ML( 6.00), ML( 7.00),
	ML( 8.00), ML( 9.00), ML(10.00), ML(10.00),
	ML(12.00), ML(12.00), ML(15.00), ML(15.00),
};
#undef ML

//  TL_TAB_LEN is calculated as:
//  11 - sinus amplitude bits     (Y axis)
//  2  - sinus sign bit           (Y axis)
//  TL_RES_LEN - sinus resolution (X axis)
const int TL_TAB_LEN = 11 * 2 * TL_RES_LEN;
static int tl_tab[TL_TAB_LEN];

// sin waveform table in 'decibel' scale
// two waveforms on OPLL type chips
static unsigned sin_tab[SIN_LEN * 2];

// LFO Amplitude Modulation table (verified on real YM3812)
// 27 output levels (triangle waveform);
// 1 level takes one of: 192, 256 or 448 samples
//
// Length: 210 elements.
//
//  Each of the elements has to be repeated
//  exactly 64 times (on 64 consecutive samples).
//  The whole table takes: 64 * 210 = 13440 samples.
//
// We use data>>1, until we find what it really is on real chip...

static const int LFO_AM_TAB_ELEMENTS = 210;
static const byte lfo_am_table[LFO_AM_TAB_ELEMENTS] =
{
	0,0,0,0,0,0,0,
	1,1,1,1,
	2,2,2,2,
	3,3,3,3,
	4,4,4,4,
	5,5,5,5,
	6,6,6,6,
	7,7,7,7,
	8,8,8,8,
	9,9,9,9,
	10,10,10,10,
	11,11,11,11,
	12,12,12,12,
	13,13,13,13,
	14,14,14,14,
	15,15,15,15,
	16,16,16,16,
	17,17,17,17,
	18,18,18,18,
	19,19,19,19,
	20,20,20,20,
	21,21,21,21,
	22,22,22,22,
	23,23,23,23,
	24,24,24,24,
	25,25,25,25,
	26,26,26,
	25,25,25,25,
	24,24,24,24,
	23,23,23,23,
	22,22,22,22,
	21,21,21,21,
	20,20,20,20,
	19,19,19,19,
	18,18,18,18,
	17,17,17,17,
	16,16,16,16,
	15,15,15,15,
	14,14,14,14,
	13,13,13,13,
	12,12,12,12,
	11,11,11,11,
	10,10,10,10,
	9,9,9,9,
	8,8,8,8,
	7,7,7,7,
	6,6,6,6,
	5,5,5,5,
	4,4,4,4,
	3,3,3,3,
	2,2,2,2,
	1,1,1,1
};

// LFO Phase Modulation table (verified on real YM2413)
static const signed char lfo_pm_table[8][8] =
{
	// FNUM2/FNUM = 0 00xxxxxx (0x0000)
	{ 0, 0, 0, 0, 0, 0, 0, 0, },

	// FNUM2/FNUM = 0 01xxxxxx (0x0040)
	{ 1, 0, 0, 0,-1, 0, 0, 0, },

	// FNUM2/FNUM = 0 10xxxxxx (0x0080)
	{ 2, 1, 0,-1,-2,-1, 0, 1, },

	// FNUM2/FNUM = 0 11xxxxxx (0x00C0)
	{ 3, 1, 0,-1,-3,-1, 0, 1, },

	// FNUM2/FNUM = 1 00xxxxxx (0x0100)
	{ 4, 2, 0,-2,-4,-2, 0, 2, },

	// FNUM2/FNUM = 1 01xxxxxx (0x0140)
	{ 5, 2, 0,-2,-5,-2, 0, 2, },

	// FNUM2/FNUM = 1 10xxxxxx (0x0180)
	{ 6, 3, 0,-3,-6,-3, 0, 3, },

	// FNUM2/FNUM = 1 11xxxxxx (0x01C0)
	{ 7, 3, 0,-3,-7,-3, 0, 3, },
};

// This is not 100% perfect yet but very close
//
// - multi parameters are 100% correct (instruments and drums)
// - LFO PM and AM enable are 100% correct
// - waveform DC and DM select are 100% correct
static const byte table[16 + 3][8] = {
	// MULT  MULT modTL DcDmFb AR/DR AR/DR SL/RR SL/RR
	//   0     1     2     3     4     5     6     7
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // user instrument
	{ 0x61, 0x61, 0x1e, 0x17, 0xf0, 0x7f, 0x00, 0x17 }, // violin
	{ 0x13, 0x41, 0x16, 0x0e, 0xfd, 0xf4, 0x23, 0x23 }, // guitar
	{ 0x03, 0x01, 0x9a, 0x04, 0xf3, 0xf3, 0x13, 0xf3 }, // piano
	{ 0x11, 0x61, 0x0e, 0x07, 0xfa, 0x64, 0x70, 0x17 }, // flute
	{ 0x22, 0x21, 0x1e, 0x06, 0xf0, 0x76, 0x00, 0x28 }, // clarinet
	{ 0x21, 0x22, 0x16, 0x05, 0xf0, 0x71, 0x00, 0x18 }, // oboe
	{ 0x21, 0x61, 0x1d, 0x07, 0x82, 0x80, 0x17, 0x17 }, // trumpet
	{ 0x23, 0x21, 0x2d, 0x16, 0x90, 0x90, 0x00, 0x07 }, // organ
	{ 0x21, 0x21, 0x1b, 0x06, 0x64, 0x65, 0x10, 0x17 }, // horn
	{ 0x21, 0x21, 0x0b, 0x1a, 0x85, 0xa0, 0x70, 0x07 }, // synthesizer
	{ 0x23, 0x01, 0x83, 0x10, 0xff, 0xb4, 0x10, 0xf4 }, // harpsichord
	{ 0x97, 0xc1, 0x20, 0x07, 0xff, 0xf4, 0x22, 0x22 }, // vibraphone
	{ 0x61, 0x00, 0x0c, 0x05, 0xc2, 0xf6, 0x40, 0x44 }, // synthesizer bass
	{ 0x01, 0x01, 0x56, 0x03, 0x94, 0xc2, 0x03, 0x12 }, // acoustic bass
	{ 0x21, 0x01, 0x89, 0x03, 0xf1, 0xe4, 0xf0, 0x23 }, // electric guitar
	// drum instruments definitions
	// MULTI MULTI modTL  xxx  AR/DR AR/DR SL/RR SL/RR
	//   0     1     2     3     4     5     6     7
	//{ 0x07, 0x21, 0x14, 0x00, 0xee, 0xf8, 0xff, 0xf8 },
	//{ 0x01, 0x31, 0x00, 0x00, 0xf8, 0xf7, 0xf8, 0xf7 },
	//{ 0x25, 0x11, 0x00, 0x00, 0xf8, 0xfa, 0xf8, 0x55 }
	{ 0x01, 0x01, 0x16, 0x00, 0xfd, 0xf8, 0x2f, 0x6d },// BD(multi verified, modTL verified, mod env - verified(close), carr. env verifed)
	{ 0x01, 0x01, 0x00, 0x00, 0xd8, 0xd8, 0xf9, 0xf8 },// HH(multi verified), SD(multi not used)
	{ 0x05, 0x01, 0x00, 0x00, 0xf8, 0xba, 0x49, 0x55 },// TOM(multi,env verified), TOP CYM(multi verified, env verified)
};

static inline FreqIndex fnumToIncrement(int block_fnum)
{
	// OPLL (YM2413) phase increment counter = 18bit
	// Chip works with 10.10 fixed point, while we use 16.16.
	const int block = (block_fnum & 0x1C00) >> 10;
	return FreqIndex(block_fnum & 0x03FF) >> (11 - block);
}

inline int Slot::calc_envelope(Channel& channel, unsigned eg_cnt, bool carrier)
{
	switch (state) {
	case EG_DUMP:
		// Dump phase is performed by both operators in each channel.
		// When CARRIER envelope gets down to zero level, phases in BOTH
		// operators are reset (at the same time?).
		// TODO: That sounds logical, but it does not match the implementation.
		if (!(eg_cnt & eg_mask_dp)) {
			egout += eg_sel_dp[(eg_cnt >> eg_sh_dp) & 7];
			if (egout >= MAX_ATT_INDEX) {
				egout = MAX_ATT_INDEX;
				setEnvelopeState(EG_ATTACK);
				phase = FreqIndex(0); // restart Phase Generator
			}
		}
		break;

	case EG_ATTACK:
		if (!(eg_cnt & eg_mask_ar)) {
			egout +=
				(~egout * eg_sel_ar[(eg_cnt >> eg_sh_ar) & 7]) >> 2;
			if (egout <= MIN_ATT_INDEX) {
				egout = MIN_ATT_INDEX;
				setEnvelopeState(EG_DECAY);
			}
		}
		break;

	case EG_DECAY:
		if (!(eg_cnt & eg_mask_dr)) {
			egout += eg_sel_dr[(eg_cnt >> eg_sh_dr) & 7];
			if (egout >= sl) {
				setEnvelopeState(EG_SUSTAIN);
			}
		}
		break;

	case EG_SUSTAIN:
		// this is important behaviour:
		// one can change percusive/non-percussive modes on the fly and
		// the chip will remain in sustain phase
		// - verified on real YM3812
		if (eg_sustain) {
			// non-percussive mode (sustained tone)
			// do nothing
		} else {
			// percussive mode
			// during sustain phase chip adds Release Rate (in
			// percussive mode)
			if (!(eg_cnt & eg_mask_rr)) {
				egout += eg_sel_rr[(eg_cnt >> eg_sh_rr) & 7];
				if (egout >= MAX_ATT_INDEX) {
					egout = MAX_ATT_INDEX;
				}
			}
			// else do nothing in sustain phase
		}
		break;

	case EG_RELEASE:
		// Exclude modulators in melody channels from performing anything in
		// this mode.
		if (carrier) {
			const bool sustain = !eg_sustain || channel.isSustained();
			const unsigned mask = sustain ? eg_mask_rs : eg_mask_rr;
			if (!(eg_cnt & mask)) {
				const byte shift = sustain ? eg_sh_rs : eg_sh_rr;
				const byte* sel = sustain ? eg_sel_rs : eg_sel_rr;
				egout += sel[(eg_cnt >> shift) & 7];
				if (egout >= MAX_ATT_INDEX) {
					egout = MAX_ATT_INDEX;
					setEnvelopeState(EG_OFF);
				}
			}
		}
		break;

	case EG_OFF:
		break;
	}
	return egout;
}

inline int Slot::calc_phase(Channel& channel, unsigned lfo_pm)
{
	if (vib) {
		const int lfo_fn_table_index_offset = lfo_pm_table
			[(channel.getBlockFNum() & 0x01FF) >> 6][lfo_pm];
		phase += fnumToIncrement(
			channel.getBlockFNum() * 2 + lfo_fn_table_index_offset
			) * mul;
	} else {
		// LFO phase modulation disabled for this operator
		phase += freq;
	}
	return phase.toInt();
}

inline void Slot::updateTotalLevel(Channel& channel)
{
	TLL = TL + (channel.getKeyScaleLevelBase() >> ksl);
}

inline void Slot::updateAttackRate(int kcodeScaled)
{
	if ((ar + kcodeScaled) < (16 + 62)) {
		eg_sh_ar  = eg_rate_shift[ar + kcodeScaled];
		eg_sel_ar = eg_inc[eg_rate_select[ar + kcodeScaled]];
	} else {
		eg_sh_ar  = 0;
		eg_sel_ar = eg_inc[13];
	}
	eg_mask_ar = (1 << eg_sh_ar) - 1;
}

inline void Slot::updateDecayRate(int kcodeScaled)
{
	eg_sh_dr  = eg_rate_shift[dr + kcodeScaled];
	eg_sel_dr = eg_inc[eg_rate_select[dr + kcodeScaled]];
	eg_mask_dr = (1 << eg_sh_dr) - 1;
}

inline void Slot::updateReleaseRate(int kcodeScaled)
{
	eg_sh_rr  = eg_rate_shift[rr + kcodeScaled];
	eg_sel_rr = eg_inc[eg_rate_select[rr + kcodeScaled]];
	eg_mask_rr = (1 << eg_sh_rr) - 1;
}

inline int Slot::calcOutput(Channel& channel, unsigned eg_cnt, bool carrier,
                            unsigned lfo_am, int phase)
{
	int egout = calc_envelope(channel, eg_cnt, carrier);
	int env = (TLL + egout + (lfo_am & AMmask)) << 5;
	int p = env + wavetable[phase & SIN_MASK];
	return p < TL_TAB_LEN ? tl_tab[p] : 0;
}

inline int Slot::calc_slot_mod(Channel& channel, unsigned eg_cnt, bool carrier,
                               unsigned lfo_pm, unsigned lfo_am)
{
	// Compute phase.
	int phase = calc_phase(channel, lfo_pm);
	if (fb_shift) {
		phase += (op1_out[0] + op1_out[1]) >> fb_shift;
	}
	// Shift output in 2-place buffer.
	op1_out[0] = op1_out[1];
	// Calculate operator output.
	op1_out[1] = calcOutput(channel, eg_cnt, carrier, lfo_am, phase);
	return op1_out[0] << 1;
}

inline int Channel::calcOutput(unsigned eg_cnt, unsigned lfo_pm, unsigned lfo_am, int fm)
{
	int phase = car.calc_phase(*this, lfo_pm) + fm;
	return car.calcOutput(*this, eg_cnt, true, lfo_am, phase);
}


// Operators used in the rhythm sounds generation process:
//
// Envelope Generator:
//
// channel  operator  register number   Bass  High  Snare Tom  Top
// / slot   number    TL ARDR SLRR Wave Drum  Hat   Drum  Tom  Cymbal
//  6 / 0   12        50  70   90   f0  +
//  6 / 1   15        53  73   93   f3  +
//  7 / 0   13        51  71   91   f1        +
//  7 / 1   16        54  74   94   f4              +
//  8 / 0   14        52  72   92   f2                    +
//  8 / 1   17        55  75   95   f5                          +
//
// Phase Generator:
//
// channel  operator  register number   Bass  High  Snare Tom  Top
// / slot   number    MULTIPLE          Drum  Hat   Drum  Tom  Cymbal
//  6 / 0   12        30                +
//  6 / 1   15        33                +
//  7 / 0   13        31                      +     +           +
//  7 / 1   16        34                -----  n o t  u s e d -----
//  8 / 0   14        32                                  +
//  8 / 1   17        35                      +                 +
//
// channel  operator  register number   Bass  High  Snare Tom  Top
// number   number    BLK/FNUM2 FNUM    Drum  Hat   Drum  Tom  Cymbal
//    6     12,15     B6        A6      +
//    7     13,16     B7        A7            +     +           +
//    8     14,17     B8        A8            +           +     +

// Phase generation is based on:
//   HH  (13) channel 7->slot 1 combined with channel 8->slot 2
//            (same combination as TOP CYMBAL but different output phases)
//   SD  (16) channel 7->slot 1
//   TOM (14) channel 8->slot 1
//   TOP (17) channel 7->slot 1 combined with channel 8->slot 2
//            (same combination as HIGH HAT but different output phases)

static inline int genPhaseHighHat(int phaseM7, int phaseC8, int noise_rng)
{
	// hi == phase >= 0x200
	bool hi;
	// enable gate based on frequency of operator 2 in channel 8
	if (phaseC8 & 0x28) {
		hi = true;
	} else {
		// base frequency derived from operator 1 in channel 7
		// VC++ requires explicit conversion to bool. Compiler bug??
		const bool bit7 = (phaseM7 & 0x80) != 0;
		const bool bit3 = (phaseM7 & 0x08) != 0;
		const bool bit2 = (phaseM7 & 0x04) != 0;
		hi = (bit2 ^ bit7) | bit3;
	}
	if (noise_rng & 1) {
		return hi ? (0x200 | 0xD0) : (0xD0 >> 2);
	} else {
		return hi ? (0x200 | (0xD0 >> 2)) : 0xD0;
	}
}

static inline int genPhaseSnare(int phaseM7, int noise_rng)
{
	// base frequency derived from operator 1 in channel 7
	// noise bit XOR'es phase by 0x100
	return ((phaseM7 & 0x100) + 0x100)
	     ^ ((noise_rng & 1) << 8);
}

static inline int genPhaseCymbal(int phaseM7, int phaseC8)
{
	// enable gate based on frequency of operator 2 in channel 8
	if (phaseC8 & 0x28) {
		return 0x300;
	} else {
		// base frequency derived from operator 1 in channel 7
		// VC++ requires explicit conversion to bool. Compiler bug??
		const bool bit7 = (phaseM7 & 0x80) != 0;
		const bool bit3 = (phaseM7 & 0x08) != 0;
		const bool bit2 = (phaseM7 & 0x04) != 0;
		return ((bit2 != bit7) || bit3) ? 0x300 : 0x100;
	}
}

static void initTables()
{
	static bool alreadyInit = false;
	if (alreadyInit) return;
	alreadyInit = true;

	for (int x = 0; x < TL_RES_LEN; ++x) {
		double m = (1 << 16) / pow(2, (x + 1) * (ENV_STEP / 4.0) / 8.0);
		m = floor(m);

		// we never reach (1 << 16) here due to the (x + 1)
		// result fits within 16 bits at maximum
		int n = int(m); // 16 bits here
		n >>= 4;        // 12 bits here
		n = (n >> 1) + (n & 1); // round to nearest
		// 11 bits here (rounded)
		for (int i = 0; i < 11; ++i) {
			tl_tab[x * 2 + 0 + i * 2 * TL_RES_LEN] = n >> i;
			tl_tab[x * 2 + 1 + i * 2 * TL_RES_LEN] = -(n >> i);
		}
	}

	unsigned* full = &sin_tab[0 * SIN_LEN]; // waveform 0: standard sinus
	unsigned* half = &sin_tab[1 * SIN_LEN]; // waveform 1: positive part of sinus
	for (int i = 0; i < SIN_LEN / 4; ++i) {
		// checked on real hardware, see also
		//   http://docs.google.com/Doc?id=dd8kqn9f_13cqjkf4gp
		double m = sin(((i * 2) + 1) * M_PI / SIN_LEN);
		int n = int(round(log(m) * (-256.0 / log(2.0))));
		full[i] = half[i] = 2 * n;
	}
	for (int i = 0; i < SIN_LEN / 4; ++i) {
		full[SIN_LEN / 4 + i] =
		half[SIN_LEN / 4 + i] = full[SIN_LEN / 4 - 1 - i];
	}
	for (int i = 0; i < SIN_LEN / 2; ++i) {
		full[SIN_LEN / 2 + i] = full[i] | 1;
		half[SIN_LEN / 2 + i] = TL_TAB_LEN;
	}
}


Slot::Slot()
	: phase(0), freq(0)
{
	ar = dr = rr = KSR = ksl = mul = 0;
	fb_shift = op1_out[0] = op1_out[1] = 0;
	TL = TLL = egout = sl = 0;
	eg_sh_dp   = eg_sh_ar   = eg_sh_dr   = eg_sh_rr   = eg_sh_rs   = 0;
	eg_sel_dp  = eg_sel_ar  = eg_sel_dr  = eg_sel_rr  = eg_sel_rs  = eg_inc[0];
	eg_mask_dp = eg_mask_ar = eg_mask_dr = eg_mask_rr = eg_mask_rs = 0;
	eg_sustain = false;
	setEnvelopeState(EG_OFF);
	key = AMmask = vib = 0;
	wavetable = &sin_tab[0 * SIN_LEN];
}

void Slot::setKeyOn(KeyPart part)
{
	if (!key) {
		// do NOT restart Phase Generator (verified on real YM2413)
		setEnvelopeState(EG_DUMP);
	}
	key |= part;
}

void Slot::setKeyOff(KeyPart part)
{
	if (key) {
		key &= ~part;
		if (!key) {
			if (isActive()) {
				setEnvelopeState(EG_RELEASE);
			}
		}
	}
}

void Slot::setKeyOnOff(KeyPart part, bool enabled)
{
	if (enabled) {
		setKeyOn(part);
	} else {
		setKeyOff(part);
	}
}

bool Slot::isActive() const
{
	return state != EG_OFF;
}

void Slot::setEnvelopeState(EnvelopeState state_)
{
	state = state_;
}

void Slot::setFrequencyMultiplier(byte value)
{
	mul = mul_tab[value];
}

void Slot::setKeyScaleRate(bool value)
{
	KSR = value ? 0 : 2;
}

void Slot::setEnvelopeSustained(bool value)
{
	eg_sustain = value;
}

void Slot::setVibrato(bool value)
{
	vib = value;
}

void Slot::setAmplitudeModulation(bool value)
{
	AMmask = value ? ~0 : 0;
}

void Slot::setTotalLevel(Channel& channel, byte value)
{
	TL = value << (ENV_BITS - 2 - 7); // 7 bits TL (bit 6 = always 0)
	updateTotalLevel(channel);
}

void Slot::setKeyScaleLevel(Channel& channel, byte value)
{
	ksl = value ? (3 - value) : 31;
	updateTotalLevel(channel);
}

void Slot::setWaveform(byte value)
{
	wavetable = &sin_tab[value * SIN_LEN];
}

void Slot::setFeedbackShift(byte value)
{
	fb_shift = value ? 8 - value : 0;
}

void Slot::setAttackRate(const Channel& channel, byte value)
{
	int kcodeScaled = channel.getKeyCode() >> KSR;
	ar = value ? 16 + (value << 2) : 0;
	updateAttackRate(kcodeScaled);
}

void Slot::setDecayRate(const Channel& channel, byte value)
{
	int kcodeScaled = channel.getKeyCode() >> KSR;
	dr = value ? 16 + (value << 2) : 0;
	updateDecayRate(kcodeScaled);
}

void Slot::setReleaseRate(const Channel& channel, byte value)
{
	int kcodeScaled = channel.getKeyCode() >> KSR;
	rr = value ? 16 + (value << 2) : 0;
	updateReleaseRate(kcodeScaled);
}

void Slot::setSustainLevel(byte value)
{
	sl = sl_tab[value];
}

void Slot::updateFrequency(Channel& channel)
{
	updateTotalLevel(channel);
	updateGenerators(channel);
}

void Slot::resetOperators()
{
	wavetable = &sin_tab[0 * SIN_LEN];
	setEnvelopeState(EG_OFF);
	egout = MAX_ATT_INDEX;
}

void Slot::updateGenerators(Channel& channel)
{
	// (frequency) phase increment counter
	freq = channel.getFrequencyIncrement() * mul;

	// calculate envelope generator rates
	const int kcodeScaled = channel.getKeyCode() >> KSR;
	updateAttackRate(kcodeScaled);
	updateDecayRate(kcodeScaled);
	updateReleaseRate(kcodeScaled);

	const int rs = channel.isSustained() ? 16 + (5 << 2) : 16 + (7 << 2);
	eg_sh_rs  = eg_rate_shift[rs + kcodeScaled];
	eg_sel_rs = eg_inc[eg_rate_select[rs + kcodeScaled]];

	const int dp = 16 + (13 << 2);
	eg_sh_dp  = eg_rate_shift[dp + kcodeScaled];
	eg_sel_dp = eg_inc[eg_rate_select[dp + kcodeScaled]];

	eg_mask_rs = (1 << eg_sh_rs) - 1;
	eg_mask_dp = (1 << eg_sh_dp) - 1;
}

Channel::Channel()
	: fc(0)
{
	instvol_r = 0;
	block_fnum = ksl_base = 0;
	sus = false;
}

void Channel::setFrequency(int block_fnum_)
{
	if (block_fnum == block_fnum_) return;
	block_fnum = block_fnum_;

	ksl_base = ksl_tab[block_fnum >> 5];
	fc       = fnumToIncrement(block_fnum * 2);

	// Refresh Total Level and frequency counter in both SLOTs of this channel.
	mod.updateFrequency(*this);
	car.updateFrequency(*this);
}

void Channel::setFrequencyLow(byte value)
{
	setFrequency((block_fnum & 0x0F00) | value);
}

void Channel::setFrequencyHigh(byte value)
{
	setFrequency((value << 8) | (block_fnum & 0x00FF));
}

int Channel::getBlockFNum() const
{
	return block_fnum;
}

FreqIndex Channel::getFrequencyIncrement() const
{
	return fc;
}

int Channel::getKeyScaleLevelBase() const
{
	return ksl_base;
}

byte Channel::getKeyCode() const
{
	// BLK 2,1,0 bits -> bits 3,2,1 of kcode, FNUM MSB -> kcode LSB
	return (block_fnum & 0x0F00) >> 8;
}

bool Channel::isSustained() const
{
	return sus;
}

void Channel::setSustain(bool sustained)
{
	sus = sustained;
}

void Channel::updateInstrumentPart(int part, byte value)
{
	switch (part) {
	case 0:
		mod.setFrequencyMultiplier(value & 0x0F);
		mod.setKeyScaleRate((value & 0x10) != 0);
		mod.setEnvelopeSustained((value & 0x20) != 0);
		mod.setVibrato((value & 0x40) != 0);
		mod.setAmplitudeModulation((value & 0x80) != 0);
		mod.updateGenerators(*this);
		break;
	case 1:
		car.setFrequencyMultiplier(value & 0x0F);
		car.setKeyScaleRate((value & 0x10) != 0);
		car.setEnvelopeSustained((value & 0x20) != 0);
		car.setVibrato((value & 0x40) != 0);
		car.setAmplitudeModulation((value & 0x80) != 0);
		car.updateGenerators(*this);
		break;
	case 2:
		mod.setKeyScaleLevel(*this, value >> 6);
		mod.setTotalLevel(*this, value & 0x3F);
		break;
	case 3:
		mod.setWaveform((value & 0x08) >> 3);
		mod.setFeedbackShift(value & 0x07);
		car.setKeyScaleLevel(*this, value >> 6);
		car.setWaveform((value & 0x10) >> 4);
		break;
	case 4:
		mod.setAttackRate(*this, value >> 4);
		mod.setDecayRate(*this, value & 0x0F);
		break;
	case 5:
		car.setAttackRate(*this, value >> 4);
		car.setDecayRate(*this, value & 0x0F);
		break;
	case 6:
		mod.setSustainLevel(value >> 4);
		mod.setReleaseRate(*this, value & 0x0F);
		break;
	case 7:
		car.setSustainLevel(value >> 4);
		car.setReleaseRate(*this, value & 0x0F);
		break;
	}
}

void Channel::updateInstrument(const byte* inst)
{
	for (int part = 0; part < 8; ++part) {
		updateInstrumentPart(part, inst[part]);
	}
}

YM2413::YM2413()
	: lfo_am_cnt(0), lfo_pm_cnt(0)
{
	initTables();

	memset(reg, 0, sizeof(reg)); // avoid UMR
	eg_cnt = 0;
	rhythm = 0;
	noise_rng = 0;

	reset();
}

void YM2413::updateCustomInstrument(int part, byte value)
{
	// Update instrument definition.
	inst_tab[0][part] = value;

	// Update every channel that has instrument 0 selected.
	const int numMelodicChannels = getNumMelodicChannels();
	for (int ch = 0; ch < numMelodicChannels; ++ch) {
		Channel& channel = channels[ch];
		if ((channel.instvol_r & 0xF0) == 0) {
			channel.updateInstrumentPart(part, value);
		}
	}
}

void YM2413::setRhythmMode(bool rhythm_)
{
	if (rhythm == rhythm_) return;
	rhythm = rhythm_;

	Channel& ch6 = channels[6];
	Channel& ch7 = channels[7];
	Channel& ch8 = channels[8];
	if (rhythm) { // OFF -> ON
		// Bass drum.
		ch6.updateInstrument(inst_tab[16]);
		// High hat and snare drum.
		ch7.updateInstrument(inst_tab[17]);
		ch7.mod.setTotalLevel(ch7, (ch7.instvol_r >> 4) << 2); // High hat
		// Tom-tom and top cymbal.
		ch8.updateInstrument(inst_tab[18]);
		ch8.mod.setTotalLevel(ch8, (ch8.instvol_r >> 4) << 2); // Tom-tom
	} else { // ON -> OFF
		ch6.updateInstrument(inst_tab[ch6.instvol_r >> 4]);
		ch7.updateInstrument(inst_tab[ch7.instvol_r >> 4]);
		ch8.updateInstrument(inst_tab[ch8.instvol_r >> 4]);
		// BD key off
		ch6.mod.setKeyOff(Slot::KEY_RHYTHM);
		ch6.car.setKeyOff(Slot::KEY_RHYTHM);
		// HH key off
		ch7.mod.setKeyOff(Slot::KEY_RHYTHM);
		// SD key off
		ch7.car.setKeyOff(Slot::KEY_RHYTHM);
		// TOM key off
		ch8.mod.setKeyOff(Slot::KEY_RHYTHM);
		// TOP-CY off
		ch8.car.setKeyOff(Slot::KEY_RHYTHM);
	}
}

void YM2413::setRhythmFlags(byte flags)
{
	// flags = X | X | mode | BD | SD | TOM | TC | HH
	setRhythmMode((flags & 0x20) != 0);
	if (rhythm) {
		// BD key on/off
		channels[6].mod.setKeyOnOff(Slot::KEY_RHYTHM, (flags & 0x10) != 0);
		channels[6].car.setKeyOnOff(Slot::KEY_RHYTHM, (flags & 0x10) != 0);
		// HH key on/off
		channels[7].mod.setKeyOnOff(Slot::KEY_RHYTHM, (flags & 0x01) != 0);
		// SD key on/off
		channels[7].car.setKeyOnOff(Slot::KEY_RHYTHM, (flags & 0x08) != 0);
		// TOM key on/off
		channels[8].mod.setKeyOnOff(Slot::KEY_RHYTHM, (flags & 0x04) != 0);
		// TOP-CY key on/off
		channels[8].car.setKeyOnOff(Slot::KEY_RHYTHM, (flags & 0x02) != 0);
	}
}

void YM2413::reset()
{
	eg_cnt    = 0;
	noise_rng = 1;    // noise shift register
	idleSamples = 0;

	// setup instruments table
	for (int instrument = 0; instrument < 19; ++instrument) {
		for (int part = 0; part < 8; ++part) {
			inst_tab[instrument][part] = table[instrument][part];
		}
	}

	// reset with register write
	writeReg(0x0F, 0); // test reg
	for (int i = 0x3F; i >= 0x10; --i) {
		writeReg(i, 0);
	}

	resetOperators();
}

void YM2413::resetOperators()
{
	for (int ch = 0; ch < 9; ++ch) {
		channels[ch].mod.resetOperators();
		channels[ch].car.resetOperators();
	}
}

int YM2413::getNumMelodicChannels() const
{
	return rhythm ? 6 : 9;
}

Channel& YM2413::getChannelForReg(byte reg)
{
	byte chan = (reg & 0x0F) % 9; // verified on real YM2413
	return channels[chan];
}

int YM2413::getAmplificationFactor() const
{
	return 1 << 4;
}

void YM2413::generateChannels(int* bufs[9 + 5], unsigned num)
{
	// TODO make channelActiveBits a member and
	//      keep it up-to-date all the time

	// bits 0-8  -> ch[0-8].car
	// bits 9-17 -> ch[0-8].mod (only ch7 and ch8 used)
	unsigned channelActiveBits = 0;

	const int numMelodicChannels = getNumMelodicChannels();
	for (int ch = 0; ch < numMelodicChannels; ++ch) {
		if (channels[ch].car.isActive()) {
			channelActiveBits |= 1 << ch;
		} else {
			bufs[ch] = nullptr;
		}
	}
	if (rhythm) {
		bufs[6] = nullptr;
		bufs[7] = nullptr;
		bufs[8] = nullptr;
		for (int ch = 6; ch < 9; ++ch) {
			if (channels[ch].car.isActive()) {
				channelActiveBits |= 1 << ch;
			} else {
				bufs[ch + 3] = nullptr;
			}
		}
		if (channels[7].mod.isActive()) {
			channelActiveBits |= 1 << (7 + 9);
		} else {
			bufs[12] = nullptr;
		}
		if (channels[8].mod.isActive()) {
			channelActiveBits |= 1 << (8 + 9);
		} else {
			bufs[13] = nullptr;
		}
	} else {
		bufs[ 9] = nullptr;
		bufs[10] = nullptr;
		bufs[11] = nullptr;
		bufs[12] = nullptr;
		bufs[13] = nullptr;
	}

	if (channelActiveBits) {
		idleSamples = 0;
	} else {
		if (idleSamples > (CLOCK_FREQ / (72 * 5))) {
			// Optimization:
			//   idle for over 1/5s = 200ms
			//   we don't care that noise / AM / PM isn't exactly
			//   in sync with the real HW when music resumes
			// Alternative:
			//   implement an efficient advance(n) method
			return;
		}
		idleSamples += num;
	}

	for (unsigned i = 0; i < num; ++i) {
		// Amplitude modulation: 27 output levels (triangle waveform)
		// 1 level takes one of: 192, 256 or 448 samples
		// One entry from LFO_AM_TABLE lasts for 64 samples
		lfo_am_cnt.addQuantum();
		if (lfo_am_cnt == LFOAMIndex(LFO_AM_TAB_ELEMENTS)) {
			// lfo_am_table is 210 elements long
			lfo_am_cnt = LFOAMIndex(0);
		}
		unsigned lfo_am = lfo_am_table[lfo_am_cnt.toInt()] >> 1;
		unsigned lfo_pm = lfo_pm_cnt.toInt() & 7;

		for (int ch = 0; ch < numMelodicChannels; ++ch) {
			Channel& channel = channels[ch];
			int fm = channel.mod.calc_slot_mod(channel, eg_cnt, false, lfo_pm, lfo_am);
			if ((channelActiveBits >> ch) & 1) {
				bufs[ch][i] += channel.calcOutput(eg_cnt, lfo_pm, lfo_am, fm);
			}
		}
		if (rhythm) {
			// Bass Drum (verified on real YM3812):
			//  - depends on the channel 6 'connect' register:
			//    when connect = 0 it works the same as in normal (non-rhythm) mode
			//                     (op1->op2->out)
			//    when connect = 1 _only_ operator 2 is present on output (op2->out),
			//                     operator 1 is ignored
			//  - output sample always is multiplied by 2
			Channel& channel6 = channels[6];
			int fm = channel6.mod.calc_slot_mod(channels[6], eg_cnt, true, lfo_pm, lfo_am);
			if (channelActiveBits & (1 << 6)) {
				bufs[ 9][i] += 2 * channel6.calcOutput(eg_cnt, lfo_pm, lfo_am, fm);
			}

			// TODO: Skip phase generation if output will 0 anyway.
			//       Possible by passing phase generator as a template parameter to
			//       calcOutput.

			/*  phaseC7 */channels[7].car.calc_phase(channels[7], lfo_pm);
			int phaseM7 = channels[7].mod.calc_phase(channels[7], lfo_pm);
			int phaseC8 = channels[8].car.calc_phase(channels[8], lfo_pm);
			int phaseM8 = channels[8].mod.calc_phase(channels[8], lfo_pm);

			// Snare Drum (verified on real YM3812)
			if (channelActiveBits & (1 << 7)) {
				Slot& SLOT7_2 = channels[7].car;
				bufs[10][i] += 2 * SLOT7_2.calcOutput(channels[7], eg_cnt, true, lfo_am, genPhaseSnare(phaseM7, noise_rng));
			}

			// Top Cymbal (verified on real YM2413)
			if (channelActiveBits & (1 << 8)) {
				Slot& SLOT8_2 = channels[8].car;
				bufs[11][i] += 2 * SLOT8_2.calcOutput(channels[8], eg_cnt, true, lfo_am, genPhaseCymbal(phaseM7, phaseC8));
			}

			// High Hat (verified on real YM3812)
			if (channelActiveBits & (1 << (7 + 9))) {
				Slot& SLOT7_1 = channels[7].mod;
				bufs[12][i] += 2 * SLOT7_1.calcOutput(channels[7], eg_cnt, true, lfo_am, genPhaseHighHat(phaseM7, phaseC8, noise_rng));
			}

			// Tom Tom (verified on real YM3812)
			if (channelActiveBits & (1 << (8 + 9))) {
				Slot& SLOT8_1 = channels[8].mod;
				bufs[13][i] += 2 * SLOT8_1.calcOutput(channels[8], eg_cnt, true, lfo_am, phaseM8);
			}
		}

		// Vibrato: 8 output levels (triangle waveform)
		// 1 level takes 1024 samples
		lfo_pm_cnt.addQuantum();

		++eg_cnt;

		// The Noise Generator of the YM3812 is 23-bit shift register.
		// Period is equal to 2^23-2 samples.
		// Register works at sampling frequency of the chip, so output
		// can change on every sample.
		//
		// Output of the register and input to the bit 22 is:
		// bit0 XOR bit14 XOR bit15 XOR bit22
		//
		// Simply use bit 22 as the noise output.

		//  int j = ((noise_rng >>  0) ^ (noise_rng >> 14) ^
		//           (noise_rng >> 15) ^ (noise_rng >> 22)) & 1;
		//  noise_rng = (j << 22) | (noise_rng >> 1);
		//
		//    Instead of doing all the logic operations above, we
		//    use a trick here (and use bit 0 as the noise output).
		//    The difference is only that the noise bit changes one
		//    step ahead. This doesn't matter since we don't know
		//    what is real state of the noise_rng after the reset.
		if (noise_rng & 1) {
			noise_rng ^= 0x800302;
		}
		noise_rng >>= 1;
	}
}

void YM2413::writeReg(byte r, byte v)
{
	reg[r] = v;

	switch (r & 0xF0) {
	case 0x00: { // 00-0F: control
		switch (r & 0x0F) {
		case 0x00:  // AM/VIB/EGTYP/KSR/MULTI (modulator)
		case 0x01:  // AM/VIB/EGTYP/KSR/MULTI (carrier)
		case 0x02:  // Key Scale Level, Total Level (modulator)
		case 0x03:  // Key Scale Level, carrier waveform, modulator waveform,
		            // Feedback
		case 0x04:  // Attack, Decay (modulator)
		case 0x05:  // Attack, Decay (carrier)
		case 0x06:  // Sustain, Release (modulator)
		case 0x07:  // Sustain, Release (carrier)
			updateCustomInstrument(r, v);
			break;
		case 0x0E:
			setRhythmFlags(v);
			break;
		}
		break;
	}
	case 0x10: {
		// 10-18: FNUM 0-7
		Channel& ch = getChannelForReg(r);
		ch.setFrequencyLow(v);
		break;
	}
	case 0x20: {
		// 20-28: suson, keyon, block, FNUM 8
		Channel& ch = getChannelForReg(r);
		ch.mod.setKeyOnOff(Slot::KEY_MAIN, (v & 0x10) != 0);
		ch.car.setKeyOnOff(Slot::KEY_MAIN, (v & 0x10) != 0);
		ch.setSustain((v & 0x20) != 0);
		// Note: When changing the frequency, a new value for RS is
		//       computed using the sustain value, so make sure the new
		//       sustain value is committed first.
		ch.setFrequencyHigh(v & 0x0F);
		break;
	}
	case 0x30: { // inst 4 MSBs, VOL 4 LSBs
		Channel& ch = getChannelForReg(r);

		byte old_instvol = ch.instvol_r;
		ch.instvol_r = v;  // store for later use

		ch.car.setTotalLevel(ch, (v & 0x0F) << 2);

		// Check wether we are in rhythm mode and handle instrument/volume
		// register accordingly.

		byte chan = (r & 0x0F) % 9; // verified on real YM2413
		if (chan >= getNumMelodicChannels()) {
			// We're in rhythm mode.
			if (chan >= 7) {
				// Only for channel 7 and 8 (channel 6 is handled in usual way)
				// modulator envelope is HH(chan=7) or TOM(chan=8).
				ch.mod.setTotalLevel(ch, (ch.instvol_r >> 4) << 2);
			}
		} else {
			if ((old_instvol & 0xF0) != (v & 0xF0)) {
				ch.updateInstrument(inst_tab[v >> 4]);
			}
		}
		break;
	}
	default:
		break;
	}
}

byte YM2413::peekReg(byte r) const
{
	return reg[r];
}

} // namespace Burczynsk

static enum_string<YM2413Burczynski::Slot::EnvelopeState> envelopeStateInfo[] = {
	{ "DUMP",    YM2413Burczynski::Slot::EG_DUMP    },
	{ "ATTACK",  YM2413Burczynski::Slot::EG_ATTACK  },
	{ "DECAY",   YM2413Burczynski::Slot::EG_DECAY   },
	{ "SUSTAIN", YM2413Burczynski::Slot::EG_SUSTAIN },
	{ "RELEASE", YM2413Burczynski::Slot::EG_RELEASE },
	{ "OFF",     YM2413Burczynski::Slot::EG_OFF     }
};
SERIALIZE_ENUM(YM2413Burczynski::Slot::EnvelopeState, envelopeStateInfo);

namespace YM2413Burczynski {

// version 1: initial version
// version 2: - removed kcodeScaled
//            - calculated more members from other state
//              (TLL, freq, eg_sel_*, eg_sh_*)
template<typename Archive>
void Slot::serialize(Archive& ar, unsigned /*version*/)
{
	// TODO some of the serialized members here could be calculated from
	//      other members
	int waveform = (wavetable == &sin_tab[0]) ? 0 : 1;
	ar.serialize("waveform", waveform);
	if (ar.isLoader()) {
		setWaveform(waveform);
	}

	ar.serialize("phase", phase);
	ar.serialize("TL", TL);
	ar.serialize("volume", egout);
	ar.serialize("sl", sl);
	ar.serialize("state", state);
	ar.serialize("op1_out", op1_out);
	ar.serialize("eg_sustain", eg_sustain);
	ar.serialize("fb_shift", fb_shift);
	ar.serialize("key", key);
	ar.serialize("ar", this->ar);
	ar.serialize("dr", dr);
	ar.serialize("rr", rr);
	ar.serialize("KSR", KSR);
	ar.serialize("ksl", ksl);
	ar.serialize("mul", mul);
	ar.serialize("AMmask", AMmask);
	ar.serialize("vib", vib);

	// These are calculated by updateTotalLevel()
	//   TLL
	// These are calculated by updateGenerators()
	//   freq, eg_sh_ar, eg_sel_ar, eg_sh_dr, eg_sel_dr, eg_sh_rr, eg_sel_rr
	//   eg_sh_rs, eg_sel_rs, eg_sh_dp, eg_sel_dp
}

// version 1: original version
// version 2: removed kcode
template<typename Archive>
void Channel::serialize(Archive& ar, unsigned /*version*/)
{
	// mod/car were originally an array, keep serializing as such for bwc
	Slot slots[2] = { mod, car };
	ar.serialize("slots", slots);
	if (ar.isLoader()) {
		mod = slots[0];
		car = slots[1];
	}

	ar.serialize("instvol_r", instvol_r);
	ar.serialize("block_fnum", block_fnum);
	ar.serialize("fc", fc);
	ar.serialize("ksl_base", ksl_base);
	ar.serialize("sus", sus);

	if (ar.isLoader()) {
		mod.updateFrequency(*this);
		car.updateFrequency(*this);
	}
}

// version 1:  initial version
// version 2:  'registers' are moved here (no longer serialized in base class)
template<typename Archive>
void YM2413::serialize(Archive& ar, unsigned version)
{
	if (ar.versionBelow(version, 2)) ar.beginTag("YM2413Core");
	ar.serialize("registers", reg);
	if (ar.versionBelow(version, 2)) ar.endTag("YM2413Core");

	// only serialize user instrument
	ar.serialize_blob("user_instrument", inst_tab[0], 8);
	ar.serialize("channels", channels);
	ar.serialize("eg_cnt", eg_cnt);
	ar.serialize("noise_rng", noise_rng);
	ar.serialize("lfo_am_cnt", lfo_am_cnt);
	ar.serialize("lfo_pm_cnt", lfo_pm_cnt);
	ar.serialize("rhythm", rhythm);
	// don't serialize idleSamples, it's only an optimization
}

} // namespace Burczynsk

using YM2413Burczynski::YM2413;
INSTANTIATE_SERIALIZE_METHODS(YM2413);
REGISTER_POLYMORPHIC_INITIALIZER(YM2413Core, YM2413, "YM2413-Jarek-Burczynski");

} // namespace openmsx
