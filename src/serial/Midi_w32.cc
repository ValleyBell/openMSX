/*
 * Win32 MIDI utility routines for openMSX.
 *
 * Copyright (c) 2003 Reikan.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef _WIN32

#include "Midi_w32.hh"
#include "MSXException.hh"
#include "MemBuffer.hh"
#include "cstdiop.hh"

#include <cstring>
#include <cstdlib>
#include <sstream>
#include <cassert>

#define MAXPATHLEN MAX_PATH

using std::string;

namespace openmsx {

/*
 * MIDI I/O helper functions for Win32.
 */
// MIDI SYSTEM MESSAGES max length is not defined...
// TODO: support variable length.
#define OPENMSX_W32_MIDI_SYSMES_MAXLEN 4096


struct vfn_midi {
	unsigned idx;
	unsigned devid;
	HMIDI    handle;
	char     vfname[MAXPATHLEN + 1];
	char     devname[MAXPNAMELEN];
};

struct outbuf {
	DWORD    shortmes;
	unsigned longmes_cnt;
	char     longmes[OPENMSX_W32_MIDI_SYSMES_MAXLEN];
	MIDIHDR  header;
};

static MemBuffer<vfn_midi> vfnt_midiout;
static MemBuffer<vfn_midi> vfnt_midiin;
static unsigned vfnt_midiout_num, vfnt_midiin_num;

static MemBuffer<int> state_out;
static MemBuffer<outbuf> buf_out;

static MIDIHDR inhdr;
static char inlongmes[OPENMSX_W32_MIDI_SYSMES_MAXLEN];


static void w32_midiDevNameConv(char *dst, char *src)
{
	size_t len = strlen(src);
	size_t i;
	for (i = 0; i < len; ++i) {
		if ((src[i] < '0') || (src[i] > 'z') ||
		    ((src[i] > '9') && (src[i] < 'A')) ||
		    ((src[i] > 'Z') && (src[i] < 'a'))) {
			dst[i] = '_';
		} else {
			dst[i] = src[i];
		}
	}
	dst[i] = '\0';
}


// MIDI-OUT
static int w32_midiOutFindDev(unsigned *idx, unsigned *dev, const char *vfn)
{
	for (unsigned i = 0; i < vfnt_midiout_num; ++i) {
		if (!strcmp(vfnt_midiout[i].vfname, vfn)) {
			*idx = i;
			*dev = vfnt_midiout[i].devid;
			return 0;
		}
	}
	return -1;
}


int w32_midiOutInit()
{
	vfnt_midiout_num = 0;
	unsigned num = midiOutGetNumDevs();
	if (!num) return 0;

	state_out.resize(num + 1);
	memset(state_out.data(), 0, (num + 1) * sizeof(int));

	buf_out.resize(num + 1);
	memset(buf_out.data(), 0, (num + 1) * sizeof(outbuf));

	vfnt_midiout.resize(num + 1);

	// MIDI_MAPPER is #define's as ((UINT)-1)
	UINT OPENMSX_MIDI_MAPPER = static_cast<UINT>(-1);
	MIDIOUTCAPSA cap;
	if (midiOutGetDevCapsA(OPENMSX_MIDI_MAPPER, &cap, sizeof(cap)) != MMSYSERR_NOERROR) {
		return 2;
	}
	vfnt_midiout[0].devid = OPENMSX_MIDI_MAPPER;
	w32_midiDevNameConv(vfnt_midiout[0].devname, cap.szPname);
	strncpy(vfnt_midiout[0].vfname, "midi-out", MAXPATHLEN + 1);
	vfnt_midiout_num ++;

	for (unsigned i = 0; i < num; ++i) {
		if (midiOutGetDevCapsA(i, &cap, sizeof(cap)) != MMSYSERR_NOERROR) {
			return 0; // atleast MIDI-MAPPER is available...
		}
		vfnt_midiout[i + 1].devid = i;
		w32_midiDevNameConv(vfnt_midiout[i + 1].devname, cap.szPname);
		snprintf(vfnt_midiout[i + 1].vfname, MAXPATHLEN + 1, "midi-out-%u", i);
		vfnt_midiout_num++;
	}
	return 0;
}

void w32_midiOutClean()
{
	vfnt_midiout_num = 0;
}


unsigned w32_midiOutGetVFNsNum()
{
	return vfnt_midiout_num;
}

string w32_midiOutGetVFN(unsigned nmb)
{
	assert(nmb < vfnt_midiout_num);
	return vfnt_midiout[nmb].vfname;
}

string w32_midiOutGetRDN(unsigned nmb)
{
	assert(nmb < vfnt_midiout_num);
	return vfnt_midiout[nmb].devname;
}


unsigned w32_midiOutOpen(const char *vfn)
{
	unsigned idx, devid;
	if (w32_midiOutFindDev(&idx, &devid,vfn)) {
		return unsigned(-1);
	}
	if (midiOutOpen(reinterpret_cast<HMIDIOUT*>(&vfnt_midiout[idx].handle), devid, 0, 0 ,0) != MMSYSERR_NOERROR) {
		return unsigned(-1);
	}
	return idx;
}

int w32_midiOutClose(unsigned idx)
{
	midiOutReset(reinterpret_cast<HMIDIOUT>(vfnt_midiout[idx].handle));
	if (midiOutClose(reinterpret_cast<HMIDIOUT>(vfnt_midiout[idx].handle)) == MMSYSERR_NOERROR) {
		return 0;
	} else {
		return -1;
	}
}


static int w32_midiOutFlushExclusiveMsg(unsigned idx)
{
	int i;
	buf_out[idx].header.lpData = buf_out[idx].longmes;
	buf_out[idx].header.dwBufferLength = buf_out[idx].longmes_cnt;
	buf_out[idx].header.dwFlags = 0;
	if ((i = midiOutPrepareHeader(reinterpret_cast<HMIDIOUT>(vfnt_midiout[idx].handle), &buf_out[idx].header, sizeof(buf_out[idx].header))) != MMSYSERR_NOERROR) {
		throw FatalError("midiOutPrepareHeader() returned ", i);
	}
	if ((i = midiOutLongMsg(reinterpret_cast<HMIDIOUT>(vfnt_midiout[idx].handle), &buf_out[idx].header, sizeof(buf_out[idx].header))) != MMSYSERR_NOERROR) {
		throw FatalError("midiOutLongMsg() returned ", i);
	}
	// Wait sending in driver.
	// This may take long...
	while (!(buf_out[idx].header.dwFlags & MHDR_DONE)) {
		Sleep(1);
	}
	// Sending Exclusive done.
	if ((i = midiOutUnprepareHeader(reinterpret_cast<HMIDIOUT>(vfnt_midiout[idx].handle), &buf_out[idx].header, sizeof(buf_out[idx].header))) != MMSYSERR_NOERROR) {
		throw FatalError("midiOutUnPrepareHeader() returned ", i);
	}
	buf_out[idx].longmes_cnt = 0;
	return 0;
}

int w32_midiOutPut(unsigned char value, unsigned idx)
{
	if ((state_out[idx] & 0x1000) || ((value & 0x0ff) == 0x0f0)) {
		if (!(state_out[idx] & 0x1000)) {
			// SYSTEM MESSAGE Exclusive start
			state_out[idx] |= 0x1000;
		}
		if (buf_out[idx].longmes_cnt >= OPENMSX_W32_MIDI_SYSMES_MAXLEN) {
			return -1;
		}
		buf_out[idx].longmes[buf_out[idx].longmes_cnt++] = value;

		if (value == 0x0f7) {
			// SYSTEM MESSAGES Exclusive end
			w32_midiOutFlushExclusiveMsg(idx);
			state_out[idx] &= ~0x1000;
		}
	} else {
		switch (state_out[idx]) {
		case 0x0000:
			switch (value & 0x0f0) {
			case 0x080: // Note Off
			case 0x090: // Note On
			case 0x0a0: // Key Pressure
			case 0x0b0: // Control Change
			case 0x0e0: // Pitch Wheel
				state_out[idx] = 0x0082;
				buf_out[idx].shortmes = DWORD(value) & 0x0ff;
				break;
			case 0x0c0: // Program Change
			case 0x0d0: // After Touch
				state_out[idx] = 0x0041;
				buf_out[idx].shortmes = DWORD(value) & 0x0ff;
				break;
			case 0x0f0: // SYSTEM MESSAGE (other than "EXCLUSIVE")
				switch (value &0x0f) {
					case 0x02: // Song Position Pointer
						state_out[idx] = 0x0082;
						buf_out[idx].shortmes = DWORD(value) & 0x0ff;
						break;
					case 0x01: // Time Code
					case 0x03: // Song Select
						state_out[idx] = 0x0041;
						buf_out[idx].shortmes = DWORD(value) & 0x0ff;
						break;
					default: // Timing Clock, Sequencer Start, Sequencer Continue,
					         // Sequencer Stop, Cable Check, System Reset, and Unknown...
						state_out[idx] = 0;
						buf_out[idx].shortmes = DWORD(value) & 0x0ff;
						midiOutShortMsg(reinterpret_cast<HMIDIOUT>(vfnt_midiout[idx].handle), buf_out[idx].shortmes);
						break;
				}
				break;
			default:
				state_out[idx] = 0;
				buf_out[idx].shortmes = DWORD(value) & 0x0ff;
				midiOutShortMsg(reinterpret_cast<HMIDIOUT>(vfnt_midiout[idx].handle), buf_out[idx].shortmes);
				break;
			}
			break;
		case 0x0041:
			buf_out[idx].shortmes |= (DWORD(value) & 0x0ff) << 8;
			midiOutShortMsg(reinterpret_cast<HMIDIOUT>(vfnt_midiout[idx].handle), buf_out[idx].shortmes);
			state_out[idx] = 0;
			break;
		case 0x0082:
			buf_out[idx].shortmes |= (DWORD(value) & 0x0ff) << 8;
			state_out[idx] = 0x0081;
			break;
		case 0x0081:
			buf_out[idx].shortmes |= (DWORD(value) & 0x0ff) << 16;
			midiOutShortMsg(reinterpret_cast<HMIDIOUT>(vfnt_midiout[idx].handle), buf_out[idx].shortmes);
			state_out[idx] = 0;
			break;
		default:
			// not reach...
			midiOutShortMsg(reinterpret_cast<HMIDIOUT>(vfnt_midiout[idx].handle), DWORD(value) & 0x0ff);
			break;
		}
	}
	return 0;
}


// MIDI-IN
static int w32_midiInFindDev(unsigned *idx, unsigned *dev, const char *vfn)
{
	for (unsigned i = 0; i < vfnt_midiin_num; ++i) {
		if (!strcmp(vfnt_midiin[i].vfname, vfn)) {
			*idx = i;
			*dev = vfnt_midiin[i].devid;
			return 0;
		}
	}
	return -1;
}


int w32_midiInInit()
{
	vfnt_midiin_num = 0;
	unsigned num = midiInGetNumDevs();
	if (!num) return 0;

	vfnt_midiin.resize(num + 1);
	for (unsigned i = 0; i < num; ++i) {
		MIDIINCAPSA cap;
		if (midiInGetDevCapsA(i, &cap, sizeof(cap)) != MMSYSERR_NOERROR) {
			return 1;
		}
		vfnt_midiin[i].devid = i;
		w32_midiDevNameConv(vfnt_midiin[i].devname, cap.szPname);
		snprintf(vfnt_midiin[i].vfname, MAXPATHLEN + 1, "midi-in-%u", i);
		vfnt_midiin_num++;
	}
	return 0;
}

void w32_midiInClean()
{
	vfnt_midiin_num = 0;
}


unsigned w32_midiInGetVFNsNum()
{
	return vfnt_midiin_num;
}

string w32_midiInGetVFN(unsigned nmb)
{
	assert(nmb < vfnt_midiin_num);
	return vfnt_midiin[nmb].vfname;
}

string w32_midiInGetRDN(unsigned nmb)
{
	assert(nmb < vfnt_midiin_num);
	return vfnt_midiin[nmb].devname;
}

unsigned w32_midiInOpen(const char *vfn, DWORD thrdid)
{
	unsigned idx, devid;
	if (w32_midiInFindDev(&idx, &devid, vfn)) {
		return unsigned(-1);
	}
	if (midiInOpen(reinterpret_cast<HMIDIIN*>(&vfnt_midiin[idx].handle), devid, thrdid, 0, CALLBACK_THREAD) != MMSYSERR_NOERROR) {
		return unsigned(-1);
	}
	memset(&inhdr, 0, sizeof(inhdr));
	inhdr.lpData = inlongmes;
	inhdr.dwBufferLength = OPENMSX_W32_MIDI_SYSMES_MAXLEN;
	if (midiInPrepareHeader(reinterpret_cast<HMIDIIN>(vfnt_midiin[idx].handle),
		                static_cast<LPMIDIHDR>(&inhdr), sizeof(inhdr)) != MMSYSERR_NOERROR) {
		return unsigned(-1);
	}
	if (midiInAddBuffer(reinterpret_cast<HMIDIIN>(vfnt_midiin[idx].handle), static_cast<LPMIDIHDR>(&inhdr), sizeof(inhdr)) != MMSYSERR_NOERROR) {
		return unsigned(-1);
	}
	if (midiInStart(reinterpret_cast<HMIDIIN>(vfnt_midiin[idx].handle)) != MMSYSERR_NOERROR) {
		return unsigned(-1);
	}
	return idx;
}

int w32_midiInClose(unsigned idx)
{
	midiInStop(reinterpret_cast<HMIDIIN>(vfnt_midiin[idx].handle));
	midiInReset(reinterpret_cast<HMIDIIN>(vfnt_midiin[idx].handle));
	midiInUnprepareHeader(reinterpret_cast<HMIDIIN>(vfnt_midiin[idx].handle),
	                      static_cast<LPMIDIHDR>(&inhdr), sizeof(inhdr));
	if (midiInClose(reinterpret_cast<HMIDIIN>(vfnt_midiin[idx].handle)) == MMSYSERR_NOERROR) {
		return 0;
	} else {
		return -1;
	}
}

} // namespace openmsx

#endif // _WIN32
